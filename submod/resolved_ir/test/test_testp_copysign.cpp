#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/copysign.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/testp.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using checker::PtxVersion;

/** Return the property selected by either floating-point `testp` variant. */
TestProperty test_property_value(const Instruction& instruction) {
  if (const auto* f32 = dynamic_cast<const TestpF32*>(&instruction))
    return f32->property.value;
  return dynamic_cast<const TestpF64&>(instruction).property.value;
}

/** Resolve a standalone `testp` form after releasing its parsed AST. */
std::unique_ptr<Instruction> resolve_owned_testp(std::string_view source) {
  const auto parsed = test_helpers::parseInstruction(source);
  EXPECT_TRUE(parsed.has_value());
  if (!parsed)
    return {};
  auto resolved = resolveTestp(*parsed);
  EXPECT_TRUE(resolved.has_value());
  if (!resolved)
    return {};
  return std::move(*resolved);
}

TEST(TestpCompleteness, ResolvesEveryPropertyForBothFloatingTypes) {
  for (const auto& [property, expected] :
       std::array<std::pair<std::string_view, TestProperty>, 6>{
           {{"finite", TestProperty::Finite},
            {"infinite", TestProperty::Infinite},
            {"number", TestProperty::Number},
            {"notanumber", TestProperty::NotANumber},
            {"normal", TestProperty::Normal},
            {"subnormal", TestProperty::Subnormal}}}) {
    for (const auto [type, destination, source] :
         {std::tuple{"f32", "%p0", "%f1"}, std::tuple{"f64", "%p0", "%fd1"}}) {
      const auto text = std::string("testp.") + std::string(property) + "." +
                        type + " " + destination + ", " + source + ";";
      SCOPED_TRACE(text);
      const auto resolved = resolve_owned_testp(text);
      ASSERT_TRUE(resolved);
      EXPECT_EQ(test_property_value(*resolved), expected);
    }
  }
}

TEST(TestpCompleteness, RequiresPredicatesFloatingTypesAndKnownProperties) {
  for (const auto source :
       {"testp.eq.f32 %p0, %f1;", "testp.finite.f16 %p0, %h1;",
        "testp.finite.f32 %r0, %f1;", "testp.finite.f32 _, %f1;",
        "testp.finite.f32 %p0, !%p1;", "testp.finite.f32 %p0, 1;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(resolveTestp(*parsed).has_value());
  }
}

/** Accept floating source literals while rejecting the integer-literal form. */
TEST(TestpCompleteness, AcceptsFloatingSourceLiterals) {
  for (const auto source :
       {"testp.finite.f32 %p0, 1.0;", "testp.normal.f64 %p0, -2.0;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveTestp(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_TRUE((*resolved)
                    ->check(checker::Context{
                        .target = {.ptx_version = {2, 0}, .sm_version = 20}})
                    .has_value());
  }
}

TEST(TestpCompleteness, ChecksIndependentPtxAndSmBoundariesAndCorruption) {
  auto resolved = resolve_owned_testp("testp.normal.f64 %p0, %fd1;");
  ASSERT_TRUE(resolved);
  const checker::Context current{
      .target = {.ptx_version = {2, 0}, .sm_version = 20}};
  EXPECT_TRUE(resolved->check(current).has_value());
  const auto old_ptx = resolved->check(
      checker::Context{.target = {.ptx_version = {1, 5}, .sm_version = 20}});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = resolved->check(
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 13}});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  auto& property = dynamic_cast<TestpF64&>(*resolved).property.value;
  property = TestProperty::Invalid;
  EXPECT_FALSE(resolved->check(current).has_value());
  property = static_cast<TestProperty>(255);
  EXPECT_FALSE(resolved->check(current).has_value());
}

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
      return (*resolved)->check(checker::Context{.target = target});
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
  auto& f32_testp =
      dynamic_cast<TestpF32&>(*owned_module->functions.front().body.front());
  const auto original_f32_source = f32_testp.src.value;
  const auto f64_source = std::get<ResolvedRegisterRef>(
      dynamic_cast<const TestpF64&>(*owned_module->functions.front().body[1])
          .src.value);
  f32_testp.src.value = f64_source;
  const auto invalid_testp = validateModule(
      *owned_module, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_testp.has_value());
  EXPECT_EQ(invalid_testp.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  f32_testp.src.value = original_f32_source;

  auto& copysign =
      dynamic_cast<CopysignF64&>(*owned_module->functions.front().body[2]);
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

TEST(CopysignCompleteness, RetainsSignThenMagnitudeSourceIdentity) {
  const auto parsed =
      test_helpers::parseInstruction("copysign.f64 %fd0, %fd1, %fd2;");
  ASSERT_TRUE(parsed.has_value());
  const auto resolved = resolveCopysign(*parsed);
  ASSERT_TRUE(resolved.has_value());
  const auto& variant = dynamic_cast<const CopysignF64&>(**resolved);
  EXPECT_EQ(std::get<ResolvedRegisterRef>(variant.sign_source.value).spelling,
            "%fd1");
  EXPECT_EQ(
      std::get<ResolvedRegisterRef>(variant.magnitude_source.value).spelling,
      "%fd2");
}

TEST(CopysignCompleteness, RejectsWrongTypesAndChecksAvailability) {
  for (const auto source :
       {"copysign.f16 %h0, %h1, %h2;", "copysign.f32 _, %f1, %f2;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(resolveCopysign(*parsed).has_value());
  }
  const auto parsed =
      test_helpers::parseInstruction("copysign.f32 %f0, 1.0, -2.0;");
  ASSERT_TRUE(parsed.has_value());
  const auto resolved = resolveCopysign(*parsed);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_TRUE(
      (*resolved)
          ->check(checker::Context{
              .target = {.ptx_version = PtxVersion{2, 0}, .sm_version = 20}})
          .has_value());
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
