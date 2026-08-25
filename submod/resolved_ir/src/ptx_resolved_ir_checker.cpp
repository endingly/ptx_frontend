#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>

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
  const auto it =
      std::ranges::find_if(fields, [field_id](const FieldView& field) {
        return field.field_id == field_id;
      });
  return it == fields.end() ? nullptr : &*it;
}

const OperandView* find_operand(std::span<const OperandView> operands,
                                std::string_view field_id) noexcept {
  const auto it =
      std::ranges::find_if(operands, [field_id](const OperandView& operand) {
        return operand.field_id == field_id;
      });
  return it == operands.end() ? nullptr : &*it;
}

void append_value_availability_diagnostics(const OperandView& operand,
                                           const Context& context,
                                           CheckDiagnostics& diagnostics) {
  if (!operand.value_availability)
    return;

  const AvailabilityDescriptor& availability = *operand.value_availability;
  const SourceRange& range = diagnostic_range(operand.locations, context);
  if (context.target.ptx_version < availability.minimum_ptx_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = range,
        .message = fmt::format(
            "Operand value '{}' requires PTX ISA >= {}, but target PTX ISA is "
            "{}.",
            operand.value_name,
            format_version(availability.minimum_ptx_version),
            format_version(context.target.ptx_version)),
    });
  }
  if (context.target.sm_version < availability.minimum_sm_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedSmVersion,
        .range = range,
        .message = fmt::format(
            "Operand value '{}' requires SM >= {}, but target SM is {}.",
            operand.value_name, availability.minimum_sm_version,
            context.target.sm_version),
    });
  }
  if (!availability.required_family.empty() &&
      !has_family(context.target.families, availability.required_family)) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
        .range = range,
        .message =
            fmt::format("Operand value '{}' requires target family "
                        "'{}'.",
                        operand.value_name, availability.required_family),
    });
  }
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
    case ModifierValueKind::CacheOperator:
      return descriptor.cache_operator == actual.cache_operator;
    case ModifierValueKind::VectorArity:
      return descriptor.vector_arity == actual.vector_arity;
    case ModifierValueKind::MemoryStateSpace:
      return descriptor.memory_state_space == actual.memory_state_space;
    case ModifierValueKind::MemoryConsistency:
      return descriptor.memory_consistency == actual.memory_consistency;
    case ModifierValueKind::MemoryScope:
      return descriptor.memory_scope == actual.memory_scope;
  }
  return false;
}

std::string_view state_space_name(MemoryStateSpace state_space) noexcept {
  switch (state_space) {
    case MemoryStateSpace::Invalid:
      return "invalid";
    case MemoryStateSpace::Generic:
      return "generic";
    case MemoryStateSpace::Global:
      return "global";
    case MemoryStateSpace::Shared:
      return "shared";
    case MemoryStateSpace::Local:
      return "local";
    case MemoryStateSpace::Parameter:
      return "param";
    case MemoryStateSpace::Constant:
      return "const";
  }
  return "invalid";
}

void append_address_constraint_availability_diagnostics(
    const AvailabilityDescriptor& availability, std::string_view constraint,
    const OperandView& operand, const Context& context,
    CheckDiagnostics& diagnostics) {
  const SourceRange& range = diagnostic_range(operand.locations, context);
  if (context.target.ptx_version < availability.minimum_ptx_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = range,
        .message = fmt::format(
            "{} requires PTX ISA >= {}, but target PTX ISA is {}.", constraint,
            format_version(availability.minimum_ptx_version),
            format_version(context.target.ptx_version)),
    });
  }
  if (context.target.sm_version < availability.minimum_sm_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedSmVersion,
        .range = range,
        .message = fmt::format(
            "{} requires SM >= {}, but target SM is {}.", constraint,
            availability.minimum_sm_version, context.target.sm_version),
    });
  }
  if (!availability.required_family.empty() &&
      !has_family(context.target.families, availability.required_family)) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
        .range = range,
        .message = fmt::format("{} requires target family '{}'.", constraint,
                               availability.required_family),
    });
  }
}

std::string_view parameter_direction_name(ParameterDirection direction) noexcept {
  switch (direction) {
    case ParameterDirection::None:
      return "unknown";
    case ParameterDirection::Input:
      return "input";
    case ParameterDirection::Return:
      return "return";
  }
  return "unknown";
}

