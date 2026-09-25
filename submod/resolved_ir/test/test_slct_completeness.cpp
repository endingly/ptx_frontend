#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/slct/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/slct/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/slct/resolution.gen.hpp>

#include "test_module_projection.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve and validate every SLCT data type with both numeric selectors. */
TEST(SlctCompleteness, ResolvesAllTypeAndSelectorCombinations) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .b16 %b16d, %b16a, %b16b;
  .reg .u16 %u16d, %u16a, %u16b;
  .reg .s16 %s16d, %s16a, %s16b;
  .reg .b32 %b32d, %b32a, %b32b;
  .reg .u32 %u32d, %u32a, %u32b;
  .reg .s32 %s32d, %s32a, %s32b, %selector;
  .reg .f32 %f32d, %f32a, %f32b, %fselector;
  .reg .b64 %b64d, %b64a, %b64b;
  .reg .u64 %u64d, %u64a, %u64b;
  .reg .s64 %s64d, %s64a, %s64b;
  .reg .f64 %f64d, %f64a, %f64b;

  slct.b16.s32 %b16d, %b16a, %b16b, %selector;
  slct.ftz.b16.f32 %b16d, %b16a, %b16b, %fselector;
  slct.u16.s32 %u16d, %u16a, %u16b, %selector;
  slct.u16.f32 %u16d, %u16a, %u16b, %fselector;
  slct.s16.s32 %s16d, %s16a, %s16b, %selector;
  slct.ftz.s16.f32 %s16d, %s16a, %s16b, %fselector;
  slct.b32.s32 %b32d, %b32a, %b32b, %selector;
  slct.b32.f32 %b32d, %b32a, %b32b, %fselector;
  slct.u32.s32 %u32d, %u32a, %u32b, %selector;
  slct.ftz.u32.f32 %u32d, %u32a, %u32b, %fselector;
  slct.s32.s32 %s32d, %s32a, %s32b, %selector;
  slct.s32.f32 %s32d, %s32a, %s32b, %fselector;
  slct.f32.s32 %f32d, %f32a, %f32b, %selector;
  slct.ftz.f32.f32 %f32d, %f32a, %f32b, %fselector;
  slct.b64.s32 %b64d, %b64a, %b64b, %selector;
  slct.b64.f32 %b64d, %b64a, %b64b, %fselector;
  slct.u64.s32 %u64d, %u64a, %u64b, %selector;
  slct.ftz.u64.f32 %u64d, %u64a, %u64b, %fselector;
  slct.s64.s32 %s64d, %s64a, %s64b, %selector;
  slct.s64.f32 %s64d, %s64a, %s64b, %fselector;
  slct.f64.s32 %f64d, %f64a, %f64b, %selector;
  slct.ftz.f64.f32 %f64d, %f64a, %f64b, %fselector;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = test_support::resolveTypedModule<Slct>(
      *parsed, test_support::ModulePipeline::CompleteContext);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 22u);
  for (size_t index = 0; index < body.size(); ++index) {
    SCOPED_TRACE(index);
    const auto* instruction = std::get_if<Slct>(&body[index]);
    ASSERT_NE(instruction, nullptr);
    EXPECT_EQ(instruction->variant.index(), index % 2u);
  }
  const auto& floating = std::get<Slct::F32>(std::get<Slct>(body[21]).variant);
  EXPECT_TRUE(floating.ftz.value);
  EXPECT_EQ(floating.dtype.value, ScalarType::F64);
}

/** Numeric literals can supply selected data and the sign selector. */
TEST(SlctCompleteness, AcceptsNumericDataAndSelectorImmediates) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .u16 %u16d, %u16a;
  .reg .u32 %u32d, %u32a;
  .reg .f32 %f32d, %f32a;
  slct.u16.s32 %u16d, 1, %u16a, -1;
  slct.ftz.u32.f32 %u32d, %u32a, 0, -1.0;
  slct.f32.s32 %f32d, %f32a, 1.0, 0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = test_support::resolveTypedModule<Slct>(
      *parsed, test_support::ModulePipeline::CompleteContext);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& integer = std::get<Slct::S32>(std::get<Slct>(body[0]).variant);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(integer.src_true.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(integer.selector.value));
  const auto& floating = std::get<Slct::F32>(std::get<Slct>(body[1]).variant);
  EXPECT_TRUE(floating.ftz.value);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(floating.src_false.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(floating.selector.value));
}

