#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/model/matrix/wgmma.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse complete numeric-role source and release text/parser/AST before return. */
std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveIntegerRoleSource(std::string body, bool check_source = true,
                         std::string_view target = "sm_90") {
  const std::string source =
      ".version 9.3\n.target " + std::string(target) +
      "\n.address_size 64\n.entry kernel() { .reg .pred %p0; "
      ".reg .b64 %q0, %adesc, %bdesc; .reg .b32 %d<2>; " +
      body + " }";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return check_source ? resolveAndValidateModule(*ast)
                      : resolveModuleOnly(*ast);
}

/** Build a legal WGMMA form whose only varied operand is its predicate control. */
std::string scaleDInstruction(std::string_view source) {
  return "wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16 "
         "{%d0,%d1}, %adesc, %bdesc, " +
         std::string(source) + ", 1, 1, 0, 0;";
}

/** Floating payloads never gain integer predicate semantics through a B64 carrier. */
TEST(IntegerControlSources,
     RejectsFloatingPredicateSourcesBeforeTruthCoercion) {
  constexpr std::array sources{"0.0",
                               "1.0",
                               "0d0000000000000000",
                               "0d0000000000000001",
                               "+0d0000000000000000",
                               "-0d8000000000000000",
                               "(0.0)",
                               "(0.5 + 0.5)",
                               "(1 ? 0.0 : 1.0)",
                               "!(0.0 + 0.0)",
                               "0f00000000",
                               "(0f00000000)",
                               "+0f00000000",
                               "-0f00000000"};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    const std::string instruction =
        "mov.pred %p0, " + std::string(source) + ";";
    PtxSyntaxParser parser(instruction);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto resolved = resolveInstruction(*ast);
    ASSERT_FALSE(resolved.has_value());
    const auto operand_range = syntax_ast::sourceRange(ast->operands[1]);
    EXPECT_EQ(resolved.error().range.start.line, operand_range.start.line);
    EXPECT_GE(resolved.error().range.start.column, operand_range.start.column);
    EXPECT_LE(resolved.error().range.end.column, operand_range.end.column);
    for (const auto check_source : {true, false})
      EXPECT_FALSE(resolveIntegerRoleSource(instruction, check_source));
  }
}

/** Parser rejection and programmatic compact ASTs both preserve integer-only inversion. */
TEST(IntegerControlSources,
     RejectsFloatingInversionAtTextAndTypedAstBoundaries) {
  for (const auto source : {"0.0", "0d0000000000000001"}) {
    for (const auto prefix : {"mov.pred %p0, ", "mov.b64 %q0, "}) {
      PtxSyntaxParser textual(std::string(prefix) + "!" + source + ";");
      const auto rejected = textual.parseInstruction();
      EXPECT_FALSE(rejected.has_value() && rejected.diagnostics.empty());

      PtxSyntaxParser parser(std::string(prefix) + source + ";");
      auto ast = parser.parseInstruction();
      ASSERT_TRUE(ast.has_value());
      ASSERT_TRUE(ast.diagnostics.empty());
      const auto literal = std::get<syntax_ast::AstImmediate>(ast->operands[1]);
      ast->operands[1] = syntax_ast::AstNegatedImmediate{
          .immediate = literal, .range = literal.syntax.range};
      const auto resolved = resolveInstruction(*ast);
      ASSERT_FALSE(resolved.has_value());
      EXPECT_EQ(resolved.error().message,
                "This operand requires an integer constant source.");
      EXPECT_EQ(resolved.error().range, literal.syntax.range);
    }
  }
}

