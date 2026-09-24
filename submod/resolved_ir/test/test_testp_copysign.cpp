#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Enforce the independent PTX and SM minima for both scalar widths and opcodes. */
TEST(TestpCopysignCompleteness, ChecksAvailabilityForBothTypesAndOpcodes) {
  for (const auto source :
       {"testp.finite.f32 %p0, %f1;", "testp.finite.f64 %p0, %fd1;",
        "copysign.f32 %f0, %f1, %f2;", "copysign.f64 %fd0, %fd1, %fd2;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
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
        check_at({.ptx_version = {2, 0}, .sm_version = 20}).has_value());
    const auto old_ptx = check_at({.ptx_version = {1, 5}, .sm_version = 20});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check_at({.ptx_version = {2, 0}, .sm_version = 13});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
}

/** Preserve typed operand constraints after parsing and syntax-AST lifetime end. */
TEST(TestpCompleteness, OwnsDeclaredOperandsAndRevalidatesCorruption) {
  std::optional<ResolvedModule> owned_module;
  {
    const std::string source = R"ptx(
.version 2.0
.target sm_20
.entry kernel() {
  .reg .pred %p<2>;
  .reg .f32 %f<3>;
  .reg .f64 %fd<3>;
  .reg .b32 %b<3>;
  .reg .b64 %bd<3>;
  testp.finite.f32 %p0, %b0;
  testp.subnormal.f64 %p1, %fd0;
  copysign.f64 %fd1, %fd2, %bd1;
}
)ptx";
    const auto parsed_module = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
    auto resolved = resolveModuleOnly(*parsed_module);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned_module.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(validateModule(*owned_module,
                             ModuleValidationPolicy::RequireCompleteContext)
                  .has_value());
  auto& f32_testp = std::get<Testp::F32>(
      std::get<Testp>(owned_module->functions.front().body.front()).variant);
  const auto original_f32_source = f32_testp.src.value;
  const auto f64_source = std::get<ResolvedRegisterRef>(
      std::get<Testp::F64>(
          std::get<Testp>(owned_module->functions.front().body[1]).variant)
          .src.value);
  f32_testp.src.value = f64_source;
  const auto invalid_testp = validateModule(
      *owned_module, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_testp.has_value());
  EXPECT_EQ(invalid_testp.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  f32_testp.src.value = original_f32_source;

  auto& copysign = std::get<Copysign::F64>(
      std::get<Copysign>(owned_module->functions.front().body[2]).variant);
  EXPECT_EQ(std::get<ResolvedRegisterRef>(copysign.sign_source.value).spelling,
            "%fd2");
  EXPECT_EQ(
      std::get<ResolvedRegisterRef>(copysign.magnitude_source.value).spelling,
      "%bd1");
  copysign.magnitude_source.value = original_f32_source;
  const auto invalid_copysign = validateModule(
      *owned_module, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_copysign.has_value());
  EXPECT_EQ(invalid_copysign.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

/** Validate both `copysign` source roles against declared containers and literals. */
TEST(CopysignCompleteness, ChecksDeclaredContainersAndSourceLiterals) {
  const auto valid = test_helpers::parseModule(R"ptx(
.version 2.0
.target sm_20
.entry kernel() {
  .reg .f32 %f<3>;
  .reg .f64 %fd<3>;
  .reg .b32 %b<3>;
  .reg .b64 %bd<3>;
  copysign.f32 %f0, %b0, 1.0;
  copysign.f64 %fd0, -2.0, %bd0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(valid);
  const auto resolved = resolveAndValidateModule(*valid);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  for (const auto source : {
           R"ptx(.version 2.0
.target sm_20
.entry kernel() { .reg .f32 %f<2>; copysign.f32 %f0, 1, %f1; })ptx",
           R"ptx(.version 2.0
.target sm_20
.entry kernel() { .reg .f32 %f<2>; copysign.f32 %f0, %f1, 1; })ptx",
           R"ptx(.version 2.0
.target sm_20
.entry kernel() { .reg .f32 %f<2>; .reg .b64 %bd<1>; copysign.f32 %f0, %bd0, %f1; })ptx",
           R"ptx(.version 2.0
.target sm_20
.entry kernel() { .reg .f32 %f<2>; .reg .b64 %bd<1>; copysign.f32 %f0, %f1, %bd0; })ptx",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
