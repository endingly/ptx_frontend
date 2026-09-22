#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve every explicit transcendental cohort with legal floating operands. */
TEST(TranscendentalCompleteness, ResolvesTypedApproxAndLowPrecisionCohorts) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<4>; .reg .f16 %h<4>; .reg .f16x2 %hx<4>;
  .reg .b16 %b<4>; .reg .b32 %r<4>;
  sin.approx.f32 %f0, %f1;
  sin.approx.ftz.f32 %f0, 1.0;
  cos.approx.f32 %f0, %f1;
  cos.approx.ftz.f32 %f0, %f1;
  lg2.approx.f32 %f0, %f1;
  lg2.approx.ftz.f32 %f0, %f1;
  ex2.approx.f32 %f0, %f1;
  ex2.approx.ftz.f32 %f0, %f1;
  tanh.approx.f32 %f0, %f1;
  ex2.approx.f16 %h0, %h1;
  ex2.approx.f16x2 %hx0, %hx1;
  ex2.approx.ftz.bf16 %b0, %b1;
  ex2.approx.ftz.bf16x2 %r0, %r1;
  tanh.approx.f16 %h0, %h1;
  tanh.approx.f16x2 %hx0, %hx1;
  tanh.approx.bf16 %b0, %b1;
  tanh.approx.bf16x2 %r0, %r1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 17u);

  EXPECT_TRUE(Sin::ApproxF32::approx);
  EXPECT_FALSE(
      std::get<Sin::ApproxF32>(std::get<Sin>(body[0]).variant).ftz.value);
  EXPECT_TRUE(
      std::get<Sin::ApproxF32>(std::get<Sin>(body[1]).variant).ftz.value);
  EXPECT_TRUE(Cos::ApproxF32::approx);
  EXPECT_TRUE(Lg2::ApproxF32::approx);
  EXPECT_TRUE(
      std::get<Ex2::ApproxF32>(std::get<Ex2>(body[7]).variant).ftz.value);
  EXPECT_TRUE(Tanh::ApproxF32::approx);
  EXPECT_EQ(Tanh::ApproxF32::type, ScalarType::F32);

  // The half cohorts carry no FTZ field at all and bind register operands only.
  EXPECT_EQ(Ex2::ApproxF16::type, ScalarType::F16);
  EXPECT_EQ(Ex2::ApproxF16x2::type, ScalarType::F16x2);
  EXPECT_TRUE(Ex2::ApproxFtzBf16::approx);
  EXPECT_TRUE(Ex2::ApproxFtzBf16::ftz);
  EXPECT_EQ(Ex2::ApproxFtzBf16::type, ScalarType::BF16);
  EXPECT_TRUE(Ex2::ApproxFtzBf16x2::ftz);
  EXPECT_EQ(Ex2::ApproxFtzBf16x2::type, ScalarType::BF16x2);

  EXPECT_EQ(Tanh::ApproxF16::type, ScalarType::F16);
  EXPECT_EQ(Tanh::ApproxF16x2::type, ScalarType::F16x2);
  EXPECT_EQ(Tanh::ApproxBf16::type, ScalarType::BF16);
  EXPECT_EQ(Tanh::ApproxBf16x2::type, ScalarType::BF16x2);
}

/** Reject legacy shorthand, forbidden modifiers, missing FTZ, and wrong arity. */
TEST(TranscendentalCompleteness, RejectsInvalidExplicitForms) {
  for (const auto source : {
           "sin.f32 %f0, %f1;",
           "cos.f32 %f0, %f1;",
           "lg2.f32 %f0, %f1;",
           "ex2.f32 %f0, %f1;",
           "tanh.f32 %f0, %f1;",
           "sin.approx.f64 %d0, %d1;",
           "sin.approx.f32x2 %f0, %f1;",
           "ex2.approx.f32x2 %f0, %f1;",
           "tanh.approx.f64 %d0, %d1;",
           "tanh.approx.ftz.f32 %f0, %f1;",
           "tanh.rz.f32 %f0, %f1;",
           "tanh.approx.sat.f32 %f0, %f1;",
           "ex2.approx.bf16 %b0, %b1;",
           "ex2.approx.bf16x2 %r0, %r1;",
           "ex2.approx.ftz.f16 %h0, %h1;",
           "ex2.approx.ftz.f16x2 %r0, %r1;",
           "tanh.approx.ftz.f16 %h0, %h1;",
           "tanh.approx.ftz.bf16 %b0, %b1;",
           "sin.approx.f32 %f0;",
           "sin.approx.f32 %f0, %f1, %f2;",
           "sin.approx.f32 %f0, 1;",
           "tanh.approx.f16 %h0, 1.0;",
           "sin.approx.f32 _, %f1;",
           "tanh.approx.f16 _, %h1;",
           "ex2.approx.ftz.bf16 _, %b1;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveInstruction(*parsed).has_value());
  }
}

/** Reject integer containers and wrong widths in representative destination and source roles. */
TEST(TranscendentalCompleteness, RejectsIntegerAndWrongWidthContainers) {
  for (
      const auto source : {
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i; .reg .f32 %f; sin.approx.f32 %i, %f; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i; .reg .f32 %f; sin.approx.f32 %f, %i; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f; .reg .f64 %d; lg2.approx.f32 %f, %d; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f; ex2.approx.f16 %f, %f; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f16x2 %hx<2>; .reg .b16 %b<2>; ex2.approx.f16x2 %hx0, %b1; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<2>; tanh.approx.bf16x2 %f0, %f1; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i<2>; tanh.approx.bf16 %i0, %i1; })ptx",
      }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    // Resolution must succeed, so the rejection comes from the container
    // contract rather than an unresolved register reference.
    const auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    const auto invalid = validateModule(
        *resolved, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

/** Accept the declared container alternatives for the half and packed cohorts. */
TEST(TranscendentalCompleteness, AcceptsDeclaredContainerAlternatives) {
  for (
      const auto source : {
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<2>; sin.approx.f32 %f0, 1.0; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f16 %h<2>; .reg .b16 %b<2>; ex2.approx.f16 %h0, %b1; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f16x2 %hx<2>; .reg .b32 %r<2>; ex2.approx.f16x2 %hx0, %r1; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b16 %b<2>; tanh.approx.bf16 %b0, %b1; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .b32 %r<2>; tanh.approx.bf16x2 %r0, %r1; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f<2>; ex2.approx.ftz.f32 %f0, %f1; })ptx",
      }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveAndValidateModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  }
}

