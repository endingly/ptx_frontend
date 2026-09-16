#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

/** Validate every scalar and pre-family-target spelling added to this slice. */
TEST(IntegerArithmeticCompleteness, ResolvesAndChecksAllScalarAndPackedForms) {
  const auto parsed_module = parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .u16 %uh<8>;
  .reg .s16 %sh<8>;
  .reg .u32 %u<12>;
  .reg .s32 %s<12>;
  .reg .u64 %ud<8>;
  .reg .s64 %sd<8>;
  .reg .b32 %b<8>;
  mad.lo.u16 %uh0, %uh1, %uh2, %uh3;
  mad.lo.u32 %u0, %u1, %u2, %u3;
  mad.lo.u64 %ud0, %ud1, %ud2, %ud3;
  mad.lo.s16 %sh0, %sh1, %sh2, %sh3;
  mad.lo.s32 %s0, %s1, %s2, %s3;
  mad.lo.s64 %sd0, %sd1, %sd2, %sd3;
  mad.hi.u16 %uh0, %uh1, %uh2, %uh3;
  mad.hi.u32 %u0, %u1, %u2, %u3;
  mad.hi.u64 %ud0, %ud1, %ud2, %ud3;
  mad.hi.s16 %sh0, %sh1, %sh2, %sh3;
  mad.hi.s32 %s0, %s1, %s2, %s3;
  mad.hi.s64 %sd0, %sd1, %sd2, %sd3;
  mad.hi.sat.s32 %s0, %s1, %s2, %s3;
  mad.wide.u16 %u0, %uh1, %uh2, %u3;
  mad.wide.u32 %ud0, %u1, %u2, %ud3;
  mad.wide.s16 %s0, %sh1, %sh2, %s3;
  mad.wide.s32 %sd0, %s1, %s2, %sd3;
  clmad.lo.u64 %ud0, %ud1, %ud2, %ud3;
  clmad.hi.u64 %ud0, %ud1, %ud2, %ud3;
  mul24.lo.u32 %u0, %u1, %u2;
  mul24.lo.s32 %s0, %s1, %s2;
  mul24.hi.u32 %u0, %u1, %u2;
  mul24.hi.s32 %s0, %s1, %s2;
  mad24.lo.u32 %u0, %u1, %u2, %u3;
  mad24.lo.s32 %s0, %s1, %s2, %s3;
  mad24.hi.u32 %u0, %u1, %u2, %u3;
  mad24.hi.s32 %s0, %s1, %s2, %s3;
  mad24.hi.sat.s32 %s0, %s1, %s2, %s3;
  sad.u16 %uh0, %uh1, %uh2, %uh3;
  sad.u32 %u0, %u1, %u2, %u3;
  sad.u64 %ud0, %ud1, %ud2, %ud3;
  sad.s16 %sh0, %sh1, %sh2, %sh3;
  sad.s32 %s0, %s1, %s2, %s3;
  sad.s64 %sd0, %sd1, %sd2, %sd3;
  div.u16 %uh0, %uh1, %uh2;
  div.u32 %u0, %u1, %u2;
  div.u64 %ud0, %ud1, %ud2;
  div.s16 %sh0, %sh1, %sh2;
  div.s32 %s0, %s1, %s2;
  div.s64 %sd0, %sd1, %sd2;
  rem.u16 %uh0, %uh1, %uh2;
  rem.u32 %u0, %u1, %u2;
  rem.u64 %ud0, %ud1, %ud2;
  rem.s16 %sh0, %sh1, %sh2;
  rem.s32 %s0, %s1, %s2;
  rem.s64 %sd0, %sd1, %sd2;
  abs.s16 %sh0, %sh1;
  abs.s32 %s0, %s1;
  abs.s64 %sd0, %sd1;
  neg.s16 %sh0, %sh1;
  neg.s32 %s0, %s1;
  neg.s64 %sd0, %sd1;
  min.u16 %uh0, %uh1, %uh2;
  min.u32 %u0, %u1, %u2;
  min.u64 %ud0, %ud1, %ud2;
  min.s16 %sh0, %sh1, %sh2;
  min.s32 %s0, %s1, %s2;
  min.s64 %sd0, %sd1, %sd2;
  min.u16x2 %b0, %b1, %b2;
  min.s16x2 %b0, %b1, %b2;
  min.relu.s32 %s0, %s1, %s2;
  min.relu.s16x2 %b0, %b1, %b2;
  max.u16 %uh0, %uh1, %uh2;
  max.u32 %u0, %u1, %u2;
  max.u64 %ud0, %ud1, %ud2;
  max.s16 %sh0, %sh1, %sh2;
  max.s32 %s0, %s1, %s2;
  max.s64 %sd0, %sd1, %sd2;
  max.u16x2 %b0, %b1, %b2;
  max.s16x2 %b0, %b1, %b2;
  max.relu.s32 %s0, %s1, %s2;
  max.relu.s16x2 %b0, %b1, %b2;
  fns.b32 %b0, 0xaaaaaaaa, 3, -1;
  szext.clamp.u32 %u0, %u1, 32;
  szext.wrap.u32 %u0, %u1, 33;
  szext.clamp.s32 %s0, %s1, 32;
  szext.wrap.s32 %s0, %s1, 33;
  bmsk.clamp.b32 %b0, %u1, 32;
  bmsk.wrap.b32 %b0, %u1, 33;
  dp4a.u32.u32 %u0, %u1, %u2, %u3;
  dp4a.u32.s32 %s0, %u1, %s2, %s3;
  dp4a.s32.u32 %s0, %s1, %u2, %s3;
  dp4a.s32.s32 %s0, %s1, %s2, %s3;
  dp2a.lo.u32.u32 %u0, %u1, %u2, %u3;
  dp2a.lo.u32.s32 %s0, %u1, %s2, %s3;
  dp2a.lo.s32.u32 %s0, %s1, %u2, %s3;
  dp2a.lo.s32.s32 %s0, %s1, %s2, %s3;
  dp2a.hi.u32.u32 %u0, %u1, %u2, %u3;
  dp2a.hi.u32.s32 %s0, %u1, %s2, %s3;
  dp2a.hi.s32.u32 %s0, %s1, %u2, %s3;
  dp2a.hi.s32.s32 %s0, %s1, %s2, %s3;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
  const auto resolved = resolveModule(*parsed_module);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  ASSERT_EQ(resolved->functions.front().body.size(), 91u);

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
      .instruction_range = parsed_module->range,
  };
  for (const auto& instruction : resolved->functions.front().body) {
    const auto checked = std::visit(
        [&context](const auto& concrete) {
          return checker::check(concrete, context);
        },
        instruction);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
}

