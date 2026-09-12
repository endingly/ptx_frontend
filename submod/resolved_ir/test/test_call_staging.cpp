#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a complete call fixture; syntax failures cannot satisfy rejection tests. */
std::optional<syntax_ast::AstModule> parseCallModule(std::string_view body) {
  const std::string source = std::string{R"ptx(
.version 9.3
.target sm_80
.address_size 64
.file 1 "staging.ptx"
.extern .func (.param .b32 output) take2(.param .b32 x, .param .b32 y);
.entry k() {
  .reg .b32 %r<3>;
  .reg .pred %p;
)ptx"} + std::string{body} + "\n}";
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    ADD_FAILURE() << "Call fixture must parse without diagnostics.";
    return std::nullopt;
  }
  return std::move(*ast);
}

/** Hoisted and interleaved declarations preserve instructions and bound arguments. */
TEST(CallStaging, PreservesCallsAcrossOrdinaryDeclarations) {
  for (const std::string_view body : {
           R"ptx(
  .param .b32 a, b, result;
  st.param.b32 [a], %r0;
  st.param.b32 [b], %r1;
  call (result), take2, (a, b);
  ld.param.b32 %r0, [result];
  ld.param.b32 %r1, [result];
)ptx",
           R"ptx(
  .param .b32 a;
  st.param.b32 [a], %r0;
  .param .b32 b;
  st.param.b32 [b], %r1;
  .param .b32 result;
  call (result), take2, (a, b);
  .reg .b32 %temporary;
  ld.param.b32 %r0, [result];
  .local .b32 scratch;
  ld.param.b32 %r1, [result];
)ptx",
           R"ptx(
  .param .b32 a;
  st.param.b32 [a], %r0;
  .loc 1 20 0
  .param .b32 b;
  .pragma "nounroll";
  st.param.b32 [b], %r1;
  .param .b32 result;
  .loc 1 21 0
  call (result), take2, (a, b);
  .param .b32 unused;
  .pragma "nounroll";
  ld.param.b32 %r0, [result];
  .reg .b32 %temporary;
  .loc 1 22 0
  ld.param.b32 %r1, [result];
)ptx"}) {
    SCOPED_TRACE(body);
    const auto ast = parseCallModule(body);
    ASSERT_TRUE(ast);
    const auto module = resolveModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    ASSERT_EQ(module->functions.size(), 2u);
    const auto& function = module->functions.back();
    ASSERT_EQ(function.body.size(), 5u);
    EXPECT_TRUE(std::holds_alternative<St>(function.body[0]));
    EXPECT_TRUE(std::holds_alternative<St>(function.body[1]));
    EXPECT_TRUE(std::holds_alternative<Ld>(function.body[3]));
    EXPECT_TRUE(std::holds_alternative<Ld>(function.body[4]));
    const auto* call = std::get_if<Call>(&function.body[2]);
    ASSERT_NE(call, nullptr);
    const auto& operands = std::get<Call::Direct::ReturnTargetInputOperands>(
        std::get<Call::Direct>(call->variant).operands);
    const auto scope = module->symbols.symbol(function.symbol_id).owned_scope;
    ASSERT_TRUE(scope);
    const auto result = module->symbols.lookup(*scope, "result");
    ASSERT_TRUE(result);
    EXPECT_EQ(operands.return_value.value.symbol_id, result->symbol);
    ASSERT_EQ(operands.arguments.value.values.size(), 2u);
    for (size_t index = 0; index < 2; ++index) {
      const auto symbol =
          module->symbols.lookup(*scope, index == 0 ? "a" : "b");
      ASSERT_TRUE(symbol);
      const auto& argument = std::get<ResolvedCallParameterRef>(
          operands.arguments.value.values[index].value);
      EXPECT_EQ(argument.symbol_id, symbol->symbol);
    }
  }
}

/** Instructions, labels, metadata, and blocks remain boundaries in both directions. */
TEST(CallStaging, RejectsInstructionAndControlBoundaries) {
  for (const bool before_call : {true, false}) {
    for (const std::string_view gap :
         {"mov.b32 %r2, %r2;", "bra resume; resume:", "resume:",
          "{ .reg .b32 %inner; }", "prototype: .callprototype _;",
          "callees: .calltargets take2;", "branches: .branchtargets done;"}) {
      SCOPED_TRACE(before_call ? "argument store" : "return load");
      SCOPED_TRACE(gap);
      const std::string body =
          std::string{".param .b32 a, b, result;\nst.param.b32 [a], %r0;\n"} +
          (before_call ? std::string{gap} : "") +
          "\ncall (result), take2, (a, b);\n" +
          (before_call ? "" : std::string{gap}) +
          "\nld.param.b32 %r1, [result];\ndone:";
      const auto ast = parseCallModule(body);
      ASSERT_TRUE(ast);
      const auto& function =
          std::get<syntax_ast::AstFunction>(ast->items.back());
      std::optional<SourceRange> offending_range;
      for (const auto& item : function.body) {
        const auto* instruction =
            std::get_if<syntax_ast::AstInstruction>(&item);
        if (instruction &&
            instruction->opcode.syntax.text == (before_call ? "st" : "ld"))
          offending_range = instruction->range;
      }
      ASSERT_TRUE(offending_range);
      const auto module = resolveModule(*ast);
      ASSERT_FALSE(module);
      ASSERT_EQ(module.error().size(), 1u);
      EXPECT_EQ(module.error().front().stage(),
                ResolveDiagnosticStage::Resolution);
      EXPECT_EQ(module.error().front().range, *offending_range);
      EXPECT_EQ(
          module.error().front().message,
          before_call
              ? "A function-local .param argument store must be in the "
                "contiguous block immediately before a call that uses it."
              : "A function-local .param return load must be in the "
                "contiguous block immediately after a call that returns it.");
    }
  }
}

