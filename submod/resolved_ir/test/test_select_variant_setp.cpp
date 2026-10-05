#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/setp.gen.hpp>
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
  const auto simple = resolveSetp(simple_ast);
  ASSERT_TRUE(simple.has_value()) << simple.error().message;
  const auto* lt = dynamic_cast<SetpUnsigned*>(simple->get());
  ASSERT_NE(lt, nullptr);
  EXPECT_EQ(lt->comparison.value, ComparisonOperator::Lt);

  const auto combined_ast =
      parse_instruction("setp.lt.and.u32 %p0, %r0, 16, !%p1;");
  const auto combined = resolveSetp(combined_ast);
  ASSERT_TRUE(combined.has_value()) << combined.error().message;
  const auto* lt_and =
      dynamic_cast<SetpUnsignedBoolean*>(combined->get());
  ASSERT_NE(lt_and, nullptr);
  EXPECT_EQ(lt_and->comparison.value, ComparisonOperator::Lt);
  EXPECT_EQ(lt_and->boolean.value, BooleanOperator::And);
  EXPECT_TRUE(
      std::get<ResolvedPredicate>(lt_and->combine.value).negated);
}

TEST(ResolveSetp, SelectsCompleteSignedVariant) {
  const auto resolved =
      resolveSetp(parse_instruction("setp.ge.s32 %p0, %r0, -1;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* ge = dynamic_cast<SetpSigned*>(resolved->get());
  ASSERT_NE(ge, nullptr);
  EXPECT_EQ(ge->comparison.value, ComparisonOperator::Ge);
}

TEST(ResolveSetp, SelectsCompleteDualPredicateVariants) {
  const auto equality_ast = parse_instruction("setp.eq.u32 %p0|%p1, %r0, %r1;");
  const auto equality = resolveSetp(equality_ast);
  ASSERT_TRUE(equality.has_value()) << equality.error().message;
  const auto* eq = dynamic_cast<SetpUnsigned*>(equality->get());
  ASSERT_NE(eq, nullptr);
  EXPECT_EQ(eq->comparison.value, ComparisonOperator::Eq);

  const auto combined_ast =
      parse_instruction("setp.lt.and.s32 %p0|%p1, %s0, %s1, %p2;");
  const auto combined = resolveSetp(combined_ast);
  ASSERT_TRUE(combined.has_value()) << combined.error().message;
  const auto* lt_and =
      dynamic_cast<SetpSignedBoolean*>(combined->get());
  ASSERT_NE(lt_and, nullptr);
  EXPECT_EQ(lt_and->comparison.value, ComparisonOperator::Lt);
  EXPECT_EQ(lt_and->boolean.value, BooleanOperator::And);
  const auto& combine =
      std::get<ResolvedPredicate>(lt_and->combine.value);
  EXPECT_FALSE(combine.negated);
  EXPECT_EQ(combine.register_ref.spelling, "%p2");
}

TEST(ResolveSetp, RejectsUnfrozenDualPredicateForms) {
  for (const auto source : {
           "setp.eq.u32 _|_, %r0, %r1;",
       }) {
    const auto selected = resolveSetp(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }

  const auto non_predicate =
      resolveSetp(parse_instruction("setp.eq.u32 %r0|%p1, %r0, %r1;"));
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
    const auto selected = resolveSetp(parse_instruction(source));
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
  const auto setp = resolveSetp(*ast);
  ASSERT_TRUE(setp.has_value()) << setp.error().message;
  const auto rejected =
      (*setp)->check( Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                           .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().range, ast->range);
  EXPECT_TRUE(
      (*setp)->check( Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                           .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedSetpGeS32Availability) {
  PtxSyntaxParser parser("setp.ge.s32 %p0, %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto setp = resolveSetp(*ast);
  ASSERT_TRUE(setp.has_value()) << setp.error().message;
  const auto rejected =
      (*setp)->check( Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                           .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      (*setp)->check( Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
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
    const auto setp = resolveSetp(*ast);
    ASSERT_TRUE(setp.has_value()) << setp.error().message;
    const auto rejected =
        (*setp)->check( Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                             .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        (*setp)->check( Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                             .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
