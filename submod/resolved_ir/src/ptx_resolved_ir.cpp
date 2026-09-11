#include <limits>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <string_view>
#include "ptx_resolved_ir_layout.hpp"
#include "ptx_resolved_ir_private.hpp"

namespace ptx_frontend::resolved_ir::check_end {

bool SyntaxModifierDescriptor::check(std::string modifier_str) const {
  if (this->presence == PresenceRequirement::Absent) {
    return std::ranges::find(this->allowed_values, modifier_str) ==
           this->allowed_values.end();
  } else if (this->presence == PresenceRequirement::Optional) {
    return true;  // any value is allowed
  } else if (this->presence == PresenceRequirement::Required) {
    return std::ranges::find(this->allowed_values, modifier_str) !=
           this->allowed_values.end();
  }
  return false;  // should not reach here
}

int32_t SyntaxVariantDescriptor::get_required_modifier_num() const {
  int32_t size = 0;
  for (const auto& item : this->modifiers) {
    if (item.presence == PresenceRequirement::Absent or
        item.presence == PresenceRequirement::Required) {
      size += 1;
    }
  }
  return size;
}

};  // namespace ptx_frontend::resolved_ir::check_end

namespace ptx_frontend::resolved_ir {

using check_end::OperandPresence;
using check_end::OperandSyntaxShape;
using check_end::ResolvedFieldDescriptor;
using check_end::ResolvedInstructionDescriptor;
using check_end::ResolvedModifierBindingDescriptor;
using check_end::ResolvedModifierDefaultKind;
using check_end::ResolvedOperandBindingDescriptor;
using check_end::ResolvedOperandLayoutDescriptor;
using check_end::ResolvedValueKind;
using check_end::ResolvedVariantDescriptor;
using check_end::SyntaxInstructionDescriptor;
using check_end::SyntaxModifierDescriptor;
using check_end::SyntaxOperandLayoutDescriptor;
using check_end::SyntaxOperandSlotDescriptor;
using check_end::SyntaxVariantDescriptor;

std::expected<ResolvedInstructionFields, ResolveDiagnostic> resolve_fields(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& syntax_instruction,
    const check_end::ResolvedInstructionDescriptor& resolved_instruction,
    std::string_view variant_name, const ResolveContext* context) {
  const SyntaxVariantDescriptor& syntax_variant =
      detail::find_syntax_variant_descriptor(syntax_instruction, variant_name);
  const ResolvedVariantDescriptor& resolved_variant =
      detail::find_resolved_variant_descriptor(resolved_instruction, variant_name);
  const auto selected_layout = detail::select_operand_layout(syntax_variant, ast);
  if (!selected_layout)
    return std::unexpected(selected_layout.error());

  if (selected_layout->index >= resolved_variant.operand_layouts.size()) {
    throw ResolveException(fmt::format(
        "Resolved descriptor variant '{}' has no operand binding layout at "
        "syntax layout index {}.",
        variant_name, selected_layout->index));
  }
  if (selected_layout->index > std::numeric_limits<uint16_t>::max()) {
    throw ResolveException(fmt::format(
        "Operand layout index {} for variant '{}' exceeds the resolved IR "
        "layout-tag range.",
        selected_layout->index, variant_name));
  }
  const auto& resolved_layout =
      resolved_variant.operand_layouts[selected_layout->index];
  if (resolved_layout.bindings.size() !=
      selected_layout->descriptor.slots.size()) {
    throw ResolveException(fmt::format(
        "Descriptor variant '{}': syntax layout {} has {} slots but resolved "
        "binding layout has {} entries.",
        variant_name, selected_layout->index,
        selected_layout->descriptor.slots.size(),
        resolved_layout.bindings.size()));
  }

  const auto actual_modifiers = collect_actual_modifiers(ast, syntax_variant);
  if (!actual_modifiers)
    return std::unexpected(actual_modifiers.error());

  ResolvedInstructionFields fields{
      .variant_name = variant_name,
      .operand_layout = ResolvedOperandLayoutTag{static_cast<uint16_t>(
          selected_layout->index)},
  };
  if (ast.predicate) {
    auto predicate = detail::resolve_predicate_identifier(
        ast.predicate->name, ast.predicate->negated, ast.predicate->range,
        context);
    if (!predicate)
      return std::unexpected(predicate.error());
    fields.execution_predicate = std::move(*predicate);
  }
  for (const auto& binding : resolved_variant.modifier_bindings) {
    const auto& syntax_modifier =
        detail::find_syntax_modifier_descriptor(syntax_variant, binding.source_kind_id);
    const auto& field = detail::find_resolved_field_descriptor(resolved_variant,
                                                               binding.target_field_id);
    const auto actual =
        actual_modifiers->find(std::string(binding.source_kind_id));
    const bool present = actual != actual_modifiers->end();

    if (syntax_modifier.presence == check_end::PresenceRequirement::Absent) {
      throw ResolveException(fmt::format(
          "Resolved binding '{}' refers to absent syntax modifier '{}'.",
          binding.target_field_id, binding.source_kind_id));
    }

    if (!present) {
      if (syntax_modifier.presence ==
          check_end::PresenceRequirement::Optional) {
        fields.modifiers.emplace(
            field.field_id,
            detail::resolve_default_modifier_value(field, binding));
        continue;
      }
      return std::unexpected(ResolveDiagnostic{
          .range = ast.range,
          .message = fmt::format("Resolved variant requires '{}' modifier.",
                                 binding.source_kind_id),
      });
    }

    auto value = detail::resolve_modifier_value(field, *actual->second);
    if (!value)
      return std::unexpected(value.error());
    fields.modifiers.emplace(field.field_id, std::move(*value));
  }

  for (size_t index = 0; index < ast.operands.size(); ++index) {
    const auto& binding = resolved_layout.bindings[index];
    const auto& field = detail::find_resolved_operand_field_descriptor(
        resolved_layout, binding.target_field_id);
    auto value = detail::resolve_operand_value(field, binding, ast.operands[index],
                                               fields, context);
    if (!value)
      return std::unexpected(value.error());
    if (auto* address = std::get_if<WithLocs<ResolvedAddress>>(&*value)) {
      const auto state_space = actual_modifiers->find("state_space");
      if (state_space != actual_modifiers->end()) {
        address->value.parameter_qualifier =
            detail::parameter_address_qualifier_from_modifier(
                state_space->second->syntax.text);
      }
    }
    const auto [_, inserted] =
        fields.operands.emplace(std::string(field.field_id), std::move(*value));
    if (!inserted) {
      throw ResolveException(
          fmt::format("Operand layout for variant '{}' repeats field '{}'.",
                      syntax_variant.variant_name, field.field_id));
    }
  }

  return fields;
}

};  // namespace ptx_frontend::resolved_ir
