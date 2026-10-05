#include <gtest/gtest.h>

#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;
using test_helpers::parseModule;

/** Resolve one source instruction after requiring syntax recovery-free parsing. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolve_mov(std::string_view source) {
  const auto parsed = parseInstruction(source);
  if (!parsed || !parsed.diagnostics.empty()) {
    return std::unexpected(ResolveDiagnostic{
        .message = parsed.diagnostics.empty()
                       ? "MOV source did not produce a syntax instruction."
                       : parsed.diagnostics.front().message,
    });
  }
  auto resolved = resolveMov(*parsed);
  if (!resolved)
    return std::unexpected(resolved.error());
  return std::move(*resolved);
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

  const auto& plain =
      dynamic_cast<const MovPred&>(*body[0]);
  const auto& plain_source =
      std::get<ResolvedPredicate>(plain.src.value);
  EXPECT_FALSE(plain_source.negated);
  EXPECT_EQ(plain_source.register_ref.spelling, "%p0");

  const auto& negated =
      dynamic_cast<const MovPred&>(*body[1]);
  const auto& negated_source =
      std::get<ResolvedPredicate>(negated.src.value);
  EXPECT_TRUE(negated_source.negated);
  EXPECT_EQ(negated_source.register_ref.spelling, "%p0");

  const auto& special =
      dynamic_cast<const MovPred&>(*body[2]);
  const auto& special_source =
      std::get<ResolvedPredicateSpecialRegister>(special.src.value);
  EXPECT_FALSE(special_source.negated);
  EXPECT_EQ(special_source.register_ref.id,
            base::lookup("%is_explicit_cluster")->id);

  const checker::Context predicate_target{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  EXPECT_TRUE(body[0]->check(predicate_target).has_value());
  EXPECT_TRUE(body[1]->check(predicate_target).has_value());
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
    const auto& pred = dynamic_cast<const MovPred&>(**resolved);
    const auto* constant =
        std::get_if<ResolvedPredicateConstant>(&pred.src.value);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(constant->value, expected);
    EXPECT_TRUE((*resolved)->check(
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
  const auto& pred = dynamic_cast<const MovPred&>(*resolved->functions.front().body.front());
  const auto* special =
      std::get_if<ResolvedPredicateSpecialRegister>(&pred.src.value);
  ASSERT_NE(special, nullptr);
  EXPECT_TRUE(special->negated);
  EXPECT_EQ(special->register_ref.id, base::lookup("%is_explicit_cluster")->id);
  const auto& instruction =
      *resolved->functions.front().body.front();
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  EXPECT_TRUE(instruction.check(
      checker::Context{.target = {.ptx_version = {9, 3},
                                  .sm_version = 90,
                                  .capabilities = cluster_capabilities}}));
  EXPECT_FALSE(instruction.check(
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

  const auto& pack = dynamic_cast<const MovB128PackUnpack&>(*body[0]);
  EXPECT_EQ(pack.type, ScalarType::B128);
  EXPECT_TRUE(
      pack.dst_register.has_value() && pack.src_register_vector.has_value());

  const auto& unpack = dynamic_cast<const MovB128PackUnpack&>(*body[1]);
  EXPECT_EQ(unpack.type, ScalarType::B128);
  ASSERT_TRUE(unpack.dst_register_vector.has_value());
  ASSERT_TRUE(unpack.src_register.has_value());
  ASSERT_EQ(unpack.dst_register_vector->value.elements.size(), 2u);
  EXPECT_FALSE(unpack.dst_register_vector->value.elements[1].has_value());

  const auto& vector_special = dynamic_cast<const MovV4U32&>(*body[2]);
  EXPECT_EQ(vector_special.type.value, ScalarType::U32);
  EXPECT_EQ(vector_special.src.value.spelling, "%clusterid");

  const checker::Context b128_target{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  EXPECT_TRUE(body[0]->check(b128_target).has_value());
  EXPECT_TRUE(body[1]->check(b128_target).has_value());
}

TEST(MovCompleteness, RevalidatesTheScalarTypeDomain) {
  auto resolved = resolve_mov("mov.b32 %r0, %r1;");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto scalar = std::move(*resolved);
  dynamic_cast<MovScalar&>(*scalar).type.value = ScalarType::B128;

  const auto checked = scalar->check(
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90}});
  ASSERT_FALSE(checked.has_value());
  ASSERT_FALSE(checked.error().empty());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
