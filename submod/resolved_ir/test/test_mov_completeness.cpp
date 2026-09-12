#include <gtest/gtest.h>

#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;
using test_helpers::parseModule;

/** Resolve one source instruction after requiring syntax recovery-free parsing. */
std::expected<Mov, ResolveDiagnostic> resolve_mov(std::string_view source) {
  const auto parsed = parseInstruction(source);
  if (!parsed || !parsed.diagnostics.empty()) {
    return std::unexpected(ResolveDiagnostic{
        .message = parsed.diagnostics.empty()
                       ? "MOV source did not produce a syntax instruction."
                       : parsed.diagnostics.front().message,
    });
  }
  const auto resolved = resolve<Mov>(*parsed);
  if (!resolved)
    return std::unexpected(resolved.error());
  return *resolved;
}

/** Predicate register and special-register sources retain complementation. */
TEST(MovCompleteness, PreservesPlainAndNegatedPredicateSources) {
  const auto parsed = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.visible .entry kernel() {
  .reg .pred %p0, %p1;
  mov.pred %p1, %p0;
  mov.pred %p1, !%p0;
  mov.pred %p1, %is_explicit_cluster;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);

  const auto& plain = std::get<Mov::Pred>(std::get<Mov>(body[0]).variant);
  const auto& plain_source = std::get<ResolvedPredicate>(plain.src.value);
  EXPECT_FALSE(plain_source.negated);
  EXPECT_EQ(plain_source.register_ref.spelling, "%p0");

  const auto& negated = std::get<Mov::Pred>(std::get<Mov>(body[1]).variant);
  const auto& negated_source = std::get<ResolvedPredicate>(negated.src.value);
  EXPECT_TRUE(negated_source.negated);
  EXPECT_EQ(negated_source.register_ref.spelling, "%p0");

  const auto& special = std::get<Mov::Pred>(std::get<Mov>(body[2]).variant);
  const auto& special_source =
      std::get<ResolvedPredicateSpecialRegister>(special.src.value);
  EXPECT_FALSE(special_source.negated);
  EXPECT_EQ(special_source.register_ref.id,
            base::lookup("%is_explicit_cluster")->id);

  const checker::Context predicate_target{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  EXPECT_TRUE(
      checker::check(std::get<Mov>(body[0]), predicate_target).has_value());
  EXPECT_TRUE(
      checker::check(std::get<Mov>(body[1]), predicate_target).has_value());
}

/** Predicate destinations remain registers and cannot be complemented. */
TEST(MovCompleteness, RejectsNegatedPredicateDestination) {
  const auto resolved = resolve_mov("mov.pred !%p1, %p0;");
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().message,
            "Operands do not match any layout of instruction variant 'Pred'.");
}

/** Integer truth values and complemented predicate sregs resolve canonically. */
TEST(MovCompleteness, ResolvesPredicateConstantsAndNegatedSpecialRegisters) {
  constexpr std::pair<std::string_view, bool> constants[] = {
      {"0", false}, {"2", true},   {"-1", true},
      {"!0", true}, {"!1", false}, {"!-1", false},
  };
  for (const auto& [source_constant, expected] : constants) {
    const std::string source =
        "mov.pred %p0, " + std::string(source_constant) + ";";
    SCOPED_TRACE(source);
    const auto resolved = resolve_mov(source);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto& pred = std::get<Mov::Pred>(resolved->variant);
    const auto* constant =
        std::get_if<ResolvedPredicateConstant>(&pred.src.value);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(constant->value, expected);
    EXPECT_TRUE(checker::check(
        *resolved,
        checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90}}));
  }

  const auto parsed = parseModule(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .pred %p0;
  mov.pred %p0, !%is_explicit_cluster;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& pred = std::get<Mov::Pred>(
      std::get<Mov>(resolved->functions.front().body.front()).variant);
  const auto* special =
      std::get_if<ResolvedPredicateSpecialRegister>(&pred.src.value);
  ASSERT_NE(special, nullptr);
  EXPECT_TRUE(special->negated);
  EXPECT_EQ(special->register_ref.id, base::lookup("%is_explicit_cluster")->id);
  const auto& instruction =
      std::get<Mov>(resolved->functions.front().body.front());
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  EXPECT_TRUE(checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {9, 3},
                                  .sm_version = 90,
                                  .capabilities = cluster_capabilities}}));
  EXPECT_FALSE(checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {9, 3},
                                  .sm_version = 89,
                                  .capabilities = cluster_capabilities}}));
}

/** Floating-point literals do not enter the predicate-constant domain. */
TEST(MovCompleteness, RejectsFloatingPredicateConstants) {
  EXPECT_FALSE(resolve_mov("mov.pred %p0, 1.0;").has_value());
}

TEST(MovCompleteness, SeparatesScalarFromBitPackUnpack) {
  const auto scalar = resolve_mov("mov.b128 %b0, %b1;");
  ASSERT_FALSE(scalar.has_value());
  EXPECT_EQ(scalar.error().message,
            "Operands do not match any layout of instruction variant "
            "'B128PackUnpack'.");

  const auto parsed = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.visible .entry kernel() {
  .reg .b128 %b0;
  .reg .b64 %rd0, %rd1;
  .reg .v4 .u32 %v;
  mov.b128 %b0, {%rd0, %rd1};
  mov.b128 {%rd0, _}, %b0;
  mov.v4.u32 %v, %clusterid;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);

  const auto& pack =
      std::get<Mov::B128PackUnpack>(std::get<Mov>(body[0]).variant);
  EXPECT_EQ(pack.type, ScalarType::B128);
  EXPECT_TRUE(
      std::holds_alternative<Mov::B128PackUnpack::PackOperands>(pack.operands));

  const auto& unpack =
      std::get<Mov::B128PackUnpack>(std::get<Mov>(body[1]).variant);
  EXPECT_EQ(unpack.type, ScalarType::B128);
  const auto& unpack_operands =
      std::get<Mov::B128PackUnpack::UnpackOperands>(unpack.operands);
  ASSERT_EQ(unpack_operands.dst.value.elements.size(), 2u);
  EXPECT_FALSE(unpack_operands.dst.value.elements[1].has_value());

  const auto& vector_special =
      std::get<Mov::V4U32>(std::get<Mov>(body[2]).variant);
  EXPECT_EQ(vector_special.type.value, ScalarType::U32);
  EXPECT_EQ(vector_special.src.value.spelling, "%clusterid");

  const checker::Context b128_target{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  EXPECT_TRUE(checker::check(std::get<Mov>(body[0]), b128_target).has_value());
  EXPECT_TRUE(checker::check(std::get<Mov>(body[1]), b128_target).has_value());
}

TEST(MovCompleteness, RevalidatesTheScalarTypeDomain) {
  const auto resolved = resolve_mov("mov.b32 %r0, %r1;");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto scalar = *resolved;
  std::get<Mov::Scalar>(scalar.variant).type.value = ScalarType::B128;

  const auto checked = checker::check(
      scalar,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90}});
  ASSERT_FALSE(checked.has_value());
  ASSERT_FALSE(checked.error().empty());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