/** Validate family-target packed byte forms and their target availability. */
TEST(IntegerArithmeticCompleteness, ResolvesFamilySpecificPackedByteForms) {
  const auto parsed_module = parseModule(R"ptx(
.version 9.3
.target sm_120f
.entry kernel() {
  .reg .b32 %b<4>;
  neg.s8x4 %b0, %b1;
  min.u8x4 %b0, %b1, %b2;
  min.s8x4 %b0, %b1, %b2;
  min.relu.s8x4 %b0, %b1, %b2;
  max.u8x4 %b0, %b1, %b2;
  max.s8x4 %b0, %b1, %b2;
  max.relu.s8x4 %b0, %b1, %b2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
  const auto resolved = resolveModule(*parsed_module);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  EXPECT_TRUE(
      validateModule(*resolved, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

/** Accept the documented alternate modifier order for packed ReLU minimum. */
TEST(IntegerArithmeticCompleteness,
     ResolvesPackedReluMinimumModifierOrderAlias) {
  const auto parsed_module = parseModule(R"ptx(
.version 8.0
.target sm_90
.entry kernel() {
  .reg .b32 %b<4>;
  min.relu.s16x2 %b0, %b1, %b2;
  min.s16x2.relu %b0, %b1, %b2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
  const auto resolved = resolveModule(*parsed_module);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 2u);

  for (const auto& instruction : resolved->functions.front().body) {
    const auto& minimum = std::get<Min>(instruction);
    ASSERT_NE(std::get_if<Min::ReluS16x2>(&minimum.variant), nullptr);
    EXPECT_TRUE(Min::ReluS16x2::relu);
    EXPECT_EQ(Min::ReluS16x2::type, ScalarType::S16x2);
    EXPECT_TRUE(checker::check(
                    minimum, checker::Context{.target = {.ptx_version = {8, 0},
                                                         .sm_version = 90}})
                    .has_value());
    EXPECT_FALSE(checker::check(
                     minimum, checker::Context{.target = {.ptx_version = {7, 9},
                                                          .sm_version = 90}})
                     .has_value());
    EXPECT_FALSE(checker::check(
                     minimum, checker::Context{.target = {.ptx_version = {8, 0},
                                                          .sm_version = 80}})
                     .has_value());
  }

  const auto duplicate_relu =
      test_helpers::parseInstruction("min.s16x2.relu.relu %r0, %r1, %r2;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(duplicate_relu);
  EXPECT_FALSE(resolveInstruction(*duplicate_relu).has_value());
}

/** Verify the PTX, SM, and target-family floors for newly added forms. */
TEST(IntegerArithmeticCompleteness, EnforcesNewFormTargetFloors) {
  constexpr std::array<std::string_view, 1> sm120f{"sm_120f"};

  /** A spelling and its two rejected and one accepted target contexts. */
  struct TargetCase {
    /** PTX instruction spelling resolved before availability validation. */
    std::string_view source;
    /** Target below the form's PTX-version floor. */
    checker::Context old_ptx;
    /** Target below the form's SM or target-family floor. */
    checker::Context old_target;
    /** Diagnostic category expected for the target-floor failure. */
    checker::CheckDiagnosticKind old_target_diagnostic;
    /** The first target that supports the complete form. */
    checker::Context supported;
  };

  const std::array cases{
      TargetCase{
          "clmad.lo.u64 %rd0, %rd1, %rd2, %rd3;",
          {.target = {.ptx_version = {9, 2}, .sm_version = 80}},
          {.target = {.ptx_version = {9, 3}, .sm_version = 75}},
          checker::CheckDiagnosticKind::UnsupportedSmVersion,
          {.target = {.ptx_version = {9, 3}, .sm_version = 80}},
      },
      TargetCase{
          "fns.b32 %r0, %r1, %r2, %r3;",
          {.target = {.ptx_version = {5, 9}, .sm_version = 30}},
          {.target = {.ptx_version = {6, 0}, .sm_version = 20}},
          checker::CheckDiagnosticKind::UnsupportedSmVersion,
          {.target = {.ptx_version = {6, 0}, .sm_version = 30}},
      },
      TargetCase{
          "szext.clamp.u32 %r0, %r1, %r2;",
          {.target = {.ptx_version = {7, 5}, .sm_version = 70}},
          {.target = {.ptx_version = {7, 6}, .sm_version = 60}},
          checker::CheckDiagnosticKind::UnsupportedSmVersion,
          {.target = {.ptx_version = {7, 6}, .sm_version = 70}},
      },
      TargetCase{
          "bmsk.wrap.b32 %r0, %r1, %r2;",
          {.target = {.ptx_version = {7, 5}, .sm_version = 70}},
          {.target = {.ptx_version = {7, 6}, .sm_version = 60}},
          checker::CheckDiagnosticKind::UnsupportedSmVersion,
          {.target = {.ptx_version = {7, 6}, .sm_version = 70}},
      },
      TargetCase{
          "dp4a.u32.s32 %r0, %r1, %r2, %r3;",
          {.target = {.ptx_version = {4, 9}, .sm_version = 61}},
          {.target = {.ptx_version = {5, 0}, .sm_version = 60}},
          checker::CheckDiagnosticKind::UnsupportedSmVersion,
          {.target = {.ptx_version = {5, 0}, .sm_version = 61}},
      },
      TargetCase{
          "dp2a.hi.s32.u32 %r0, %r1, %r2, %r3;",
          {.target = {.ptx_version = {4, 9}, .sm_version = 61}},
          {.target = {.ptx_version = {5, 0}, .sm_version = 60}},
          checker::CheckDiagnosticKind::UnsupportedSmVersion,
          {.target = {.ptx_version = {5, 0}, .sm_version = 61}},
      },
      TargetCase{
          "min.s8x4 %r0, %r1, %r2;",
          {.target = {.ptx_version = {9, 1},
                      .sm_version = 120,
                      .enabled_family_features = sm120f}},
          {.target = {.ptx_version = {9, 2}, .sm_version = 120}},
          checker::CheckDiagnosticKind::UnsupportedTargetFamily,
          {.target = {.ptx_version = {9, 2},
                      .sm_version = 120,
                      .enabled_family_features = sm120f}},
      },
  };

  for (const auto& target_case : cases) {
    SCOPED_TRACE(target_case.source);
    const auto parsed_instruction =
        test_helpers::parseInstruction(target_case.source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
    const auto resolved = resolveInstruction(*parsed_instruction);
    ASSERT_TRUE(resolved.has_value());
    const auto check = [&resolved](const checker::Context& context) {
      return std::visit(
          [&context](const auto& concrete) {
            return checker::check(concrete, context);
          },
          *resolved);
    };
    const auto old_ptx = check(target_case.old_ptx);
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_target = check(target_case.old_target);
    ASSERT_FALSE(old_target.has_value());
    EXPECT_EQ(old_target.error().front().kind,
              target_case.old_target_diagnostic);
    EXPECT_TRUE(check(target_case.supported).has_value());
  }
}

TEST(IntegerArithmeticCompleteness,
     RejectsSpecialModifierAndImmediateBoundaries) {
  constexpr std::array<std::string_view, 3> rejected_sources{{
      "mad.wide.u64 %rd0, %rd1, %rd2, %rd3;",
      "mad.lo.sat.s32 %r0, %r1, %r2, %r3;",
      "mad24.lo.sat.s32 %r0, %r1, %r2, %r3;",
  }};
  for (const auto source : rejected_sources) {
    SCOPED_TRACE(source);
    const auto parsed_instruction = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
    const auto resolved = resolveInstruction(*parsed_instruction);
    EXPECT_FALSE(resolved.has_value());
  }

  const auto parsed_fns =
      test_helpers::parseInstruction("fns.b32 %r0, %r1, 32, 1;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_fns);
  const auto resolved_fns = resolveInstruction(*parsed_fns);
  ASSERT_TRUE(resolved_fns.has_value());
  const auto checked = checker::check(
      std::get<Fns>(*resolved_fns),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 30}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
}

/** Keep newly added typed operands valid after their syntax tree is released. */
TEST(IntegerArithmeticCompleteness, RetainsOwnedOperandsAndRejectsMutation) {
  std::optional<ResolvedModule> owned_module;
  {
    const auto parsed_module = parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .u32 %u<4>;
  .reg .s32 %s<4>;
  .reg .b32 %b<2>;
  fns.b32 %b0, %b1, %u0, %s0;
  dp4a.u32.s32 %s0, %u1, %s1, %s2;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
    auto resolved = resolveModule(*parsed_module);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned_module.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(validateModule(*owned_module,
                             ModuleValidationPolicy::RequireCompleteContext)
                  .has_value());
  auto& dp4a = std::get<Dp4a::U32S32>(
      std::get<Dp4a>(owned_module->functions.front().body[1]).variant);
  std::get<ResolvedRegisterRef>(dp4a.accumulator.value).declared_type =
      ScalarType::U64;
  const auto invalid = validateModule(
      *owned_module, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