/** Reject unsupported data/selector suffixes and FTZ on integer selection. */
TEST(SlctCompleteness, RejectsInvalidModifierCombinations) {
  for (const std::string_view source : {
           "slct.ftz.u32.s32 %r0, %r1, %r2, %r3;",
           "slct.u32.u32 %r0, %r1, %r2, %r3;",
           "slct.f16.s32 %r0, %r1, %r2, %r3;",
           "slct.sat.u32.f32 %r0, %r1, %r2, %r3;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolve<Slct>(*parsed).has_value());
  }
}

/** Same-width bit aliases are valid, while unrelated float/integer types fail. */
TEST(SlctCompleteness, EnforcesNominalDataAndSelectorContainers) {
  const std::pair<std::string_view, bool> cases[] = {
      {"slct.u32.s32 %u32d, %s32a, %b32b, %u32c;", true},
      {"slct.f32.s32 %f32d, %b32a, %f32a, %s32c;", true},
      {"slct.b32.s32 %b32d, %f32a, %u32a, %s32c;", true},
      {"slct.b16.s32 %b16d, %f16a, %u16a, %s32c;", true},
      {"slct.b64.f32 %b64d, %f64a, %u64a, %b32c;", true},
      {"slct.u32.s32 %u32d, %f32a, %u32a, %s32c;", false},
      {"slct.f32.s32 %f32d, %u32a, %f32a, %s32c;", false},
      {"slct.u16.s32 %u16d, %f16a, %u16a, %s32c;", false},
      {"slct.f64.f32 %f64d, %u64a, %f64a, %f32c;", false},
      {"slct.u32.s32 %u32d, %u32a, %u32a, %f32c;", false},
      {"slct.u32.f32 %u32d, %u32a, %u32a, %s32c;", false},
      {"slct.u32.s32 %u32d, %u32a, %u32a, %p0;", false},
  };
  for (const auto& [source, accepted] : cases) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(std::string(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p0;
  .reg .b16 %b16d;
  .reg .u16 %u16a, %u16d;
  .reg .f16 %f16a;
  .reg .b32 %b32a, %b32b, %b32c, %b32d;
  .reg .u32 %u32a, %u32c, %u32d;
  .reg .s32 %s32a, %s32c;
  .reg .f32 %f32a, %f32c, %f32d;
  .reg .b64 %b64d;
  .reg .u64 %u64a;
  .reg .f64 %f64a, %f64d;
)ptx") + std::string(source) + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = test_support::resolveTypedModule<Slct>(
        *parsed, test_support::ModulePipeline::CompleteContext);
    EXPECT_EQ(resolved.has_value(), accepted);
  }
}

/** Reject floating literals in integer slots and integer literals in FP slots. */
TEST(SlctCompleteness, RejectsWrongTypedImmediates) {
  for (const std::string_view source : {
           "slct.u32.s32 %d, 1.0, %a, %c;",
           "slct.f32.s32 %f, 1, %f, %c;",
           "slct.u32.s32 %d, %a, %a, 1.0;",
           "slct.u32.f32 %d, %a, %a, 1;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(std::string(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .u32 %d, %a;
  .reg .f32 %f;
  .reg .s32 %c;
)ptx") + std::string(source) + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(
        test_support::resolveAndValidateModuleSnapshot(*parsed).has_value());
  }
}

/** F64 selected data needs SM 13 while every SLCT form needs PTX 1.0. */
TEST(SlctCompleteness, EnforcesPtxAndF64SmMinimum) {
  for (const std::string_view source : {
           "slct.f64.s32 %r0, %r1, %r2, %r3;",
           "slct.ftz.f64.f32 %r0, %r1, %r2, %r3;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Slct>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_FALSE(checker::check(
        *resolved,
        checker::Context{.target = {.ptx_version = {0, 9}, .sm_version = 13}}));
    EXPECT_FALSE(checker::check(
        *resolved,
        checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 12}}));
    EXPECT_TRUE(checker::check(
        *resolved,
        checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 13}}));
  }
}

/** Owned numeric operands and typed controls are rechecked after syntax release. */
TEST(SlctCompleteness, RevalidatesOwnedAndMutatedInstruction) {
  std::optional<Slct> owned;
  {
    const std::string source = "slct.ftz.u32.f32 %r0, 1, %r1, -1.0;";
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Slct>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    owned = *resolved;
  }
  ASSERT_TRUE(owned.has_value());
  auto& floating = std::get<Slct::F32>(owned->variant);
  EXPECT_TRUE(floating.ftz.value);
  EXPECT_EQ(floating.dtype.value, ScalarType::U32);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(floating.src_true.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(floating.selector.value));
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  EXPECT_TRUE(checker::check(*owned, context));
  floating.dtype.value = ScalarType::F16;
  EXPECT_FALSE(checker::check(*owned, context));
  floating.dtype.value = ScalarType::U32;
  std::get<ResolvedImmediate>(floating.selector.value).type = ScalarType::S32;
  EXPECT_FALSE(checker::check(*owned, context));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