void append_parameter_address_diagnostics(
    const OperandDescriptor& descriptor, const OperandView& operand,
    std::optional<MemoryStateSpace> selected_state_space,
    const Context& context, CheckDiagnostics& diagnostics) {
  const auto& constraint = descriptor.parameter_constraint;
  // A known non-parameter base belongs to the exact state-space diagnostic;
  // do not infer parameter identity from the selected modifier alone.
  if (constraint.direction == ParameterDirection::None ||
      selected_state_space != MemoryStateSpace::Parameter ||
      (operand.address_state_space &&
       *operand.address_state_space != MemoryStateSpace::Parameter)) {
    return;
  }

  if (operand.parameter_direction != ParameterDirection::None &&
      operand.parameter_direction != constraint.direction) {
    // Direction is the more specific error and suppresses contextual target
    // diagnostics for the same address.
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::ParameterDirectionMismatch,
        .range = diagnostic_range(operand.locations, context),
        .message = fmt::format(
            "Address operand '{}' refers to a {} parameter but the "
            "instruction requires a {} parameter address.",
            descriptor.target_field_id,
            parameter_direction_name(operand.parameter_direction),
            parameter_direction_name(constraint.direction)),
    });
    return;
  }

  if (constraint.direction == ParameterDirection::Return ||
      operand.enclosing_function_kind == EnclosingFunctionKind::Device) {
    append_address_constraint_availability_diagnostics(
        constraint.function_availability, "Parameter address", operand,
        context, diagnostics);
  }
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

