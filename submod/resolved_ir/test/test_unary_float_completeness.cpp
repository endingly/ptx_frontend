#include <gtest/gtest.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/rcp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/rsqrt.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sqrt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"
#include "test_module_snapshot.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve one unary floating instruction to its exact final class. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic>
resolveUnaryFloat(const syntax_ast::AstInstruction& ast) {
  if (ast.opcode.syntax.text == "rcp") return resolveRcp(ast);
  if (ast.opcode.syntax.text == "sqrt") return resolveSqrt(ast);
  return resolveRsqrt(ast);
}

/** Resolve each typed reciprocal and square-root mode with legal floating operands. */
TEST(UnaryFloatCompleteness, ResolvesTypedModesAndFloatingContainers) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<12>; .reg .f64 %d<12>; .reg .b32 %r<4>; .reg .b64 %rd<4>;
  rcp.approx.ftz.f32 %r0, 1.0; rcp.rz.f32 %f0, %r1; rcp.rn.f64 %rd0, 1.0;
  rcp.rp.f64 %d0, %rd1; rcp.approx.ftz.f64 %d1, 1.0;
  sqrt.approx.f32 %f1, %r2; sqrt.rm.ftz.f32 %f2, 1.0; sqrt.rn.f64 %rd2, %d2;
  sqrt.rp.f64 %d3, 1.0; rsqrt.approx.ftz.f32 %f3, 1.0;
  rsqrt.approx.f64 %rd3, %d3; rsqrt.approx.ftz.f64 %d4, 1.0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  const auto& rcp_approx = dynamic_cast<const RcpApproxF32&>(*body[0]);
EXPECT_TRUE(RcpApproxF32::approx);
  EXPECT_TRUE(rcp_approx.ftz.value);
  EXPECT_EQ(dynamic_cast<const RcpDirectedF32&>(*body[1])
                .rounding.value,
            RoundingMode::Rz);
  EXPECT_EQ(RcpRnF64::rounding, RoundingMode::Rn);
  EXPECT_EQ(dynamic_cast<const RcpDirectedF64&>(*body[3])
                .rounding.value,
            RoundingMode::Rp);
  EXPECT_TRUE(RcpApproxFtzF64::approx);
  EXPECT_TRUE(RcpApproxFtzF64::ftz);
  EXPECT_TRUE(SqrtApproxF32::approx);
  EXPECT_EQ(dynamic_cast<const SqrtDirectedF32&>(*body[6])
                .rounding.value,
            RoundingMode::Rm);
  EXPECT_EQ(SqrtRnF64::rounding, RoundingMode::Rn);
  EXPECT_EQ(dynamic_cast<const SqrtDirectedF64&>(*body[8])
                .rounding.value,
            RoundingMode::Rp);
  EXPECT_TRUE(RsqrtApproxF32::approx);
  EXPECT_TRUE(dynamic_cast<const RsqrtApproxF32&>(*body[9])
                  .ftz.value);
  EXPECT_TRUE(RsqrtApproxF64::approx);
  EXPECT_TRUE(RsqrtApproxFtzF64::approx);
  EXPECT_TRUE(RsqrtApproxFtzF64::ftz);

  for (const auto [source, expected] : {
           std::pair{"rcp.rz.f32 %f0, %f1;", RoundingMode::Rz},
           {"rcp.rm.f32 %f0, %f1;", RoundingMode::Rm},
           {"rcp.rp.f32 %f0, %f1;", RoundingMode::Rp},
           {"sqrt.rz.f32 %f0, %f1;", RoundingMode::Rz},
           {"sqrt.rm.f32 %f0, %f1;", RoundingMode::Rm},
           {"sqrt.rp.f32 %f0, %f1;", RoundingMode::Rp},
       }) {
    const auto parsed_instruction = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
    if (std::string_view{source}.starts_with("rcp")) {
      const auto instruction = resolveRcp(*parsed_instruction);
      ASSERT_TRUE(instruction.has_value()) << instruction.error().message;
      EXPECT_EQ(dynamic_cast<const RcpDirectedF32&>(**instruction)
                    .rounding.value,
                expected);
    } else {
      const auto instruction = resolveSqrt(*parsed_instruction);
      ASSERT_TRUE(instruction.has_value()) << instruction.error().message;
      EXPECT_EQ(dynamic_cast<const SqrtDirectedF32&>(**instruction)
                    .rounding.value,
                expected);
    }
  }
  for (const auto [source, expected] : {
           std::pair{"rcp.rz.f64 %d0, %d1;", RoundingMode::Rz},
           {"rcp.rm.f64 %d0, %d1;", RoundingMode::Rm},
           {"rcp.rp.f64 %d0, %d1;", RoundingMode::Rp},
           {"sqrt.rz.f64 %d0, %d1;", RoundingMode::Rz},
           {"sqrt.rm.f64 %d0, %d1;", RoundingMode::Rm},
           {"sqrt.rp.f64 %d0, %d1;", RoundingMode::Rp},
       }) {
    const auto parsed_instruction = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
    if (std::string_view{source}.starts_with("rcp")) {
      const auto instruction = resolveRcp(*parsed_instruction);
      ASSERT_TRUE(instruction.has_value()) << instruction.error().message;
      EXPECT_EQ(dynamic_cast<const RcpDirectedF64&>(**instruction)
                    .rounding.value,
                expected);
    } else {
      const auto instruction = resolveSqrt(*parsed_instruction);
      ASSERT_TRUE(instruction.has_value()) << instruction.error().message;
      EXPECT_EQ(dynamic_cast<const SqrtDirectedF64&>(**instruction)
                    .rounding.value,
                expected);
    }
  }
}

