#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/addc.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/mad.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/madc.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sub.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/subc.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using checker::PtxVersion;

/** Narrow resolver and descriptor entry points for one carry family. */
struct CarryApi {
  /** Creates the exact final form without declaration binding. */
  std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> (*resolve)(
      const syntax_ast::AstInstruction&, const ResolveContext*);
  /** Supplies syntax variant selection metadata. */
  const check_end::SyntaxInstructionDescriptor& (*syntax)() noexcept;
  /** Supplies resolved variant effect metadata. */
  const check_end::ResolvedInstructionDescriptor& (*resolved)() noexcept;
};

/** Check each width, guard, implicit effect, and availability boundary. */
template <typename Narrow, typename Wide>
  requires(std::derived_from<Narrow, Instruction> &&
           std::derived_from<Wide, Instruction>)
void check_carry_form(std::string_view spelling, ConditionCodeEffect effect,
                      CarryApi api, bool multiply_add = false,
                      PtxVersion minimum_32 = {1, 2},
                      PtxVersion too_old_32 = {1, 1},
                      uint16_t minimum_sm_32 = 10) {
  EXPECT_EQ(Narrow::condition_code_effect, effect);
  EXPECT_EQ(Wide::condition_code_effect, effect);
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
      const auto resolved = api.resolve(*ast, nullptr);
      ASSERT_TRUE(resolved) << resolved.error().message;
      EXPECT_EQ((*resolved)->execution_predicate.has_value(), *guard != '\0');
      if ((*resolved)->execution_predicate)
        EXPECT_TRUE((*resolved)->execution_predicate->value.negated);
      if (wide)
        EXPECT_NE(dynamic_cast<Wide*>(resolved->get()), nullptr);
      else
        EXPECT_NE(dynamic_cast<Narrow*>(resolved->get()), nullptr);
      const auto selected = select_variant_name(*ast, api.syntax());
      ASSERT_TRUE(selected.has_value()) << selected.error().message;
      const auto& variants = api.resolved().variants;
      const auto variant = std::find_if(
          variants.begin(), variants.end(),
          [&](const auto& entry) { return entry.variant_name == *selected; });
      ASSERT_NE(variant, variants.end());
      EXPECT_EQ(variant->condition_code_effect, effect);
      const checker::Context minimum{
          .target = {.ptx_version = wide ? PtxVersion{4, 3} : minimum_32,
                     .sm_version =
                         static_cast<uint32_t>(wide ? 20 : minimum_sm_32)},
          .instruction_range = ast->range,
      };
      EXPECT_TRUE((*resolved)->check(minimum));
      auto too_old = minimum;
      too_old.target.ptx_version = wide ? PtxVersion{4, 2} : too_old_32;
      EXPECT_FALSE((*resolved)->check(too_old));
      if (wide || minimum_sm_32 > 10) {
        too_old = minimum;
        too_old.target.sm_version = wide ? 19 : minimum_sm_32 - 1;
        EXPECT_FALSE((*resolved)->check(too_old));
      }
    }
  }
}

/** Deliver all original implicit carry/borrow effects and target minima. */
TEST(CarryCompleteness, DeliversAllImplicitEffectsAndAvailability) {
  constexpr CarryApi add{resolveAdd, add_syntax_descriptor,
                         add_resolved_descriptor};
  constexpr CarryApi addc{resolveAddc, addc_syntax_descriptor,
                          addc_resolved_descriptor};
  constexpr CarryApi sub{resolveSub, sub_syntax_descriptor,
                         sub_resolved_descriptor};
  constexpr CarryApi subc{resolveSubc, subc_syntax_descriptor,
                          subc_resolved_descriptor};
  constexpr CarryApi mad{resolveMad, mad_syntax_descriptor,
                         mad_resolved_descriptor};
  constexpr CarryApi madc{resolveMadc, madc_syntax_descriptor,
                          madc_resolved_descriptor};
  check_carry_form<AddCc32, AddCc64>("add.cc", ConditionCodeEffect::CarryOut,
                                     add);
  check_carry_form<AddcPlain32, AddcPlain64>(
      "addc", ConditionCodeEffect::CarryIn, addc);
  check_carry_form<AddcCc32, AddcCc64>("addc.cc",
                                       ConditionCodeEffect::CarryInOut, addc);
  check_carry_form<SubCc32, SubCc64>("sub.cc", ConditionCodeEffect::BorrowOut,
                                     sub);
  check_carry_form<SubcPlain32, SubcPlain64>(
      "subc", ConditionCodeEffect::BorrowIn, subc);
  check_carry_form<SubcCc32, SubcCc64>("subc.cc",
                                       ConditionCodeEffect::BorrowInOut, subc);
  check_carry_form<MadHiCc32, MadHiCc64>("mad.hi.cc",
                                         ConditionCodeEffect::CarryOut, mad,
                                         true, {3, 0}, {2, 9}, 20);
  check_carry_form<MadLoCc32, MadLoCc64>("mad.lo.cc",
                                         ConditionCodeEffect::CarryOut, mad,
                                         true, {3, 0}, {2, 9}, 20);
  check_carry_form<MadcHiPlain32, MadcHiPlain64>(
      "madc.hi", ConditionCodeEffect::CarryIn, madc, true, {3, 0}, {2, 9}, 20);
  check_carry_form<MadcLoPlain32, MadcLoPlain64>(
      "madc.lo", ConditionCodeEffect::CarryIn, madc, true, {3, 0}, {2, 9}, 20);
  check_carry_form<MadcHiCc32, MadcHiCc64>("madc.hi.cc",
                                           ConditionCodeEffect::CarryInOut,
                                           madc, true, {3, 0}, {2, 9}, 20);
  check_carry_form<MadcLoCc32, MadcLoCc64>("madc.lo.cc",
                                           ConditionCodeEffect::CarryInOut,
                                           madc, true, {3, 0}, {2, 9}, 20);
}

/** Reject the original illegal type, modifier, and operand boundaries. */
void reject_invalid_carry_forms(std::string_view opcode, CarryApi api,
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
    EXPECT_FALSE(api.resolve(*ast, nullptr));
  }
}

/** Keep invalid boundary rejection for every carry opcode family. */
TEST(CarryCompleteness, RejectsInvalidBoundariesAcrossAllFamilies) {
  reject_invalid_carry_forms("add.cc", {resolveAdd});
  reject_invalid_carry_forms("addc", {resolveAddc});
  reject_invalid_carry_forms("addc.cc", {resolveAddc});
  reject_invalid_carry_forms("sub.cc", {resolveSub});
  reject_invalid_carry_forms("subc", {resolveSubc});
  reject_invalid_carry_forms("subc.cc", {resolveSubc});
  reject_invalid_carry_forms("mad.hi.cc", {resolveMad}, true);
  reject_invalid_carry_forms("madc.lo", {resolveMadc}, true);
  reject_invalid_carry_forms("madc.lo.cc", {resolveMadc}, true);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
