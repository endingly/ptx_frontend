#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/lop3/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/lop3/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/lop3/resolution.gen.hpp>
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

TEST(ResolveLop3, SelectsFrozenB32LutVariant) {
  for (const auto source :
       {"lop3.b32 %r0, %r1, %r2, %r3, 0x1a;", "lop3.b32 %r0, %r1, %r2, %r3, 0;",
        "lop3.b32 %r0, %r1, %r2, %r3, 255;"}) {
    SCOPED_TRACE(source);
    const auto resolved = resolve<Lop3>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_NE(std::get_if<Lop3::B32>(&resolved->variant), nullptr);
    EXPECT_EQ(Lop3::B32::type, ScalarType::B32);
  }
}

TEST(ResolveLop3, SelectsBoolopLayoutAndRejectsNonImmediateLut) {
  const auto boolop = resolve<Lop3>(
      parse_instruction("lop3.and.b32 _|%p0, 1, %r2, 3, 0x1a, !%p1;"));
  ASSERT_TRUE(boolop.has_value()) << boolop.error().message;
  ASSERT_NE(std::get_if<Lop3::BoolopB32>(&boolop->variant), nullptr);
  EXPECT_FALSE(
      resolve<Lop3>(parse_instruction("lop3.b32 %r0, %r1, %r2, %r3, %r4;"))
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedLop3AvailabilityAndLutRange) {
  for (const auto source : {"lop3.b32 %r0, %r1, %r2, %r3, 0;",
                            "lop3.b32 %r0, %r1, %r2, %r3, 255;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto lop3 = resolve<Lop3>(*ast);
    ASSERT_TRUE(lop3.has_value()) << lop3.error().message;
    const auto old_ptx = check(
        *lop3, Context{.target = {.ptx_version = {4, 2}, .sm_version = 50},
                       .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check(
        *lop3, Context{.target = {.ptx_version = {4, 3}, .sm_version = 49},
                       .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(check(*lop3, Context{.target = {.ptx_version = {4, 3},
                                                .sm_version = 50},
                                     .instruction_range = ast->range})
                    .has_value());
  }

  for (const auto source : {"lop3.b32 %r0, %r1, %r2, %r3, 256;",
                            "lop3.b32 %r0, %r1, %r2, %r3, -1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto lop3 = resolve<Lop3>(*ast);
    ASSERT_TRUE(lop3.has_value()) << lop3.error().message;
    const auto checked = check(
        *lop3, Context{.target = {.ptx_version = {4, 3}, .sm_version = 50},
                       .instruction_range = ast->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedLop3BoolopAvailability) {
  PtxSyntaxParser parser("lop3.or.b32 _|%p0, 1, %r1, 3, 255, !%p1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto lop3 = resolve<Lop3>(*ast);
  ASSERT_TRUE(lop3.has_value()) << lop3.error().message;
  const auto old_ptx =
      check(*lop3, Context{.target = {.ptx_version = {8, 1}, .sm_version = 70},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      check(*lop3, Context{.target = {.ptx_version = {8, 2}, .sm_version = 69},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*lop3, Context{.target = {.ptx_version = {8, 2}, .sm_version = 70},
                           .instruction_range = ast->range})
          .has_value());
}

/** Paired data/predicate destinations retain the same complemented-write rule. */
TEST(ResolvedIrChecker, RevalidationRejectsMutatedLop3PredicateDestination) {
  PtxSyntaxParser parser("lop3.and.b32 _|%p0, 1, %r1, 3, 255, !%p1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  auto resolved = resolve<Lop3>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& destination =
      std::get<Lop3::BoolopB32>(resolved->variant).dst.value.predicate;
  ASSERT_TRUE(destination.has_value());
  destination->value.negated = true;
  const auto checked = check(
      *resolved, Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                         .instruction_range = ast->range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            CheckDiagnosticKind::UnsupportedOperandShape);
}

/** Revalidation rejects a predicate lane removed from a lop3 paired output. */
TEST(ResolvedIrChecker, RevalidationRejectsMissingLop3PredicateLane) {
  PtxSyntaxParser parser("lop3.and.b32 %r0|%p0, 1, %r1, 3, 255, %p1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  auto resolved = resolve<Lop3>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& destination = std::get<Lop3::BoolopB32>(resolved->variant).dst.value;
  destination.predicate.reset();
  const auto checked = check(
      *resolved, Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                         .instruction_range = ast->range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            CheckDiagnosticKind::UnsupportedOperandShape);
}

/** Revalidation rejects a lop3 paired output after both lanes are removed. */
TEST(ResolvedIrChecker, RevalidationRejectsEmptyLop3PairedDestination) {
  PtxSyntaxParser parser("lop3.and.b32 %r0|%p0, 1, %r1, 3, 255, %p1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  auto resolved = resolve<Lop3>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& destination = std::get<Lop3::BoolopB32>(resolved->variant).dst.value;
  destination.data.reset();
  destination.predicate.reset();
  const auto checked = check(
      *resolved, Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                         .instruction_range = ast->range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            CheckDiagnosticKind::UnsupportedOperandShape);
}

/** A lop3 Boolean result permits only its data lane to become a sink. */
TEST(ResolvedIrChecker, RevalidationAllowsLop3DataSinkWithPredicateLane) {
  PtxSyntaxParser parser("lop3.and.b32 %r0|%p0, 1, %r1, 3, 255, %p1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  auto resolved = resolve<Lop3>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  std::get<Lop3::BoolopB32>(resolved->variant).dst.value.data.reset();
  EXPECT_TRUE(check(*resolved, Context{.target = {.ptx_version = {9, 3},
                                                  .sm_version = 100},
                                       .instruction_range = ast->range})
                  .has_value());
}

/** Revalidation requires the lop3 paired predicate lane to retain `.pred`. */
TEST(ResolvedIrChecker, RevalidationRejectsWrongLop3PredicateLaneType) {
  PtxSyntaxParser parser("lop3.and.b32 %r0|%p0, 1, %r1, 3, 255, %p1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  auto resolved = resolve<Lop3>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& predicate =
      std::get<Lop3::BoolopB32>(resolved->variant).dst.value.predicate;
  ASSERT_TRUE(predicate.has_value());
  predicate->value.register_ref.declared_type = ScalarType::U32;
  const auto checked = check(
      *resolved, Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                         .instruction_range = ast->range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