/** Check independent PTX and target floors for all transcendental cohorts. */
TEST(TranscendentalCompleteness, ChecksIndependentAvailability) {
  /** One representative source and independent availability boundary for a cohort. */
  struct AvailabilityCase {
    /** Representative explicit instruction source. */
    const char* source;
    /** First PTX version accepting the cohort. */
    checker::PtxVersion ptx;
    /** First target SM accepting the cohort. */
    unsigned sm;
    /** Previous valid PTX version used for the PTX diagnostic. */
    checker::PtxVersion older_ptx;
    /** Previous target SM used for the target diagnostic when applicable. */
    unsigned older_sm;
  };
  for (const auto& item : {
           AvailabilityCase{"sin.approx.f32 %f0, %f1;", {1, 4}, 0, {1, 3}, 0},
           AvailabilityCase{"cos.approx.f32 %f0, %f1;", {1, 4}, 0, {1, 3}, 0},
           AvailabilityCase{"lg2.approx.f32 %f0, %f1;", {1, 4}, 0, {1, 3}, 0},
           AvailabilityCase{"ex2.approx.f32 %f0, %f1;", {1, 4}, 0, {1, 3}, 0},
           AvailabilityCase{
               "tanh.approx.f32 %f0, %f1;", {7, 0}, 75, {6, 5}, 70},
           AvailabilityCase{"ex2.approx.f16 %h0, %h1;", {7, 0}, 75, {6, 5}, 70},
           AvailabilityCase{
               "ex2.approx.f16x2 %r0, %r1;", {7, 0}, 75, {6, 5}, 70},
           AvailabilityCase{
               "ex2.approx.ftz.bf16 %b0, %b1;", {7, 8}, 90, {7, 5}, 80},
           AvailabilityCase{
               "ex2.approx.ftz.bf16x2 %r0, %r1;", {7, 8}, 90, {7, 5}, 80},
           AvailabilityCase{
               "tanh.approx.f16 %h0, %h1;", {7, 0}, 75, {6, 5}, 70},
           AvailabilityCase{
               "tanh.approx.f16x2 %r0, %r1;", {7, 0}, 75, {6, 5}, 70},
           AvailabilityCase{
               "tanh.approx.bf16 %b0, %b1;", {7, 8}, 90, {7, 5}, 80},
           AvailabilityCase{
               "tanh.approx.bf16x2 %r0, %r1;", {7, 8}, 90, {7, 5}, 80},
       }) {
    SCOPED_TRACE(item.source);
    const auto parsed = test_helpers::parseInstruction(item.source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveInstruction(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto check_at = [&](checker::TargetInfo target) {
      return std::visit(
          [&](const auto& instruction) {
            return checker::check(instruction,
                                  checker::Context{.target = target});
          },
          *resolved);
    };
    EXPECT_TRUE(
        check_at({.ptx_version = item.ptx, .sm_version = item.sm}).has_value());
    const auto ptx_failure =
        check_at({.ptx_version = item.older_ptx, .sm_version = item.sm});
    ASSERT_FALSE(ptx_failure.has_value());
    EXPECT_EQ(ptx_failure.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    if (item.sm != 0) {
      const auto sm_failure =
          check_at({.ptx_version = item.ptx, .sm_version = item.older_sm});
      ASSERT_FALSE(sm_failure.has_value());
      EXPECT_EQ(sm_failure.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedSmVersion);
    }
  }
}

/** Preserve owned bindings and reject a valid bound packed substitution in an exact BF16 slot. */
TEST(TranscendentalCompleteness, OwnsSourcesAndRevalidatesBoundWidth) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() { .reg .f16x2 %hx<3>; .reg .b16 %b<3>; .reg .b32 %r<3>;
  ex2.approx.f16x2 %hx0, %r0; ex2.approx.ftz.bf16 %b0, %b1; }
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
  auto& bf16 = std::get<Ex2::ApproxFtzBf16>(
      std::get<Ex2>(owned->functions.front().body[1]).variant);
  const auto packed =
      std::get<Ex2::ApproxF16x2>(
          std::get<Ex2>(owned->functions.front().body[0]).variant)
          .src.value;
  bf16.src.value = packed;
  const auto invalid =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
