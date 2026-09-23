#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve every second-slice unary form against declared physical containers. */
TEST(AbsNegCompleteness, ResolvesEveryFloatingCohort) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<4>;
  .reg .f64 %fd<4>;
  .reg .f16 %h<4>;
  .reg .f16x2 %hx<4>;
  .reg .b16 %b<4>;
  .reg .b32 %r<4>;
  abs.ftz.f32 %f0, %f1;
  abs.f64 %fd0, %fd1;
  abs.ftz.f16 %h0, %b1;
  abs.ftz.f16x2 %hx0, %r1;
  abs.bf16 %b0, %b1;
  abs.bf16x2 %r0, %r1;
  neg.ftz.f32 %f0, %f1;
  neg.f64 %fd0, %fd1;
  neg.ftz.f16 %h0, %b1;
  neg.ftz.f16x2 %r0, %r1;
  neg.bf16 %b0, %b1;
  neg.bf16x2 %r0, %r1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 12u);
  EXPECT_TRUE(std::holds_alternative<Abs::F32>(
      std::get<Abs>(resolved->functions.front().body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Abs::Bf16x2>(
      std::get<Abs>(resolved->functions.front().body[5]).variant));
  EXPECT_TRUE(std::holds_alternative<Neg::F16x2>(
      std::get<Neg>(resolved->functions.front().body[9]).variant));
  EXPECT_TRUE(std::holds_alternative<Neg::Bf16x2>(
      std::get<Neg>(resolved->functions.front().body[11]).variant));
}

/** Accept floating literals and matching bit containers for scalar FP forms. */
TEST(AbsNegCompleteness, AcceptsScalarFloatingLiteralsAndBitContainers) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .b32 %r<2>;
  .reg .b64 %rd<2>;
  abs.ftz.f32 %r0, 1.0;
  neg.f32 %r1, %r0;
  abs.f64 %rd0, 0d3ff0000000000000;
  neg.f64 %rd1, %rd0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  EXPECT_EQ(resolved->functions.front().body.size(), 4u);
}

/** Reject unsupported modifiers, non-floating literals, sinks, and operand counts. */
TEST(AbsNegCompleteness, RejectsForbiddenForms) {
  for (const auto source : {
           "abs.ftz.f64 %fd0, %fd1;",
           "abs.ftz.bf16 %b0, %b1;",
           "neg.ftz.bf16x2 %r0, %r1;",
           "neg.rn.f32 %f0, %f1;",
           "abs.f32 %f0, 1;",
           "neg.f16 %h0, 1.0;",
           "neg.f64 _, %fd1;",
           "abs.f16 %h0, %h1, %h2;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveInstruction(*parsed).has_value());
  }
}

/** Check independently old PTX and SM values for the non-uniform cohorts. */
TEST(AbsNegCompleteness, ChecksDistinctCohortAvailability) {
  /** One independent minimum and predecessor pair for a selected unary form. */
  struct AvailabilityCase {
    /** Complete instruction text for this selected variant. */
    const char* source;
    /** First PTX version accepted by the selected variant. */
    checker::PtxVersion minimum_ptx;
    /** First SM version accepted by the selected variant. */
    unsigned minimum_sm;
    /** A valid older PTX version when the form has one. */
    std::optional<checker::PtxVersion> older_ptx;
    /** A valid older SM version when the form has one. */
    std::optional<unsigned> older_sm;
  };
  for (const auto& availability : {
           AvailabilityCase{
               "abs.f64 %fd0, %fd1;", {1, 0}, 13, std::nullopt, 12},
           AvailabilityCase{
               "neg.f64 %fd0, %fd1;", {1, 0}, 13, std::nullopt, 12},
           AvailabilityCase{
               "abs.f16 %h0, %h1;", {6, 5}, 53, checker::PtxVersion{6, 4}, 52},
           AvailabilityCase{"abs.f16x2 %r0, %r1;",
                            {6, 5},
                            53,
                            checker::PtxVersion{6, 4},
                            52},
           AvailabilityCase{
               "neg.f16 %h0, %h1;", {6, 0}, 53, checker::PtxVersion{5, 0}, 52},
           AvailabilityCase{"neg.f16x2 %r0, %r1;",
                            {6, 0},
                            53,
                            checker::PtxVersion{5, 0},
                            52},
           AvailabilityCase{
               "abs.bf16 %b0, %b1;", {7, 0}, 80, checker::PtxVersion{6, 5}, 75},
           AvailabilityCase{"abs.bf16x2 %r0, %r1;",
                            {7, 0},
                            80,
                            checker::PtxVersion{6, 5},
                            75},
           AvailabilityCase{
               "neg.bf16 %b0, %b1;", {7, 0}, 80, checker::PtxVersion{6, 5}, 75},
           AvailabilityCase{"neg.bf16x2 %r0, %r1;",
                            {7, 0},
                            80,
                            checker::PtxVersion{6, 5},
                            75},
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
    if (availability.older_sm) {
      const auto sm_failure = check_at({.ptx_version = availability.minimum_ptx,
                                        .sm_version = *availability.older_sm});
      ASSERT_FALSE(sm_failure.has_value());
      EXPECT_EQ(sm_failure.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedSmVersion);
    }
  }
}

/** Gate the optional FP32 FTZ spelling without raising the base instruction floor. */
TEST(AbsNegCompleteness, ChecksFp32FtzValueAvailability) {
  for (const auto source : {"abs.ftz.f32 %f0, %f1;", "neg.ftz.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveInstruction(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto check_at = [&](checker::PtxVersion version) {
      return std::visit(
          [&](const auto& instruction) {
            return checker::check(
                instruction, checker::Context{.target = {.ptx_version = version,
                                                         .sm_version = 100}});
          },
          *resolved);
    };
    const auto before_ftz = check_at({1, 3});
    ASSERT_FALSE(before_ftz.has_value());
    EXPECT_EQ(before_ftz.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(check_at({1, 4}).has_value());
  }
}

/** Retain bound operand identities after source destruction and reject width drift. */
TEST(AbsNegCompleteness, OwnsBoundOperandsAndRevalidatesWrongWidth) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<2>;
  .reg .f64 %fd<2>;
  abs.f32 %f0, %f1;
  neg.f64 %fd0, %fd1;
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
  auto& abs = std::get<Abs::F32>(
      std::get<Abs>(owned->functions.front().body.front()).variant);
  const auto f64_source =
      std::get<Neg::F64>(
          std::get<Neg>(owned->functions.front().body[1]).variant)
          .src.value;
  abs.src.value = f64_source;
  const auto invalid =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
