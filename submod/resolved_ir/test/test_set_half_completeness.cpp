#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/checker/comparison_and_selection.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection.gen.hpp>
#include <ptx_frontend/resolved_ir/resolution/comparison_and_selection.gen.hpp>

#include "test_module_projection.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve and validate every half/bfloat SET result and Boolean shape. */
TEST(SetHalfCompleteness, ResolvesEveryTypedCohort) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p0;
  .reg .b16 %hb0, %hb1, %hb2;
  .reg .b32 %b0, %b1, %b2;
  .reg .f16 %h0, %h1, %h2;
  .reg .f16x2 %hx0, %hx1, %hx2;
  .reg .f32 %f0, %f1;
  .reg .f64 %d0, %d1;
  .reg .u16 %ud0, %u0, %u1;
  .reg .s16 %sd0, %s0, %s1;
  .reg .u32 %ur0, %ur1, %ur2;
  .reg .s32 %sr0, %sr1, %sr2;

  set.eq.f16.b16 %h0, %hb0, %hb1;
  set.ne.xor.f16.b16 %h0, %hb0, %hb1, !%p0;
  set.lt.f16.s32 %h0, %sr1, -1;
  set.ge.and.f16.u16 %h0, %u0, %u1, 1;
  set.num.ftz.f16.f16 %h0, %h1, %h2;
  set.ltu.or.ftz.f16.f16 %h0, %hb0, %hb1, !%p0;
  set.neu.ftz.f16.f32 %h0, %f0, %f1;
  set.nan.and.f16.f32 %h0, %f0, %f1, %p0;
  set.geu.f16.f64 %h0, %d0, %d1;
  set.eq.xor.f16.f64 %h0, %d0, %d1, !%p0;

  set.eq.bf16.b16 %hb0, %hb1, %hb2;
  set.ne.xor.bf16.b16 %hb0, %hb1, %hb2, !%p0;
  set.lt.bf16.u32 %hb0, %ur1, %ur2;
  set.ge.and.bf16.s16 %hb0, %s0, %s1, %p0;
  set.ltu.bf16.f16 %hb0, %h1, %h2;
  set.nan.or.bf16.f16 %hb0, %h1, %h2, !%p0;
  set.num.bf16.f32 %hb0, %f0, %f1;
  set.geu.xor.bf16.f64 %hb0, %d0, %d1, !%p0;

  set.eq.ftz.u16.f16 %ud0, %h1, %h2;
  set.nan.and.s32.f16 %sr0, %hb0, %hb1, !%p0;
  set.ne.s16.bf16 %sd0, %hb1, %hb2;
  set.equ.or.u32.bf16 %ur0, %hb1, %hb2, %p0;
  set.eq.ftz.f16x2.f16x2 %hx0, %hx1, %hx2;
  set.num.xor.f16x2.f16x2 %b0, %b1, %b2, !%p0;
  set.nan.ftz.u32.f16x2 %ur0, %hx1, %hx2;
  set.gt.and.s32.f16x2 %sr0, %b1, %b2, %p0;
  set.equ.bf16x2.bf16x2 %b0, %b1, %b2;
  set.geu.or.bf16x2.bf16x2 %b0, %b1, %b2, !%p0;
  set.num.u32.bf16x2 %ur0, %b1, %b2;
  set.neu.and.s32.bf16x2 %sr0, %b1, %b2, %p0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = test_support::resolveTypedModule<Set>(
      *parsed, test_support::ModulePipeline::CompleteContext);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 30u);
  for (size_t i = 0; i < body.size(); ++i) {
    SCOPED_TRACE(i);
    const auto* instruction = std::get_if<Set>(&body[i]);
    ASSERT_NE(instruction, nullptr);
    EXPECT_EQ(instruction->variant.index(), i + 10u);
  }
  const auto& half =
      std::get<Set::HalfF16F16Boolean>(std::get<Set>(body[5]).variant);
  EXPECT_TRUE(half.ftz.value);
  EXPECT_TRUE(std::get<ResolvedPredicate>(half.combine.value).negated);
  const auto& bfloat =
      std::get<Set::HalfBf16F16Boolean>(std::get<Set>(body[15]).variant);
  EXPECT_TRUE(std::get<ResolvedPredicate>(bfloat.combine.value).negated);
}

