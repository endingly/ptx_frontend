#include "ptx_ir/ptx_resolved_ir_checker.hpp"

#include <algorithm>

#include <fmt/format.h>

namespace ptx_frontend::resolved_ir::checker {
namespace {

std::string format_version(PtxVersion version) {
  return fmt::format("{}.{}", version.major, version.minor);
}

bool has_family(std::span<const std::string_view> families,
                std::string_view required_family) noexcept {
  return std::ranges::find(families, required_family) != families.end();
}

bool allows_shape(OperandShape allowed, OperandShape actual) noexcept {
  using Underlying = std::underlying_type_t<OperandShape>;
  return (static_cast<Underlying>(allowed) & static_cast<Underlying>(actual)) !=
         0;
}

const SourceRange& diagnostic_range(std::span<const SourceRange> locations,
                                    const Context& context) noexcept {
  return locations.empty() ? context.instruction_range : locations.front();
}

const FieldView* find_field(std::span<const FieldView> fields,
                            std::string_view field_id) noexcept {
  const auto it = std::ranges::find_if(
      fields, [field_id](const FieldView& field) {
        return field.field_id == field_id;
      });
  return it == fields.end() ? nullptr : &*it;
}

const OperandView* find_operand(std::span<const OperandView> operands,
                                std::string_view field_id) noexcept {
  const auto it = std::ranges::find_if(
      operands, [field_id](const OperandView& operand) {
        return operand.field_id == field_id;
      });
  return it == operands.end() ? nullptr : &*it;
}

bool matches_modifier_value(
    const ModifierValueAvailabilityDescriptor& descriptor,
    const ModifierValueView& actual) noexcept {
  if (descriptor.kind_id != actual.kind_id ||
      descriptor.value_kind != actual.value_kind) {
    return false;
  }
  switch (descriptor.value_kind) {
    case ModifierValueKind::Bool:
      return descriptor.bool_value == actual.bool_value;
    case ModifierValueKind::ScalarType:
      return descriptor.scalar_type == actual.scalar_type;
    case ModifierValueKind::RoundingMode:
      return descriptor.rounding_mode == actual.rounding_mode;
  }
  return false;
}

}  // namespace

bool is_available(const AvailabilityDescriptor& availability,
                  const TargetInfo& target) noexcept {
  return target.ptx_version >= availability.minimum_ptx_version &&
         target.sm_version >= availability.minimum_sm_version &&
         (availability.required_family.empty() ||
          has_family(target.families, availability.required_family));
}

const VariantDescriptor* find_variant_descriptor(
    const InstructionDescriptor& instruction,
    std::string_view variant_name) noexcept {
  const auto it = std::ranges::find_if(
      instruction.variants, [variant_name](const VariantDescriptor& variant) {
        return variant.variant_name == variant_name;
      });
  return it == instruction.variants.end() ? nullptr : &*it;
}

CheckResult check_availability(const VariantDescriptor& variant,
                               const Context& context) {
  CheckDiagnostics diagnostics;
  const auto& availability = variant.availability;
  const auto& target = context.target;

  if (target.ptx_version < availability.minimum_ptx_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = context.instruction_range,
        .message = fmt::format(
            "Instruction variant '{}' requires PTX ISA >= {}, but target PTX "
            "ISA is {}.",
            variant.variant_name,
            format_version(availability.minimum_ptx_version),
            format_version(target.ptx_version)),
    });
  }

  if (target.sm_version < availability.minimum_sm_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedSmVersion,
        .range = context.instruction_range,
        .message = fmt::format(
            "Instruction variant '{}' requires SM >= {}, but target SM is {}.",
            variant.variant_name, availability.minimum_sm_version,
            target.sm_version),
    });
  }

  if (!availability.required_family.empty() &&
      !has_family(target.families, availability.required_family)) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
        .range = context.instruction_range,
        .message =
            fmt::format("Instruction variant '{}' requires target family '{}'.",
                        variant.variant_name, availability.required_family),
    });
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_common(const InstructionDescriptor& instruction,
                         std::string_view variant_name,
                         const Context& context) {
  const VariantDescriptor* variant =
      find_variant_descriptor(instruction, variant_name);
  if (variant == nullptr) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::MissingVariantDescriptor,
        .range = context.instruction_range,
        .message = fmt::format("Checker descriptor for instruction '{}' has no "
                               "variant named '{}'.",
                               instruction.opcode_name, variant_name),
    }});
  }
  return check_availability(*variant, context);
}

