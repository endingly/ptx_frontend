#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using checker::PtxVersion;

/** Check typed CC metadata and target minima for one extended arithmetic form. */
template <PtxOperator T>
void check_carry_form(std::string_view spelling, ConditionCodeEffect effect,
                      bool multiply_add = false, PtxVersion minimum_32 = {1, 2},
                      PtxVersion too_old_32 = {1, 1},
                      uint16_t minimum_sm_32 = 10) {
  for (const auto type : {"u32", "s32", "u64", "s64"}) {
    const bool wide = std::string_view(type).ends_with("64");
    for (const auto guard : {"", "@!%p0 "}) {
      const std::string source =
          std::string(guard) + std::string(spelling) + "." + type +
          (wide ? (multiply_add ? " %rd0, %rd1, 7, 9;" : " %rd0, %rd1, 7;")
                : (multiply_add ? " %r0, %r1, 7, 9;" : " %r0, %r1, 7;"));
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
          .target = {.ptx_version = wide ? PtxVersion{4, 3} : minimum_32,
                     .sm_version =
                         static_cast<uint32_t>(wide ? 20 : minimum_sm_32)},
          .instruction_range = ast->range,
      };
      EXPECT_TRUE(checker::check(*resolved, minimum));
      auto too_old = minimum;
      too_old.target.ptx_version = wide ? PtxVersion{4, 2} : too_old_32;
      EXPECT_FALSE(checker::check(*resolved, too_old));
      if (wide || minimum_sm_32 > 10) {
        too_old = minimum;
        too_old.target.sm_version = wide ? 19 : minimum_sm_32 - 1;
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
  check_carry_form<Mad>("mad.hi.cc", ConditionCodeEffect::CarryOut, true,
                        {3, 0}, {2, 9}, 20);
  check_carry_form<Mad>("mad.lo.cc", ConditionCodeEffect::CarryOut, true,
                        {3, 0}, {2, 9}, 20);
  check_carry_form<Madc>("madc.hi", ConditionCodeEffect::CarryIn, true, {3, 0},
                         {2, 9}, 20);
  check_carry_form<Madc>("madc.lo", ConditionCodeEffect::CarryIn, true, {3, 0},
                         {2, 9}, 20);
  check_carry_form<Madc>("madc.hi.cc", ConditionCodeEffect::CarryInOut, true,
                         {3, 0}, {2, 9}, 20);
  check_carry_form<Madc>("madc.lo.cc", ConditionCodeEffect::CarryInOut, true,
                         {3, 0}, {2, 9}, 20);
}

/** Reject the same illegal type/modifier/layout boundaries for every CC family. */
template <PtxOperator T>
void reject_invalid_carry_forms(std::string_view opcode,
                                bool multiply_add = false) {
  for (const auto suffix : {".u16 %r0", ".f32 %f0", ".sat.u32 %r0",
                            ".rn.u32 %r0", ".u32 1", ".u32 %r0, {%r1, %r2}"}) {
    const bool vector_source = std::string_view(suffix).contains('{');
    const auto remaining_sources =
        vector_source ? (multiply_add ? ", %r2, %r3;" : ", %r2;")
                      : (multiply_add ? ", %r1, %r2, %r3;" : ", %r1, %r2;");
    const auto source = std::string(opcode) + suffix + remaining_sources;
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
  reject_invalid_carry_forms<Mad>("mad.hi.cc", true);
  reject_invalid_carry_forms<Madc>("madc.lo", true);
  reject_invalid_carry_forms<Madc>("madc.lo.cc", true);
}

TEST(CarryCompleteness, RejectsInvalidFormsAndRevalidatesType) {
  const auto duplicate =
      test_helpers::parseInstruction("addc.cc.cc.u32 %r0, %r1, %r2;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(duplicate);
  EXPECT_FALSE(resolve<Addc>(*duplicate));
  for (const auto source :
       {"mad.cc.u32 %r0, %r1, %r2, %r3;", "madc.u32 %r0, %r1, %r2, %r3;",
        "madc.cc.u32 %r0, %r1, %r2, %r3;"}) {
    SCOPED_TRACE(source);
    const auto omitted_mode = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(omitted_mode);
    EXPECT_FALSE(resolveInstruction(*omitted_mode).has_value());
  }
  for (const auto source : {"mad.hi.cc.u32 %rd0, %r1, %r2, %r3;",
                            "mad.hi.cc.u32 %r0, %r1, %r2, %rd3;",
                            "madc.lo.u32 %rd0, %r1, %r2, %r3;",
                            "madc.lo.u32 %r0, %r1, %r2, %rd3;"}) {
    SCOPED_TRACE(source);
    const auto mismatched_width = test_helpers::parseModule(
        std::string(".version 9.3\n.target sm_90\n.entry kernel() {\n") +
        "  .reg .u32 %r<4>;\n  .reg .u64 %rd<4>;\n  " + source + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(mismatched_width);
    EXPECT_FALSE(resolveModule(*mismatched_width).has_value());
  }
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

/** Retain immutable CC metadata after AST release and reject mutable type drift. */
TEST(CarryCompleteness, RetainsOwnedMultiplyAddCarryContract) {
  std::optional<ResolvedModule> owned_module;
  {
    const auto parsed_module = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .pred %p<1>;
  .reg .u32 %r<4>;
  @!%p0 madc.lo.cc.u32 %r0, %r1, %r2, %r3;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
    auto resolved = resolveModule(*parsed_module);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned_module.emplace(std::move(*resolved));
  }

  const auto& madc =
      std::get<Madc>(owned_module->functions.front().body.front());
  const auto* variant = std::get_if<Madc::LoCc32>(&madc.variant);
  ASSERT_NE(variant, nullptr);
  EXPECT_EQ(variant->condition_code_effect, ConditionCodeEffect::CarryInOut);
  EXPECT_EQ(Madc::get_resolved_descriptor()
                .variants[madc.variant.index()]
                .condition_code_effect,
            variant->condition_code_effect);
  static_assert(
      !std::is_assignable_v<decltype(Madc::LoCc32::condition_code_effect),
                            ConditionCodeEffect>);
  ASSERT_TRUE(validateModule(*owned_module,
                             ModuleValidationPolicy::RequireCompleteContext)
                  .has_value());

  auto& mutable_variant = std::get<Madc::LoCc32>(
      std::get<Madc>(owned_module->functions.front().body.front()).variant);
  mutable_variant.type.value = ScalarType::U64;
  EXPECT_FALSE(validateModule(*owned_module,
                              ModuleValidationPolicy::RequireCompleteContext)
                   .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
