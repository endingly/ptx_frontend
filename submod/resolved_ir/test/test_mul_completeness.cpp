#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;
using test_helpers::parseModule;

TEST(MulCompleteness, ResolvesAndChecksEveryPtx93FormWithDeclaredOperands) {
  const auto parsed_module = parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .u16 %uh<3>;
  .reg .u32 %u<3>;
  .reg .u64 %ud<3>;
  .reg .s16 %sh<3>;
  .reg .s32 %s<3>;
  .reg .s64 %sd<3>;
  .reg .b16 %b16<3>;
  .reg .b32 %b32<3>;
  .reg .b64 %b64<3>;
  .reg .f32 %f<3>;
  .reg .f64 %fd<3>;

  mul.lo.u16 %uh0, %uh1, 7;
  mul.lo.u32 %u0, %u1, 7;
  mul.lo.u64 %ud0, %ud1, 7;
  mul.lo.s16 %sh0, %sh1, -7;
  mul.lo.s32 %s0, %s1, -7;
  mul.lo.s64 %sd0, %sd1, -7;
  mul.hi.u16 %uh0, %uh1, %uh2;
  mul.hi.u32 %u0, %u1, %u2;
  mul.hi.u64 %ud0, %ud1, %ud2;
  mul.hi.s16 %sh0, %sh1, %sh2;
  mul.hi.s32 %s0, %s1, %s2;
  mul.hi.s64 %sd0, %sd1, %sd2;
  mul.wide.u16 %u0, %uh1, %uh2;
  mul.wide.s16 %s0, %sh1, %sh2;
  mul.wide.u32 %ud0, %u1, %u2;
  mul.wide.s32 %sd0, %s1, %s2;
  mul.f32 %f0, %f1, 1.0;
  mul.rz.ftz.sat.f32 %f0, %f1, %f2;
  mul.rp.ftz.f32x2 %b640, %b641, %b642;
  mul.rm.f64 %fd0, %fd1, 1.0;
  mul.rn.ftz.sat.f16 %b160, %b161, %b162;
  mul.rn.ftz.sat.f16x2 %b320, %b321, %b322;
  mul.rn.bf16 %b160, %b161, %b162;
  mul.rn.bf16x2 %b320, %b321, %b322;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
  const auto resolved = resolveModule(*parsed_module);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  ASSERT_EQ(resolved->functions.front().body.size(), 24u);

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
      .instruction_range = parsed_module->range,
  };
  for (const auto& instruction : resolved->functions.front().body) {
    const auto& mul = std::get<Mul>(instruction);
    const auto checked = checker::check(mul, context);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
}

TEST(MulCompleteness, EnforcesPerFormAvailabilityAndExactPackedContainers) {
  const std::array<std::pair<std::string_view, checker::TargetInfo>, 5>
      unavailable_cases{{
          {"mul.rm.f32 %f0, %f1, %f2;",
           {.ptx_version = {1, 0}, .sm_version = 10}},
          {"mul.f64 %fd0, %fd1, %fd2;",
           {.ptx_version = {1, 0}, .sm_version = 12}},
          {"mul.f32x2 %rd0, %rd1, %rd2;",
           {.ptx_version = {8, 6}, .sm_version = 90}},
          {"mul.f16 %h0, %h1, %h2;", {.ptx_version = {4, 2}, .sm_version = 52}},
          {"mul.bf16 %h0, %h1, %h2;",
           {.ptx_version = {7, 8}, .sm_version = 89}},
      }};
  for (const auto& [source, target] : unavailable_cases) {
    SCOPED_TRACE(source);
    const auto parsed_instruction = parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
    const auto resolved = resolve<Mul>(*parsed_instruction);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto checked = checker::check(
        *resolved,
        checker::Context{.target = target,
                         .instruction_range = parsed_instruction->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_TRUE(checked.error().front().kind ==
                    checker::CheckDiagnosticKind::UnsupportedPtxVersion ||
                checked.error().front().kind ==
                    checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }

  const auto parsed_module = parseModule(R"ptx(
.version 8.6
.target sm_100
.entry kernel() {
  .reg .b64 %r<3>;
  mul.f32x2 %r0, %r1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
  auto resolved = resolveModule(*parsed_module);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& packed = std::get<Mul::F32x2>(
      std::get<Mul>(resolved->functions.front().body.front()).variant);
  ASSERT_EQ(packed.dst.value.declared_type, ScalarType::B64);
  packed.dst.value.declared_type = ScalarType::B32;
  const auto checked = checker::check(
      std::get<Mul>(resolved->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {8, 6}, .sm_version = 100}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto invalid_module = parseModule(R"ptx(
.version 8.6
.target sm_100
.entry kernel() {
  .reg .b32 %r<3>;
  mul.f32x2 %r0, %r1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(invalid_module);
  EXPECT_FALSE(resolveModule(*invalid_module).has_value());
}

TEST(MulCompleteness, RejectsIllegalModesModifiersAndPackedImmediates) {
  constexpr std::array<std::string_view, 6> rejected_sources{{
      "mul.wide.u64 %rd0, %rd1, %rd2;",
      "mul.sat.f64 %fd0, %fd1, %fd2;",
      "mul.sat.f32x2 %rd0, %rd1, %rd2;",
      "mul.rz.f16 %h0, %h1, %h2;",
      "mul.ftz.bf16 %h0, %h1, %h2;",
      "mul.f32x2 %rd0, 1.0, %rd1;",
  }};
  for (const auto source : rejected_sources) {
    SCOPED_TRACE(source);
    const auto parsed_instruction = parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
    EXPECT_FALSE(resolve<Mul>(*parsed_instruction).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