CheckResult check_operands(std::span<const OperandDescriptor> descriptors,
                           std::span<const FieldView> fields,
                           std::span<const OperandView> operands,
                           const Context& context) {
  CheckDiagnostics diagnostics;

  for (const OperandDescriptor& descriptor : descriptors) {
    const OperandView* operand =
        find_operand(operands, descriptor.target_field_id);
    if (operand == nullptr) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::MissingOperand,
          .range = context.instruction_range,
          .message = fmt::format("Resolved operand '{}' is unavailable.",
                                 descriptor.target_field_id),
      });
      continue;
    }

    if (!allows_shape(descriptor.allowed_shapes, operand->actual_shape)) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedOperandShape,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format(
              "Resolved operand '{}' has a shape not accepted by this "
              "instruction layout.",
              descriptor.target_field_id),
      });
    }

    const auto& expression = descriptor.type_expression;
    if (expression.kind == OperandTypeExpressionKind::None)
      continue;

    ScalarType expected_type = ScalarType::Invalid;
    std::string_view expected_type_source = "fixed scalar type";
    if (expression.kind == OperandTypeExpressionKind::FixedScalar) {
      expected_type = expression.fixed_scalar_type;
    } else if (expression.kind == OperandTypeExpressionKind::ModifierField) {
      const FieldView* type_field =
          find_field(fields, expression.modifier_field_id);
      if (type_field == nullptr || !type_field->scalar_type) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::MissingTypeField,
            .range = diagnostic_range(operand->locations, context),
            .message = fmt::format(
                "Resolved operand '{}' requires scalar type field '{}'.",
                descriptor.target_field_id, expression.modifier_field_id),
        });
        continue;
      }
      expected_type = *type_field->scalar_type;
      expected_type_source = expression.modifier_field_id;
    } else {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::MissingTypeField,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format(
              "Resolved operand '{}' has an invalid type expression descriptor.",
              descriptor.target_field_id),
      });
      continue;
    }

    if (operand->immediate_type && *operand->immediate_type != expected_type) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::OperandTypeMismatch,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format(
              "Immediate operand '{}' has type '{}' but instruction type "
              "source '{}' is '{}'.",
              descriptor.target_field_id, to_string(*operand->immediate_type),
              expected_type_source, to_string(expected_type)),
      });
    }
  }

  for (const OperandView& operand : operands) {
    if (std::ranges::find_if(
            descriptors, [&operand](const OperandDescriptor& descriptor) {
              return descriptor.target_field_id == operand.field_id;
            }) != descriptors.end()) {
      continue;
    }
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnexpectedOperand,
        .range = diagnostic_range(operand.locations, context),
        .message = fmt::format("Resolved operand '{}' is not declared by this "
                               "instruction layout.",
                               operand.field_id),
    });
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_operand_layout_tag(std::string_view variant_name,
                                     uint16_t selected_layout,
                                     size_t layout_count,
                                     const Context& context) {
  if (selected_layout < layout_count)
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::InvalidOperandLayoutTag,
      .range = context.instruction_range,
      .message = fmt::format(
          "Resolved instruction variant '{}' selects operand layout {}, but "
          "only {} layout(s) are declared.",
          variant_name, selected_layout, layout_count),
  }});
}

CheckResult check_operand_layout_availability(
    const VariantDescriptor& variant, uint16_t selected_layout,
    const Context& context) {
  if (selected_layout >= variant.operand_layouts.size()) {
    return check_operand_layout_tag(variant.variant_name, selected_layout,
                                    variant.operand_layouts.size(), context);
  }

  const auto& layout = variant.operand_layouts[selected_layout];
  const auto& availability = layout.availability;
  const auto& target = context.target;
  CheckDiagnostics diagnostics;

  if (target.ptx_version < availability.minimum_ptx_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = context.instruction_range,
        .message = fmt::format(
            "Operand layout '{}' of instruction variant '{}' requires PTX ISA "
            ">= {}, but target PTX ISA is {}.",
            layout.layout_name, variant.variant_name,
            format_version(availability.minimum_ptx_version),
            format_version(target.ptx_version)),
    });
  }

  if (target.sm_version < availability.minimum_sm_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedSmVersion,
        .range = context.instruction_range,
        .message = fmt::format(
            "Operand layout '{}' of instruction variant '{}' requires SM >= "
            "{}, but target SM is {}.",
            layout.layout_name, variant.variant_name,
            availability.minimum_sm_version, target.sm_version),
    });
  }

  if (!availability.required_family.empty() &&
      !has_family(target.families, availability.required_family)) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
        .range = context.instruction_range,
        .message = fmt::format(
            "Operand layout '{}' of instruction variant '{}' requires target "
            "family '{}'.",
            layout.layout_name, variant.variant_name,
            availability.required_family),
    });
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_modifier_value_availability(
    std::span<const ModifierValueAvailabilityDescriptor> descriptors,
    std::span<const ModifierValueView> actual_values, const Context& context) {
  CheckDiagnostics diagnostics;
  for (const ModifierValueView& actual : actual_values) {
    if (!actual.is_present)
      continue;

    const auto it = std::ranges::find_if(
        descriptors, [&actual](const ModifierValueAvailabilityDescriptor& entry) {
          return matches_modifier_value(entry, actual);
        });
    if (it == descriptors.end())
      continue;

    const auto& availability = it->availability;
    const auto& target = context.target;
    const SourceRange& range = diagnostic_range(actual.locations, context);
    if (target.ptx_version < availability.minimum_ptx_version) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
          .range = range,
          .message = fmt::format(
              "Modifier '{}' requires PTX ISA >= {}, but target PTX ISA is {}.",
              actual.kind_id, format_version(availability.minimum_ptx_version),
              format_version(target.ptx_version)),
      });
    }
    if (target.sm_version < availability.minimum_sm_version) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedSmVersion,
          .range = range,
          .message = fmt::format(
              "Modifier '{}' requires SM >= {}, but target SM is {}.",
              actual.kind_id, availability.minimum_sm_version, target.sm_version),
      });
    }
    if (!availability.required_family.empty() &&
        !has_family(target.families, availability.required_family)) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
          .range = range,
          .message = fmt::format("Modifier '{}' requires target family '{}'.",
                                 actual.kind_id,
                                 availability.required_family),
      });
    }
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

}  // namespace ptx_frontend::resolved_ir::checker
