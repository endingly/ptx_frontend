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