/** Reject comparator and FTZ controls outside their source-type domains. */
TEST(SetHalfCompleteness, RejectsIllegalControlsAndPredicateSources) {
  for (const std::string_view source : {
           "set.lt.f16.b16 %h0, %b0, %b1;",
           "set.lo.f16.u32 %h0, %u0, %u1;",
           "set.nan.bf16.s32 %b0, %s0, %s1;",
           "set.eq.ftz.f16.b16 %h0, %b0, %b1;",
           "set.eq.ftz.f16.f64 %h0, %d0, %d1;",
           "set.eq.ftz.bf16.f16 %b0, %h0, %h1;",
           "set.eq.ftz.bf16x2.bf16x2 %r0, %r1, %r2;",
           "set.eq.and.f16.f16 %h0, %h1, %h2;",
           "set.eq.f16.f16 %h0, %h1, %h2, %p0;",
           "set.eq.and.f16.f16 %h0, %h1, %h2, 1.0;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolve<Set>(*parsed).has_value());
  }
}

/** Half source registers and packed physical containers remain type checked. */
TEST(SetHalfCompleteness, RejectsIllegalSourceAndDestinationContainers) {
  for (const std::string_view source : {
           "set.eq.u16.f16 %ud0, %h0, 0x3c00;",
           "set.eq.u16.bf16 %ud0, %hb0, 0x3c00;",
           "set.eq.f16x2.f16x2 %u0, %b0, %b1;",
           "set.eq.f16x2.f16x2 %hx0, %u0, %b1;",
           "set.eq.bf16x2.bf16x2 %u0, %b0, %b1;",
           "set.eq.bf16x2.bf16x2 %b0, %hx0, %b1;",
           "set.eq.bf16.f16 %h0, %h1, %h2;",
           "set.eq.f16.s32 %h0, %s0, 1.0;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(std::string(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .b16 %hb0;
  .reg .b32 %b0, %b1;
  .reg .f16 %h0, %h1, %h2;
  .reg .f16x2 %hx0;
  .reg .u16 %ud0;
  .reg .u32 %u0;
  .reg .s32 %s0;
)ptx") + std::string(source) + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(
        test_support::resolveAndValidateModuleSnapshot(*parsed).has_value());
  }
}

/** Distinguish floating, bit, and integer 16-bit destination containers. */
TEST(SetHalfCompleteness, ChecksScalarResultContainers) {
  const std::pair<std::string_view, bool> cases[] = {
      {"set.eq.f16.f16 %h0, %h1, %h2;", true},
      {"set.eq.f16.f16 %hb0, %h1, %h2;", true},
      {"set.eq.f16.f16 %ud0, %h1, %h2;", false},
      {"set.eq.f16.f16 %sd0, %h1, %h2;", false},
      {"set.eq.u16.f16 %ud0, %h1, %h2;", true},
      {"set.eq.u16.f16 %hb0, %h1, %h2;", true},
      {"set.eq.u16.f16 %h0, %h1, %h2;", false},
      {"set.eq.s16.bf16 %ud0, %hb1, %hb2;", true},
      {"set.eq.s16.bf16 %h0, %hb1, %hb2;", false},
  };
  for (const auto& [source, accepted] : cases) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(std::string(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .b16 %hb0, %hb1, %hb2;
  .reg .f16 %h0, %h1, %h2;
  .reg .u16 %ud0;
  .reg .s16 %sd0;
)ptx") + std::string(source) + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto result = test_support::resolveTypedModule<Set>(
        *parsed, test_support::ModulePipeline::CompleteContext);
    EXPECT_EQ(result.has_value(), accepted);
  }
}

