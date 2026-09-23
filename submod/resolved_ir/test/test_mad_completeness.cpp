#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve all explicit floating MAD rounding forms with their legal modifiers. */
TEST(MadCompleteness, ResolvesEveryExplicitFloatingRoundingForm) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<8>;
  .reg .f64 %d<8>;
  .reg .b32 %r<4>;
  .reg .b64 %rd<4>;
  mad.rn.ftz.sat.f32 %r0, 1.0, %f2, %r3;
  mad.rz.f32 %f0, %r1, 1.0, %f3;
  mad.rm.ftz.f32 %f0, %f1, %r2, 1.0;
  mad.rp.sat.f32 %r0, %f1, %f2, %f3;
  mad.rn.f64 %rd0, 1.0, %d2, %rd3;
  mad.rz.f64 %d0, %rd1, 1.0, %d3;
  mad.rm.f64 %d0, %d1, %rd2, 1.0;
  mad.rp.f64 %rd0, %d1, %d2, %d3;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 8u);
  const auto& rn_f32 = std::get<Mad::RnF32>(
      std::get<Mad>(resolved->functions.front().body[0]).variant);
  EXPECT_TRUE(rn_f32.ftz.value);
  EXPECT_TRUE(rn_f32.saturate.value);
  const auto& rz_f32 = std::get<Mad::DirectedF32>(
      std::get<Mad>(resolved->functions.front().body[1]).variant);
  EXPECT_EQ(rz_f32.rounding.value, RoundingMode::Rz);
  EXPECT_FALSE(rz_f32.ftz.value);
  EXPECT_FALSE(rz_f32.saturate.value);
  const auto& rm_f32 = std::get<Mad::DirectedF32>(
      std::get<Mad>(resolved->functions.front().body[2]).variant);
  EXPECT_EQ(rm_f32.rounding.value, RoundingMode::Rm);
  EXPECT_TRUE(rm_f32.ftz.value);
  EXPECT_FALSE(rm_f32.saturate.value);
  const auto& rp_f32 = std::get<Mad::DirectedF32>(
      std::get<Mad>(resolved->functions.front().body[3]).variant);
  EXPECT_EQ(rp_f32.rounding.value, RoundingMode::Rp);
  EXPECT_FALSE(rp_f32.ftz.value);
  EXPECT_TRUE(rp_f32.saturate.value);
  EXPECT_TRUE(std::holds_alternative<Mad::RnF64>(
      std::get<Mad>(resolved->functions.front().body[4]).variant));
  EXPECT_EQ(Mad::RnF64::rounding, RoundingMode::Rn);
  EXPECT_EQ(std::get<Mad::DirectedF64>(
                std::get<Mad>(resolved->functions.front().body[5]).variant)
                .rounding.value,
            RoundingMode::Rz);
  EXPECT_EQ(std::get<Mad::DirectedF64>(
                std::get<Mad>(resolved->functions.front().body[6]).variant)
                .rounding.value,
            RoundingMode::Rm);
  EXPECT_EQ(std::get<Mad::DirectedF64>(
                std::get<Mad>(resolved->functions.front().body[7]).variant)
                .rounding.value,
            RoundingMode::Rp);
}

