#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/testp/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/testp/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/testp/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Return the property selected by either floating-point `testp` variant. */
TestProperty test_property_value(const Testp& instruction) {
  return std::visit([](const auto& variant) { return variant.property.value; },
                    instruction.variant);
}

/** Resolve a standalone `testp` form after releasing its parsed AST. */
std::optional<Testp> resolve_owned_testp(std::string_view source) {
  const auto parsed = test_helpers::parseInstruction(source);
  EXPECT_TRUE(parsed.has_value());
  if (!parsed)
    return std::nullopt;
  const auto resolved = resolve<Testp>(*parsed);
  EXPECT_TRUE(resolved.has_value());
  if (!resolved)
    return std::nullopt;
  return *resolved;
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
    EXPECT_FALSE(resolve<Testp>(*parsed).has_value());
  }
}

/** Accept floating source literals while rejecting the integer-literal form. */
TEST(TestpCompleteness, AcceptsFloatingSourceLiterals) {
  for (const auto source :
       {"testp.finite.f32 %p0, 1.0;", "testp.normal.f64 %p0, -2.0;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Testp>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_TRUE(
        checker::check(*resolved,
                       checker::Context{
                           .target = {.ptx_version = {2, 0}, .sm_version = 20}})
            .has_value());
  }
}

TEST(TestpCompleteness, ChecksIndependentPtxAndSmBoundariesAndCorruption) {
  auto resolved = resolve_owned_testp("testp.normal.f64 %p0, %fd1;");
  ASSERT_TRUE(resolved);
  const checker::Context current{
      .target = {.ptx_version = {2, 0}, .sm_version = 20}};
  EXPECT_TRUE(checker::check(*resolved, current).has_value());
  const auto old_ptx = checker::check(
      *resolved,
      checker::Context{.target = {.ptx_version = {1, 5}, .sm_version = 20}});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = checker::check(
      *resolved,
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 13}});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  auto& property = std::get<Testp::F64>(resolved->variant).property.value;
  property = TestProperty::Invalid;
  EXPECT_FALSE(checker::check(*resolved, current).has_value());
  property = static_cast<TestProperty>(255);
  EXPECT_FALSE(checker::check(*resolved, current).has_value());
}


}  // namespace
}  // namespace ptx_frontend::resolved_ir
