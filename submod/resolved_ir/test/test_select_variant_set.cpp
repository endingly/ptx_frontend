#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/set/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/set/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/set/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for a generated-opcode test. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

TEST(ResolveSet, SelectsOrdinaryTypedFamilies) {
  const auto eq =
      resolve<Set>(parse_instruction("set.eq.u32.u32 %r0, %r1, 16;"));
  ASSERT_TRUE(eq.has_value()) << eq.error().message;
  const auto* unsigned_result = std::get_if<Set::Unsigned>(&eq->variant);
  ASSERT_NE(unsigned_result, nullptr);
  EXPECT_EQ(unsigned_result->comparison.value, ComparisonOperator::Eq);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(unsigned_result->src2.value));

  const auto lt_and =
      resolve<Set>(parse_instruction("set.lt.and.f32.s32 %f0, %s0, -1, !%p0;"));
  ASSERT_TRUE(lt_and.has_value()) << lt_and.error().message;
  const auto* signed_boolean =
      std::get_if<Set::SignedBoolean>(&lt_and->variant);
  ASSERT_NE(signed_boolean, nullptr);
  EXPECT_EQ(signed_boolean->comparison.value, ComparisonOperator::Lt);
  EXPECT_EQ(signed_boolean->boolean.value, BooleanOperator::And);
  EXPECT_TRUE(
      std::get<ResolvedPredicate>(signed_boolean->combine.value).negated);
}

TEST(ResolveSet, RejectsInvalidOrdinaryModifierDomains) {
  for (const auto source : {
           "set.lt.u32.b32 %r0, %r1, %r2;",
           "set.nan.u32.s32 %r0, %r1, %r2;",
           "set.eq.ftz.u32.f64 %r0, %r1, %r2;",
           "set.eq.u16.u32 %r0, %r1, %r2;",
       }) {
    const auto selected = selectVariant<Set>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedSetAvailability) {
  for (const auto source : {
           "set.eq.u32.u32 %r0, %r1, %r2;",
           "set.lt.and.f32.s32 %f0, %s0, %s1, !%p0;",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto set = resolve<Set>(*ast);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    const auto rejected =
        check(*set, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        check(*set, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