CheckResult check_operands(
    std::span<const OperandDescriptor> descriptors,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    std::span<const OperandTypeCompatibilityDescriptor> type_compatibilities,
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

    std::optional<MemoryStateSpace> selected_state_space;
    if (!descriptor.state_space_modifier_field_id.empty()) {
      const FieldView* state_space_field =
          find_field(fields, descriptor.state_space_modifier_field_id);
      if (state_space_field == nullptr ||
          !state_space_field->memory_state_space) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::MissingStateSpaceField,
            .range = diagnostic_range(operand->locations, context),
            .message = fmt::format(
                "Resolved address operand '{}' requires state-space field "
                "'{}'.",
                descriptor.target_field_id,
                descriptor.state_space_modifier_field_id),
        });
      } else {
        selected_state_space = state_space_field->memory_state_space;
        if (operand->address_state_space &&
            *operand->address_state_space != *selected_state_space) {
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::AddressStateSpaceMismatch,
              .range = diagnostic_range(operand->locations, context),
              .message = fmt::format(
                  "Address operand '{}' has effective .{} state space but the "
                  "instruction requires .{}.",
                  descriptor.target_field_id,
                  state_space_name(*operand->address_state_space),
                  state_space_name(*selected_state_space)),
          });
        }
      }
    }
    append_parameter_address_diagnostics(descriptor, *operand,
                                         selected_state_space, context,
                                         diagnostics);

    if (!descriptor.allowed_address_state_spaces.empty() &&
        operand->address_state_space) {
      const auto allowed = std::ranges::find_if(
          descriptor.allowed_address_state_spaces,
          [&](const AddressStateSpaceDescriptor& entry) {
            return entry.state_space == *operand->address_state_space;
          });
      if (allowed == descriptor.allowed_address_state_spaces.end()) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::AddressStateSpaceMismatch,
            .range = diagnostic_range(operand->locations, context),
            .message = fmt::format(
                "Address operand '{}' has effective .{} state space, which "
                "this instruction operand does not accept.",
                descriptor.target_field_id,
                state_space_name(*operand->address_state_space)),
        });
      } else {
        const std::string constraint_name = fmt::format(
            "Address state space '.{}'", state_space_name(allowed->state_space));
        append_address_constraint_availability_diagnostics(
            allowed->availability, constraint_name, *operand, context,
            diagnostics);
      }
    }
    // A register, immediate, or unresolved standalone address has no
    // trustworthy effective state space. Do not infer one from spelling.

    const auto& expression = descriptor.type_expression;
    if (expression.kind == OperandTypeExpressionKind::None) {
      append_value_availability_diagnostics(*operand, context, diagnostics);
      continue;
    }

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
        append_value_availability_diagnostics(*operand, context, diagnostics);
        continue;
      }
      expected_type = *type_field->scalar_type;
      expected_type_source = expression.modifier_field_id;
    } else {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::MissingTypeField,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format("Resolved operand '{}' has an invalid type "
                                 "expression descriptor.",
                                 descriptor.target_field_id),
      });
      append_value_availability_diagnostics(*operand, context, diagnostics);
      continue;
    }

    std::optional<OperandView> contextual_operand;
    if (operand->special_register_id) {
      const auto compatibility = std::ranges::find_if(
          type_compatibilities,
          [&](const OperandTypeCompatibilityDescriptor& candidate) {
            return candidate.target_field_id == descriptor.target_field_id &&
                   candidate.special_register_kind ==
                       operand->special_register_id->kind &&
                   candidate.instruction_width ==
                       scalar_size_of(expected_type) * 8;
          });
      if (compatibility != type_compatibilities.end()) {
        // Historical instruction forms change only this check's view; the
        // resolved operand retains target-independent intrinsic identity.
        contextual_operand = *operand;
        contextual_operand->special_register_type =
            compatibility->effective_type;
        contextual_operand->value_availability = compatibility->availability;
        operand = &*contextual_operand;
      }
    }
    append_value_availability_diagnostics(*operand, context, diagnostics);

    if (operand->actual_shape == OperandShape::Vector) {
      const SourceRange& range = diagnostic_range(operand->locations, context);
      std::optional<uint8_t> required_vector_arity;
      if (!descriptor.vector_arity_modifier_field_id.empty()) {
        const FieldView* arity_field =
            find_field(fields, descriptor.vector_arity_modifier_field_id);
        if (arity_field == nullptr || !arity_field->vector_arity) {
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::MissingVectorArityField,
              .range = range,
              .message = fmt::format(
                  "Resolved vector operand '{}' requires vector arity field "
                  "'{}'.",
                  descriptor.target_field_id,
                  descriptor.vector_arity_modifier_field_id),
          });
          continue;
        }
        required_vector_arity = vector_arity_count(*arity_field->vector_arity);
      }
      if (descriptor.vector_type_policy == VectorTypePolicy::Aggregate &&
          scalar_kind(expected_type) != base::ScalarKind::Bit) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::OperandTypeMismatch,
            .range = range,
            .message = fmt::format(
                "Vector operand '{}' requires a bit-size instruction type.",
                descriptor.target_field_id),
        });
        continue;
      }
      if (operand->vector_arity == 0 || operand->vector_arity > 8 ||
          (required_vector_arity &&
           operand->vector_arity != *required_vector_arity) ||
          (!required_vector_arity &&
           std::ranges::find(descriptor.allowed_vector_arities,
                             operand->vector_arity) ==
               descriptor.allowed_vector_arities.end())) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::InvalidVectorOperand,
            .range = range,
            .message = fmt::format(
                "Vector operand '{}' has an unsupported element count.",
                descriptor.target_field_id),
        });
        continue;
      }
      const size_t vector_payload_bits =
          (descriptor.vector_type_policy == VectorTypePolicy::Aggregate
               ? static_cast<size_t>(scalar_size_of(expected_type))
               : static_cast<size_t>(operand->vector_arity) *
                     scalar_size_of(expected_type)) *
          8u;
      if (vector_payload_bits > kMaxRegisterVectorPayloadBits) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::OperandTypeMismatch,
            .range = range,
            .message = fmt::format(
                "Vector operand '{}' payload width ({} bits) exceeds the "
                "supported {} bit limit.",
                descriptor.target_field_id,
                vector_payload_bits,
                kMaxRegisterVectorPayloadBits),
        });
        continue;
      }
      if ((!descriptor.allow_vector_sink && operand->vector_sink_count != 0) ||
          operand->vector_sink_count >= operand->vector_arity) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::InvalidVectorOperand,
            .range = range,
            .message = fmt::format(
                "Vector operand '{}' uses the '_' sink in an invalid "
                "position.",
                descriptor.target_field_id),
        });
        continue;
      }

      if (descriptor.vector_type_policy == VectorTypePolicy::Aggregate) {
        const uint8_t instruction_bytes = scalar_size_of(expected_type);
        if (instruction_bytes % operand->vector_arity != 0 ||
            instruction_bytes / operand->vector_arity == 0) {
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::OperandTypeMismatch,
              .range = range,
              .message = fmt::format(
                  "Vector operand '{}' would require sub-byte elements.",
                  descriptor.target_field_id),
          });
          continue;
        }
        const uint8_t element_bytes = instruction_bytes / operand->vector_arity;
        const auto mismatched = std::ranges::find_if(
            operand->vector_element_types.begin(),
            operand->vector_element_types.begin() + operand->vector_arity,
            [element_bytes](ScalarType element_type) {
              return element_type != ScalarType::Invalid &&
                     scalar_size_of(element_type) != element_bytes;
            });
        if (mismatched !=
            operand->vector_element_types.begin() + operand->vector_arity) {
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::OperandTypeMismatch,
              .range = range,
              .message = fmt::format(
                  "Vector operand '{}' has an element type '{}' but the "
                  "instruction requires {}-bit elements.",
                  descriptor.target_field_id, to_string(*mismatched),
                  element_bytes * 8),
          });
        }
      } else {
        const auto mismatched = std::ranges::find_if(
            operand->vector_element_types.begin(),
            operand->vector_element_types.begin() + operand->vector_arity,
            [&](ScalarType element_type) {
              return element_type != ScalarType::Invalid &&
                     !scalar_types_compatible(
                         element_type, expected_type,
                         descriptor.register_width_policy);
            });
        if (mismatched !=
            operand->vector_element_types.begin() + operand->vector_arity) {
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::OperandTypeMismatch,
              .range = range,
              .message = fmt::format(
                  "Vector operand '{}' has an element type '{}' incompatible "
                  "with instruction type '{}'.",
                  descriptor.target_field_id, to_string(*mismatched),
                  to_string(expected_type)),
          });
        }
      }
      continue;
    }

    if (operand->immediate_type &&
        !scalar_types_compatible(*operand->immediate_type, expected_type)) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::OperandTypeMismatch,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format(
              "Immediate operand '{}' has type '{}' but instruction type "
              "source '{}' is '{}'.",
              descriptor.target_field_id, to_string(*operand->immediate_type),
              expected_type_source, to_string(expected_type)),
      });
    } else if (operand->register_type &&
               !scalar_types_compatible(*operand->register_type,
                                        expected_type,
                                        descriptor.register_width_policy)) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::OperandTypeMismatch,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format(
              "Register operand '{}' has declared type '{}' but instruction "
              "type source '{}' is '{}'.",
              descriptor.target_field_id, to_string(*operand->register_type),
              expected_type_source, to_string(expected_type)),
      });
    } else if (operand->special_register_type &&
               !scalar_types_compatible(*operand->special_register_type,
                                        expected_type)) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::OperandTypeMismatch,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format(
              "Special-register operand '{}' has declared type '{}' but "
              "instruction type source '{}' is '{}'.",
              descriptor.target_field_id,
              to_string(*operand->special_register_type), expected_type_source,
              to_string(expected_type)),
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