/** Reject legacy omissions, forbidden rounded FP64 FTZ, mixed modes, sinks, and integer literals. */
TEST(UnaryFloatCompleteness, RejectsInvalidExplicitForms) {
  for (const auto source : {
           "rcp.f32 %f0, %f1;",
           "rcp.approx.rn.f32 %f0, %f1;",
           "rcp.rn.ftz.f64 %d0, %d1;",
           "rcp.approx.f64 %d0, %d1;",
           "sqrt.f64 %d0, %d1;",
           "sqrt.rp.ftz.f64 %d0, %d1;",
           "sqrt.rn.f32 _, %f1;",
           "sqrt.rn.sat.f32 %f0, %f1;",
           "sqrt.rn.full.f32 %f0, %f1;",
           "rsqrt.f32 %f0, %f1;",
           "rsqrt.approx.rn.f32 %f0, %f1;",
           "rsqrt.approx.f64 _, %d1;",
           "rsqrt.approx.ftz.f64 %d0, 1;",
           "rcp.approx.f32 %f0;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveUnaryFloat(*parsed).has_value());
  }
}

/** Reject integer containers and widths in representative destination and source roles. */
TEST(UnaryFloatCompleteness, RejectsIntegerAndWrongWidthContainers) {
  for (
      const auto source : {
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i; .reg .f32 %f; rcp.approx.f32 %i, %f; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i; .reg .f32 %f; rcp.approx.f32 %f, %i; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i; .reg .f32 %f; sqrt.rn.f32 %i, %f; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i; .reg .f32 %f; sqrt.rn.f32 %f, %i; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i; .reg .f32 %f; rsqrt.approx.f32 %i, %f; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .u32 %i; .reg .f32 %f; rsqrt.approx.f32 %f, %i; })ptx",
          R"ptx(.version 9.3 .target sm_100 .entry kernel() { .reg .f32 %f; .reg .f64 %d; rcp.approx.f32 %f, %d; })ptx",
      }) {
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(
        test_support::resolveAndValidateModuleSnapshot(*parsed).has_value());
  }
}

/** Check independent PTX and target floors for all unary floating cohorts. */
TEST(UnaryFloatCompleteness, ChecksIndependentAvailability) {
  /** One representative source and independent availability boundary for a unary cohort. */
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
           AvailabilityCase{"rcp.approx.f32 %f0, %f1;", {1, 4}, 0, {1, 3}, 0},
           AvailabilityCase{"rcp.rn.f32 %f0, %f1;", {2, 0}, 20, {1, 5}, 13},
           AvailabilityCase{"rcp.rz.f32 %f0, %f1;", {2, 0}, 20, {1, 5}, 13},
           AvailabilityCase{"rcp.rn.f64 %d0, %d1;", {1, 4}, 13, {1, 3}, 12},
           AvailabilityCase{"rcp.rz.f64 %d0, %d1;", {2, 0}, 20, {1, 5}, 13},
           AvailabilityCase{
               "rcp.approx.ftz.f64 %d0, %d1;", {2, 1}, 20, {2, 0}, 13},
           AvailabilityCase{"sqrt.approx.f32 %f0, %f1;", {1, 4}, 0, {1, 3}, 0},
           AvailabilityCase{"sqrt.rn.f32 %f0, %f1;", {2, 0}, 20, {1, 5}, 13},
           AvailabilityCase{"sqrt.rp.f32 %f0, %f1;", {2, 0}, 20, {1, 5}, 13},
           AvailabilityCase{"sqrt.rn.f64 %d0, %d1;", {1, 4}, 13, {1, 3}, 12},
           AvailabilityCase{"sqrt.rm.f64 %d0, %d1;", {2, 0}, 20, {1, 5}, 13},
           AvailabilityCase{"rsqrt.approx.f32 %f0, %f1;", {1, 4}, 0, {1, 3}, 0},
           AvailabilityCase{
               "rsqrt.approx.f64 %d0, %d1;", {1, 4}, 13, {1, 3}, 12},
           AvailabilityCase{
               "rsqrt.approx.ftz.f64 %d0, %d1;", {4, 0}, 20, {3, 2}, 13},
       }) {
    SCOPED_TRACE(item.source);
    const auto parsed = test_helpers::parseInstruction(item.source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveUnaryFloat(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto check_at = [&](checker::TargetInfo target) {
      return (*resolved)->check(checker::Context{.target = target});
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

/** Preserve unary owned bindings and reject a valid bound FP64 substitution in FP32 forms. */
TEST(UnaryFloatCompleteness, OwnsSourcesAndRevalidatesBoundWidth) {
  std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() { .reg .f32 %f<3>; .reg .f64 %d<3>;
  rcp.rn.f32 %f0, %f1; rcp.rn.f64 %d0, %d1; }
)ptx";
  {
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  }
  const auto checked = test_support::checkOwnedModuleMutation(
      std::move(source), test_support::OwnedMutationScenario::RcpSourceWidth);
  ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  ASSERT_TRUE(checked->before.has_value());
  ASSERT_FALSE(checked->after.has_value());
  EXPECT_EQ(checked->after.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
