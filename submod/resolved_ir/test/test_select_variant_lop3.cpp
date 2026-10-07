#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/lop3.gen.hpp>
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
    const auto resolved = resolveLop3(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_NE(dynamic_cast<Lop3B32*>(resolved->get()), nullptr);
    EXPECT_EQ(Lop3B32::type, ScalarType::B32);
  }
}

TEST(ResolveLop3, SelectsBoolopLayoutAndRejectsNonImmediateLut) {
  const auto boolop = resolveLop3(
      parse_instruction("lop3.and.b32 _|%p0, 1, %r2, 3, 0x1a, !%p1;"));
  ASSERT_TRUE(boolop.has_value()) << boolop.error().message;
  ASSERT_NE(dynamic_cast<Lop3BoolopB32*>(boolop->get()), nullptr);
  EXPECT_FALSE(
      resolveLop3(parse_instruction("lop3.b32 %r0, %r1, %r2, %r3, %r4;"))
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
    const auto lop3 = resolveLop3(*ast);
    ASSERT_TRUE(lop3.has_value()) << lop3.error().message;
    const auto old_ptx = (*lop3)->check(
        Context{.target = {.ptx_version = {4, 2}, .sm_version = 50},
                .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = (*lop3)->check(
        Context{.target = {.ptx_version = {4, 3}, .sm_version = 49},
                .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(
        (*lop3)
            ->check(Context{.target = {.ptx_version = {4, 3}, .sm_version = 50},
                            .instruction_range = ast->range})
            .has_value());
  }

  for (const auto source : {"lop3.b32 %r0, %r1, %r2, %r3, 256;",
                            "lop3.b32 %r0, %r1, %r2, %r3, -1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto lop3 = resolveLop3(*ast);
    ASSERT_TRUE(lop3.has_value()) << lop3.error().message;
    const auto checked = (*lop3)->check(
        Context{.target = {.ptx_version = {4, 3}, .sm_version = 50},
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
  const auto lop3 = resolveLop3(*ast);
  ASSERT_TRUE(lop3.has_value()) << lop3.error().message;
  const auto old_ptx = (*lop3)->check(
      Context{.target = {.ptx_version = {8, 1}, .sm_version = 70},
              .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = (*lop3)->check(
      Context{.target = {.ptx_version = {8, 2}, .sm_version = 69},
              .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      (*lop3)
          ->check(Context{.target = {.ptx_version = {8, 2}, .sm_version = 70},
                          .instruction_range = ast->range})
          .has_value());
}

/** Paired data/predicate destinations retain the same complemented-write rule. */
TEST(ResolvedIrChecker, RevalidationRejectsMutatedLop3PredicateDestination) {
  PtxSyntaxParser parser("lop3.and.b32 _|%p0, 1, %r1, 3, 255, !%p1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  auto resolved = resolveLop3(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& destination =
      dynamic_cast<Lop3BoolopB32&>(**resolved).dst.value.predicate;
  ASSERT_TRUE(destination.has_value());
  destination->value.negated = true;
  const auto checked = (*resolved)->check(
      Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
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
  auto resolved = resolveLop3(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& destination = dynamic_cast<Lop3BoolopB32&>(**resolved).dst.value;
  destination.predicate.reset();
  const auto checked = (*resolved)->check(
      Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
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
  auto resolved = resolveLop3(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& destination = dynamic_cast<Lop3BoolopB32&>(**resolved).dst.value;
  destination.data.reset();
  destination.predicate.reset();
  const auto checked = (*resolved)->check(
      Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
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
  auto resolved = resolveLop3(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  dynamic_cast<Lop3BoolopB32&>(**resolved).dst.value.data.reset();
  EXPECT_TRUE(
      (*resolved)
          ->check(Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                          .instruction_range = ast->range})
          .has_value());
}

/** Revalidation requires the lop3 paired predicate lane to retain `.pred`. */
TEST(ResolvedIrChecker, RevalidationRejectsWrongLop3PredicateLaneType) {
  PtxSyntaxParser parser("lop3.and.b32 %r0|%p0, 1, %r1, 3, 255, %p1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  auto resolved = resolveLop3(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& predicate =
      dynamic_cast<Lop3BoolopB32&>(**resolved).dst.value.predicate;
  ASSERT_TRUE(predicate.has_value());
  predicate->value.register_ref.declared_type = ScalarType::U32;
  const auto checked = (*resolved)->check(
      Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
              .instruction_range = ast->range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
