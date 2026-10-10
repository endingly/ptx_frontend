#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <string>
#include <string_view>
#include "test_module_snapshot.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Build a complete scalar module with independently typed register declarations. */
std::string moduleSource(std::string_view body, std::string_view globals = {}) {
  return ".version 9.3\n.target sm_80\n.address_size 64\n" +
         std::string(globals) +
         "\n.entry kernel() { .reg .u32 %r<3>; .reg .u64 %rd<3>; "
         ".reg .f32 %f<3>; .reg .f64 %d<3>; .reg .pred %p<3>;\n" +
         std::string(body) + "\nret; }";
}

/** Inspect the numeric source of the first scalar MOV in a resolved module. */
const ResolvedImmediate& firstMov(const ResolvedModule& module) {
  const auto& mov =
      dynamic_cast<const MovScalar&>(*module.functions.back().body.front());
  return std::get<ResolvedImmediate>(mov.src_mov_source.value().value);
}

/** Obtain an owned source expression through the public syntax entry point. */
std::optional<syntax_ast::AstConstantOperand> expression(
    std::string_view text) {
  PtxSyntaxParser parser("mov.u64 %rd0, (" + std::string(text) + ");");
  auto parsed = parser.parseInstruction();
  EXPECT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed.diagnostics.empty());
  if (!parsed || !parsed.diagnostics.empty())
    return std::nullopt;
  return std::get<syntax_ast::AstConstantOperand>(
      std::move(parsed->operands[1]));
}

/** Locate the final matching single-line failing slice independently of AST ranges. */
SourceRange rangeOf(std::string_view source, std::string_view slice) {
  const auto offset = source.rfind(slice);
  EXPECT_NE(offset, std::string_view::npos);
  if (offset == std::string_view::npos)
    return {};
  SourcePos start{1, 1};
  for (const char character : source.substr(0, offset)) {
    if (character == '\n') {
      ++start.line;
      start.column = 1;
    } else {
      ++start.column;
    }
  }
  return {start,
          {start.line, start.column + static_cast<int32_t>(slice.size())}};
}

/** Paired instruction, declaration and evaluator cases pin source signedness. */
TEST(InstructionConstantExpressions,
     IntegerOperatorsShareDeclarationSemantics) {
  /** Expected value and source signedness shared by all numeric consumers. */
  struct Case {
    /** Written expression and its expected 64-bit source result. */
    std::string_view text;
    /** Expected result before destination-width conversion. */
    uint64_t bits;
    /** PTX source signedness before instruction conversion. */
    bool is_unsigned{};
  };
  const Case cases[] = {{"1 + 2 * 3", 7},
                        {"(1 + 2) * 3", 9},
                        {"(.s64)(-1U) >> 2", UINT64_MAX},
                        {"(.u64)-1 >> 63", 1, true},
                        {"(-5 % 3)", 2},
                        {"WARP_SZ + 1", 33},
                        {"1 ? -1 : 2U", UINT64_MAX, true},
                        {"(((-1 & 1) - 2) < 0)", 1},
                        {"(((0 | 1) - 2) < 0)", 1},
                        {"(((0 ^ 1) - 2) < 0)", 1},
                        {"(((-1 & 1U) - 2) < 0)", 0},
                        {"(((0 | (.u64)1) - 2) < 0)", 0},
                        {"(((0 ^ 1U) - 2) < 0)", 0},
                        {"(~0 < 0)", 0},
                        {"(2 <= 3) && (4 != 5)", 1}};
  for (const auto& fixture : cases) {
    SCOPED_TRACE(fixture.text);
    auto source_expression = expression(fixture.text);
    ASSERT_TRUE(source_expression);
    auto evaluated = declaration_semantics::numericConstantValue(
        *source_expression->expression);
    ASSERT_TRUE(evaluated) << evaluated.error().message;
    ASSERT_TRUE(
        std::holds_alternative<declaration_semantics::IntegerConstantValue>(
            *evaluated));
    EXPECT_EQ(
        std::get<declaration_semantics::IntegerConstantValue>(*evaluated).bits,
        fixture.bits);
    EXPECT_EQ(std::get<declaration_semantics::IntegerConstantValue>(*evaluated)
                  .is_unsigned,
              fixture.is_unsigned);
    PtxSyntaxParser parser(moduleSource(
        "mov.u64 %rd0, (" + std::string(fixture.text) + ");",
        ".global .u64 value = (" + std::string(fixture.text) + ");"));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty()) << ast.diagnostics.front().message;
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_EQ(firstMov(*resolved).bits, fixture.bits);
    const auto snapshot = test_support::resolveModuleSnapshot(*ast);
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->storage_declarations.front().first_constant_bits,
              fixture.bits);
    EXPECT_TRUE(validateModule(*resolved));
  }
}

