#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/checker/comparison_and_selection.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection.gen.hpp>
#include <ptx_frontend/resolved_ir/resolution/comparison_and_selection.gen.hpp>

#include "test_module_projection.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve and check every ordinary comparison source family and Boolean shape. */
TEST(SetCompleteness, ResolvesOrdinaryFamilies) {
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
  set.eq.u32.b16 %u320, %b160, %b161;
  set.ne.xor.s32.b64 %s320, %b640, %b641, !%p;
  set.lt.f32.s16 %f320, %s160, %s161;
  set.ge.and.u32.s64 %u320, %s640, %s641, 1;
  set.hs.s32.u16 %s320, %u160, %u161;
  set.lo.or.f32.u64 %f320, %u640, %u641, !0;
  set.nan.ftz.f32.f32 %f320, %f321, %f322;
  set.ltu.xor.u32.f32 %u320, %f321, %f322, %p;
  set.num.s32.f64 %s320, %f640, %f641;
  set.geu.and.f32.f64 %f320, %f640, %f641, !%p;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = test_support::resolveTypedModule<Set>(
      *parsed, test_support::ModulePipeline::CompleteContext);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  EXPECT_TRUE(std::holds_alternative<Set::Bit>(std::get<Set>(body[0]).variant));
  EXPECT_TRUE(
      std::holds_alternative<Set::BitBoolean>(std::get<Set>(body[1]).variant));
  EXPECT_TRUE(
      std::holds_alternative<Set::Signed>(std::get<Set>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<Set::SignedBoolean>(
      std::get<Set>(body[3]).variant));
  EXPECT_TRUE(
      std::holds_alternative<Set::Unsigned>(std::get<Set>(body[4]).variant));
  EXPECT_TRUE(std::holds_alternative<Set::UnsignedBoolean>(
      std::get<Set>(body[5]).variant));
  EXPECT_TRUE(
      std::holds_alternative<Set::Float>(std::get<Set>(body[6]).variant));
  EXPECT_TRUE(std::holds_alternative<Set::FloatBoolean>(
      std::get<Set>(body[7]).variant));
  EXPECT_TRUE(
      std::holds_alternative<Set::FloatF64>(std::get<Set>(body[8]).variant));
  EXPECT_TRUE(std::holds_alternative<Set::FloatF64Boolean>(
      std::get<Set>(body[9]).variant));
}

/** Reject comparison suffixes outside each source family and illegal FTZ use. */
TEST(SetCompleteness, RejectsInvalidOrdinaryModifiers) {
  for (const std::string_view source : {
           "set.lt.u32.b16 %r0, %r1, %r2;",
           "set.lo.u32.s32 %r0, %r1, %r2;",
           "set.nan.u32.u32 %r0, %r1, %r2;",
           "set.eq.ftz.s32.f64 %r0, %r1, %r2;",
           "set.eq.ftz.u32.b32 %r0, %r1, %r2;",
           "set.eq.u16.u32 %r0, %r1, %r2;",
           "set.eq.u32.u32 %r0, %r1, %r2, %p;",
           "set.eq.and.u32.u32 %r0, %r1, %r2;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolve<Set>(*parsed).has_value());
  }
}

/** The checker rejects incompatible declarations and numeric source immediates. */
TEST(SetCompleteness, RejectsWrongDeclaredTypesAndSourceImmediate) {
  for (const std::string_view source : {
           "set.lt.s32.s32 %d, %wide, %s;",
           "set.eq.f32.u32 %s, %u, %u;",
           "set.eq.and.u32.u32 %u, %u, %u, %s;",
           "set.eq.u32.s32 %u, %s, 1.0;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(std::string(R"ptx(.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p;
  .reg .s32 %d, %s;
  .reg .s64 %wide;
  .reg .u32 %u;
)ptx") + std::string(source) + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(
        test_support::resolveAndValidateModuleSnapshot(*parsed).has_value());
  }
}

/** F64 comparison sources are available at SM 13 and not SM 12. */
TEST(SetCompleteness, EnforcesF64TargetMinimum) {
  const auto parsed =
      test_helpers::parseInstruction("set.eq.u32.f64 %r0, %fd0, %fd1;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolve<Set>(*parsed);
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
}

/** Recheck mutated public IR comparison and result-type domains. */
TEST(SetCompleteness, RejectsMutatedTypedFields) {
  const auto parsed =
      test_helpers::parseInstruction("set.eq.u32.b32 %r0, %r1, %r2;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  auto resolved = resolve<Set>(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& bit = std::get<Set::Bit>(resolved->variant);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  bit.comparison.value = ComparisonOperator::Lt;
  EXPECT_FALSE(checker::check(*resolved, context).has_value());
  bit.comparison.value = ComparisonOperator::Eq;
  bit.dtype.value = ScalarType::U16;
  EXPECT_FALSE(checker::check(*resolved, context).has_value());
}

/** Recheck typed operands and predicate truth after source and AST destruction. */
TEST(SetCompleteness, RevalidatesOwnedInstructionAfterSourceRelease) {
  std::optional<Set> owned;
  {
    const std::string source = "set.nan.xor.f32.f32 %f0, %f1, %f2, !0;";
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Set>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    owned = *resolved;
  }
  ASSERT_TRUE(owned.has_value());
  const auto& variant = std::get<Set::FloatBoolean>(owned->variant);
  EXPECT_EQ(variant.comparison.value, ComparisonOperator::Nan);
  EXPECT_TRUE(std::get<ResolvedPredicateConstant>(variant.combine.value).value);
  EXPECT_TRUE(
      checker::check(*owned, checker::Context{.target = {.ptx_version = {9, 3},
                                                         .sm_version = 100}})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
