#include <gtest/gtest.h>

#include "../src/ptx_resolved_ir_private.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

TEST(ModifierDomainMapping, RejectsUnsupportedComparisonDefault) {
  const check_end::ResolvedFieldDescriptor field{
      .field_id = "comparison",
      .value_kind = check_end::ResolvedValueKind::ComparisonOperator,
  };
  const check_end::ResolvedModifierBindingDescriptor binding{
      .source_kind_id = "comparison",
      .target_field_id = "comparison",
      .default_value = {.kind = check_end::ResolvedModifierDefaultKind::None},
  };

  try {
    static_cast<void>(detail::resolve_default_modifier_value(field, binding));
    FAIL() << "comparison modifiers must not accept a generated default";
  } catch (const ResolveException& exception) {
    EXPECT_STREQ(
        exception.what(),
        "Optional modifier 'comparison' cannot use a "
        "comparison-operator default for resolved field 'comparison'.");
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
