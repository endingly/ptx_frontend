#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Accept the audited scalar source immediates and the mixed FP32 addend literal. */
TEST(AddSubAudit, AcceptsAuditedLiteralPositions) {
  for (
      const auto source : {
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<3>; add.f32 %f0, 1.0, %f1; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<3>; add.f32 %f0, %f1, 1.0; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f64 %d<3>; add.f64 %d0, 1.0, %d1; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f64 %d<3>; sub.f64 %d0, %d1, 1.0; })ptx",
          // The mixed FP32 addend/subtrahend is immediate-capable; the narrow
          // source and the destination are not.
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<3>; .reg .f16 %h<2>; add.f32.f16 %f0, %h1, 1.0; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<3>; .reg .f16 %h<2>; sub.f32.f16 %f0, %h1, 1.0; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<3>; .reg .b16 %b<2>; add.f32.bf16 %f0, %b1, 1.0; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<3>; .reg .b16 %b<2>; sub.f32.bf16 %f0, %b1, 1.0; })ptx",
      }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveAndValidateModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  }
}

/** Reject literals outside the audited positions.
 *
 * These are operand-shape rules, so instruction resolution is the right level:
 * the rejection is decided by layout selection alone, with no module binding
 * that an unrelated unresolved name could fail on instead.
 */
TEST(AddSubAudit, RejectsLiteralsOutsideAuditedPositions) {
  for (const auto source : {
           // Destinations are registers for every cohort.
           "add.f32 1.0, %f1, %f2;",
           "add.f32.bf16 1.0, %b1, %f2;",
           // The narrow source is register-only, including the BF16 cohort.
           "add.f32.f16 %f0, 1.0, %f2;",
           "sub.f32.bf16 %f0, 1.0, %f2;",
           // The newly immediate-capable addend admits floating literals only.
           "add.f32.f16 %f0, %h1, 1;",
           "sub.f32.f16 %f0, %h1, 1;",
           "add.f32.bf16 %f0, %b1, 1;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveInstruction(*parsed).has_value());
  }
}

/** Pin the typed immediate handoff for the newly immediate-capable mixed operand. */
TEST(AddSubAudit, OwnsMixedAddendImmediateAndRevalidates) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() { .reg .f32 %f<3>; .reg .f16 %h<2>;
  add.f32.f16 %f0, %h1, 1.0; sub.f32.f16 %f0, %h1, 2.0; }
)ptx";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());

  const auto& add = std::get<Add::MixedF32>(
      std::get<Add>(owned->functions.front().body[0]).variant);
  const auto* addend = std::get_if<ResolvedImmediate>(&add.addend.value);
  ASSERT_NE(addend, nullptr);
  EXPECT_EQ(addend->type, ScalarType::F32);
  EXPECT_EQ(addend->bits, 0x3F800000u);
  // The narrow source keeps its register representation, not the wide one.
  EXPECT_EQ(add.src.value.spelling, "%h1");

  const auto& sub = std::get<Sub::MixedF32>(
      std::get<Sub>(owned->functions.front().body[1]).variant);
  const auto* subtrahend =
      std::get_if<ResolvedImmediate>(&sub.subtrahend.value);
  ASSERT_NE(subtrahend, nullptr);
  EXPECT_EQ(subtrahend->type, ScalarType::F32);
  EXPECT_EQ(subtrahend->bits, 0x40000000u);
}

/** Pin the audited physical containers each cohort accepts and rejects. */
TEST(AddSubAudit, EnforcesAuditedPhysicalContainers) {
  struct ContainerCase {
    /** Representative complete module source. */
    const char* source;
    /** Whether the assembler-verified contract accepts this spelling. */
    bool accepted;
    /** What the case establishes. */
    const char* note;
  };
  for (
      const auto& item : {
          // Half scalar and packed accept native and matching bit containers.
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b16 %b<3>; add.f16 %b0, %b1, %b2; })ptx",
              true, "f16 via .b16"},
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b32 %r<3>; add.f16x2 %r0, %r1, %r2; })ptx",
              true, "f16x2 via .b32"},
          // BF16 is an instruction-only format: exactly the bit containers.
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b16 %b<3>; add.bf16 %b0, %b1, %b2; })ptx",
              true, "bf16 via .b16"},
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f16 %h<3>; add.bf16 %h0, %h1, %h2; })ptx",
              false, "bf16 must reject .f16"},
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f16x2 %hx<3>; add.bf16x2 %hx0, %hx1, %hx2; })ptx",
              false, "bf16x2 must reject .f16x2"},
          // The packed FP32 cohort binds exactly .b64, not any other 64-bit type.
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b64 %rd<3>; add.f32x2 %rd0, %rd1, %rd2; })ptx",
              true, "f32x2 via .b64"},
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f64 %d<3>; add.f32x2 %d0, %d1, %d2; })ptx",
              false, "f32x2 must reject .f64"},
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u64 %u<3>; sub.f32x2 %u0, %u1, %u2; })ptx",
              false, "f32x2 must reject .u64"},
          // The narrow mixed source is held to its own width.
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b16 %b<2>; .reg .f32 %f<3>; add.f32.f16 %f0, %b1, %f2; })ptx",
              true, "mixed f16 via .b16"},
          ContainerCase{
              R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b32 %r<2>; .reg .f32 %f<3>; add.f32.f16 %f0, %r1, %f2; })ptx",
              false, "mixed narrow must reject .b32"},
      }) {
    SCOPED_TRACE(item.note);
    const auto parsed = test_helpers::parseModule(item.source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveAndValidateModule(*parsed);
    EXPECT_EQ(resolved.has_value(), item.accepted)
        << (resolved.has_value() ? "unexpectedly accepted"
                                 : resolved.error().front().message);
  }
}

/** Pin the audited modifier legality per cohort. */
TEST(AddSubAudit, EnforcesAuditedModifierLegality) {
  // FP64 and the packed FP32 cohort admit no saturation.
  for (
      const auto source : {
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f64 %d<3>; add.sat.f64 %d0, %d1, %d2; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b64 %rd<3>; add.sat.f32x2 %rd0, %rd1, %rd2; })ptx",
          // BF16 admits neither FTZ nor saturation, and is RN-only.
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b16 %b<3>; add.ftz.bf16 %b0, %b1, %b2; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b16 %b<3>; add.sat.bf16 %b0, %b1, %b2; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b16 %b<3>; add.rz.bf16 %b0, %b1, %b2; })ptx",
          // Half rounding is RN-only, but half does admit FTZ and saturation.
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f16 %h<3>; add.rz.f16 %h0, %h1, %h2; })ptx",
      }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value());
  }

  for (
      const auto source : {
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f16 %h<3>; add.sat.f16 %h0, %h1, %h2; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f16 %h<3>; add.ftz.f16 %h0, %h1, %h2; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<3>; add.sat.f32 %f0, %f1, %f2; })ptx",
      }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveAndValidateModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
