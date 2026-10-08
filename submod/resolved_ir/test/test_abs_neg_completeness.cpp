#include <gtest/gtest.h>

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/abs.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/neg.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_module_snapshot.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve either unary opcode while retaining its exact semantic class. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolveAbsOrNeg(
    const syntax_ast::AstInstruction& ast) {
  if (ast.opcode.syntax.text == "abs")
    return resolveAbs(ast);
  return resolveNeg(ast);
}

/** Resolve every floating unary cohort with complete declaration context. */
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
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 12u);
  EXPECT_NE(dynamic_cast<AbsF32*>(body[0].get()), nullptr);
  EXPECT_NE(dynamic_cast<AbsBf16x2*>(body[5].get()), nullptr);
  EXPECT_NE(dynamic_cast<NegF16x2*>(body[9].get()), nullptr);
  EXPECT_NE(dynamic_cast<NegBf16x2*>(body[11].get()), nullptr);
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

/** Reject unsupported modifiers, non-floating literals, sinks, and counts. */
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
    EXPECT_FALSE(resolveAbsOrNeg(*parsed).has_value());
  }
}

/** Check distinct PTX and SM availability boundaries for each unary cohort. */
TEST(AbsNegCompleteness, ChecksDistinctCohortAvailability) {
  /** A selected instruction and its minimum and predecessor targets. */
  struct AvailabilityCase {
    /** Complete source instruction. */
    const char* source;
    /** Minimum supported PTX version. */
    checker::PtxVersion minimum_ptx;
    /** Minimum supported SM version. */
    unsigned minimum_sm;
    /** Older PTX version when one exists. */
    std::optional<checker::PtxVersion> older_ptx;
    /** Older SM version when one exists. */
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
    const auto resolved = resolveAbsOrNeg(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto check_at = [&](checker::TargetInfo target) {
      return (*resolved)->check(checker::Context{.target = target});
    };
    EXPECT_TRUE(check_at({.ptx_version = availability.minimum_ptx,
                          .sm_version = availability.minimum_sm})
                    .has_value());
    if (availability.older_ptx) {
      const auto failure = check_at({.ptx_version = *availability.older_ptx,
                                     .sm_version = availability.minimum_sm});
      ASSERT_FALSE(failure.has_value());
      EXPECT_EQ(failure.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    }
    if (availability.older_sm) {
      const auto failure = check_at({.ptx_version = availability.minimum_ptx,
                                     .sm_version = *availability.older_sm});
      ASSERT_FALSE(failure.has_value());
      EXPECT_EQ(failure.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedSmVersion);
    }
  }
}

/** FTZ raises the PTX requirement without changing the base F32 instruction. */
TEST(AbsNegCompleteness, ChecksFp32FtzValueAvailability) {
  for (const auto source : {"abs.ftz.f32 %f0, %f1;", "neg.ftz.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveAbsOrNeg(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto check_at = [&](checker::PtxVersion version) {
      return (*resolved)->check(checker::Context{
          .target = {.ptx_version = version, .sm_version = 100}});
    };
    const auto before_ftz = check_at({1, 3});
    ASSERT_FALSE(before_ftz.has_value());
    EXPECT_EQ(before_ftz.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(check_at({1, 4}).has_value());
  }
}

/** Retain bound operands after AST release and reject a later width mutation. */
TEST(AbsNegCompleteness, OwnsBoundOperandsAndRevalidatesWrongWidth) {
  std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<2>;
  .reg .f64 %fd<2>;
  abs.f32 %f0, %f1;
  neg.f64 %fd0, %fd1;
}
)ptx";
  {
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  }
  const auto checked = test_support::checkOwnedModuleMutation(
      std::move(source), test_support::OwnedMutationScenario::AbsSourceWidth);
  ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  ASSERT_TRUE(checked->before.has_value());
  ASSERT_FALSE(checked->after.has_value());
  EXPECT_EQ(checked->after.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
