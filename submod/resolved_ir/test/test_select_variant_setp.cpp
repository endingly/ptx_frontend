#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/setp/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/setp/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/setp/checker.gen.hpp>
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

TEST(ResolveSetp, SelectsCompleteUnsignedVariants) {
  const auto simple_ast = parse_instruction("setp.lt.u32 %p0, %r0, 16;");
  const auto simple = resolve<Setp>(simple_ast);
  ASSERT_TRUE(simple.has_value()) << simple.error().message;
  const auto* lt = std::get_if<Setp::Unsigned>(&simple->variant);
  ASSERT_NE(lt, nullptr);
  EXPECT_EQ(lt->comparison.value, ComparisonOperator::Lt);

  const auto combined_ast =
      parse_instruction("setp.lt.and.u32 %p0, %r0, 16, !%p1;");
  const auto combined = resolve<Setp>(combined_ast);
  ASSERT_TRUE(combined.has_value()) << combined.error().message;
  const auto* lt_and = std::get_if<Setp::UnsignedBoolean>(&combined->variant);
  ASSERT_NE(lt_and, nullptr);
  EXPECT_EQ(lt_and->comparison.value, ComparisonOperator::Lt);
  EXPECT_EQ(lt_and->boolean.value, BooleanOperator::And);
  const auto& operands =
      std::get<Setp::UnsignedBoolean::SingleOperands>(lt_and->operands);
  EXPECT_TRUE(std::get<ResolvedPredicate>(operands.combine.value).negated);
}

TEST(ResolveSetp, SelectsCompleteSignedVariant) {
  const auto resolved =
      resolve<Setp>(parse_instruction("setp.ge.s32 %p0, %r0, -1;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* ge = std::get_if<Setp::Signed>(&resolved->variant);
  ASSERT_NE(ge, nullptr);
  EXPECT_EQ(ge->comparison.value, ComparisonOperator::Ge);
}

TEST(ResolveSetp, SelectsCompleteDualPredicateVariants) {
  const auto equality_ast = parse_instruction("setp.eq.u32 %p0|%p1, %r0, %r1;");
  const auto equality = resolve<Setp>(equality_ast);
  ASSERT_TRUE(equality.has_value()) << equality.error().message;
  const auto* eq = std::get_if<Setp::Unsigned>(&equality->variant);
  ASSERT_NE(eq, nullptr);
  EXPECT_EQ(eq->comparison.value, ComparisonOperator::Eq);

  const auto combined_ast =
      parse_instruction("setp.lt.and.s32 %p0|%p1, %s0, %s1, %p2;");
  const auto combined = resolve<Setp>(combined_ast);
  ASSERT_TRUE(combined.has_value()) << combined.error().message;
  const auto* lt_and = std::get_if<Setp::SignedBoolean>(&combined->variant);
  ASSERT_NE(lt_and, nullptr);
  EXPECT_EQ(lt_and->comparison.value, ComparisonOperator::Lt);
  EXPECT_EQ(lt_and->boolean.value, BooleanOperator::And);
  const auto& operands =
      std::get<Setp::SignedBoolean::PairOperands>(lt_and->operands);
  const auto& combine = std::get<ResolvedPredicate>(operands.combine.value);
  EXPECT_FALSE(combine.negated);
  EXPECT_EQ(combine.register_ref.spelling, "%p2");
}

TEST(ResolveSetp, RejectsUnfrozenDualPredicateForms) {
  for (const auto source : {
           "setp.eq.u32 _|_, %r0, %r1;",
       }) {
    const auto selected = resolve<Setp>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }

  const auto non_predicate =
      resolve<Setp>(parse_instruction("setp.eq.u32 %r0|%p1, %r0, %r1;"));
  EXPECT_FALSE(non_predicate.has_value());
}

TEST(ResolveSetp, RejectsIllegalCompleteForms) {
  for (const auto source : {
           "setp.lo.s32 %p0, %r0, %r1;",
           "setp.equ.u32 %p0, %r0, %r1;",
           "setp.ftz.f64 %p0, %fd0, %fd1;",
           "setp.eq.f16x2 %p0, %r0, %r1;",
           "setp.ge.s32 %r0, %r1, %r2;",
       }) {
    const auto selected = resolve<Setp>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedSetpLtU32Availability) {
  PtxSyntaxParser parser("setp.lt.u32 %p0, %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto setp = resolve<Setp>(*ast);
  ASSERT_TRUE(setp.has_value()) << setp.error().message;
  const auto rejected =
      check(*setp, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                           .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().range, ast->range);
  EXPECT_TRUE(
      check(*setp, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                           .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedSetpGeS32Availability) {
  PtxSyntaxParser parser("setp.ge.s32 %p0, %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto setp = resolve<Setp>(*ast);
  ASSERT_TRUE(setp.has_value()) << setp.error().message;
  const auto rejected =
      check(*setp, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                           .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*setp, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                           .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedSetpDualPredicateAvailability) {
  for (const auto source : {
           "setp.eq.u32 %p0|%p1, %r0, %r1;",
           "setp.lt.and.s32 %p0|%p1, %s0, %s1, %p2;",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto setp = resolve<Setp>(*ast);
    ASSERT_TRUE(setp.has_value()) << setp.error().message;
    const auto rejected =
        check(*setp, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                             .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        check(*setp, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                             .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