/** Floating arithmetic evaluates in f64 and comparisons yield signed integers. */
TEST(InstructionConstantExpressions, FloatingDomainAndFinalNarrowing) {
  for (const auto text :
       {"1.0 + 2.0 * 3.0", "1 ? 7.0 : 9.0", "0d401c000000000000 + 0.0"}) {
    SCOPED_TRACE(text);
    auto source_expression = expression(text);
    ASSERT_TRUE(source_expression);
    auto value = declaration_semantics::numericConstantValue(
        *source_expression->expression);
    ASSERT_TRUE(value) << value.error().message;
    EXPECT_EQ(
        std::get<declaration_semantics::FloatingConstantValue>(*value).value,
        7.0);
    PtxSyntaxParser parser(
        moduleSource("mov.f32 %f0, (" + std::string(text) + ");"));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_EQ(firstMov(*resolved).bits, 0x40e00000u);
  }
  for (const auto text : {"0d7ff0000000000000 + 1.0", "1e308 * 1e308"}) {
    auto source_expression = expression(text);
    ASSERT_TRUE(source_expression);
    const auto value = declaration_semantics::numericConstantValue(
        *source_expression->expression);
    ASSERT_TRUE(value);
    EXPECT_TRUE(std::isinf(
        std::get<declaration_semantics::FloatingConstantValue>(*value).value));
  }
  PtxSyntaxParser tie_parser(
      moduleSource("mov.f32 %f0, (1.0 + 0.000000059604644775390625);"));
  auto tie_ast = tie_parser.parseModule();
  ASSERT_TRUE(tie_ast.has_value());
  auto tie_module = resolveAndValidateModule(*tie_ast);
  ASSERT_TRUE(tie_module);
  EXPECT_EQ(firstMov(*tie_module).bits, 0x3f800000u);
  for (const auto text :
       {"1.0 < 2.0", "2.0 == 2.0", "-2.0 <= -1.0", "3.0 > 2.0", "3.0 != 2.0"}) {
    auto source_expression = expression(text);
    ASSERT_TRUE(source_expression);
    auto value = declaration_semantics::numericConstantValue(
        *source_expression->expression);
    ASSERT_TRUE(value);
    const auto result =
        std::get<declaration_semantics::IntegerConstantValue>(*value);
    EXPECT_EQ(result.bits, 1u);
    EXPECT_FALSE(result.is_unsigned);
  }
}