/** Reject omitted-rounding legacy forms and invalid explicit floating combinations. */
TEST(MadCompleteness, RejectsLegacyAndInvalidExplicitForms) {
  for (const auto source : {
           "mad.f32 %f0, %f1, %f2, %f3;",
           "mad.f64 %d0, %d1, %d2, %d3;",
           "mad.rn.ftz.f64 %d0, %d1, %d2, %d3;",
           "mad.rn.sat.f64 %d0, %d1, %d2, %d3;",
           "mad.rn.f32 %f0, 1, %f2, %f3;",
           "mad.rn.f32 _, %f1, %f2, %f3;",
           "mad.rn.f64 _, %d1, %d2, %d3;",
           "mad.rn.f32 %f0, %f1, %f2;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveInstruction(*parsed).has_value());
  }
}

/** Reject integer register containers in every explicit floating MAD operand role. */
TEST(MadCompleteness, RejectsIntegerRegisterContainers) {
  for (const auto source : {
           R"ptx(.version 9.3
.target sm_100
.entry kernel() { .reg .u32 %i; .reg .f32 %f<4>; mad.rn.f32 %i, %f1, %f2, %f3; })ptx",
           R"ptx(.version 9.3
.target sm_100
.entry kernel() { .reg .u32 %i; .reg .f32 %f<4>; mad.rn.f32 %f0, %i, %f2, %f3; })ptx",
           R"ptx(.version 9.3
.target sm_100
.entry kernel() { .reg .u32 %i; .reg .f32 %f<4>; mad.rn.f32 %f0, %f1, %i, %f3; })ptx",
           R"ptx(.version 9.3
.target sm_100
.entry kernel() { .reg .u32 %i; .reg .f32 %f<4>; mad.rn.f32 %f0, %f1, %f2, %i; })ptx",
       }) {
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveAndValidateModule(*parsed);
    ASSERT_FALSE(resolved.has_value());
  }
}

/** Check the independent PTX and SM floors for each explicit scalar MAD cohort. */
TEST(MadCompleteness, ChecksExplicitFloatingAvailability) {
  struct AvailabilityCase {
    /** Complete source text for the representative explicit rounding form. */
    const char* source;
    /** First PTX version which accepts this instruction cohort. */
    checker::PtxVersion minimum_ptx;
    /** First SM version which accepts this instruction cohort. */
    unsigned minimum_sm;
    /** Earlier valid PTX version used to isolate the PTX availability diagnostic. */
    std::optional<checker::PtxVersion> older_ptx;
    /** Earlier valid SM version used to isolate the SM availability diagnostic. */
    unsigned older_sm;
  };
  for (const auto& availability : {
           AvailabilityCase{"mad.rn.f32 %f0, %f1, %f2, %f3;",
                            {2, 0},
                            20,
                            checker::PtxVersion{1, 5},
                            13},
           AvailabilityCase{"mad.rp.ftz.sat.f32 %f0, %f1, %f2, %f3;",
                            {2, 0},
                            20,
                            checker::PtxVersion{1, 5},
                            13},
           AvailabilityCase{
               "mad.rn.f64 %d0, %d1, %d2, %d3;", {1, 0}, 13, std::nullopt, 12},
           AvailabilityCase{
               "mad.rz.f64 %d0, %d1, %d2, %d3;", {1, 0}, 13, std::nullopt, 12},
       }) {
    SCOPED_TRACE(availability.source);
    const auto parsed = test_helpers::parseInstruction(availability.source);
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
    EXPECT_TRUE(check_at({.ptx_version = availability.minimum_ptx,
                          .sm_version = availability.minimum_sm})
                    .has_value());
    if (availability.older_ptx) {
      const auto ptx_failure =
          check_at({.ptx_version = *availability.older_ptx,
                    .sm_version = availability.minimum_sm});
      ASSERT_FALSE(ptx_failure.has_value());
      EXPECT_EQ(ptx_failure.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    }
    const auto sm_failure = check_at({.ptx_version = availability.minimum_ptx,
                                      .sm_version = availability.older_sm});
    ASSERT_FALSE(sm_failure.has_value());
    EXPECT_EQ(sm_failure.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
}

/** Keep bound source identities alive and reject a valid wrong-width substitution. */
TEST(MadCompleteness, OwnsBoundSourcesAndRevalidatesWrongWidth) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<4>;
  .reg .f64 %d<4>;
  mad.rn.f32 %f0, %f1, %f2, %f3;
  mad.rn.f64 %d0, %d1, %d2, %d3;
}
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
  auto& f32 = std::get<Mad::RnF32>(
      std::get<Mad>(owned->functions.front().body.front()).variant);
  const auto f64_source =
      std::get<Mad::RnF64>(
          std::get<Mad>(owned->functions.front().body[1]).variant)
          .src2.value;
  f32.src2.value = f64_source;
  const auto invalid =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