/** Integer truth, inversion, and integer-result floating comparisons retain canonical bools. */
TEST(IntegerControlSources,
     PreservesIntegerPredicateResultsAfterCopyCloneAndSemanticEdit) {
  /** One expression and its independently specified canonical truth value. */
  struct Fixture {
    /** Written integer-valued source, which may contain floating comparisons. */
    std::string_view source;
    /** Expected truth after any source inversion. */
    bool value;
  };
  constexpr std::array fixtures{Fixture{"0", false},
                                Fixture{"2", true},
                                Fixture{"+2", true},
                                Fixture{"+0", false},
                                Fixture{"-1", true},
                                Fixture{"-0", false},
                                Fixture{"18446744073709551615U", true},
                                Fixture{"!0", true},
                                Fixture{"!1", false},
                                Fixture{"!-1", false},
                                Fixture{"(1.0 < 2.0)", true},
                                Fixture{"(2.0 < 1.0)", false},
                                Fixture{"!(1.0 < 2.0)", false},
                                Fixture{"!(2.0 < 1.0)", true},
                                Fixture{"(!(-2))", false}};
  for (const auto& fixture : fixtures) {
    SCOPED_TRACE(fixture.source);
    for (const auto check_source : {true, false}) {
      auto module = resolveIntegerRoleSource(
          "mov.pred %p0, " + std::string(fixture.source) + ";", check_source);
      ASSERT_TRUE(module.has_value()) << module.error().front().message;
      auto& instruction = *module->functions.front().body.front();
      auto& predicate = std::get<ResolvedPredicateConstant>(
          dynamic_cast<MovPred&>(instruction).src.value);
      EXPECT_EQ(predicate.value, fixture.value);
      EXPECT_TRUE(validateModule(*module));
      auto copy = *module;
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_TRUE(validateModule(copy));
      // A public canonical bool supports semantic edits without erased source history.
      predicate.value = !predicate.value;
      EXPECT_TRUE(validateModule(*module));
    }
  }
}

/** Scale-d rejects floating origin before its independent zero/one range checks. */
TEST(IntegerControlSources,
     RejectsFloatingWgmmaScaleDAndPreservesIntegerRangeRules) {
  for (const auto source :
       {"0.0", "0d0000000000000001", "-0d8000000000000000", "(0.0 + 0.0)",
        "(1 ? 0.0 : 1.0)", "!(0.0 + 0.0)", "2", "-1"}) {
    SCOPED_TRACE(source);
    for (const auto check_source : {true, false})
      EXPECT_FALSE(resolveIntegerRoleSource(scaleDInstruction(source),
                                            check_source, "sm_90a"));
  }
  for (const auto source : {"0", "1", "-0", "!0", "!1", "(1.0 < 2.0)",
                            "(2.0 < 1.0)", "!(1.0 < 2.0)", "!(2.0 < 1.0)"}) {
    SCOPED_TRACE(source);
    const auto module =
        resolveIntegerRoleSource(scaleDInstruction(source), true, "sm_90a");
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    const auto& instruction = *module->functions.front().body.front();
    const auto& wgmma =
        dynamic_cast<const WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain&>(
            instruction);
    EXPECT_TRUE(
        std::holds_alternative<ResolvedPredicateConstant>(wgmma.scale_d.value));
    EXPECT_TRUE(validateModule(*module));
    auto copy = *module;
    copy.functions.front().body.front() = instruction.clone();
    EXPECT_TRUE(validateModule(copy));
  }
}

/** Compact logical-not must check origin before creating its integer boolean. */
TEST(IntegerControlSources,
     LogicalNotRetainsIntegerDomainWithoutBanningFloatingBitData) {
  for (const auto source : {"!(1.0 + 2.0)"}) {
    SCOPED_TRACE(source);
    for (const auto check_source : {true, false})
      EXPECT_FALSE(resolveIntegerRoleSource(
          "mov.b64 %q0, " + std::string(source) + ";", check_source));
  }
  for (const auto source : {"0.0", "0d0000000000000001", "(1.0 + 2.0)", "!0",
                            "!2", "!(1.0 < 2.0)", "!(2.0 < 1.0)"}) {
    SCOPED_TRACE(source);
    const auto module =
        resolveIntegerRoleSource("mov.b64 %q0, " + std::string(source) + ";");
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    const auto& immediate = std::get<ResolvedImmediate>(
        dynamic_cast<const MovScalar&>(*module->functions.front().body.front())
            .src_mov_source->value);
    EXPECT_EQ(immediate.type, ScalarType::B64);
    EXPECT_EQ(immediate.integer_source_bits.has_value(),
              std::string_view(source).starts_with('!'));
    EXPECT_TRUE(validateModule(*module));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
