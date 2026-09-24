#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/ldu/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ldu/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ldu/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;

/** Require an LDU form to resolve and pass the target-aware checker. */
void expect_ldu(std::string_view source, checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<Ldu>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto checked = checker::check(
      *resolved,
      checker::Context{.target = target, .instruction_range = ast->range});
  ASSERT_TRUE(checked.has_value())
      << (checked.error().empty() ? "LDU rejected without a diagnostic"
                                  : checked.error().front().message);
}

/** Require a syntactically valid LDU form to fail resolution or checking. */
void expect_ldu_rejected(std::string_view source, checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<Ldu>(*ast);
  if (!resolved)
    return;
  EXPECT_FALSE(checker::check(
      *resolved,
      checker::Context{.target = target, .instruction_range = ast->range}));
}

/** All documented scalar types work with generic and explicit global syntax. */
TEST(LduCompleteness, ResolvesScalarTypesAndGlobalSpellings) {
  constexpr std::array<std::string_view, 15> types{
      "b8",  "b16", "b32", "b64", "b128", "u8",  "u16", "u32",
      "u64", "s8",  "s16", "s32", "s64",  "f32", "f64"};
  const checker::TargetInfo target{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto type : types) {
    for (const auto prefix : {"ldu.", "ldu.global."}) {
      const std::string source =
          std::string(prefix) + std::string(type) + " %r0, [%rd0];";
      SCOPED_TRACE(source);
      expect_ldu(source, target);
    }
  }
}

/** The two supported vector arities share the scalar type and address rules. */
TEST(LduCompleteness, ResolvesBothVectorAritiesAndGlobalSpellings) {
  const checker::TargetInfo target{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto source : {
           "ldu.v2.u8 {%r1, %r2}, [%rd0];",
           "ldu.global.v2.s64 {%r1, %r2}, [%rd0];",
           "ldu.v4.f32 {%r1, %r2, %r3, %r4}, [%rd0];",
           "ldu.global.v4.u32 {%r1, %r2, %r3, %r4}, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    expect_ldu(source, target);
  }
}

/** PTX version, target architecture, and syntax exclusions remain distinct. */
TEST(LduCompleteness, EnforcesAvailabilityAndTypeBoundaries) {
  expect_ldu_rejected("ldu.u32 %r0, [%rd0];",
                      {.ptx_version = {1, 9}, .sm_version = 90});
  expect_ldu_rejected("ldu.u32 %r0, [%rd0];",
                      {.ptx_version = {2, 0}, .sm_version = 19});
  expect_ldu("ldu.u32 %r0, [%rd0];", {.ptx_version = {2, 0}, .sm_version = 20});
  expect_ldu_rejected("ldu.global.v2.f64 {%r1, %r2}, [%rd0];",
                      {.ptx_version = {9, 3}, .sm_version = 12});
  expect_ldu("ldu.global.v2.f64 {%r1, %r2}, [%rd0];",
             {.ptx_version = {9, 3}, .sm_version = 13});
  expect_ldu_rejected("ldu.global.b128 %r0, [%rd0];",
                      {.ptx_version = {8, 2}, .sm_version = 90});
  expect_ldu("ldu.global.b128 %r0, [%rd0];",
             {.ptx_version = {8, 3}, .sm_version = 70});

  const checker::TargetInfo target{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto source : {
           "ldu.global.v8.u32 {%r1, %r2, %r3, %r4, %r5, %r6, %r7, %r8}, "
           "[%rd0];",
           "ldu.shared.u32 %r0, [%rd0];",
           "ldu.global.f16 %r0, [%rd0];",
           "ldu.global.ca.u32 %r0, [%rd0];",
           "ldu.global.v2.b128 {%r1, %r2}, [%rd0];",
           "ldu.global.v4.f64 {%r1, %r2, %r3, %r4}, [%rd0];",
           "ldu.v4.u64 {%r1, %r2, %r3, %r4}, [%rd0];",
           "ldu.global.v2.u32 {%r1, _}, [%rd0];",
           "ldu.v2.u32 {%r1, _}, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    expect_ldu_rejected(source, target);
  }
}

/** Owned instructions are checked again after their typed fields are changed. */
TEST(LduCompleteness, RevalidatesOwnedMutation) {
  const auto ast = parseInstruction("ldu.global.v2.u32 {%r1, %r2}, [%rd0];");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  auto resolved = resolve<Ldu>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& vector = std::get<Ldu::ExplicitV2>(resolved->variant);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = ast->range};
  ASSERT_TRUE(checker::check(*resolved, context));
  vector.vector.value = VectorArity::V8;
  EXPECT_FALSE(checker::check(*resolved, context));
  vector.vector.value = VectorArity::V2;
  vector.type.value = ScalarType::F16;
  EXPECT_FALSE(checker::check(*resolved, context));
  vector.type.value = ScalarType::U32;
  vector.address.value.unified = true;
  EXPECT_FALSE(checker::check(*resolved, context));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
