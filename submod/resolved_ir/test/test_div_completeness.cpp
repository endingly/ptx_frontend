#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/div/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/div/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/div/resolution.gen.hpp>

#include "test_module_projection.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve every explicit floating DIV mode with its typed flags and rounding. */
TEST(DivCompleteness, ResolvesExplicitFloatingModesAndOperands) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<8>;
  .reg .f64 %d<6>;
  .reg .b32 %r<4>;
  .reg .b64 %rd<3>;
  div.approx.ftz.f32 %r0, 1.0, %f2;
  div.full.f32 %f0, %r1, 1.0;
  div.rn.ftz.f32 %f0, %f1, 1.0;
  div.rz.f32 %r0, 1.0, %f2;
  div.rm.ftz.f32 %f0, %f1, %r2;
  div.rp.f32 %f0, %f1, %f2;
  div.rn.f64 %rd0, 1.0, %d2;
  div.rz.f64 %d0, %rd1, 1.0;
  div.rm.f64 %d0, %d1, %d2;
  div.rp.f64 %rd0, %d1, %d2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = test_support::resolveTypedModule<Div>(
      *parsed, test_support::ModulePipeline::CompleteContext);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  const auto& approx = std::get<Div::ApproxF32>(std::get<Div>(body[0]).variant);
  EXPECT_TRUE(Div::ApproxF32::approx);
  EXPECT_TRUE(approx.ftz.value);
  const auto& full = std::get<Div::FullF32>(std::get<Div>(body[1]).variant);
  EXPECT_TRUE(Div::FullF32::full);
  EXPECT_FALSE(full.ftz.value);
  const auto& rn_f32 = std::get<Div::RnF32>(std::get<Div>(body[2]).variant);
  EXPECT_TRUE(rn_f32.ftz.value);
  EXPECT_EQ(Div::RnF32::rounding, RoundingMode::Rn);
  EXPECT_EQ(
      std::get<Div::DirectedF32>(std::get<Div>(body[3]).variant).rounding.value,
      RoundingMode::Rz);
  EXPECT_TRUE(
      std::get<Div::DirectedF32>(std::get<Div>(body[4]).variant).ftz.value);
  EXPECT_EQ(
      std::get<Div::DirectedF32>(std::get<Div>(body[5]).variant).rounding.value,
      RoundingMode::Rp);
  EXPECT_EQ(Div::RnF64::rounding, RoundingMode::Rn);
  EXPECT_EQ(
      std::get<Div::DirectedF64>(std::get<Div>(body[7]).variant).rounding.value,
      RoundingMode::Rz);
  EXPECT_EQ(
      std::get<Div::DirectedF64>(std::get<Div>(body[8]).variant).rounding.value,
      RoundingMode::Rm);
  EXPECT_EQ(
      std::get<Div::DirectedF64>(std::get<Div>(body[9]).variant).rounding.value,
      RoundingMode::Rp);
}

/** Reject legacy, mixed-mode, forbidden-flag, sink, and integer source forms. */
TEST(DivCompleteness, RejectsInvalidExplicitForms) {
  for (const auto source : {
           "div.f32 %f0, %f1, %f2;",
           "div.f64 %d0, %d1, %d2;",
           "div.approx.rn.f32 %f0, %f1, %f2;",
           "div.approx.full.f32 %f0, %f1, %f2;",
           "div.rn.approx.f32 %f0, %f1, %f2;",
           "div.rn.ftz.f64 %d0, %d1, %d2;",
           "div.rp.ftz.f64 %d0, %d1, %d2;",
           "div.full.f64 %d0, %d1, %d2;",
           "div.approx.f32 _, %f1, %f2;",
           "div.rn.f64 _, %d1, %d2;",
           "div.rn.f32 %f0, 1, %f2;",
           "div.rn.f32 %f0, %f1;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolve<Div>(*parsed).has_value());
  }
}

/** Verify independent availability floors for each explicit floating DIV cohort. */
TEST(DivCompleteness, ChecksIndependentExplicitAvailability) {
  struct AvailabilityCase {
    /** Representative source for the explicit DIV cohort. */
    const char* source;
    /** First supported PTX version. */
    checker::PtxVersion minimum_ptx;
    /** First supported target SM. */
    unsigned minimum_sm;
    /** Earlier valid PTX version, if the cohort has one. */
    std::optional<checker::PtxVersion> older_ptx;
    /** Earlier supported SM to isolate the target diagnostic. */
    unsigned older_sm;
  };
  for (const auto& availability : {
           AvailabilityCase{"div.approx.f32 %f0, %f1, %f2;",
                            {1, 4},
                            0,
                            checker::PtxVersion{1, 3},
                            0},
           AvailabilityCase{"div.full.f32 %f0, %f1, %f2;",
                            {1, 4},
                            0,
                            checker::PtxVersion{1, 3},
                            0},
           AvailabilityCase{"div.rp.f32 %f0, %f1, %f2;",
                            {1, 4},
                            20,
                            checker::PtxVersion{1, 3},
                            13},
           AvailabilityCase{"div.rn.f64 %d0, %d1, %d2;",
                            {1, 4},
                            13,
                            checker::PtxVersion{1, 3},
                            12},
           AvailabilityCase{"div.rz.f64 %d0, %d1, %d2;",
                            {1, 4},
                            20,
                            checker::PtxVersion{1, 3},
                            13},
       }) {
    SCOPED_TRACE(availability.source);
    const auto parsed = test_helpers::parseInstruction(availability.source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Div>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto check_at = [&](checker::TargetInfo target) {
      return checker::check(*resolved, checker::Context{.target = target});
    };
    EXPECT_TRUE(check_at({.ptx_version = availability.minimum_ptx,
                          .sm_version = availability.minimum_sm})
                    .has_value());
    const auto ptx_failure = check_at({.ptx_version = *availability.older_ptx,
                                       .sm_version = availability.minimum_sm});
    ASSERT_FALSE(ptx_failure.has_value());
    EXPECT_EQ(ptx_failure.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    if (availability.minimum_sm != 0) {
      const auto sm_failure = check_at({.ptx_version = availability.minimum_ptx,
                                        .sm_version = availability.older_sm});
      ASSERT_FALSE(sm_failure.has_value());
      EXPECT_EQ(sm_failure.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedSmVersion);
    }
  }
}

/** Preserve owned DIV bindings after source destruction and reject a bound width mismatch. */
TEST(DivCompleteness, OwnsBoundSourcesAndRevalidatesWrongWidth) {
  std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<3>;
  .reg .f64 %d<3>;
  div.rn.f32 %f0, %f1, %f2;
  div.rn.f64 %d0, %d1, %d2;
}
)ptx";
  {
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  }
  const auto checked = test_support::checkOwnedModuleMutation(
      std::move(source), test_support::OwnedMutationScenario::DivSourceWidth);
  ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  ASSERT_TRUE(checked->before.has_value());
  ASSERT_FALSE(checked->after.has_value());
  EXPECT_EQ(checked->after.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
