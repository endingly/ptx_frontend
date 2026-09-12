#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using checker::PtxVersion;

/** Check typed CC metadata and target minima for one extended arithmetic form. */
template <PtxOperator T>
void check_carry_form(std::string_view spelling, ConditionCodeEffect effect) {
  for (const auto type : {"u32", "s32", "u64", "s64"}) {
    const bool wide = std::string_view(type).ends_with("64");
    for (const auto guard : {"", "@!%p0 "}) {
      const std::string source = std::string(guard) + std::string(spelling) +
                                 "." + type +
                                 (wide ? " %rd0, %rd1, 7;" : " %r0, %r1, 7;");
      SCOPED_TRACE(source);
      const auto ast = test_helpers::parseInstruction(source);
      ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
      const auto resolved = resolve<T>(*ast);
      ASSERT_TRUE(resolved) << resolved.error().message;
      EXPECT_EQ(resolved->execution_predicate.has_value(), *guard != '\0');
      if (resolved->execution_predicate)
        EXPECT_TRUE(resolved->execution_predicate->value.negated);
      EXPECT_EQ(
          std::visit(
              [](const auto& variant) { return variant.condition_code_effect; },
              resolved->variant),
          effect);
      EXPECT_EQ(T::get_resolved_descriptor()
                    .variants[resolved->variant.index()]
                    .condition_code_effect,
                effect);
      const checker::Context minimum{
          .target = {.ptx_version = wide ? PtxVersion{4, 3} : PtxVersion{1, 2},
                     .sm_version = static_cast<uint16_t>(wide ? 20 : 10)},
          .instruction_range = ast->range,
      };
      EXPECT_TRUE(checker::check(*resolved, minimum));
      auto too_old = minimum;
      too_old.target.ptx_version = wide ? PtxVersion{4, 2} : PtxVersion{1, 1};
      EXPECT_FALSE(checker::check(*resolved, too_old));
      if (wide) {
        too_old = minimum;
        too_old.target.sm_version = 19;
        EXPECT_FALSE(checker::check(*resolved, too_old));
      }
    }
  }
}

TEST(CarryCompleteness, DeliversAllImplicitEffectsAndAvailability) {
  check_carry_form<Add>("add.cc", ConditionCodeEffect::CarryOut);
  check_carry_form<Addc>("addc", ConditionCodeEffect::CarryIn);
  check_carry_form<Addc>("addc.cc", ConditionCodeEffect::CarryInOut);
  check_carry_form<Sub>("sub.cc", ConditionCodeEffect::BorrowOut);
  check_carry_form<Subc>("subc", ConditionCodeEffect::BorrowIn);
  check_carry_form<Subc>("subc.cc", ConditionCodeEffect::BorrowInOut);
}

/** Reject the same illegal type/modifier/layout boundaries for every CC family. */
template <PtxOperator T>
void reject_invalid_carry_forms(std::string_view opcode) {
  for (const auto suffix : {".u16 %r0, %r1, %r2;", ".f32 %f0, %f1, %f2;",
                            ".sat.u32 %r0, %r1, %r2;", ".rn.u32 %r0, %r1, %r2;",
                            ".u32 1, %r1, %r2;", ".u32 %r0, {%r1, %r2}, 1;"}) {
    const auto source = std::string(opcode) + suffix;
    SCOPED_TRACE(source);
    const auto ast = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolve<T>(*ast));
  }
}

TEST(CarryCompleteness, RejectsInvalidBoundariesAcrossAllFamilies) {
  reject_invalid_carry_forms<Add>("add.cc");
  reject_invalid_carry_forms<Addc>("addc");
  reject_invalid_carry_forms<Addc>("addc.cc");
  reject_invalid_carry_forms<Sub>("sub.cc");
  reject_invalid_carry_forms<Subc>("subc");
  reject_invalid_carry_forms<Subc>("subc.cc");
}

TEST(CarryCompleteness, RejectsInvalidFormsAndRevalidatesType) {
  const auto duplicate =
      test_helpers::parseInstruction("addc.cc.cc.u32 %r0, %r1, %r2;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(duplicate);
  EXPECT_FALSE(resolve<Addc>(*duplicate));
  const auto ast = test_helpers::parseInstruction("addc.u32 %r0, %r1, 1;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  auto resolved = resolve<Addc>(*ast);
  ASSERT_TRUE(resolved);
  std::get<Addc::Plain32>(resolved->variant).type.value = ScalarType::U64;
  EXPECT_FALSE(checker::check(
      *resolved,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90}}));
  const auto ordinary = test_helpers::parseInstruction("add.u32 %r0, %r1, 1;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ordinary);
  const auto add = resolve<Add>(*ordinary);
  ASSERT_TRUE(add);
  EXPECT_EQ(
      std::visit(
          [](const auto& variant) { return variant.condition_code_effect; },
          add->variant),
      ConditionCodeEffect::None);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