CheckResult check_operand_layout_availability(const VariantDescriptor& variant,
                                              uint16_t selected_layout,
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
        descriptors,
        [&actual](const ModifierValueAvailabilityDescriptor& entry) {
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
              actual.kind_id, availability.minimum_sm_version,
              target.sm_version),
      });
    }
    if (!availability.required_family.empty() &&
        !has_family(target.families, availability.required_family)) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
          .range = range,
          .message = fmt::format("Modifier '{}' requires target family '{}'.",
                                 actual.kind_id, availability.required_family),
      });
    }
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_memory_consistency(
    const VariantDescriptor::MemoryConsistencyDescriptor& descriptor,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    const Context& context) {
  if (descriptor.semantics_field_id.empty())
    return {};

  const FieldView* semantics_field =
      find_field(fields, descriptor.semantics_field_id);
  const FieldView* scope_field = find_field(fields, descriptor.scope_field_id);
  const FieldView* mmio_field = descriptor.mmio_field_id.empty()
                                    ? nullptr
                                    : find_field(fields, descriptor.mmio_field_id);
  const FieldView* cache_field = find_field(fields, descriptor.cache_field_id);
  const OperandView* address = find_operand(operands, descriptor.address_field_id);
  if (semantics_field == nullptr || scope_field == nullptr ||
      cache_field == nullptr || address == nullptr ||
      !semantics_field->memory_consistency || !scope_field->memory_scope ||
      !cache_field->cache_operator ||
      (mmio_field != nullptr && !mmio_field->bool_value)) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = "Generated memory-consistency descriptor has missing fields.",
    }});
  }

  const MemoryConsistency semantics = *semantics_field->memory_consistency;
  const MemoryScope scope = *scope_field->memory_scope;
  const bool mmio = mmio_field != nullptr && *mmio_field->bool_value;
  const bool cached = *cache_field->cache_operator != CacheOperator::Unspecified;
  CheckDiagnostics diagnostics;
  const auto violation = [&](const FieldView& field, std::string_view message) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::MemoryConsistencyViolation,
        .range = diagnostic_range(field.locations, context),
        .message = std::string(message),
    });
  };

  const bool scoped = semantics == MemoryConsistency::Relaxed ||
                      semantics == MemoryConsistency::Acquire ||
                      semantics == MemoryConsistency::Release;
  if (scoped != (scope != MemoryScope::None)) {
    violation(scoped ? *semantics_field : *scope_field,
              scoped ? "Memory semantics requires an explicit scope."
                     : "Memory scope is only valid with relaxed, acquire, or release semantics.");
  }
  if (cached && (semantics == MemoryConsistency::Volatile || scoped || mmio)) {
    violation(*cache_field,
              "Cache operator is not valid with volatile, ordered, or mmio memory semantics.");
  }

  std::optional<MemoryStateSpace> state_space = address->address_state_space;
  if (!descriptor.state_space_field_id.empty()) {
    const FieldView* field = find_field(fields, descriptor.state_space_field_id);
    if (field != nullptr && field->memory_state_space)
      state_space = *field->memory_state_space;
  }
  const bool known_global_or_shared =
      state_space == MemoryStateSpace::Global || state_space == MemoryStateSpace::Shared;
  const bool volatile_local = semantics == MemoryConsistency::Volatile &&
                              state_space == MemoryStateSpace::Local;
  const bool strong = scoped || semantics == MemoryConsistency::Volatile;
  if (strong && state_space && !known_global_or_shared && !volatile_local) {
    violation(*semantics_field,
              "Strong memory semantics require a global or shared address space.");
  }
  if (volatile_local && context.target.ptx_version < PtxVersion{9, 1}) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = diagnostic_range(semantics_field->locations, context),
        .message = fmt::format("volatile.local requires PTX ISA >= 9.1, but target PTX ISA is {}.",
                               format_version(context.target.ptx_version)),
    });
  }
  if (mmio) {
    if (semantics != MemoryConsistency::Relaxed || scope != MemoryScope::Sys) {
      violation(*mmio_field, "mmio requires .relaxed.sys semantics.");
    }
    if (state_space && *state_space != MemoryStateSpace::Global) {
      violation(*mmio_field,
                "mmio requires a global address space when the address space is known.");
    }
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_memory_vector(
    const VariantDescriptor::MemoryVectorDescriptor& descriptor,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    const Context& context) {
  if (descriptor.vector_field_id.empty())
    return {};

  const FieldView* type = find_field(fields, descriptor.type_field_id);
  const OperandView* vector = find_operand(operands, descriptor.vector_field_id);
  const OperandView* address = find_operand(operands, descriptor.address_field_id);
  if (type == nullptr || vector == nullptr || address == nullptr ||
      !type->scalar_type || vector->actual_shape != OperandShape::Vector) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = "Generated memory-vector descriptor has missing fields.",
    }});
  }

  const size_t payload_bits =
      static_cast<size_t>(vector->vector_arity) * scalar_size_of(*type->scalar_type) * 8u;
  const bool modern_candidate = vector->vector_arity > 4 ||
                                payload_bits > 128 ||
                                vector->vector_sink_count != 0;
  if (!modern_candidate)
    return {};

  CheckDiagnostics diagnostics;
  const SourceRange& vector_range = diagnostic_range(vector->locations, context);
  if (payload_bits != 256) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = vector_range,
        .message = "Modern memory vectors require an exact 256-bit payload.",
    });
  }

  std::optional<MemoryStateSpace> state_space = address->address_state_space;
  const FieldView* state_space_field = nullptr;
  if (!descriptor.state_space_field_id.empty()) {
    state_space_field = find_field(fields, descriptor.state_space_field_id);
    if (state_space_field == nullptr || !state_space_field->memory_state_space) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = context.instruction_range,
          .message = "Generated memory-vector descriptor has an invalid state-space field.",
      });
    } else {
      state_space = *state_space_field->memory_state_space;
    }
  }
  if (state_space && *state_space != MemoryStateSpace::Global) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = state_space_field != nullptr
                     ? diagnostic_range(state_space_field->locations, context)
                     : diagnostic_range(address->locations, context),
        .message = "Modern memory vectors require a global address space when known.",
    });
  }

  const auto& availability = descriptor.availability;
  if (context.target.ptx_version < availability.minimum_ptx_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = vector_range,
        .message = fmt::format(
            "Modern memory vectors require PTX ISA >= {}, but target PTX ISA is {}.",
            format_version(availability.minimum_ptx_version),
            format_version(context.target.ptx_version)),
    });
  }
  if (context.target.sm_version < availability.minimum_sm_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedSmVersion,
        .range = vector_range,
        .message = fmt::format(
            "Modern memory vectors require SM >= {}, but target SM is {}.",
            availability.minimum_sm_version, context.target.sm_version),
    });
  }
  if (!availability.required_family.empty() &&
      !has_family(context.target.families, availability.required_family)) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
        .range = vector_range,
        .message = fmt::format("Modern memory vectors require target family '{}'.",
                               availability.required_family),
    });
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

}  // namespace ptx_frontend::resolved_ir::checker
