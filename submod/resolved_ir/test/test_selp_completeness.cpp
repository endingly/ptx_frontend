#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/selp/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/selp/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/selp/resolution.gen.hpp>

#include "test_module_projection.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Preserve the U32 public variant while resolving every other ordinary type. */
TEST(SelpCompleteness, ResolvesAndChecksEveryOrdinaryType) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p;
  .reg .b16 %b16<3>;
  .reg .b32 %b32<3>;
  .reg .b64 %b64<3>;
  .reg .u16 %u16<3>;
  .reg .u32 %u32<3>;
  .reg .u64 %u64<3>;
  .reg .s16 %s16<3>;
  .reg .s32 %s32<3>;
  .reg .s64 %s64<3>;
  .reg .f32 %f32<3>;
  .reg .f64 %f64<3>;
  selp.b16 %b160, %b161, %b162, %p;
  selp.b32 %b320, %b321, %b322, %p;
  selp.b64 %b640, %b641, %b642, %p;
  selp.u16 %u160, %u161, %u162, %p;
  selp.u32 %u320, %u321, 0, %p;
  selp.u64 %u640, %u641, %u642, %p;
  selp.s16 %s160, %s161, %s162, %p;
  selp.s32 %s320, %s321, -1, %p;
  selp.s64 %s640, %s641, %s642, %p;
  selp.f32 %f320, %f321, 1.0, %p;
  selp.f64 %f640, %f641, %f642, %p;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = test_support::resolveTypedModule<Selp>(
      *parsed, test_support::ModulePipeline::CompleteContext);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 11u);
  for (std::size_t index = 0; index < body.size(); ++index) {
    const auto& selp = std::get<Selp>(body[index]);
    EXPECT_EQ(std::holds_alternative<Selp::U32>(selp.variant), index == 4);
    EXPECT_EQ(std::holds_alternative<Selp::Scalar>(selp.variant), index != 4);
  }
}

/** Reject non-ISA types, missing or extra operands, and non-predicate sources. */
TEST(SelpCompleteness, RejectsUnsupportedShapesAndModifiers) {
  for (const std::string_view source : {
           "selp.f16 %h0, %h1, %h2, %p;",
           "selp.pred %p0, %p1, %p2, %p3;",
           "selp.b128 %r0, %r1, %r2, %p;",
           "selp %r0, %r1, %r2, %p;",
           "selp.s32 %r0, %r1, %r2;",
           "selp.s32 %r0, %r1, %r2, %p, %p;",
           "selp.s32 %r0, %r1, %r2, 1.0;",
           "selp.s32 _, %r1, %r2, %p;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolve<Selp>(*parsed).has_value());
  }
}

/** Predicate register complements and integer constants survive owned resolution. */
TEST(SelpCompleteness, PreservesPredicateSourceTruthValues) {
  for (const auto& [source, expected] :
       {std::pair{"selp.u32 %r0, %r1, %r2, !0;", true},
        std::pair{"selp.s32 %r0, %r1, %r2, 2;", true},
        std::pair{"selp.s32 %r0, %r1, %r2, !2;", false}}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Selp>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto& predicate = std::visit(
        [](const auto& variant) -> const ResolvedPredicateSource& {
          return variant.predicate.value;
        },
        resolved->variant);
    ASSERT_TRUE(std::holds_alternative<ResolvedPredicateConstant>(predicate));
    EXPECT_EQ(std::get<ResolvedPredicateConstant>(predicate).value, expected);
  }

  const auto parsed =
      test_helpers::parseInstruction("selp.s32 %r0, %r1, %r2, !%p0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolve<Selp>(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& predicate =
      std::get<Selp::Scalar>(resolved->variant).predicate.value;
  ASSERT_TRUE(std::holds_alternative<ResolvedPredicate>(predicate));
  EXPECT_TRUE(std::get<ResolvedPredicate>(predicate).negated);
}

/** Check declarations, numeric immediates, and predicate shape in a full module. */
TEST(SelpCompleteness, RejectsWrongSourceImmediateAndPredicate) {
  for (const std::string_view source : {
           "selp.s32 %d, %wide, %s, %p;",
           "selp.f32 %f, %s, %f, %p;",
           "selp.s32 %d, %s, %s, %s;",
           "selp.s32 %d, %s, 1.0, %p;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(std::string(R"ptx(.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p;
  .reg .s32 %d, %s;
  .reg .s64 %wide;
  .reg .f32 %f;
)ptx") + std::string(source) + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(
        test_support::resolveAndValidateModuleSnapshot(*parsed).has_value());
  }
}

/** A scalar F64 selection carries the ISA's SM 13 minimum on its type value. */
TEST(SelpCompleteness, GatesOnlyF64AtSm13) {
  const auto parsed =
      test_helpers::parseInstruction("selp.f64 %fd0, %fd1, %fd2, %p0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolve<Selp>(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto old_target = checker::check(
      *resolved,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 12}});
  ASSERT_FALSE(old_target.has_value());
  EXPECT_EQ(old_target.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(checker::check(*resolved,
                             checker::Context{.target = {.ptx_version = {1, 0},
                                                         .sm_version = 13}})
                  .has_value());

  const auto f32_parsed =
      test_helpers::parseInstruction("selp.f32 %f0, %f1, %f2, %p0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(f32_parsed);
  const auto f32 = resolve<Selp>(*f32_parsed);
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  EXPECT_TRUE(
      checker::check(*f32, checker::Context{.target = {.ptx_version = {1, 0},
                                                       .sm_version = 0}})
          .has_value());
}

/** Public IR revalidation rejects a type outside the selected scalar domain. */
TEST(SelpCompleteness, RejectsMutatedTypeValue) {
  const auto parsed =
      test_helpers::parseInstruction("selp.s32 %r0, %r1, %r2, %p0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  auto resolved = resolve<Selp>(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& scalar = std::get<Selp::Scalar>(resolved->variant);
  scalar.type.value = ScalarType::F16;
  const auto checked = checker::check(
      *resolved,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 100}});
  EXPECT_FALSE(checked.has_value());
}

/** Recheck a selected type and constant after source and AST destruction. */
TEST(SelpCompleteness, RevalidatesOwnedInstructionAfterSourceRelease) {
  std::optional<Selp> owned;
  {
    const std::string source = "selp.f64 %fd0, %fd1, %fd2, !2;";
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Selp>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    owned = *resolved;
  }
  ASSERT_TRUE(owned.has_value());
  const auto& variant = std::get<Selp::Scalar>(owned->variant);
  EXPECT_EQ(variant.type.value, ScalarType::F64);
  EXPECT_FALSE(
      std::get<ResolvedPredicateConstant>(variant.predicate.value).value);
  EXPECT_TRUE(
      checker::check(*owned, checker::Context{.target = {.ptx_version = {9, 3},
                                                         .sm_version = 100}})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