/** Declarations must not hide illegal predication of either staging operation. */
TEST(CallStaging, RejectsPredicationAcrossDeclarations) {
  for (const bool store : {true, false}) {
    SCOPED_TRACE(store ? "argument store" : "return load");
    const std::string body =
        std::string{".param .b32 a, b, result;\n"} + (store ? "@%p " : "") +
        "st.param.b32 [a], %r0;\n" +
        ".reg .b32 %temporary;\ncall (result), take2, (a, b);\n" +
        ".param .b32 unused;\n" + (store ? "" : "@%p ") +
        "ld.param.b32 %r1, [result];";
    const auto ast = parseCallModule(body);
    ASSERT_TRUE(ast);
    const auto& function = std::get<syntax_ast::AstFunction>(ast->items.back());
    std::optional<SourceRange> predicate_range;
    for (const auto& item : function.body) {
      if (const auto* instruction =
              std::get_if<syntax_ast::AstInstruction>(&item);
          instruction && instruction->predicate)
        predicate_range = instruction->predicate->range;
    }
    ASSERT_TRUE(predicate_range);
    const auto module = resolveModule(*ast);
    ASSERT_FALSE(module);
    ASSERT_EQ(module.error().size(), 1u);
    EXPECT_EQ(module.error().front().stage(),
              ResolveDiagnosticStage::Resolution);
    EXPECT_EQ(module.error().front().range, *predicate_range);
    EXPECT_EQ(
        module.error().front().message,
        store ? "A function-local .param argument store cannot be predicated."
              : "A function-local .param return load cannot be predicated.");
  }
}

/** A nested sequence binds its own parameters, but cannot load an outer call's return. */
TEST(CallStaging, KeepsNestedSequencesWithinTheirScope) {
  const auto ast = parseCallModule(R"ptx(
  .param .b32 result;
  {
    .param .b32 a, b;
    st.param.b32 [a], %r0;
    .param .b32 result;
    call (result), take2, (a, b);
    .reg .b32 %temporary;
    ld.param.b32 %temporary, [result];
  }
)ptx");
  ASSERT_TRUE(ast);
  const auto module = resolveModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  const auto& function = module->functions.back();
  ASSERT_EQ(function.body.size(), 3u);
  const auto scope = module->symbols.symbol(function.symbol_id).owned_scope;
  ASSERT_TRUE(scope);
  const auto outer = module->symbols.lookup(*scope, "result");
  ASSERT_TRUE(outer);
  const auto& operands = std::get<Call::Direct::ReturnTargetInputOperands>(
      std::get<Call::Direct>(std::get<Call>(function.body[1]).variant)
          .operands);
  ASSERT_TRUE(operands.return_value.value.symbol_id);
  EXPECT_NE(operands.return_value.value.symbol_id, outer->symbol);
  const auto& load =
      std::get<Ld::ExplicitScalar>(std::get<Ld>(function.body[2]).variant);
  EXPECT_EQ(std::get<ResolvedSymbolRef>(load.address.value.base).symbol_id,
            operands.return_value.value.symbol_id);

  const auto crossed_ast = parseCallModule(R"ptx(
  .param .b32 a, b, result;
  call (result), take2, (a, b);
  {
    .reg .b32 %temporary;
    ld.param.b32 %temporary, [result];
  }
)ptx");
  ASSERT_TRUE(crossed_ast);
  const auto crossed = resolveModule(*crossed_ast);
  ASSERT_FALSE(crossed);
  ASSERT_EQ(crossed.error().size(), 1u);
  const auto& crossed_function =
      std::get<syntax_ast::AstFunction>(crossed_ast->items.back());
  const auto& block = *std::get<std::unique_ptr<syntax_ast::AstBlock>>(
      crossed_function.body.back());
  EXPECT_EQ(crossed.error().front().range,
            std::get<syntax_ast::AstInstruction>(block.body.back()).range);
  EXPECT_EQ(crossed.error().front().stage(),
            ResolveDiagnosticStage::Resolution);
  EXPECT_EQ(crossed.error().front().message,
            "A function-local .param return load must be in the contiguous "
            "block immediately after a call that returns it.");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