/** Native packed outputs and packed-source integer outputs differ by container. */
TEST(SetHalfCompleteness, ChecksPackedResultContainers) {
  const std::pair<std::string_view, bool> cases[] = {
      {"set.eq.f16x2.f16x2 %hx0, %hx1, %hx2;", true},
      {"set.eq.f16x2.f16x2 %b0, %b1, %b2;", true},
      {"set.eq.f16x2.f16x2 %u0, %b1, %b2;", false},
      {"set.eq.f16x2.f16x2 %b0, %u1, %b2;", false},
      {"set.eq.bf16x2.bf16x2 %b0, %b1, %b2;", true},
      {"set.eq.bf16x2.bf16x2 %u0, %b1, %b2;", false},
      {"set.eq.bf16x2.bf16x2 %b0, %hx1, %b2;", false},
      {"set.eq.u32.f16x2 %u0, %b1, %b2;", true},
      {"set.eq.u32.f16x2 %hx0, %b1, %b2;", true},
      {"set.eq.u32.bf16x2 %u0, %b1, %b2;", true},
      {"set.eq.u32.bf16x2 %hx0, %b1, %b2;", true},
  };
  for (const auto& [source, accepted] : cases) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(std::string(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .b32 %b0, %b1, %b2;
  .reg .f16x2 %hx0, %hx1, %hx2;
  .reg .u32 %u0, %u1;
)ptx") + std::string(source) + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto result = test_support::resolveTypedModule<Set>(
        *parsed, test_support::ModulePipeline::CompleteContext);
    EXPECT_EQ(result.has_value(), accepted);
  }
}

/** Check PTX and SM minima immediately below and at each introduced cohort. */
TEST(SetHalfCompleteness, EnforcesPtxAndTargetBoundaries) {
  /** One syntax form at its unsupported and supported target boundaries. */
  struct AvailabilityCase {
    /** Source spelling resolved without module declarations. */
    std::string_view source;
    /** PTX version immediately before introduction. */
    checker::TargetInfo below_ptx;
    /** SM level immediately before introduction. */
    checker::TargetInfo below_sm;
    /** First fully supported target profile. */
    checker::TargetInfo supported;
  };
  const AvailabilityCase cases[] = {
      {"set.eq.f16.f16 %h0, %h1, %h2;",
       {{4, 1}, 53},
       {{4, 2}, 52},
       {{4, 2}, 53}},
      {"set.eq.u16.f16 %r0, %h1, %h2;",
       {{6, 4}, 53},
       {{6, 5}, 52},
       {{6, 5}, 53}},
      {"set.eq.u32.f16x2 %r0, %r1, %r2;",
       {{6, 4}, 53},
       {{6, 5}, 52},
       {{6, 5}, 53}},
      {"set.eq.bf16.f16 %r0, %h1, %h2;",
       {{7, 7}, 90},
       {{7, 8}, 89},
       {{7, 8}, 90}},
      {"set.eq.u16.bf16 %r0, %r1, %r2;",
       {{7, 7}, 90},
       {{7, 8}, 89},
       {{7, 8}, 90}},
      {"set.eq.bf16x2.bf16x2 %r0, %r1, %r2;",
       {{7, 7}, 90},
       {{7, 8}, 89},
       {{7, 8}, 90}},
  };
  for (const auto& entry : cases) {
    SCOPED_TRACE(entry.source);
    const auto parsed = test_helpers::parseInstruction(entry.source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Set>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_FALSE(
        checker::check(*resolved, checker::Context{.target = entry.below_ptx}));
    EXPECT_FALSE(
        checker::check(*resolved, checker::Context{.target = entry.below_sm}));
    EXPECT_TRUE(
        checker::check(*resolved, checker::Context{.target = entry.supported}));
  }
}

/** Owned half SET controls and predicate sources survive the syntax lifetime. */
TEST(SetHalfCompleteness, RevalidatesOwnedAndMutatedInstruction) {
  std::optional<Set> owned;
  {
    const std::string source = "set.num.xor.ftz.f16x2.f16x2 %b0, %b1, %b2, !0;";
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Set>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    owned = *resolved;
  }
  ASSERT_TRUE(owned.has_value());
  auto& packed = std::get<Set::HalfNativeF16x2Boolean>(owned->variant);
  EXPECT_TRUE(packed.ftz.value);
  EXPECT_TRUE(std::get<ResolvedPredicateConstant>(packed.combine.value).value);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  EXPECT_TRUE(checker::check(*owned, context));
  packed.comparison.value = ComparisonOperator::Lo;
  EXPECT_FALSE(checker::check(*owned, context));
  packed.comparison.value = ComparisonOperator::Num;
  packed.src1.value.declared_type = ScalarType::U32;
  EXPECT_FALSE(checker::check(*owned, context));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