/** Errors retain their failing subtree and never become a numeric zero. */
TEST(InstructionConstantExpressions, InvalidAndDeferredAreDistinct) {
  for (const auto text :
       {"1 / 0", "1 % 0", "1 << 64", "1 >> -1", "1 + 2.0", "(.u64)1.0",
        "1.0 & 2.0", "1.0 ? 2.0 : 3.0", "0f3f800000 + 1.0", "1.0 / 0.0",
        "0.0 / -0.0", "1 ? 7 : 1/0", "0 && 1/0", "1 || 1/0",
        "18446744073709551616 + 1"}) {
    SCOPED_TRACE(text);
    auto source_expression = expression(text);
    ASSERT_TRUE(source_expression);
    auto value = declaration_semantics::numericConstantValue(
        *source_expression->expression);
    ASSERT_FALSE(value);
    EXPECT_FALSE(value.error().deferred);
    EXPECT_LT(value.error().range.start.column, value.error().range.end.column);
    PtxSyntaxParser parser(
        moduleSource("mov.u64 %rd0, (" + std::string(text) + ");"));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  for (const auto text :
       {"1/0", "1.0/0.0", "0.0/-0.0", "1 ? 7 : 1/0", "0 && 1/0", "1 || 1/0"}) {
    SCOPED_TRACE(text);
    PtxSyntaxParser parser(moduleSource(
        "", ".global .u64 invalid = (" + std::string(text) + ");"));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  auto symbolic = expression("symbol + 1");
  ASSERT_TRUE(symbolic);
  auto deferred =
      declaration_semantics::numericConstantValue(*symbolic->expression);
  ASSERT_FALSE(deferred);
  EXPECT_TRUE(deferred.error().deferred);
  auto hidden_symbol = expression("1 ? 7 : symbol");
  ASSERT_TRUE(hidden_symbol);
  EXPECT_FALSE(
      declaration_semantics::numericConstantValue(*hidden_symbol->expression));
  auto mixed_invalid = expression("symbol + (1 / 0)");
  ASSERT_TRUE(mixed_invalid);
  const auto invalid =
      declaration_semantics::numericConstantValue(*mixed_invalid->expression);
  ASSERT_FALSE(invalid);
  EXPECT_FALSE(invalid.error().deferred);
  for (const auto& [text, failing] :
       {std::pair<std::string_view, std::string_view>{"1 ? 7 : 1/0", "1/0"},
        {"symbol + (1/0)", "1/0"},
        {"(1/0) + (2/0)", "1/0"},
        {"0 && 1/0", "1/0"},
        {"1 || 1/0", "1/0"},
        {"1 << 64", "64"},
        {"1.0 / -0.0", "1.0 / -0.0"}}) {
    SCOPED_TRACE(text);
    auto located = expression(text);
    ASSERT_TRUE(located);
    auto value =
        declaration_semantics::numericConstantValue(*located->expression);
    ASSERT_FALSE(value);
    const std::string fragment = "mov.u64 %rd0, (" + std::string(text) + ");";
    EXPECT_EQ(value.error().range, rangeOf(fragment, failing));

    const auto source = moduleSource(fragment, ".global .u64 symbol;");
    PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto resolved = resolveAndValidateModule(*ast);
    ASSERT_FALSE(resolved);
    EXPECT_EQ(resolved.error().front().range, rangeOf(source, failing));
  }
}

/** Copies preserve expression ownership and source identity observes operators. */
TEST(InstructionConstantExpressions, DeepCopyOwnedLifetimeAndSourceIdentity) {
  auto original = expression("1 + 2");
  ASSERT_TRUE(original);
  auto copied = *original;
  EXPECT_NE(copied.expression.get(), original->expression.get());
  auto& parenthesized =
      std::get<syntax_ast::AstConstantParenthesized>(copied.expression->node);
  auto& binary =
      std::get<syntax_ast::AstConstantBinary>(parenthesized.expression->node);
  binary.operation = syntax_ast::AstConstantBinaryOperator::Multiply;
  auto first =
      declaration_semantics::numericConstantValue(*original->expression);
  auto second = declaration_semantics::numericConstantValue(*copied.expression);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(std::get<declaration_semantics::IntegerConstantValue>(*first).bits,
            3u);
  EXPECT_EQ(std::get<declaration_semantics::IntegerConstantValue>(*second).bits,
            2u);
  std::optional<ResolvedModule> owned;
  {
    PtxSyntaxParser parser(moduleSource("mov.u64 %rd0, (1 + 2);"));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  EXPECT_EQ(firstMov(*owned).bits, 3u);
  EXPECT_TRUE(validateModule(*owned));
  PtxSyntaxParser changed_parser(moduleSource("mov.u64 %rd0, (1 * 2);"));
  auto changed = changed_parser.parseModule();
  ASSERT_TRUE(changed.has_value());
  EXPECT_FALSE(validateModule(*changed, *owned));
}

/** Every numeric entry honors the depth budget and recovers at a semicolon. */
TEST(InstructionConstantExpressions, DepthAndMalformedRecovery) {
  for (const auto depth : {PtxCstParser::maxConstantTreeDepth - 1,
                           PtxCstParser::maxConstantTreeDepth}) {
    const auto text =
        std::string(depth - 1, '(') + "1" + std::string(depth - 1, ')');
    PtxSyntaxParser parser(moduleSource("mov.u64 %rd0, " + text + ";"));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_TRUE(resolveAndValidateModule(*ast));
  }
  for (const auto text : {"(1 + )", "(1 + 2", "1 ? 2", "~"}) {
    SCOPED_TRACE(text);
    PtxSyntaxParser parser(moduleSource("mov.u64 %rd0, " + std::string(text) +
                                        "; mov.u32 %r0, 7;"));
    auto ast = parser.parseModule();
    EXPECT_FALSE(ast.diagnostics.empty());
    ASSERT_TRUE(ast.has_value());
    const auto* function =
        std::get_if<syntax_ast::AstFunction>(&ast->items.back());
    ASSERT_NE(function, nullptr);
    bool survivor = false;
    for (const auto& item : function->body) {
      const auto* instruction = std::get_if<syntax_ast::AstInstruction>(&item);
      if (instruction && instruction->opcode.syntax.text == "mov" &&
          instruction->operands.size() == 2) {
        const auto* source =
            std::get_if<syntax_ast::AstImmediate>(&instruction->operands[1]);
        survivor |= source && source->syntax.text == "7";
      }
    }
    EXPECT_TRUE(survivor);
  }
  const auto text = std::string(PtxCstParser::maxConstantTreeDepth, '(') + "1" +
                    std::string(PtxCstParser::maxConstantTreeDepth, ')');
  PtxSyntaxParser parser(
      moduleSource("mov.u64 %rd0, " + text + "; mov.u32 %r0, 7;"));
  auto ast = parser.parseModule();
  EXPECT_FALSE(ast.diagnostics.empty());
}

/** Numeric expressions do not widen destinations or register-only source slots. */
TEST(InstructionConstantExpressions,
     NumericConsumersAndRegisterOnlyBoundaries) {
  for (const auto body :
       {"add.u32 %r0, %r1, (1 + 2);", "mov.u32 %r0, !1;",
        "mov.u32 %r0, !1 + 2;", "mov.u32 %r0, !(1 + 2) + 3;",
        "vadd.u32.u32.u32 %r0, (1 + 2), (3 + 4);", "mov.pred %p0, !(1 + 2);",
        "ld.global.u32 %r0, [(4 * 4)];", "ld.global.u32 %r0, [010 + (2 * 4)];",
        "ld.global.u32 %r0, [(1 + 2 < 4 ? 16 : 32)];",
        "ld.global.u32 %r0, [%rd0 + (4 * 4)];", "bar.sync (1 - 1);",
        "shl.b32 %r0, %r1, (1 + 2);"}) {
    SCOPED_TRACE(body);
    PtxSyntaxParser parser(moduleSource(body));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty()) << ast.diagnostics.front().message;
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_TRUE(validateModule(*resolved));
  }
  for (const auto body :
       {"add.u32 (1 + 2), %r0, %r1;", "st.global.u32 [%rd0], (1 + 2);",
        "mov.b64 %rd0, {(1 + 2), (3 + 4)};"}) {
    PtxSyntaxParser parser(moduleSource(body));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
/** Specialized controls consume evaluated values and preserve their own limits. */
TEST(InstructionConstantExpressions, SpecializedNumericControls) {
  const std::string wgmma = R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.entry kernel() {
  .reg .b32 %d<2>;
  .reg .b64 %adesc, %bdesc;
  wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16
    {%d0,%d1}, %adesc, %bdesc, (0 + 1), (0 - 1), (0 + 1), (1 - 1), (0 + 1);
  ret;
})ptx";
  const std::string transfer = R"ptx(
.version 9.3
.target sm_110a
.address_size 64
.entry kernel() {
  .reg .b32 %r0, %t;
  tcgen05.ld.sync.aligned.16x32bx2.x1.b32 {%r0}, [%t], (0 - 16);
  tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r0}, [(1 - 1)];
  tcgen05.wait::ld.sync.aligned;
  ret;
})ptx";
  const std::string surface = moduleSource(
      "sust.b.1d.v2.b32.trap [surf0,{(1 - 1)}], {(1 + 2), (3 + 4)};",
      ".global .surfref surf0;");
  const std::string copy = moduleSource(
      "cp.async.ca.shared.global [dst], [src], (2 + 2), (1 + 1);",
      ".shared .align 16 .b8 dst[16]; .global .align 16 .b8 src[16];");
  for (const auto& source : {wgmma, transfer, surface, copy}) {
    PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty()) << ast.diagnostics.front().message;
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_TRUE(validateModule(*resolved));
  }
  for (const auto body :
       {"bar.sync (8 + 8);", "ld.global.u32 %r0, [(1U << 32)];",
        "ld.global.u32 %r0, [%rd0 + (1U << 32)];"}) {
    PtxSyntaxParser parser(moduleSource(body));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Calls retain the evaluated domain until a formal supplies its scalar width. */
TEST(InstructionConstantExpressions, CallFormalTypingAndOwnedRevalidation) {
  const std::string declarations =
      ".extern .func take(.reg .u32 x, .reg .f32 y);";
  std::optional<ResolvedModule> owned;
  {
    PtxSyntaxParser parser(
        moduleSource("call take, ((1 + 2), (1.0 + 2.0));", declarations));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty()) << ast.diagnostics.front().message;
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  const auto& call =
      dynamic_cast<const CallDirect&>(*owned->functions.back().body.front());
  ASSERT_TRUE(call.arguments);
  const auto& integer =
      std::get<ResolvedCallLiteral>(call.arguments->value.values[0].value);
  const auto& floating =
      std::get<ResolvedCallLiteral>(call.arguments->value.values[1].value);
  ASSERT_TRUE(integer.source_value);
  ASSERT_TRUE(integer.value);
  ASSERT_TRUE(floating.source_value);
  ASSERT_TRUE(floating.value);
  EXPECT_EQ(integer.value->bits, 3u);
  EXPECT_EQ(integer.value->type, ScalarType::U32);
  EXPECT_EQ(floating.value->bits, 0x40400000u);
  EXPECT_EQ(floating.value->type, ScalarType::F32);
  EXPECT_TRUE(validateModule(*owned));
  auto& mutable_call =
      dynamic_cast<CallDirect&>(*owned->functions.back().body.front());
  auto& retained = std::get<ResolvedCallLiteral>(
      mutable_call.arguments->value.values[0].value);
  retained.source_value = declaration_semantics::IntegerConstantValue{4, false};
  EXPECT_FALSE(validateModule(*owned));
  for (const auto arguments :
       {"((1U << 32), (1.0 + 2.0))", "((1.0 + 2.0), (1.0 + 2.0))"}) {
    PtxSyntaxParser parser(moduleSource(
        "call take, " + std::string(arguments) + ";", declarations));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
