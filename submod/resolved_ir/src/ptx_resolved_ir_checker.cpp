#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>

#include "ptx_resolved_ir_packed_literal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include <fmt/format.h>

namespace ptx_frontend::resolved_ir::checker {
namespace {

/** Compare source positions without relying on unavailable ordering operators. */
bool fabric_pos_le(SourcePos first, SourcePos second) noexcept {
  return first.line < second.line ||
         (first.line == second.line && first.column <= second.column);
}

/** Strict source ordering, so zero-length mutable ranges are rejected. */
bool fabric_pos_lt(SourcePos first, SourcePos second) noexcept {
  return fabric_pos_le(first, second) && first != second;
}

/** Require a nonempty source range enclosed by the complete handle operand. */
bool fabric_range_inside(SourceRange inner, SourceRange outer) noexcept {
  return inner.start.line > 0 && inner.start.column > 0 && inner.end.line > 0 &&
         inner.end.column > 0 && fabric_pos_lt(inner.start, inner.end) &&
         fabric_pos_lt(outer.start, outer.end) &&
         fabric_pos_le(outer.start, inner.start) &&
         fabric_pos_le(inner.end, outer.end);
}

std::string format_version(PtxVersion version) {
  return fmt::format("{}.{}", version.major, version.minor);
}

bool has_enabled_family_feature(
    std::span<const std::string_view> enabled_family_features,
    std::string_view required_family) noexcept {
  return std::ranges::find(enabled_family_features, required_family) !=
         enabled_family_features.end();
}

bool has_capability(std::span<const std::string_view> capabilities,
                    std::string_view required) noexcept {
  return std::ranges::find(capabilities, required) != capabilities.end();
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

/** Find the generated modifier view identified by its stable semantic name. */
const ModifierValueView* find_modifier(
    std::span<const ModifierValueView> modifiers,
    std::string_view kind_id) noexcept {
  const auto it = std::ranges::find_if(
      modifiers, [kind_id](const ModifierValueView& modifier) {
        return modifier.kind_id == kind_id;
      });
  return it == modifiers.end() ? nullptr : &*it;
}

/** Return whether a scalar is an integer or bit-size conversion type. */
bool is_integer_type(ScalarType type) noexcept {
  return base::scalar_kind(type) == base::ScalarKind::Unsigned ||
         base::scalar_kind(type) == base::ScalarKind::Signed ||
         base::scalar_kind(type) == base::ScalarKind::Bit;
}

/** Return whether a scalar belongs to PTX's floating conversion category. */
bool is_float_type(ScalarType type) noexcept {
  return base::scalar_kind(type) == base::ScalarKind::Float;
}

/** Return whether a rounding value produces an integer-valued result. */
bool is_integer_rounding(RoundingMode rounding) noexcept {
  return rounding == RoundingMode::Rni || rounding == RoundingMode::Rzi ||
         rounding == RoundingMode::Rmi || rounding == RoundingMode::Rpi;
}

/** Return whether a rounding value selects a floating conversion direction. */
bool is_float_rounding(RoundingMode rounding) noexcept {
  return rounding == RoundingMode::Rn || rounding == RoundingMode::Rz ||
         rounding == RoundingMode::Rm || rounding == RoundingMode::Rp;
}

/** Return the ordered precision rank used by ordinary scalar float conversion. */
int float_precision_rank(ScalarType type) noexcept {
  switch (type) {
    case ScalarType::F16:
    case ScalarType::BF16:
      return 1;
    case ScalarType::F32:
      return 2;
    case ScalarType::F64:
      return 3;
    default:
      return 0;
  }
}

/** Return whether every source integer value is representable by destination. */
bool integer_range_contains(ScalarType destination,
                            ScalarType source) noexcept {
  if (!is_integer_type(destination) || !is_integer_type(source))
    return false;
  const uint8_t destination_bits = base::scalar_size_of(destination) * 8;
  const uint8_t source_bits = base::scalar_size_of(source) * 8;
  const bool destination_signed =
      base::scalar_kind(destination) == base::ScalarKind::Signed;
  const bool source_signed =
      base::scalar_kind(source) == base::ScalarKind::Signed;
  if (!destination_signed && source_signed)
    return false;
  if (destination_signed && !source_signed)
    return destination_bits > source_bits;
  return destination_bits >= source_bits;
}

/** Construct a conversion diagnostic at the selected instruction range. */
CheckResult cvt_rule_violation(
    const Context& context, std::string_view message,
    CheckDiagnosticKind kind = CheckDiagnosticKind::RuleViolation) {
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = kind,
      .range = context.instruction_range,
      .message = std::string{message},
  }});
}

/**
 * Return the evaluated integer value used by fixed immediate constraints.
 *
 * Integer source bits precede a data operand's width conversion, so a source
 * value that narrows to an allowed bit pattern cannot satisfy a control rule.
 * @pre `operand.immediate_bits` is engaged.
 */
std::pair<uint64_t, bool> integer_constraint_value(
    const OperandView& operand) noexcept {
  return {operand.integer_source_bits.value_or(*operand.immediate_bits),
          operand.immediate_is_negative.value_or(false)};
}

/**
 * Recheck integer-source use-width bits and raw 32-bit packed FP8 literals.
 * Fixed integer constraints use the original source value for legality, while
 * consumers observe the converted bits; both representations must agree.
 */
CheckResult check_source_immediate_consistency(const OperandView& operand,
                                               const Context& context) {
  if (!operand.immediate_bits || !operand.immediate_type)
    return {};
  const bool packed_raw32 =
      ::ptx_frontend::resolved_ir::detail::is_raw32_fp8x4_type(
          *operand.immediate_type);
  if (packed_raw32 &&
      (*operand.immediate_bits > std::numeric_limits<uint32_t>::max() ||
       (!operand.integer_source_bits &&
        operand.immediate_is_negative.value_or(false)))) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::ImmediateValueMismatch,
        .range = diagnostic_range(operand.locations, context),
        .message = fmt::format(
            "Packed FP8 immediate '{}' has invalid raw 32-bit metadata.",
            operand.field_id),
    }});
  }
  if (!operand.integer_source_bits ||
      (!is_integer_type(*operand.immediate_type) && !packed_raw32))
    return {};
  const uint8_t byte_width = base::scalar_size_of(*operand.immediate_type);
  if (byte_width == 0 || byte_width > sizeof(uint64_t))
    return {};
  const uint8_t bit_width = byte_width * 8;
  const uint64_t mask = bit_width == 64 ? std::numeric_limits<uint64_t>::max()
                                        : (uint64_t{1} << bit_width) - 1;
  if (*operand.immediate_bits == (*operand.integer_source_bits & mask))
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::ImmediateValueMismatch,
      .range = diagnostic_range(operand.locations, context),
      .message = fmt::format(
          "Immediate operand '{}' has bits inconsistent with its integer "
          "source value.",
          operand.field_id),
  }});
}

void append_value_availability_diagnostics(const OperandView& operand,
                                           const Context& context,
                                           CheckDiagnostics& diagnostics) {
  if (!operand.value_availability)
    return;

  const AvailabilityDescriptor& availability = *operand.value_availability;
  const SourceRange& range = diagnostic_range(operand.locations, context);
  if (is_available(availability, context.target))
    return;
  if (availability.any_of_count != 0) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedAvailability,
        .range = range,
        .message = fmt::format(
            "Operand value '{}' has no matching availability clause.",
            operand.value_name),
    });
    return;
  }
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
      !has_enabled_family_feature(context.target.enabled_family_features,
                                  availability.required_family)) {
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

/** Restrict shared typed-value comparison to generated modifier descriptors. */
template <typename Descriptor>
concept ModifierValueDescriptor =
    std::same_as<std::remove_cvref_t<Descriptor>,
                 ModifierValueAvailabilityDescriptor> ||
    std::same_as<std::remove_cvref_t<Descriptor>,
                 ModifierValueDomainDescriptor>;

/** Compare one generated typed modifier value with a projected resolved value. */
template <ModifierValueDescriptor Descriptor>
bool matches_modifier_value(const Descriptor& descriptor,
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
    case ModifierValueKind::ComparisonOperator:
      return descriptor.comparison_operator == actual.comparison_operator;
    case ModifierValueKind::TestProperty:
      return descriptor.test_property == actual.test_property;
    case ModifierValueKind::BooleanOperator:
      return descriptor.boolean_operator == actual.boolean_operator;
    case ModifierValueKind::CacheOperator:
      return descriptor.cache_operator == actual.cache_operator;
    case ModifierValueKind::EvictionPriority:
      return descriptor.eviction_priority == actual.eviction_priority;
    case ModifierValueKind::PrefetchSize:
      return descriptor.prefetch_size == actual.prefetch_size;
    case ModifierValueKind::VectorArity:
      return descriptor.vector_arity == actual.vector_arity;
    case ModifierValueKind::MemoryStateSpace:
      return descriptor.memory_state_space == actual.memory_state_space;
    case ModifierValueKind::MemoryConsistency:
      return descriptor.memory_consistency == actual.memory_consistency;
    case ModifierValueKind::MemoryScope:
      return descriptor.memory_scope == actual.memory_scope;
    case ModifierValueKind::MbarrierPhaseType:
      return descriptor.mbarrier_phase_type == actual.mbarrier_phase_type;
    case ModifierValueKind::MbarrierLayout:
      return descriptor.mbarrier_layout == actual.mbarrier_layout;
    case ModifierValueKind::TcgenCtaGroup:
      return descriptor.tcgen_cta_group == actual.tcgen_cta_group;
    case ModifierValueKind::TcgenScaleVectorSize:
      return descriptor.tcgen_scale_vector_size ==
             actual.tcgen_scale_vector_size;
    case ModifierValueKind::TcgenCollectorControl:
      return descriptor.tcgen_collector == actual.tcgen_collector;
    case ModifierValueKind::TcgenDataMovementShape:
      return descriptor.tcgen_shape == actual.tcgen_shape;
    case ModifierValueKind::TcgenRepeat:
      return descriptor.tcgen_repeat == actual.tcgen_repeat;
    case ModifierValueKind::TcgenReductionOp:
      return descriptor.tcgen_reduction_op == actual.tcgen_reduction_op;
    case ModifierValueKind::TcgenWaitClass:
      return descriptor.tcgen_wait_class == actual.tcgen_wait_class;
    case ModifierValueKind::AsyncProxyKind:
      return descriptor.async_proxy_kind == actual.async_proxy_kind;
    case ModifierValueKind::ProxyKindPair:
      return descriptor.proxy_kind_pair == actual.proxy_kind_pair;
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
  if (is_available(availability, context.target))
    return;
  if (availability.any_of_count != 0) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedAvailability,
        .range = range,
        .message =
            fmt::format("{} has no matching availability clause.", constraint),
    });
    return;
  }
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
        .message = fmt::format("{} requires SM >= {}, but target SM is {}.",
                               constraint, availability.minimum_sm_version,
                               context.target.sm_version),
    });
  }
  if (!availability.required_family.empty() &&
      !has_enabled_family_feature(context.target.enabled_family_features,
                                  availability.required_family)) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
        .range = range,
        .message = fmt::format("{} requires target family '{}'.", constraint,
                               availability.required_family),
    });
  }
}

std::string_view parameter_direction_name(
    ParameterDirection direction) noexcept {
  switch (direction) {
    case ParameterDirection::None:
      return "unknown";
    case ParameterDirection::Input:
      return "input";
    case ParameterDirection::Return:
      return "return";
    case ParameterDirection::CallArgument:
      return "call argument";
  }
  return "unknown";
}

std::string_view parameter_qualifier_name(
    ParameterAddressQualifier qualifier) noexcept {
  switch (qualifier) {
    case ParameterAddressQualifier::Default:
      return ".param";
    case ParameterAddressQualifier::Entry:
      return ".param::entry";
    case ParameterAddressQualifier::Function:
      return ".param::func";
  }
  return ".param";
}

void append_parameter_qualifier_diagnostics(
    const OperandDescriptor& descriptor, const OperandView& operand,
    std::optional<MemoryStateSpace> selected_state_space,
    const Context& context, CheckDiagnostics& diagnostics) {
  if (selected_state_space != MemoryStateSpace::Parameter ||
      operand.parameter_qualifier == ParameterAddressQualifier::Default) {
    return;
  }

  bool mismatch = false;
  if (operand.parameter_qualifier == ParameterAddressQualifier::Entry) {
    mismatch =
        operand.enclosing_function_kind == EnclosingFunctionKind::Device ||
        operand.parameter_direction == ParameterDirection::Return ||
        operand.parameter_direction == ParameterDirection::CallArgument;
  } else {
    mismatch =
        operand.enclosing_function_kind == EnclosingFunctionKind::Entry &&
        operand.parameter_direction == ParameterDirection::Input;
  }
  if (!mismatch)
    return;

  diagnostics.push_back(CheckDiagnostic{
      .kind = CheckDiagnosticKind::ParameterQualifierMismatch,
      .range = diagnostic_range(operand.locations, context),
      .message = fmt::format(
          "Address operand '{}' uses {} for an incompatible parameter context.",
          descriptor.target_field_id,
          parameter_qualifier_name(operand.parameter_qualifier)),
  });
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
      operand.parameter_direction != ParameterDirection::CallArgument &&
      operand.parameter_direction != constraint.direction) {
    // Direction is the more specific error and suppresses contextual target
    // diagnostics for the same address.
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::ParameterDirectionMismatch,
        .range = diagnostic_range(operand.locations, context),
        .message =
            fmt::format("Address operand '{}' refers to a {} parameter but the "
                        "instruction requires a {} parameter address.",
                        descriptor.target_field_id,
                        parameter_direction_name(operand.parameter_direction),
                        parameter_direction_name(constraint.direction)),
    });
    return;
  }

  if (constraint.direction == ParameterDirection::Return ||
      operand.enclosing_function_kind == EnclosingFunctionKind::Device ||
      operand.parameter_direction == ParameterDirection::CallArgument) {
    append_address_constraint_availability_diagnostics(
        constraint.function_availability, "Parameter address", operand, context,
        diagnostics);
  }
}

}  // namespace

CheckResult check_fabric_handle(const WithLocs<ResolvedFabricHandle>& handle,
                                bool counted, const Context& context) {
  const auto fail = [&](std::string_view message) -> CheckResult {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = handle.locs.empty() ? context.instruction_range
                                     : handle.locs.front(),
        .message = std::string(message),
    }});
  };
  const auto& value = handle.value;
  if (handle.locs.size() != 1 ||
      !fabric_range_inside(handle.locs.front(), context.instruction_range) ||
      value.counter_offset.has_value() != counted ||
      value.comma_ranges.size() != (counted ? 2u : 1u))
    return fail("Fabric handle arity or source range is inconsistent.");
  const SourceRange outer = handle.locs.front();
  if (!fabric_range_inside(value.left_bracket_range, outer) ||
      !fabric_range_inside(value.right_bracket_range, outer))
    return fail("Fabric handle bracket locations are invalid.");
  const auto check_component = [&](const WithLocs<ResolvedRegisterRef>& part,
                                   uint8_t required_width) -> bool {
    const auto& reg = part.value;
    const auto kind = reg.declared_type ? base::scalar_kind(*reg.declared_type)
                                        : base::ScalarKind::Invalid;
    return part.locs.size() == 1 &&
           fabric_range_inside(part.locs.front(), outer) &&
           reg.register_class == ResolvedRegisterClass::General &&
           !reg.vector_width && (!reg.symbol_id || reg.declared_type) &&
           (!reg.declared_type ||
            (base::scalar_size_of(*reg.declared_type) == required_width &&
             (kind == base::ScalarKind::Unsigned ||
              kind == base::ScalarKind::Signed ||
              kind == base::ScalarKind::Bit)));
  };
  if (!check_component(value.endpoint, 4) ||
      !check_component(value.data_offset, 8) ||
      (value.counter_offset && !check_component(*value.counter_offset, 8)))
    return fail(
        "Fabric handle components require scalar 32/64-bit integer registers.");
  std::array<SourceRange, 3> component_ranges{
      value.endpoint.locs.front(), value.data_offset.locs.front(),
      value.counter_offset ? value.counter_offset->locs.front()
                           : SourceRange{}};
  if (!fabric_pos_le(value.left_bracket_range.end, component_ranges[0].start) ||
      !fabric_pos_le(component_ranges[counted ? 2 : 1].end,
                     value.right_bracket_range.start))
    return fail("Fabric handle component order is invalid.");
  for (size_t index = 0; index < value.comma_ranges.size(); ++index) {
    const auto comma = value.comma_ranges[index];
    if (!fabric_range_inside(comma, outer) ||
        !fabric_pos_le(component_ranges[index].end, comma.start) ||
        !fabric_pos_le(comma.end, component_ranges[index + 1].start))
      return fail("Fabric handle separator locations are invalid.");
  }
  return {};
}

OperandView project_tensor_operand(
    std::string_view field_id, const WithLocs<ResolvedTensorOperand>& operand) {
  const auto& tensor = operand.value;
  const auto& address = tensor.tensor_map.address;
  const auto* symbol = std::get_if<ResolvedSymbolRef>(&address.base);
  std::optional<MemoryStateSpace> space;
  if (symbol && symbol->address_state_space) {
    switch (*symbol->address_state_space) {
      case syntax_ast::AstStateSpace::Global:
        space = MemoryStateSpace::Global;
        break;
      case syntax_ast::AstStateSpace::Shared:
        space = MemoryStateSpace::Shared;
        break;
      case syntax_ast::AstStateSpace::Local:
        space = MemoryStateSpace::Local;
        break;
      case syntax_ast::AstStateSpace::Parameter:
        space = MemoryStateSpace::Parameter;
        break;
      case syntax_ast::AstStateSpace::Constant:
        space = MemoryStateSpace::Constant;
        break;
      case syntax_ast::AstStateSpace::Register:
        break;
    }
  }
  const auto low_bit = [](uint64_t value) {
    return value == 0 ? uint64_t{0} : value & (~value + 1);
  };
  std::optional<uint64_t> alignment;
  if (symbol)
    alignment = symbol->address_alignment;
  else if (const auto* immediate =
               std::get_if<ResolvedImmediate>(&address.base))
    alignment = low_bit(immediate->bits);
  if (alignment && address.offset) {
    const uint64_t offset_alignment = low_bit(address.offset->value.bits);
    if (offset_alignment != 0 &&
        (*alignment == 0 || offset_alignment < *alignment))
      alignment = offset_alignment;
  }
  OperandView view{
      .field_id = field_id,
      .actual_shape = OperandShape::TensorOperand,
      .address_state_space = space,
      .address_base_kind =
          std::holds_alternative<ResolvedRegisterRef>(address.base)
              ? AddressBaseKind::Register
          : std::holds_alternative<ResolvedSymbolRef>(address.base)
              ? AddressBaseKind::Symbol
              : AddressBaseKind::Immediate,
      .address_offset_fits_signed32 =
          !address.offset || address_offset_fits_signed32(*address.offset),
      .address_alignment = alignment,
      .enclosing_function_kind = address.enclosing_function_kind,
      .parameter_direction = symbol && symbol->declaration_kind &&
                                     *symbol->declaration_kind ==
                                         binding::SymbolKind::InputParameter
                                 ? ParameterDirection::Input
                             : symbol && symbol->declaration_kind &&
                                     *symbol->declaration_kind ==
                                         binding::SymbolKind::ReturnParameter
                                 ? ParameterDirection::Return
                             : symbol && symbol->declaration_kind &&
                                     *symbol->declaration_kind ==
                                         binding::SymbolKind::CallParameter
                                 ? ParameterDirection::CallArgument
                                 : ParameterDirection::None,
      .tensor_rank = tensor.rank,
      .tensor_operand = &tensor,
      .vector_arity = tensor.coordinates.elements.size(),
      .locations = operand.locs,
  };
  for (size_t index = 0; index < tensor.coordinates.elements.size() &&
                         index < kMaxOperandElements;
       ++index) {
    const auto& element = tensor.coordinates.elements[index];
    if (const auto* reg = std::get_if<ResolvedRegisterRef>(&element)) {
      view.vector_element_shapes[index] = OperandShape::Register;
      view.vector_element_types[index] =
          reg->declared_type.value_or(ScalarType::Invalid);
    } else {
      const auto& immediate = std::get<ResolvedImmediate>(element);
      view.vector_element_shapes[index] = OperandShape::Immediate;
      view.vector_element_types[index] = immediate.type;
      view.tensor_has_negative_immediate |=
          (immediate.bits & uint64_t{0x80000000}) != 0;
    }
  }
  return view;
}

OperandView project_tensor_im2col_info(
    std::string_view field_id,
    const WithLocs<ResolvedTensorIm2colInfo>& operand) {
  OperandView view{
      .field_id = field_id,
      .actual_shape = OperandShape::Vector,
      .vector_arity = operand.value.elements.size(),
      .locations = operand.locs,
  };
  for (size_t index = 0;
       index < operand.value.elements.size() && index < kMaxOperandElements;
       ++index) {
    const auto& element = operand.value.elements[index];
    if (const auto* reg = std::get_if<ResolvedRegisterRef>(&element)) {
      view.vector_element_shapes[index] = OperandShape::Register;
      view.vector_element_types[index] =
          reg->declared_type.value_or(ScalarType::Invalid);
      view.vector_element_registers[index] = reg;
    } else {
      const auto& immediate = std::get<ResolvedImmediate>(element);
      view.vector_element_shapes[index] = OperandShape::Immediate;
      view.vector_element_types[index] = immediate.type;
    }
  }
  return view;
}

CheckResult check_tensor_store_coordinates(const OperandView& operand,
                                           const Context& context) {
  if (!operand.tensor_has_negative_immediate)
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::RuleViolation,
      .range = diagnostic_range(operand.locations, context),
      .message = "Tensor write coordinates must not be statically negative."}});
}

bool is_available(const AvailabilityDescriptor& availability,
                  const TargetInfo& target) noexcept {
  if (availability.any_of_count != 0) {
    for (size_t index = 0; index < availability.any_of_count; ++index) {
      const AvailabilityClause& clause = availability.any_of[index];
      if (target.ptx_version < clause.minimum_ptx_version ||
          target.sm_version < clause.minimum_sm_version ||
          (clause.has_exact_target &&
           (!target.identity ||
            target.identity->architecture != clause.exact_target_architecture ||
            target.identity->flavor != clause.exact_target_flavor)) ||
          (!clause.required_family.empty() &&
           !has_enabled_family_feature(target.enabled_family_features,
                                       clause.required_family)))
        continue;
      bool capabilities_match = true;
      for (size_t capability = 0; capability < clause.capability_count;
           ++capability) {
        if (!has_capability(target.capabilities,
                            clause.capabilities[capability])) {
          capabilities_match = false;
          break;
        }
      }
      if (capabilities_match)
        return true;
    }
    return false;
  }
  return target.ptx_version >= availability.minimum_ptx_version &&
         target.sm_version >= availability.minimum_sm_version &&
         (availability.required_family.empty() ||
          has_enabled_family_feature(target.enabled_family_features,
                                     availability.required_family));
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

  if (is_available(availability, target))
    return {};
  if (availability.any_of_count != 0) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedAvailability,
        .range = context.instruction_range,
        .message = fmt::format(
            "Instruction variant '{}' has no matching availability clause.",
            variant.variant_name),
    }});
  }

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
      !has_enabled_family_feature(target.enabled_family_features,
                                  availability.required_family)) {
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

CheckResult check_execution_predicate(
    const std::optional<WithLocs<ResolvedPredicate>>& predicate,
    const Context& context) {
  if (!predicate)
    return {};

  const ResolvedRegisterRef& register_ref = predicate->value.register_ref;
  CheckDiagnostics diagnostics;
  const auto invalid = [&](std::string_view reason) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::InvalidExecutionPredicate,
        .range = diagnostic_range(predicate->locs, context),
        .message = fmt::format("Instruction execution predicate {}.", reason),
    });
  };
  if (register_ref.register_class != ResolvedRegisterClass::Predicate)
    invalid("does not retain predicate register class");
  if (register_ref.declared_type &&
      *register_ref.declared_type != ScalarType::Pred) {
    invalid("has a non-.pred declared type");
  }
  if (register_ref.vector_width)
    invalid("has vector register shape");
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

/** Append diagnostics for immutable matrix controls and scale selectors. */
void append_matrix_operand_diagnostics(const MatrixInstructionDescriptor*,
                                       std::span<const OperandView>,
                                       const Context&, CheckDiagnostics&);

CheckResult check_operands(
    std::span<const OperandDescriptor> descriptors,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    std::span<const OperandTypeCompatibilityDescriptor> type_compatibilities,
    const Context& context, const MatrixInstructionDescriptor* matrix) {
  CheckDiagnostics diagnostics;

  for (const OperandView& operand : operands) {
    if ((operand.actual_shape != OperandShape::Vector &&
         operand.actual_shape != OperandShape::TensorOperand) ||
        (operand.vector_arity != 0 &&
         operand.vector_arity <= kMaxOperandElements)) {
      continue;
    }
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::InvalidVectorOperand,
        .range = diagnostic_range(operand.locations, context),
        .message =
            fmt::format("Vector operand '{}' has an unsupported element count.",
                        operand.field_id),
    });
  }
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));

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

    // Every source-backed data immediate retains both the evaluated source
    // integer and its use-width bits, including operands with no fixed
    // immediate constraint of their own.
    if (auto consistency =
            check_source_immediate_consistency(*operand, context);
        !consistency) {
      diagnostics.insert(diagnostics.end(), consistency.error().begin(),
                         consistency.error().end());
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
    if (operand->actual_shape == OperandShape::TensorOperand) {
      const auto* tensor = operand->tensor_operand;
      const bool invalid_structure =
          tensor == nullptr || !descriptor.expected_tensor_mode ||
          !descriptor.expected_tensor_rank ||
          *descriptor.expected_tensor_rank < TensorRank::One ||
          *descriptor.expected_tensor_rank > TensorRank::Five ||
          (*descriptor.expected_tensor_mode != TensorAccessMode::Tiled &&
           *descriptor.expected_tensor_mode != TensorAccessMode::Im2colNoOffs &&
           *descriptor.expected_tensor_mode != TensorAccessMode::Im2col &&
           *descriptor.expected_tensor_mode != TensorAccessMode::Im2colW &&
           *descriptor.expected_tensor_mode != TensorAccessMode::Im2colW128 &&
           *descriptor.expected_tensor_mode != TensorAccessMode::TileGather4 &&
           *descriptor.expected_tensor_mode !=
               TensorAccessMode::TileScatter4) ||
          tensor->mode != *descriptor.expected_tensor_mode ||
          tensor->rank != *descriptor.expected_tensor_rank ||
          tensor->coordinate_ranges.size() != operand->vector_arity ||
          tensor->tensor_map.range == SourceRange{};
      if (invalid_structure) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::RuleViolation,
            .range = diagnostic_range(operand->locations, context),
            .message =
                "Tensor operand rank, mode, or source metadata is invalid."});
      }
      if (operand->address_state_space == MemoryStateSpace::Parameter &&
          (operand->enclosing_function_kind != EnclosingFunctionKind::Entry ||
           operand->parameter_direction != ParameterDirection::Input)) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::AddressStateSpaceMismatch,
            .range = diagnostic_range(operand->locations, context),
            .message =
                "Tensor-map parameter storage requires a kernel parameter."});
      }
      if (tensor != nullptr) {
        for (size_t index = 0; index < tensor->coordinates.elements.size();
             ++index) {
          const SourceRange range =
              index < tensor->coordinate_ranges.size() &&
                      tensor->coordinate_ranges[index] != SourceRange{}
                  ? tensor->coordinate_ranges[index]
                  : diagnostic_range(operand->locations, context);
          const auto* reg = std::get_if<ResolvedRegisterRef>(
              &tensor->coordinates.elements[index]);
          if (reg) {
            const bool valid =
                reg->register_class == ResolvedRegisterClass::General &&
                !reg->vector_width && (!reg->symbol_id || reg->declared_type) &&
                (!reg->declared_type ||
                 (is_integer_type(*reg->declared_type) &&
                  base::scalar_size_of(*reg->declared_type) == 4));
            if (!valid)
              diagnostics.push_back(CheckDiagnostic{
                  .kind = CheckDiagnosticKind::OperandTypeMismatch,
                  .range = range,
                  .message = "Tensor coordinates require scalar 32-bit "
                             "integer/bit registers.",
              });
            continue;
          }
          const auto* immediate = std::get_if<ResolvedImmediate>(
              &tensor->coordinates.elements[index]);
          if (!immediate)
            continue;
          const bool valid =
              immediate->type == ScalarType::S32 &&
              immediate->integer_source_bits.has_value() &&
              immediate->bits ==
                  (*immediate->integer_source_bits & uint64_t{0xffffffff});
          if (valid)
            continue;
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::OperandTypeMismatch,
              .range = range,
              .message = "Tensor coordinate immediate has invalid signed "
                         "32-bit metadata."});
        }
      }
    }
    if (operand->actual_shape == OperandShape::Vector &&
        (descriptor.access == OperandAccess::Write ||
         descriptor.access == OperandAccess::ReadWrite) &&
        operand->vector_arity <= kMaxOperandElements) {
      for (size_t index = 0; index < operand->vector_arity; ++index) {
        const ResolvedRegisterRef* lane =
            operand->vector_element_registers[index];
        if (lane == nullptr)
          continue;
        for (size_t previous = 0; previous < index; ++previous) {
          const ResolvedRegisterRef* earlier =
              operand->vector_element_registers[previous];
          if (earlier == nullptr)
            continue;
          const bool same_register =
              (lane->symbol_id && earlier->symbol_id &&
               lane->symbol_id == earlier->symbol_id &&
               lane->parameterized_index == earlier->parameterized_index) ||
              lane->spelling == earlier->spelling;
          if (!same_register)
            continue;
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::InvalidVectorOperand,
              .range = index < operand->locations.size()
                           ? operand->locations[index]
                           : diagnostic_range(operand->locations, context),
              .message =
                  fmt::format("Destination vector '{}' writes register '{}' "
                              "more than once.",
                              descriptor.target_field_id, lane->spelling),
          });
          break;
        }
      }
    }
    if (operand->actual_shape == OperandShape::PredicatePair &&
        !operand->predicate_pair_has_destination) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedOperandShape,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format("Predicate-pair operand '{}' must retain at "
                                 "least one destination.",
                                 descriptor.target_field_id),
      });
    }
    if (operand->actual_shape == OperandShape::ShflDestination) {
      if (!operand->paired_destination_data_present &&
          !operand->paired_destination_predicate_present) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::UnsupportedOperandShape,
            .range = diagnostic_range(operand->locations, context),
            .message = fmt::format("Paired destination '{}' must retain a data "
                                   "or predicate output.",
                                   descriptor.target_field_id),
        });
      }
      if (!operand->paired_destination_data_present &&
          !descriptor.allow_destination_sink) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::UnsupportedOperandShape,
            .range = diagnostic_range(operand->locations, context),
            .message = fmt::format("Paired destination '{}' cannot discard its "
                                   "data output.",
                                   descriptor.target_field_id),
        });
      }
      if (!operand->paired_destination_predicate_present &&
          !descriptor.allow_predicate_sink) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::UnsupportedOperandShape,
            .range = diagnostic_range(operand->locations, context),
            .message = fmt::format("Paired destination '{}' cannot discard its "
                                   "predicate output.",
                                   descriptor.target_field_id),
        });
      }
      if (operand->paired_destination_predicate_type &&
          *operand->paired_destination_predicate_type != ScalarType::Pred) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::OperandTypeMismatch,
            .range = diagnostic_range(operand->locations, context),
            .message = fmt::format(
                "Paired destination '{}' has predicate "
                "output type '{}' rather than '.pred'.",
                descriptor.target_field_id,
                to_string(*operand->paired_destination_predicate_type)),
        });
      }
    }
    if (descriptor.role == OperandRole::Destination &&
        operand->destination_predicate_negated) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedOperandShape,
          .range = diagnostic_range(operand->locations, context),
          .message =
              fmt::format("Predicate destination '{}' cannot be negated.",
                          descriptor.target_field_id),
      });
    }
    if (operand->is_sink) {
      append_address_constraint_availability_diagnostics(
          descriptor.sink_availability, "mbarrier state-token sink", *operand,
          context, diagnostics);
    }

    if (descriptor.minimum_elements != 0) {
      const SourceRange& range = diagnostic_range(operand->locations, context);
      if ((operand->actual_shape != OperandShape::Vector &&
           operand->actual_shape != OperandShape::TensorOperand) ||
          operand->vector_arity < descriptor.minimum_elements ||
          operand->vector_arity > descriptor.maximum_elements ||
          operand->vector_arity > kMaxOperandElements) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::InvalidVectorOperand,
            .range = range,
            .message = fmt::format(
                "Vector operand '{}' has an unsupported element count.",
                descriptor.target_field_id),
        });
      } else {
        const auto mismatched = std::ranges::find_if(
            operand->vector_element_shapes.begin(),
            operand->vector_element_shapes.begin() + operand->vector_arity,
            [&](OperandShape element_shape) {
              return !allows_shape(descriptor.allowed_element_shapes,
                                   element_shape);
            });
        if (mismatched !=
            operand->vector_element_shapes.begin() + operand->vector_arity) {
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::UnsupportedOperandShape,
              .range = range,
              .message = fmt::format(
                  "Vector operand '{}' has an element shape not accepted by "
                  "this instruction layout.",
                  descriptor.target_field_id),
          });
        }
      }
    }

    if (descriptor.address_base_policy == AddressBasePolicy::Register &&
        operand->address_base_kind != AddressBaseKind::Register) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(operand->locations, context),
          .message =
              fmt::format("Address operand '{}' requires a register base.",
                          descriptor.target_field_id),
      });
    }
    if (descriptor.address_offset_domain == AddressOffsetDomain::Signed32 &&
        !operand->address_offset_fits_signed32) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(operand->locations, context),
          .message = fmt::format(
              "Address operand '{}' requires a signed 32-bit offset.",
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
    append_parameter_qualifier_diagnostics(
        descriptor, *operand, selected_state_space, context, diagnostics);
    append_parameter_address_diagnostics(
        descriptor, *operand, selected_state_space, context, diagnostics);

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
        const std::string constraint_name =
            fmt::format("Address state space '.{}'",
                        state_space_name(allowed->state_space));
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

    if (operand->actual_shape == OperandShape::PredicatePair) {
      for (size_t index = 0; index < operand->predicate_pair_types.size();
           ++index) {
        const ScalarType actual = operand->predicate_pair_types[index];
        if (actual != ScalarType::Invalid &&
            !scalar_types_compatible(actual, expected_type,
                                     descriptor.register_width_policy)) {
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::OperandTypeMismatch,
              .range = index < operand->locations.size()
                           ? operand->locations[index]
                           : diagnostic_range(operand->locations, context),
              .message = fmt::format(
                  "Predicate-pair operand '{}' has an endpoint type '{}' "
                  "incompatible with instruction type source '{}' ('{}').",
                  descriptor.target_field_id, to_string(actual),
                  expected_type_source, to_string(expected_type)),
          });
        }
      }
      continue;
    }

    if ((operand->actual_shape == OperandShape::Vector ||
         operand->actual_shape == OperandShape::TensorOperand) &&
        descriptor.minimum_elements != 0) {
      if (operand->vector_arity <= kMaxOperandElements) {
        const auto mismatched = std::ranges::find_if(
            operand->vector_element_types.begin(),
            operand->vector_element_types.begin() + operand->vector_arity,
            [&](ScalarType element_type) {
              // Standalone operands have no declaration to establish a type.
              return element_type != ScalarType::Invalid &&
                     !scalar_types_compatible(element_type, expected_type,
                                              descriptor.register_width_policy);
            });
        if (mismatched !=
            operand->vector_element_types.begin() + operand->vector_arity) {
          const size_t index = static_cast<size_t>(
              mismatched - operand->vector_element_types.begin());
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::OperandTypeMismatch,
              .range = index < operand->locations.size()
                           ? operand->locations[index]
                           : diagnostic_range(operand->locations, context),
              .message = fmt::format(
                  "Vector operand '{}' has an element type '{}' incompatible "
                  "with instruction type source '{}' ('{}').",
                  descriptor.target_field_id, to_string(*mismatched),
                  expected_type_source, to_string(expected_type)),
          });
        }
      }
      continue;
    }

    if (operand->actual_shape == OperandShape::Vector &&
        descriptor.minimum_elements == 0) {
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
                descriptor.target_field_id, vector_payload_bits,
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
      if (operand->vector_sink_count != 0 &&
          descriptor.vector_sink_payload_bits != 0 &&
          vector_payload_bits != descriptor.vector_sink_payload_bits) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::InvalidVectorOperand,
            .range = range,
            .message =
                fmt::format("Vector operand '{}' uses the '_' sink with {} "
                            "bits; {} bits are required.",
                            descriptor.target_field_id, vector_payload_bits,
                            descriptor.vector_sink_payload_bits),
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
      } else if (!descriptor.allowed_register_types.empty()) {
        std::optional<bool> floating_register_family;
        for (size_t index = 0; index < operand->vector_arity; ++index) {
          if (operand->vector_element_shapes[index] != OperandShape::Register)
            continue;
          const ScalarType element_type = operand->vector_element_types[index];
          if (element_type == ScalarType::Invalid)
            continue;
          const SourceRange& lane_range = index < operand->locations.size()
                                              ? operand->locations[index]
                                              : range;
          if (std::ranges::find(descriptor.allowed_register_types,
                                element_type) ==
              descriptor.allowed_register_types.end()) {
            diagnostics.push_back(CheckDiagnostic{
                .kind = CheckDiagnosticKind::OperandTypeMismatch,
                .range = lane_range,
                .message = fmt::format("Vector operand '{}' has disallowed "
                                       "register lane type '{}'.",
                                       descriptor.target_field_id,
                                       to_string(element_type)),
            });
            continue;
          }
          if (!descriptor.require_uniform_register_family)
            continue;
          const auto kind = scalar_kind(element_type);
          if (kind == base::ScalarKind::Bit)
            continue;
          const bool floating = kind == base::ScalarKind::Float;
          if ((kind != base::ScalarKind::Float &&
               kind != base::ScalarKind::Signed &&
               kind != base::ScalarKind::Unsigned) ||
              (floating_register_family &&
               *floating_register_family != floating)) {
            diagnostics.push_back(CheckDiagnostic{
                .kind = CheckDiagnosticKind::OperandTypeMismatch,
                .range = lane_range,
                .message = fmt::format(
                    "Vector operand '{}' mixes integer and floating register "
                    "lanes.",
                    descriptor.target_field_id),
            });
          } else {
            floating_register_family = floating;
          }
        }
      } else {
        const auto mismatched = std::ranges::find_if(
            operand->vector_element_types.begin(),
            operand->vector_element_types.begin() + operand->vector_arity,
            [&](ScalarType element_type) {
              return element_type != ScalarType::Invalid &&
                     !scalar_types_compatible(element_type, expected_type,
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
               !scalar_types_compatible(*operand->register_type, expected_type,
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

  append_matrix_operand_diagnostics(matrix, operands, context, diagnostics);
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

/** Add matrix immediate and selector diagnostics after generic operand checks. */
void append_matrix_operand_diagnostics(
    const MatrixInstructionDescriptor* matrix,
    std::span<const OperandView> operands, const Context& context,
    CheckDiagnostics& diagnostics) {
  if (matrix != nullptr) {
    // Recheck descriptor carriers and source-backed controls independently of
    // the syntax tree; standalone unknown register types remain deferred.
    for (const OperandView& operand : operands) {
      if ((matrix->family == MatrixFamily::WGMMA ||
           matrix->family == MatrixFamily::WGMMA_SPARSE) &&
          (operand.field_id == "a_desc" || operand.field_id == "b_desc")) {
        const bool valid_carrier =
            operand.actual_shape == OperandShape::Register &&
            operand.register_class == ResolvedRegisterClass::General &&
            !operand.register_vector_width &&
            (!operand.register_symbol_id || operand.register_type) &&
            (!operand.register_type ||
             (is_integer_type(*operand.register_type) &&
              base::scalar_size_of(*operand.register_type) == 8));
        if (!valid_carrier)
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::OperandTypeMismatch,
              .range = diagnostic_range(operand.locations, context),
              .message = "WGMMA shared descriptor requires a scalar 64-bit "
                         "general register.",
          });
      }
      if ((matrix->family == MatrixFamily::WGMMA ||
           matrix->family == MatrixFamily::WGMMA_SPARSE) &&
          operand.field_id == "scale_d") {
        const bool valid_constant =
            operand.actual_shape == OperandShape::Immediate &&
            operand.immediate_type == ScalarType::Pred;
        const bool valid_register =
            operand.actual_shape == OperandShape::Predicate &&
            operand.register_class == ResolvedRegisterClass::Predicate &&
            !operand.register_vector_width &&
            (!operand.register_symbol_id || operand.register_type) &&
            (!operand.register_type ||
             *operand.register_type == ScalarType::Pred) &&
            !operand.destination_predicate_negated;
        if (!valid_constant && !valid_register)
          diagnostics.push_back(CheckDiagnostic{
              .kind = CheckDiagnosticKind::OperandTypeMismatch,
              .range = diagnostic_range(operand.locations, context),
              .message = "WGMMA scale-D requires a scalar predicate or 0/1 "
                         "constant.",
          });
      }
      if (operand.actual_shape != OperandShape::Immediate ||
          !operand.immediate_bits)
        continue;
      const uint64_t source =
          operand.integer_source_bits.value_or(*operand.immediate_bits);
      const uint64_t current = *operand.immediate_bits;
      const size_t width = operand.immediate_type
                               ? scalar_size_of(*operand.immediate_type) * 8u
                               : 0u;
      const bool fits_width =
          width >= 64 || (width != 0 && source < (uint64_t{1} << width));
      const bool is_wgmma_scale =
          (matrix->family == MatrixFamily::WGMMA ||
           matrix->family == MatrixFamily::WGMMA_SPARSE) &&
          (operand.field_id == "scale_a" || operand.field_id == "scale_b");
      const bool valid_scale =
          operand.immediate_type == ScalarType::S32 &&
          ((source == 1 && !operand.immediate_is_negative.value_or(false) &&
            current == 1) ||
           (source == UINT64_MAX &&
            operand.immediate_is_negative.value_or(false) &&
            current == UINT32_MAX));
      if ((is_wgmma_scale && !valid_scale) ||
          (!is_wgmma_scale && (operand.immediate_is_negative.value_or(false) ||
                               source != current || !fits_width))) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::ImmediateValueMismatch,
            .range = diagnostic_range(operand.locations, context),
            .message = fmt::format(
                "Matrix control immediate '{}' does not match its unsigned "
                "source value and declared width.",
                operand.field_id),
        });
      }
    }
    for (size_t index = 0; index < matrix->scale_selector_count; ++index) {
      const auto& selector = matrix->scale_selectors[index];
      const OperandView* operand =
          find_operand(operands, selector.operand_field_id);
      if (operand == nullptr || operand->actual_shape != OperandShape::Vector ||
          operand->vector_arity != 2)
        continue;
      for (size_t lane = 0; lane < 2; ++lane) {
        if (operand->vector_element_shapes[lane] != OperandShape::Immediate)
          continue;
        const auto source = operand->vector_immediate_source_bits[lane];
        const auto current = operand->vector_immediate_bits[lane];
        const bool valid =
            source && current && *source == *current && *source <= UINT16_MAX &&
            !operand->vector_immediate_negative[lane] &&
            (lane == 0 ? (*source < 8 && (selector.byte_mask & (1u << *source)))
                       : (*source <= selector.thread_max));
        if (valid)
          continue;
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::RuleViolation,
            .range = lane < operand->locations.size()
                         ? operand->locations[lane]
                         : diagnostic_range(operand->locations, context),
            .message = fmt::format(
                "Matrix scale selector '{}' has an invalid {} ID.",
                selector.operand_field_id, lane == 0 ? "byte" : "thread"),
        });
      }
    }
  }
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

CheckResult check_operand_layout_modifiers(
    const VariantDescriptor& variant, uint16_t selected_layout,
    std::span<const ModifierValueView> actual_values, const Context& context) {
  if (selected_layout >= variant.operand_layouts.size()) {
    return check_operand_layout_tag(variant.variant_name, selected_layout,
                                    variant.operand_layouts.size(), context);
  }

  const auto& layout = variant.operand_layouts[selected_layout];
  if (layout.forbidden_modifiers.empty())
    return {};

  CheckDiagnostics diagnostics;
  for (const ModifierValueView& actual : actual_values) {
    if (!actual.is_present)
      continue;
    if (!std::ranges::contains(layout.forbidden_modifiers, actual.slot))
      continue;
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::ModifierNotAllowedForLayout,
        .range = diagnostic_range(actual.locations, context),
        .message = fmt::format(
            "Operand layout '{}' of instruction variant '{}' does not accept "
            "modifier '{}'.",
            layout.layout_name, variant.variant_name, actual.kind_id),
    });
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
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

  if (is_available(availability, target))
    return {};
  if (availability.any_of_count != 0) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedAvailability,
        .range = context.instruction_range,
        .message = fmt::format("Operand layout '{}' of instruction variant "
                               "'{}' has no matching availability clause.",
                               layout.layout_name, variant.variant_name),
    }});
  }

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
      !has_enabled_family_feature(target.enabled_family_features,
                                  availability.required_family)) {
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
    if (is_available(availability, target))
      continue;
    if (availability.any_of_count != 0) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedAvailability,
          .range = range,
          .message =
              fmt::format("Modifier '{}' has no matching availability clause.",
                          actual.kind_id),
      });
      continue;
    }
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
        !has_enabled_family_feature(target.enabled_family_features,
                                    availability.required_family)) {
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

CheckResult check_modifier_value_domain(
    std::span<const ModifierValueDomainDescriptor> descriptors,
    std::span<const ModifierValueView> actual_values, const Context& context) {
  CheckDiagnostics diagnostics;
  for (const ModifierValueView& actual : actual_values) {
    const auto it = std::ranges::find_if(
        descriptors, [&actual](const ModifierValueDomainDescriptor& entry) {
          return matches_modifier_value(entry, actual);
        });
    if (it != descriptors.end()) {
      // A two-valued boolean domain is a presence-only optional flag: its
      // written token and stored value must remain paired after resolution.
      if (actual.value_kind == ModifierValueKind::Bool &&
          std::ranges::any_of(
              descriptors,
              [&actual](const ModifierValueDomainDescriptor& entry) {
                return entry.kind_id == actual.kind_id &&
                       entry.value_kind == ModifierValueKind::Bool &&
                       entry.bool_value != actual.bool_value;
              }) &&
          actual.bool_value != !actual.locations.empty()) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::ModuleSourceMismatch,
            .range = diagnostic_range(actual.locations, context),
            .message = fmt::format(
                "Optional modifier '{}' disagrees with its source presence.",
                actual.kind_id),
        });
      }
      continue;
    }
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::ModifierValueDomainMismatch,
        .range = diagnostic_range(actual.locations, context),
        .message = fmt::format("Modifier '{}' has a value outside the selected "
                               "instruction variant's "
                               "semantic domain.",
                               actual.kind_id),
    });
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_cvt_rule(std::span<const ModifierValueView> modifiers,
                           std::span<const OperandView> operands,
                           const Context& context) {
  const ModifierValueView* destination = find_modifier(modifiers, "dst_type");
  const ModifierValueView* source = find_modifier(modifiers, "src_type");
  if (destination == nullptr || source == nullptr ||
      destination->value_kind != ModifierValueKind::ScalarType ||
      source->value_kind != ModifierValueKind::ScalarType) {
    return cvt_rule_violation(
        context, "cvt requires typed destination and source modifiers.");
  }

  const ScalarType destination_type = destination->scalar_type;
  const ScalarType source_type = source->scalar_type;
  const ModifierValueView* rounding = find_modifier(modifiers, "rounding");
  const RoundingMode rounding_mode =
      rounding == nullptr || !rounding->is_present ? RoundingMode::Invalid
                                                   : rounding->rounding_mode;
  const ModifierValueView* ftz = find_modifier(modifiers, "ftz");
  const bool has_ftz = ftz != nullptr && ftz->is_present && ftz->bool_value;
  const ModifierValueView* saturate = find_modifier(modifiers, "sat");
  const bool has_saturate =
      saturate != nullptr && saturate->is_present && saturate->bool_value;
  const ModifierValueView* scaled = find_modifier(modifiers, "scaled");
  const bool has_scaled =
      scaled != nullptr && scaled->is_present && scaled->bool_value;
  const bool has_scale_factor =
      std::ranges::any_of(operands, [](const OperandView& operand) {
        return operand.field_id == "scale_factor";
      });

  const bool destination_integer = is_integer_type(destination_type);
  const bool source_integer = is_integer_type(source_type);
  const bool destination_float = is_float_type(destination_type);
  const bool source_float = is_float_type(source_type);
  if ((!destination_integer && !destination_float) ||
      (!source_integer && !source_float)) {
    return cvt_rule_violation(context,
                              "cvt requires scalar integer or floating source "
                              "and destination types.");
  }

  const auto has_exact_width = [](const OperandView* operand, ScalarType type) {
    return operand == nullptr || !operand->register_type.has_value() ||
           base::scalar_size_of(*operand->register_type) ==
               base::scalar_size_of(type);
  };
  const bool exact_destination = destination_type == ScalarType::BF16 ||
                                 destination_type == ScalarType::BF16x2 ||
                                 destination_type == ScalarType::TF32;
  const bool exact_source =
      source_type == ScalarType::BF16 || source_type == ScalarType::BF16x2;
  if ((exact_destination &&
       !has_exact_width(find_operand(operands, "dst"), destination_type)) ||
      (exact_source &&
       !has_exact_width(find_operand(operands, "src"), source_type))) {
    return cvt_rule_violation(
        context,
        "bfloat and tf32 cvt operands require an exact-width register.",
        CheckDiagnosticKind::OperandTypeMismatch);
  }

  if ((destination_type == ScalarType::F64 || source_type == ScalarType::F64) &&
      context.target.sm_version < 13) {
    return cvt_rule_violation(
        context, "cvt conversions to or from f64 require SM13 or newer.",
        CheckDiagnosticKind::UnsupportedSmVersion);
  }

  if (has_scaled != has_scale_factor) {
    return cvt_rule_violation(
        context,
        "cvt scaled::n2::ue8m0 requires exactly one scale-factor operand.");
  }

  if (has_ftz && destination_type != ScalarType::F32 &&
      source_type != ScalarType::F32) {
    return cvt_rule_violation(
        context, "cvt.ftz requires an f32 source or destination type.");
  }

  if (destination_float && has_saturate &&
      destination_type != ScalarType::F16 &&
      destination_type != ScalarType::F32 &&
      destination_type != ScalarType::F64) {
    return cvt_rule_violation(
        context,
        "cvt.sat floating destinations are limited to f16, f32, and f64.");
  }
  if (destination_integer && source_integer && has_saturate &&
      integer_range_contains(destination_type, source_type)) {
    return cvt_rule_violation(context,
                              "cvt.sat is not permitted when the integer "
                              "destination range contains the source range.");
  }

  if (destination_integer && source_integer) {
    if (rounding_mode != RoundingMode::Invalid)
      return cvt_rule_violation(
          context, "integer-to-integer cvt does not admit rounding.");
    return {};
  }
  if (destination_integer && source_float) {
    if (!is_integer_rounding(rounding_mode))
      return cvt_rule_violation(
          context, "floating-to-integer cvt requires integer rounding.");
    return {};
  }
  if (destination_float && source_integer) {
    if (!is_float_rounding(rounding_mode))
      return cvt_rule_violation(
          context, "integer-to-floating cvt requires floating rounding.");
    return {};
  }

  const int destination_rank = float_precision_rank(destination_type);
  const int source_rank = float_precision_rank(source_type);
  const bool f16_bf16_pair =
      (destination_type == ScalarType::F16 &&
       source_type == ScalarType::BF16) ||
      (destination_type == ScalarType::BF16 && source_type == ScalarType::F16);
  if (f16_bf16_pair) {
    if (rounding_mode == RoundingMode::Invalid ||
        is_float_rounding(rounding_mode)) {
      return {};
    }
    return cvt_rule_violation(
        context, "bfloat conversion pairs only admit floating rounding.");
  }
  if (destination_type == ScalarType::BF16 && source_type == ScalarType::F32) {
    if (is_float_rounding(rounding_mode))
      return {};
    return cvt_rule_violation(
        context, "f32 to bf16 conversion requires floating rounding.");
  }
  if (destination_type == ScalarType::F32 && source_type == ScalarType::BF16) {
    if (rounding_mode == RoundingMode::Invalid)
      return {};
    return cvt_rule_violation(
        context, "bf16 to f32 conversion does not admit rounding.");
  }
  if (destination_type == source_type && is_integer_rounding(rounding_mode)) {
    return {};
  }
  if (destination_rank == 0 || source_rank == 0)
    return {};
  if (destination_rank < source_rank) {
    if (!is_float_rounding(rounding_mode))
      return cvt_rule_violation(
          context, "precision-losing floating cvt requires floating rounding.");
  } else if (rounding_mode != RoundingMode::Invalid) {
    return cvt_rule_violation(
        context, "non-lossy floating cvt does not admit rounding.");
  }
  return {};
}

CheckResult check_atomic_qualifiers(
    const VariantDescriptor::AtomicAddressQualifierDescriptor& descriptor,
    const WithLocs<AtomicAddressQualifier>& qualifier,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    const Context& context) {
  const SourceRange& range = diagnostic_range(qualifier.locs, context);
  const auto written = qualifier.value;
  if (std::ranges::find(descriptor.allowed_values, written) ==
      descriptor.allowed_values.end()) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::ModifierValueDomainMismatch,
        .range = range,
        .message = "Written atomic address qualifier is not admitted by this "
                   "variant.",
    }});
  }
  const FieldView* space_field =
      find_field(fields, descriptor.state_space_field_id);
  const OperandView* address =
      find_operand(operands, descriptor.address_operand_id);
  if (!space_field || !space_field->memory_state_space || !address) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = "Atomic qualifier fields or address are missing.",
    }});
  }

  const MemoryStateSpace selected = *space_field->memory_state_space;
  MemoryStateSpace expected;
  switch (written) {
    case AtomicAddressQualifier::Generic:
      expected = MemoryStateSpace::Generic;
      break;
    case AtomicAddressQualifier::Global:
      expected = MemoryStateSpace::Global;
      break;
    case AtomicAddressQualifier::Shared:
    case AtomicAddressQualifier::SharedCta:
    case AtomicAddressQualifier::SharedCluster:
      expected = MemoryStateSpace::Shared;
      break;
    default:
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::ModifierValueDomainMismatch,
          .range = range,
          .message = "Written atomic address qualifier is invalid.",
      }});
  }
  CheckDiagnostics diagnostics;
  if (selected != expected) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::ModifierValueDomainMismatch,
        .range = range,
        .message = "Written atomic address qualifier disagrees with the "
                   "selected state-space modifier.",
    });
  }
  if (address->address_state_space &&
      *address->address_state_space != MemoryStateSpace::Global &&
      *address->address_state_space != MemoryStateSpace::Shared) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::AddressStateSpaceMismatch,
        .range = diagnostic_range(address->locations, context),
        .message = "Atomic address must refer to global or shared memory.",
    });
  } else if (address->address_state_space &&
             written != AtomicAddressQualifier::Generic &&
             *address->address_state_space != expected) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::AddressStateSpaceMismatch,
        .range = diagnostic_range(address->locations, context),
        .message = "Atomic address provenance conflicts with the written "
                   "state-space qualifier.",
    });
  }

  if (const FieldView* cache_hint = find_field(fields, "cache_hint")) {
    const bool written_hint = cache_hint->bool_value.value_or(false);
    const bool has_policy = find_operand(operands, "cache_policy") != nullptr;
    if (written_hint != has_policy) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(cache_hint->locations, context),
          .message = "Atomic cache hint requires exactly one trailing "
                     "cache-policy register.",
      });
    }
    if (written_hint && written != AtomicAddressQualifier::Generic &&
        written != AtomicAddressQualifier::Global) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::AddressStateSpaceMismatch,
          .range = range,
          .message = "Atomic cache hint requires global addressing.",
      });
    }
    if (written_hint && address->address_state_space &&
        *address->address_state_space != MemoryStateSpace::Global) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::AddressStateSpaceMismatch,
          .range = diagnostic_range(address->locations, context),
          .message = "Atomic cache hint requires a global address.",
      });
    }
  }

  PtxVersion minimum_ptx{};
  int minimum_sm = 0;
  if (written == AtomicAddressQualifier::Generic) {
    minimum_ptx = {2, 0};
    minimum_sm = 20;
  } else if (written == AtomicAddressQualifier::SharedCta) {
    minimum_ptx = {7, 8};
    minimum_sm = 30;
  } else if (written == AtomicAddressQualifier::SharedCluster) {
    minimum_ptx = {7, 8};
    minimum_sm = 90;
  }
  if (context.target.ptx_version < minimum_ptx) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = range,
        .message = "Atomic address qualifier is unavailable for the target PTX "
                   "version.",
    });
  }
  if (context.target.sm_version < minimum_sm) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedSmVersion,
        .range = range,
        .message = "Atomic address qualifier is unavailable for the target SM "
                   "version.",
    });
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_red_async_release_qualifiers(
    std::span<const FieldView> fields, const Context& context) {
  const FieldView* mmio = find_field(fields, "mmio");
  const FieldView* scope = find_field(fields, "scope");
  if (!mmio || !mmio->bool_value || !scope || !scope->memory_scope) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = "Async release reduction has missing qualifier fields.",
    }});
  }
  if (*mmio->bool_value && *scope->memory_scope != MemoryScope::Sys) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = diagnostic_range(mmio->locations, context),
        .message = "Async MMIO release reduction requires system scope.",
    }});
  }
  return {};
}

/** Gate a 32-bit bulk-store size register while retaining legacy immediates. */
CheckResult check_st_bulk_size_width(std::span<const OperandView> operands,
                                     const Context& context) {
  const OperandView* size = find_operand(operands, "size");
  if (size == nullptr)
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = "Bulk store has no size operand.",
    }});
  if (size->register_type && base::scalar_size_of(*size->register_type) == 4 &&
      context.target.ptx_version < PtxVersion{9, 0}) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = diagnostic_range(size->locations, context),
        .message = "32-bit bulk-store size requires PTX 9.0 or newer.",
    }});
  }
  return {};
}

/** Revalidate converted allocation scalars and the shared result-slot role. */
CheckResult check_tcgen_allocation_rule(TcgenAllocationAction action,
                                        std::span<const OperandView> operands,
                                        const Context& context) {
  CheckDiagnostics diagnostics;
  const auto reject = [&](const OperandView* operand, CheckDiagnosticKind kind,
                          std::string_view message) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = kind,
        .range = operand ? diagnostic_range(operand->locations, context)
                         : context.instruction_range,
        .message = std::string(message),
    });
  };
  const auto check_scalar = [&](const OperandView* operand,
                                bool is_column_count) {
    if (!operand) {
      reject(nullptr, CheckDiagnosticKind::MissingOperand,
             "Tensor Memory allocation is missing an operand.");
      return;
    }
    if (operand->actual_shape == OperandShape::Register) {
      if (operand->register_class != ResolvedRegisterClass::General ||
          operand->register_vector_width ||
          (!operand->register_type && operand->register_symbol_id) ||
          (operand->register_type &&
           *operand->register_type != ScalarType::B32 &&
           *operand->register_type != ScalarType::U32 &&
           *operand->register_type != ScalarType::S32)) {
        reject(operand, CheckDiagnosticKind::OperandTypeMismatch,
               "Tensor Memory scalar requires a general-class scalar "
               "b32/u32/s32 register.");
      }
      return;
    }
    if (operand->actual_shape != OperandShape::Immediate ||
        operand->immediate_type != ScalarType::U32 ||
        !operand->immediate_bits || !operand->integer_source_bits ||
        !operand->immediate_is_negative ||
        *operand->immediate_bits !=
            (*operand->integer_source_bits & uint64_t{0xffffffff})) {
      reject(operand, CheckDiagnosticKind::ImmediateValueMismatch,
             "Tensor Memory immediate must retain its 32-bit converted "
             "value and integer source.");
      return;
    }
    if (is_column_count && *operand->immediate_bits != 32 &&
        *operand->immediate_bits != 64 && *operand->immediate_bits != 128 &&
        *operand->immediate_bits != 256 && *operand->immediate_bits != 512) {
      reject(operand, CheckDiagnosticKind::ImmediateValueMismatch,
             "Tensor Memory column count must convert to 32, 64, 128, "
             "256, or 512.");
    }
  };

  switch (action) {
    case TcgenAllocationAction::Alloc: {
      const OperandView* slot = find_operand(operands, "dst");
      if (!slot || slot->actual_shape != OperandShape::Address ||
          (slot->address_state_space &&
           *slot->address_state_space != MemoryStateSpace::Shared)) {
        reject(slot, CheckDiagnosticKind::AddressStateSpaceMismatch,
               "Tensor Memory allocation result requires a shared-CTA "
               "slot or an unresolved generic shared-window pointer.");
      }
      check_scalar(find_operand(operands, "ncols"), true);
      break;
    }
    case TcgenAllocationAction::Dealloc:
      check_scalar(find_operand(operands, "taddr"), false);
      check_scalar(find_operand(operands, "ncols"), true);
      break;
    case TcgenAllocationAction::RelinquishAllocPermit:
      break;
    default:
      reject(nullptr, CheckDiagnosticKind::RuleViolation,
             "Tensor Memory allocation action is invalid.");
      break;
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

/** Preserve the allocation slot's scalar pointer contract in owned checking. */
CheckResult check_tcgen_allocation_result_slot(
    const WithLocs<ResolvedAddress>& slot, const Context& context) {
  const auto* register_ref = std::get_if<ResolvedRegisterRef>(&slot.value.base);
  if (!register_ref)
    return {};
  const auto type = register_ref->declared_type;
  const bool scalar_pointer_type =
      type &&
      (base::scalar_kind(*type) == base::ScalarKind::Bit ||
       base::scalar_kind(*type) == base::ScalarKind::Signed ||
       base::scalar_kind(*type) == base::ScalarKind::Unsigned) &&
      (base::scalar_size_of(*type) == 4 || base::scalar_size_of(*type) == 8);
  if (register_ref->register_class == ResolvedRegisterClass::General &&
      !register_ref->vector_width &&
      (scalar_pointer_type || (!type && !register_ref->symbol_id))) {
    return {};
  }
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::OperandTypeMismatch,
      .range = diagnostic_range(slot.locs, context),
      .message = "Tensor Memory allocation result-slot register must be a "
                 "scalar General-class b32/s32/u32/b64/s64/u64 pointer "
                 "carrier with known declaration type when bound.",
  }});
}

/** Keep commit's mbarrier pointer contract after lossy operand projection. */
CheckResult check_tcgen_commit_address(const WithLocs<ResolvedAddress>& address,
                                       const Context& context) {
  if (const auto* pointer =
          std::get_if<ResolvedRegisterRef>(&address.value.base)) {
    const auto type = pointer->declared_type;
    const bool compatible =
        type &&
        (base::scalar_kind(*type) == base::ScalarKind::Bit ||
         base::scalar_kind(*type) == base::ScalarKind::Signed ||
         base::scalar_kind(*type) == base::ScalarKind::Unsigned) &&
        (base::scalar_size_of(*type) == 4 || base::scalar_size_of(*type) == 8);
    if (pointer->register_class != ResolvedRegisterClass::General ||
        pointer->vector_width ||
        (!compatible && (type || pointer->symbol_id))) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::OperandTypeMismatch,
          .range = diagnostic_range(address.locs, context),
          .message = "TCGEN commit requires a scalar General-class 32/64-bit "
                     "integer or bit mbarrier pointer with known type when "
                     "bound.",
      }});
    }
  } else if (const auto* symbol =
                 std::get_if<ResolvedSymbolRef>(&address.value.base)) {
    if (symbol->address_state_space &&
        *symbol->address_state_space != base::DeclarationStateSpace::Shared) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::AddressStateSpaceMismatch,
          .range = diagnostic_range(address.locs, context),
          .message =
              "TCGEN commit mbarrier must reside in cluster shared memory.",
      }});
    }
  }
  return {};
}

/** Preserve register class, width, and binding metadata of a CTA mask. */
CheckResult check_tcgen_commit_mask(const WithLocs<ResolvedRegisterRef>& mask,
                                    const Context& context) {
  const auto& value = mask.value;
  const auto type = value.declared_type;
  const bool compatible =
      type &&
      (base::scalar_kind(*type) == base::ScalarKind::Bit ||
       base::scalar_kind(*type) == base::ScalarKind::Signed ||
       base::scalar_kind(*type) == base::ScalarKind::Unsigned) &&
      base::scalar_size_of(*type) == 2;
  if (value.register_class == ResolvedRegisterClass::General &&
      !value.vector_width && (compatible || (!type && !value.symbol_id))) {
    return {};
  }
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::OperandTypeMismatch,
      .range = diagnostic_range(mask.locs, context),
      .message = "TCGEN multicast mask requires a scalar General-class "
                 "b16/u16/s16 register with known type when bound.",
  }});
}

/** Decode the closed repeat domain without accepting an invalid enum tag. */
static size_t tcgen_repeat_count(TcgenRepeat repeat) noexcept {
  switch (repeat) {
    case TcgenRepeat::X1:
      return 1;
    case TcgenRepeat::X2:
      return 2;
    case TcgenRepeat::X4:
      return 4;
    case TcgenRepeat::X8:
      return 8;
    case TcgenRepeat::X16:
      return 16;
    case TcgenRepeat::X32:
      return 32;
    case TcgenRepeat::X64:
      return 64;
    case TcgenRepeat::X128:
      return 128;
  }
  return 0;
}

/** Validate transfer shape, repeat, and fragment arity after typed selection. */
CheckResult check_tcgen_transfer_rule(std::span<const FieldView> fields,
                                      std::span<const OperandView> operands,
                                      bool reduction, const Context& context) {
  const FieldView* shape = find_field(fields, "shape");
  const FieldView* num = find_field(fields, "num");
  const OperandView* r = find_operand(operands, "r");
  if (!shape || !shape->tcgen_shape || !num || !num->tcgen_repeat || !r)
    return cvt_rule_violation(context,
                              "Tensor Memory transfer metadata is missing.");
  const size_t repeat = tcgen_repeat_count(*num->tcgen_repeat);
  size_t multiplier = 0;
  size_t maximum_repeat = 128;
  switch (*shape->tcgen_shape) {
    case TcgenDataMovementShape::S32x32b:
    case TcgenDataMovementShape::S16x64b:
    case TcgenDataMovementShape::S16x32bx2:
      multiplier = 1;
      break;
    case TcgenDataMovementShape::S16x128b:
      multiplier = 2;
      maximum_repeat = 64;
      break;
    case TcgenDataMovementShape::S16x256b:
      multiplier = 4;
      maximum_repeat = 32;
      break;
    case TcgenDataMovementShape::S128x256b:
    case TcgenDataMovementShape::S4x256b:
    case TcgenDataMovementShape::S128x128b:
    case TcgenDataMovementShape::S64x128b:
    case TcgenDataMovementShape::S32x128b:
      break;
  }
  if (!multiplier || !repeat || repeat > maximum_repeat ||
      (reduction &&
       (repeat == 1 ||
        (*shape->tcgen_shape != TcgenDataMovementShape::S32x32b &&
         *shape->tcgen_shape != TcgenDataMovementShape::S16x32bx2))) ||
      r->vector_arity != multiplier * repeat) {
    return cvt_rule_violation(context,
                              "Tensor Memory transfer has an invalid shape, "
                              "repeat, or fragment cardinality.",
                              CheckDiagnosticKind::InvalidVectorOperand);
  }
  return {};
}

/** Check a 32-bit General scalar carrier without requiring standalone types. */
static bool tcgen_scalar_register32(const ResolvedRegisterRef& value) noexcept {
  return value.register_class == ResolvedRegisterClass::General &&
         !value.vector_width &&
         (!value.declared_type
              ? !value.symbol_id
              : base::scalar_size_of(*value.declared_type) == 4 &&
                    base::scalar_types_compatible(*value.declared_type,
                                                  ScalarType::B32));
}

/** Validate the bracketed 32-bit Tensor Memory address carrier. */
CheckResult check_tcgen_transfer_address(
    const WithLocs<TensorMemoryAddress>& address, const Context& context) {
  if (!address.value.bracketed)
    return cvt_rule_violation(
        context, "Tensor Memory transfer requires bracketed taddr.");
  if (const auto* register_ref =
          std::get_if<ResolvedRegisterRef>(&address.value.value)) {
    if (register_ref->register_class != ResolvedRegisterClass::General ||
        register_ref->vector_width ||
        (register_ref->symbol_id && !register_ref->declared_type) ||
        (register_ref->declared_type &&
         (!base::scalar_types_compatible(*register_ref->declared_type,
                                         ScalarType::U32) ||
          base::scalar_size_of(*register_ref->declared_type) != 4))) {
      return cvt_rule_violation(
          context,
          "Tensor Memory address requires a scalar General 32-bit register.",
          CheckDiagnosticKind::OperandTypeMismatch);
    }
  } else if (const auto* immediate =
                 std::get_if<ResolvedImmediate>(&address.value.value)) {
    if (immediate->type != ScalarType::U32 || !immediate->integer_source_bits ||
        immediate->bits !=
            (*immediate->integer_source_bits & uint64_t{0xffffffff})) {
      return cvt_rule_violation(
          context, "Tensor Memory address lost its 32-bit source conversion.",
          CheckDiagnosticKind::ImmediateValueMismatch);
    }
  }
  return {};
}

/** Keep a copy descriptor opaque while checking its register carrier. */
CheckResult check_tcgen_copy_descriptor(
    const WithLocs<ResolvedRegisterRef>& descriptor, const Context& context) {
  if (tcgen_copy_descriptor_view(descriptor.value))
    return {};
  return cvt_rule_violation(
      context,
      "Tensor Memory copy descriptor requires a scalar General-class "
      "b64/u64/s64 register with known type when bound.",
      CheckDiagnosticKind::OperandTypeMismatch);
}

/** Check one known MMA register carrier without interpreting its bits. */
static bool tcgen_mma_carrier(const ResolvedRegisterRef& value, size_t bytes,
                              bool permit_float = false) noexcept {
  if (value.register_class != ResolvedRegisterClass::General ||
      value.vector_width || (value.symbol_id && !value.declared_type))
    return false;
  if (!value.declared_type)
    return true;
  const auto kind = base::scalar_kind(*value.declared_type);
  return base::scalar_size_of(*value.declared_type) == bytes &&
         (kind == base::ScalarKind::Bit || kind == base::ScalarKind::Signed ||
          kind == base::ScalarKind::Unsigned ||
          (permit_float && kind == base::ScalarKind::Float));
}

/** Validate owned MMA source ranges against scalar or vector cardinality. */
static bool tcgen_mma_valid_source_ranges(std::span<const SourceRange> ranges,
                                          size_t expected) noexcept {
  if (ranges.size() != expected)
    return false;
  return std::ranges::all_of(ranges, [](const SourceRange& range) {
    if (range.start.line <= 0 || range.start.column <= 0 ||
        range.end.line <= 0 || range.end.column <= 0)
      return false;
    return range.end.line > range.start.line ||
           (range.end.line == range.start.line &&
            range.end.column >= range.start.column);
  });
}

/** Reject mixed omission sentinels and values outside the typed collector domain. */
static bool tcgen_collector_pair_valid(TcgenCollectorControl value) noexcept {
  if (value.buffer == TcgenCollectorBuffer::Unspecified ||
      value.operation == TcgenCollectorOp::Unspecified)
    return value.buffer == TcgenCollectorBuffer::Unspecified &&
           value.operation == TcgenCollectorOp::Unspecified;
  const bool buffer = value.buffer == TcgenCollectorBuffer::A ||
                      value.buffer == TcgenCollectorBuffer::B0 ||
                      value.buffer == TcgenCollectorBuffer::B1 ||
                      value.buffer == TcgenCollectorBuffer::B2 ||
                      value.buffer == TcgenCollectorBuffer::B3;
  const bool operation = value.operation == TcgenCollectorOp::Fill ||
                         value.operation == TcgenCollectorOp::Use ||
                         value.operation == TcgenCollectorOp::LastUse ||
                         value.operation == TcgenCollectorOp::Discard;
  return buffer && operation;
}

/** Check the selected dense MMA's owned carriers and source provenance. */
CheckResult check_tcgen_mma_sources(
    const WithLocs<TcgenCtaGroup>& group_source,
    const WithLocs<TensorMemoryAddress>& d,
    const WithLocs<TensorMemoryAddress>* a_address,
    const WithLocs<ResolvedRegisterRef>* a_shared,
    const WithLocs<ResolvedRegisterRef>& b,
    const WithLocs<ResolvedRegisterRef>& idesc,
    const WithLocs<ResolvedRegisterVector>* mask,
    const WithLocs<ResolvedPredicateSource>& enable_d,
    const WithLocs<ResolvedImmediate>* scale,
    const WithLocs<TcgenScaleVectorSize>* scale_selector,
    const WithLocs<TensorMemoryAddress>* scale_a,
    const WithLocs<TensorMemoryAddress>* scale_b, const WithLocs<bool>* ashift,
    const WithLocs<TcgenCollectorControl>* collector,
    const WithLocs<TensorMemoryAddress>* metadata,
    const WithLocs<ResolvedRegisterRef>* zero_column, bool ws, bool sparse,
    const Context& context) {
  const TcgenCtaGroup group = group_source.value;
  if (group != TcgenCtaGroup::One && group != TcgenCtaGroup::Two)
    return cvt_rule_violation(context, "Invalid TCGEN MMA CTA group.");
  if (ws && (group != TcgenCtaGroup::One || mask || scale || scale_selector ||
             scale_a || scale_b || ashift || !collector))
    return cvt_rule_violation(
        context, "WS MMA requires CTA one and excludes non-WS operands.");
  if (!ws && zero_column)
    return cvt_rule_violation(context,
                              "Non-WS MMA cannot use a zero-column operand.");
  if (sparse != (metadata != nullptr))
    return cvt_rule_violation(
        context, "Sparse MMA requires exactly one metadata address.");
  if ((a_address == nullptr) == (a_shared == nullptr))
    return cvt_rule_violation(context,
                              "TCGEN MMA requires exactly one A placement.");
  if (ashift &&
      ((ashift->value && !tcgen_mma_valid_source_ranges(ashift->locs, 1)) ||
       (!ashift->value && !ashift->locs.empty()) ||
       (ashift->value && a_shared)))
    return cvt_rule_violation(context,
                              "TCGEN MMA ashift requires Tensor Memory A and "
                              "valid source provenance.");
  if (collector) {
    const auto control = collector->value;
    const bool selected_buffer =
        ws ? (control.buffer == TcgenCollectorBuffer::B0 ||
              control.buffer == TcgenCollectorBuffer::B1 ||
              control.buffer == TcgenCollectorBuffer::B2 ||
              control.buffer == TcgenCollectorBuffer::B3)
           : control.buffer == TcgenCollectorBuffer::A;
    if (!tcgen_collector_pair_valid(control) ||
        (control.is_present() &&
         (!tcgen_mma_valid_source_ranges(collector->locs, 1) ||
          !selected_buffer)) ||
        (!control.is_present() && !collector->locs.empty()))
      return cvt_rule_violation(
          context,
          "TCGEN MMA collector value or source provenance is invalid.");
    if (ashift && ashift->value &&
        (control.operation == TcgenCollectorOp::Fill ||
         control.operation == TcgenCollectorOp::Use))
      return cvt_rule_violation(
          context, "TCGEN MMA ashift cannot pair with collector fill or use.");
  }
  if (!tcgen_mma_valid_source_ranges(group_source.locs, 1) ||
      !tcgen_mma_valid_source_ranges(d.locs, 1) ||
      !tcgen_mma_valid_source_ranges(
          a_address ? a_address->locs : a_shared->locs, 1) ||
      !tcgen_mma_valid_source_ranges(b.locs, 1) ||
      (metadata && !tcgen_mma_valid_source_ranges(metadata->locs, 1)) ||
      !tcgen_mma_valid_source_ranges(idesc.locs, 1) ||
      !tcgen_mma_valid_source_ranges(enable_d.locs, 1) ||
      (zero_column && !tcgen_mma_valid_source_ranges(zero_column->locs, 1)) ||
      (mask && !tcgen_mma_valid_source_ranges(
                   mask->locs, group == TcgenCtaGroup::One ? 4 : 8)) ||
      (scale && !tcgen_mma_valid_source_ranges(scale->locs, 1)))
    return cvt_rule_violation(
        context,
        "TCGEN MMA owned operand source ranges are incomplete or "
        "malformed.",
        CheckDiagnosticKind::RuleViolation);
  if (auto result = check_tcgen_transfer_address(d, context); !result)
    return result;
  if (metadata) {
    if (auto result = check_tcgen_transfer_address(*metadata, context); !result)
      return result;
  }
  if (a_address) {
    if (auto result = check_tcgen_transfer_address(*a_address, context);
        !result)
      return result;
  } else if (!tcgen_mma_carrier(a_shared->value, 8)) {
    return cvt_rule_violation(
        context, "TCGEN MMA shared A requires scalar General b64/u64/s64.",
        CheckDiagnosticKind::OperandTypeMismatch);
  }
  if (!tcgen_mma_carrier(b.value, 8))
    return cvt_rule_violation(
        context, "TCGEN MMA shared B requires scalar General b64/u64/s64.",
        CheckDiagnosticKind::OperandTypeMismatch);
  if (!tcgen_mma_carrier(idesc.value, 4))
    return cvt_rule_violation(
        context,
        "TCGEN MMA instruction descriptor requires scalar General "
        "b32/u32/s32.",
        CheckDiagnosticKind::OperandTypeMismatch);
  if (zero_column && !tcgen_mma_carrier(zero_column->value, 8))
    return cvt_rule_violation(
        context,
        "TCGEN WS zero-column descriptor requires scalar General "
        "b64/u64/s64.",
        CheckDiagnosticKind::OperandTypeMismatch);
  if (mask) {
    const size_t expected = group == TcgenCtaGroup::One ? 4 : 8;
    if (mask->value.elements.size() != expected)
      return cvt_rule_violation(
          context,
          "TCGEN MMA output-lane mask cardinality disagrees "
          "with its CTA group.");
    for (const auto& element : mask->value.elements) {
      if (!element || !tcgen_mma_carrier(*element, 4, true))
        return cvt_rule_violation(
            context,
            "TCGEN MMA output-lane mask requires scalar General "
            "b32/u32/s32/f32 register entries.",
            CheckDiagnosticKind::OperandTypeMismatch);
    }
  }
  if (const auto* predicate = std::get_if<ResolvedPredicate>(&enable_d.value)) {
    const auto& value = predicate->register_ref;
    if (value.register_class != ResolvedRegisterClass::Predicate ||
        value.vector_width || (value.symbol_id && !value.declared_type) ||
        (value.declared_type && *value.declared_type != ScalarType::Pred))
      return cvt_rule_violation(
          context, "TCGEN MMA enable-D requires a scalar predicate register.",
          CheckDiagnosticKind::OperandTypeMismatch);
  } else if (!std::holds_alternative<ResolvedPredicateConstant>(
                 enable_d.value)) {
    return cvt_rule_violation(
        context,
        "TCGEN MMA enable-D requires a predicate or integer truth constant.",
        CheckDiagnosticKind::OperandTypeMismatch);
  }
  if ((scale_selector != nullptr) != (scale_a != nullptr && scale_b != nullptr))
    return cvt_rule_violation(
        context, "TCGEN MMA block scaling requires both scale operands.");
  if (scale_selector) {
    if ((scale_selector->value == TcgenScaleVectorSize::Absent &&
         !scale_selector->locs.empty()) ||
        (scale_selector->value != TcgenScaleVectorSize::Absent &&
         !tcgen_mma_valid_source_ranges(scale_selector->locs, 1)))
      return cvt_rule_violation(
          context, "TCGEN MMA scale selector provenance is invalid.");
    if (auto result = check_tcgen_transfer_address(*scale_a, context); !result)
      return result;
    if (auto result = check_tcgen_transfer_address(*scale_b, context); !result)
      return result;
    if (!tcgen_mma_valid_source_ranges(scale_a->locs, 1) ||
        !tcgen_mma_valid_source_ranges(scale_b->locs, 1))
      return cvt_rule_violation(
          context, "TCGEN MMA scale address source ranges are invalid.");
  }
  if (scale) {
    const auto& value = scale->value;
    if (value.type != ScalarType::U32 || value.is_negative ||
        !value.integer_source_bits || *value.integer_source_bits > 15 ||
        value.bits != *value.integer_source_bits)
      return cvt_rule_violation(
          context, "TCGEN MMA D scale requires original integer 0..15.",
          CheckDiagnosticKind::ImmediateValueMismatch);
  }
  return {};
}

/** Preserve the public f16 check entrypoint through the shared dense rules. */
CheckResult check_tcgen_mma_f16_sources(
    const WithLocs<TcgenCtaGroup>& group_source,
    const WithLocs<TensorMemoryAddress>& d,
    const WithLocs<TensorMemoryAddress>* a_address,
    const WithLocs<ResolvedRegisterRef>* a_shared,
    const WithLocs<ResolvedRegisterRef>& b,
    const WithLocs<ResolvedRegisterRef>& idesc,
    const WithLocs<ResolvedRegisterVector>* mask,
    const WithLocs<ResolvedPredicateSource>& enable_d,
    const WithLocs<ResolvedImmediate>* scale, const Context& context) {
  return check_tcgen_mma_sources(group_source, d, a_address, a_shared, b, idesc,
                                 mask, enable_d, scale, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr, nullptr,
                                 false, false, context);
}

/** Match copy qualifiers against the selected closed shape and format sets. */
CheckResult check_tcgen_copy_rule(
    std::span<const FieldView> fields,
    std::span<const TcgenCopyShapePair> allowed_pairs,
    std::span<const uint8_t> allowed_formats, const Context& context) {
  const auto* shape = find_field(fields, "shape");
  if (!shape || !shape->tcgen_shape)
    return cvt_rule_violation(context, "Tensor Memory copy shape is missing.");
  const auto flag = [&](std::string_view name) -> std::optional<bool> {
    const auto* field = find_field(fields, name);
    return field ? field->bool_value : std::nullopt;
  };
  const auto warp_a = flag("warpx2_02_13");
  const auto warp_b = flag("warpx2_01_23");
  const auto warp_four = flag("warpx4");
  const auto dst = flag("dst_format");
  const auto src_b6 = flag("src_b6");
  const auto src_b4 = flag("src_b4");
  if (!warp_a || !warp_b || !warp_four || !dst || !src_b6 || !src_b4)
    return cvt_rule_violation(
        context, "Tensor Memory copy qualifier metadata is missing.");
  const unsigned multicast_count =
      unsigned(*warp_a) + unsigned(*warp_b) + unsigned(*warp_four);
  if (multicast_count > 1)
    return cvt_rule_violation(
        context, "Tensor Memory copy multicast qualifiers conflict.");
  const auto multicast = *warp_a      ? TcgenCopyMulticast::WarpX2_02_13
                         : *warp_b    ? TcgenCopyMulticast::WarpX2_01_23
                         : *warp_four ? TcgenCopyMulticast::WarpX4
                                      : TcgenCopyMulticast::None;
  const bool valid_shape =
      std::ranges::any_of(allowed_pairs, [&](const TcgenCopyShapePair& pair) {
        return pair.shape == *shape->tcgen_shape && pair.multicast == multicast;
      });
  const uint8_t format =
      uint8_t(*dst) | (uint8_t(*src_b6) << 1) | (uint8_t(*src_b4) << 2);
  const bool valid_format =
      std::ranges::find(allowed_formats, format) != allowed_formats.end();
  if (!valid_shape || !valid_format)
    return cvt_rule_violation(
        context,
        "Tensor Memory copy shape/multicast or paired format is invalid.");
  return {};
}

/** Check the address carrier and immediate lane alignment for shift. */
CheckResult check_tcgen_shift_address(
    const WithLocs<TensorMemoryAddress>& address, const Context& context) {
  if (auto result = check_tcgen_transfer_address(address, context); !result)
    return result;
  if (const auto* immediate =
          std::get_if<ResolvedImmediate>(&address.value.value)) {
    const uint32_t lane = uint32_t((immediate->bits >> 16) & 0xffff);
    if (lane % 32 != 0)
      return cvt_rule_violation(
          context, "Tensor Memory shift lane component must be 32-aligned.",
          CheckDiagnosticKind::ImmediateValueMismatch);
  }
  return {};
}

/** Check every source or destination fragment lane as a 32-bit scalar. */
CheckResult check_tcgen_transfer_fragment(
    const WithLocs<ResolvedRegisterVector>& fragment, const Context& context) {
  for (const auto& lane : fragment.value.elements) {
    if (!lane || !tcgen_scalar_register32(*lane))
      return cvt_rule_violation(context,
                                "Tensor Memory fragment requires scalar "
                                "General 32-bit registers without sinks.",
                                CheckDiagnosticKind::OperandTypeMismatch);
  }
  return {};
}

/** Check the reduction accumulator's 32-bit scalar carrier. */
CheckResult check_tcgen_reduction_result(
    const WithLocs<ResolvedRegisterRef>& result, const Context& context) {
  if (!tcgen_scalar_register32(result.value))
    return cvt_rule_violation(context,
                              "Tensor Memory reduction result requires a "
                              "scalar General 32-bit register.",
                              CheckDiagnosticKind::OperandTypeMismatch);
  return {};
}

/** Reject corrupted signedness provenance on a half-split offset. */
CheckResult check_tcgen_half_split_offset(
    const WithLocs<TcgenHalfSplitOffset>& offset, const Context& context) {
  if (offset.value.source_kind != TcgenIntegerSourceKind::Signed &&
      offset.value.source_kind != TcgenIntegerSourceKind::Unsigned)
    return cvt_rule_violation(
        context,
        "Tensor Memory split offset has invalid integer-source metadata.",
        CheckDiagnosticKind::ImmediateValueMismatch);
  return {};
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
  const FieldView* mmio_field =
      descriptor.mmio_field_id.empty()
          ? nullptr
          : find_field(fields, descriptor.mmio_field_id);
  const FieldView* cache_field =
      descriptor.cache_field_id.empty()
          ? nullptr
          : find_field(fields, descriptor.cache_field_id);
  const OperandView* address =
      find_operand(operands, descriptor.address_field_id);
  if (semantics_field == nullptr || scope_field == nullptr ||
      address == nullptr || !semantics_field->memory_consistency ||
      !scope_field->memory_scope ||
      (!descriptor.cache_field_id.empty() &&
       (cache_field == nullptr || !cache_field->cache_operator)) ||
      (!descriptor.mmio_field_id.empty() &&
       (mmio_field == nullptr || !mmio_field->bool_value))) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message =
            "Generated memory-consistency descriptor has missing fields.",
    }});
  }

  const MemoryConsistency semantics = *semantics_field->memory_consistency;
  const MemoryScope scope = *scope_field->memory_scope;
  const bool mmio = mmio_field != nullptr && *mmio_field->bool_value;
  const bool cached = cache_field != nullptr && *cache_field->cache_operator !=
                                                    CacheOperator::Unspecified;
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
  const FieldView* type_field = find_field(fields, descriptor.type_field_id);
  if (scope == MemoryScope::Sys && type_field != nullptr &&
      type_field->scalar_type && *type_field->scalar_type == ScalarType::B128 &&
      context.target.ptx_version < PtxVersion{8, 4}) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = diagnostic_range(type_field->locations, context),
        .message = "The .sys scope with .b128 requires PTX ISA >= 8.4.",
    });
  }
  if (scoped != (scope != MemoryScope::None)) {
    violation(scoped ? *semantics_field : *scope_field,
              scoped ? "Memory semantics requires an explicit scope."
                     : "Memory scope is only valid with relaxed, acquire, or "
                       "release semantics.");
  }
  if (cached && (semantics == MemoryConsistency::Volatile || scoped || mmio)) {
    violation(*cache_field,
              "Cache operator is not valid with volatile, ordered, or mmio "
              "memory semantics.");
  }

  std::optional<MemoryStateSpace> state_space = address->address_state_space;
  if (!descriptor.state_space_field_id.empty()) {
    const FieldView* field =
        find_field(fields, descriptor.state_space_field_id);
    if (field != nullptr && field->memory_state_space)
      state_space = *field->memory_state_space;
  }
  const bool known_global_or_shared = state_space == MemoryStateSpace::Global ||
                                      state_space == MemoryStateSpace::Shared;
  const bool volatile_local = semantics == MemoryConsistency::Volatile &&
                              state_space == MemoryStateSpace::Local;
  const bool strong = scoped || semantics == MemoryConsistency::Volatile;
  if (strong && address->address_unified) {
    violation(*semantics_field,
              "An .unified address is only valid with weak memory semantics.");
  }
  if (strong && state_space && !known_global_or_shared && !volatile_local) {
    violation(
        *semantics_field,
        "Strong memory semantics require a global or shared address space.");
  }
  if (volatile_local && context.target.ptx_version < PtxVersion{9, 1}) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = diagnostic_range(semantics_field->locations, context),
        .message = fmt::format(
            "volatile.local requires PTX ISA >= 9.1, but target PTX ISA is {}.",
            format_version(context.target.ptx_version)),
    });
  }
  if (mmio) {
    const auto mmio_semantic = std::ranges::find_if(
        descriptor.mmio_semantics, [semantics](const auto& candidate) {
          return candidate.semantics == semantics;
        });
    if (mmio_semantic == descriptor.mmio_semantics.end() ||
        scope != MemoryScope::Sys) {
      violation(*mmio_field,
                "mmio requires a descriptor-admitted semantic and .sys scope.");
    } else if (!is_available(mmio_semantic->availability, context.target)) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedAvailability,
          .range = diagnostic_range(semantics_field->locations, context),
          .message = "The selected mmio memory semantic is unavailable for the "
                     "target.",
      });
    }
    if (state_space && *state_space != MemoryStateSpace::Global) {
      violation(*mmio_field,
                "mmio requires a global address space when the address space "
                "is known.");
    }
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_unified_address_suffix(const VariantDescriptor& descriptor,
                                         std::span<const FieldView> fields,
                                         std::span<const OperandView> operands,
                                         const Context& context) {
  CheckDiagnostics diagnostics;
  for (const OperandView& operand : operands) {
    if (operand.address_unified && !descriptor.permits_unified_address) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(operand.locations, context),
          .message = "This instruction form does not admit an .unified address "
                     "suffix.",
      });
    }
    if (operand.address_unified) {
      if (context.target.ptx_version < PtxVersion{8, 0}) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
            .range = diagnostic_range(operand.locations, context),
            .message = "The .unified address suffix requires PTX ISA >= 8.0.",
        });
      }
      if (context.target.sm_version < 90) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::UnsupportedSmVersion,
            .range = diagnostic_range(operand.locations, context),
            .message = "The .unified address suffix requires SM >= 90.",
        });
      }
      std::optional<MemoryStateSpace> effective_space =
          operand.address_state_space;
      for (const FieldView& field : fields) {
        if (field.memory_state_space) {
          effective_space = *field.memory_state_space;
          break;
        }
      }
      if (effective_space && *effective_space != MemoryStateSpace::Global &&
          *effective_space != MemoryStateSpace::Generic) {
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::RuleViolation,
            .range = diagnostic_range(operand.locations, context),
            .message = "The .unified address suffix requires a global or "
                       "generic address.",
        });
      }
    }
    if (descriptor.unified_address_access ==
            VariantDescriptor::UnifiedAddressAccess::Read &&
        operand.address_declaration_is_unified &&
        *operand.address_declaration_is_unified != operand.address_unified) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(operand.locations, context),
          .message = *operand.address_declaration_is_unified
                         ? "An address of a .unified declaration requires an "
                           ".unified suffix."
                         : "An .unified address suffix requires a .unified "
                           "declaration.",
      });
    }
    if (descriptor.unified_address_access ==
            VariantDescriptor::UnifiedAddressAccess::Write &&
        operand.address_declaration_is_unified &&
        *operand.address_declaration_is_unified) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(operand.locations, context),
          .message = "A .unified declaration is read-only and cannot be a "
                     "store address.",
      });
    }
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_address_alignment(
    const AddressAlignmentConstraint& descriptor,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    const Context& context) {
  if (descriptor.address_field_ids.empty())
    return {};
  uint64_t required = descriptor.alignment;
  if (!descriptor.immediate_operand_field_id.empty()) {
    const OperandView* immediate =
        find_operand(operands, descriptor.immediate_operand_field_id);
    if (immediate == nullptr ||
        immediate->actual_shape != OperandShape::Immediate ||
        !immediate->immediate_bits) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = context.instruction_range,
          .message = "Generated address-alignment descriptor has a missing "
                     "immediate field.",
      }});
    }
    required =
        immediate->integer_source_bits.value_or(*immediate->immediate_bits);
  } else if (required == 0) {
    const FieldView* type = find_field(fields, descriptor.type_field_id);
    const FieldView* vector =
        descriptor.vector_field_id.empty()
            ? nullptr
            : find_field(fields, descriptor.vector_field_id);
    if (type == nullptr || !type->scalar_type ||
        (!descriptor.vector_field_id.empty() &&
         (vector == nullptr || !vector->vector_arity))) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = context.instruction_range,
          .message =
              "Generated address-alignment descriptor has missing fields.",
      }});
    }
    const uint64_t element_size = base::scalar_size_of(*type->scalar_type);
    const uint64_t arity =
        vector == nullptr ? 1 : vector_arity_count(*vector->vector_arity);
    if (element_size == 0 || arity == 0) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = context.instruction_range,
          .message = "Generated address-alignment descriptor has invalid type "
                     "or vector fields.",
      }});
    }
    required = element_size * arity;
  }
  if (required == 0) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message =
            "Generated address-alignment descriptor has invalid alignment.",
    }});
  }
  for (const std::string_view field_id : descriptor.address_field_ids) {
    const OperandView* address = find_operand(operands, field_id);
    if (address == nullptr) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = context.instruction_range,
          .message = "Generated address-alignment descriptor has a missing "
                     "address field.",
      }});
    }
    // Register/standalone addresses remain unknown and are intentionally valid.
    if (!address->address_alignment || *address->address_alignment == 0 ||
        *address->address_alignment >= required)
      continue;
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::AddressAlignmentMismatch,
        .range = diagnostic_range(address->locations, context),
        .message = fmt::format("Address operand '{}' is aligned to {} byte(s), "
                               "but this access requires {} byte alignment.",
                               field_id, *address->address_alignment, required),
    }});
  }
  return {};
}

CheckResult check_memory_vector(
    const VariantDescriptor::MemoryVectorDescriptor& descriptor,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    const Context& context) {
  if (descriptor.vector_field_id.empty())
    return {};

  const FieldView* type = find_field(fields, descriptor.type_field_id);
  const OperandView* vector =
      find_operand(operands, descriptor.vector_field_id);
  const OperandView* address =
      find_operand(operands, descriptor.address_field_id);
  if (type == nullptr || vector == nullptr || address == nullptr ||
      !type->scalar_type || vector->actual_shape != OperandShape::Vector) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = "Generated memory-vector descriptor has missing fields.",
    }});
  }

  const size_t payload_bits = static_cast<size_t>(vector->vector_arity) *
                              scalar_size_of(*type->scalar_type) * 8u;
  const bool modern_candidate =
      descriptor.require_modern || vector->vector_arity > 4 ||
      payload_bits > 128 || vector->vector_sink_count != 0;
  if (!modern_candidate)
    return {};

  CheckDiagnostics diagnostics;
  const SourceRange& vector_range =
      diagnostic_range(vector->locations, context);
  if (payload_bits != 256) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = vector_range,
        .message = "Modern memory vectors require an exact 256-bit payload.",
    });
  }
  const bool legal_modern_shape =
      (vector->vector_arity == 8 && scalar_size_of(*type->scalar_type) == 4) ||
      (vector->vector_arity == 4 && scalar_size_of(*type->scalar_type) == 8);
  if (!legal_modern_shape) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = vector_range,
        .message = "Modern memory vectors allow only .v8 32-bit or .v4 64-bit "
                   "elements.",
    });
  }

  std::optional<MemoryStateSpace> state_space = address->address_state_space;
  const FieldView* state_space_field = nullptr;
  if (!descriptor.state_space_field_id.empty()) {
    state_space_field = find_field(fields, descriptor.state_space_field_id);
    if (state_space_field == nullptr ||
        !state_space_field->memory_state_space) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = context.instruction_range,
          .message = "Generated memory-vector descriptor has an invalid "
                     "state-space field.",
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
        .message =
            "Modern memory vectors require a global address space when known.",
    });
  }

  const auto& availability = descriptor.availability;
  if (is_available(availability, context.target)) {
    if (diagnostics.empty())
      return {};
    return std::unexpected(std::move(diagnostics));
  }
  if (availability.any_of_count != 0) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedAvailability,
        .range = vector_range,
        .message =
            "Modern memory vectors have no matching availability clause.",
    });
    return std::unexpected(std::move(diagnostics));
  }
  if (context.target.ptx_version < availability.minimum_ptx_version) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
        .range = vector_range,
        .message = fmt::format("Modern memory vectors require PTX ISA >= {}, "
                               "but target PTX ISA is {}.",
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
      !has_enabled_family_feature(context.target.enabled_family_features,
                                  availability.required_family)) {
    diagnostics.push_back(CheckDiagnostic{
        .kind = CheckDiagnosticKind::UnsupportedTargetFamily,
        .range = vector_range,
        .message =
            fmt::format("Modern memory vectors require target family '{}'.",
                        availability.required_family),
    });
  }

  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_immediate_value(
    const VariantDescriptor::ImmediateValueDescriptor& descriptor,
    std::span<const OperandView> operands, const Context& context) {
  if (descriptor.operand_field_id.empty())
    return {};
  const OperandView* operand =
      find_operand(operands, descriptor.operand_field_id);
  if (operand == nullptr)
    return {};
  if (operand->actual_shape != OperandShape::Immediate ||
      !operand->immediate_bits) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = fmt::format("Immediate-value constraint references missing "
                               "immediate operand '{}'.",
                               descriptor.operand_field_id),
    }});
  }
  if (auto consistency = check_source_immediate_consistency(*operand, context);
      !consistency)
    return consistency;
  const auto [value, negative] = integer_constraint_value(*operand);
  if (!negative && std::ranges::find(descriptor.allowed_values, value) !=
                       descriptor.allowed_values.end()) {
    return {};
  }
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::ImmediateValueMismatch,
      .range = diagnostic_range(operand->locations, context),
      .message = fmt::format("Immediate operand '{}' has unsupported value {}.",
                             descriptor.operand_field_id, value),
  }});
}

CheckResult check_immediate_multiple_of(
    const VariantDescriptor::ImmediateMultipleOfDescriptor& descriptor,
    std::span<const OperandView> operands, const Context& context) {
  if (descriptor.operand_field_id.empty())
    return {};
  const OperandView* operand =
      find_operand(operands, descriptor.operand_field_id);
  if (operand == nullptr)
    return {};
  if (descriptor.divisor == 0) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message =
            fmt::format("Immediate-multiple constraint for '{}' has zero "
                        "divisor.",
                        descriptor.operand_field_id),
    }});
  }
  // A register operand is dynamically unknown; the generated divisibility
  // rule applies only when it resolves to an immediate.
  if (operand->actual_shape == OperandShape::Register)
    return {};
  if (operand->actual_shape != OperandShape::Immediate ||
      !operand->immediate_bits) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message =
            fmt::format("Immediate-multiple constraint references missing "
                        "immediate operand '{}'.",
                        descriptor.operand_field_id),
    }});
  }
  if (auto consistency = check_source_immediate_consistency(*operand, context);
      !consistency)
    return consistency;
  const auto [value, negative] = integer_constraint_value(*operand);
  if (!negative && value % descriptor.divisor == 0) {
    return {};
  }
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::ImmediateValueMismatch,
      .range = diagnostic_range(operand->locations, context),
      .message =
          fmt::format("Immediate operand '{}' has value {} that is not a "
                      "multiple of {}.",
                      descriptor.operand_field_id, value, descriptor.divisor),
  }});
}

CheckResult check_immediate_range(
    const VariantDescriptor::ImmediateRangeDescriptor& descriptor,
    std::span<const OperandView> operands, const Context& context) {
  if (descriptor.operand_field_id.empty())
    return {};
  const OperandView* operand =
      find_operand(operands, descriptor.operand_field_id);
  if (operand == nullptr)
    return {};
  // A register operand is dynamically unknown; the generated range applies
  // only when it resolves to an immediate.
  if (operand->actual_shape == OperandShape::Register)
    return {};
  if (operand->actual_shape != OperandShape::Immediate ||
      !operand->immediate_bits) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = fmt::format("Immediate-range constraint references missing "
                               "immediate operand '{}'.",
                               descriptor.operand_field_id),
    }});
  }
  if (auto consistency = check_source_immediate_consistency(*operand, context);
      !consistency)
    return consistency;
  const auto [value, negative] = integer_constraint_value(*operand);
  if (!negative && value >= descriptor.minimum &&
      (!descriptor.has_maximum || value <= descriptor.maximum)) {
    return {};
  }
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::ImmediateValueMismatch,
      .range = diagnostic_range(operand->locations, context),
      .message = fmt::format(
          "Immediate operand '{}' has value {} outside the supported range.",
          descriptor.operand_field_id, value),
  }});
}

/** Validate source-known createpolicy fraction and range-size values. */
CheckResult check_createpolicy_rule(std::span<const OperandView> operands,
                                    const Context& context) {
  if (const OperandView* fraction = find_operand(operands, "fraction")) {
    // A register value is dynamic; only source constants can be bounded here.
    if (fraction->actual_shape != OperandShape::Register) {
      if (fraction->actual_shape != OperandShape::Immediate ||
          fraction->immediate_type != ScalarType::F32 ||
          !fraction->immediate_bits || *fraction->immediate_bits > UINT32_MAX) {
        return std::unexpected(CheckDiagnostics{CheckDiagnostic{
            .kind = CheckDiagnosticKind::RuleViolation,
            .range = diagnostic_range(fraction->locations, context),
            .message = "createpolicy fraction must be an f32 value.",
        }});
      }
      const float value = std::bit_cast<float>(
          static_cast<uint32_t>(*fraction->immediate_bits));
      if (!std::isfinite(value) || value <= 0.0f || value > 1.0f) {
        return std::unexpected(CheckDiagnostics{CheckDiagnostic{
            .kind = CheckDiagnosticKind::ImmediateValueMismatch,
            .range = diagnostic_range(fraction->locations, context),
            .message = "createpolicy fraction must be in (0.0, 1.0].",
        }});
      }
    }
  }

  const OperandView* primary = find_operand(operands, "primary_size");
  const OperandView* total = find_operand(operands, "total_size");
  if (primary == nullptr && total == nullptr)
    return {};
  if (primary == nullptr || total == nullptr) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = "createpolicy range requires both size operands.",
    }});
  }
  for (const OperandView* size : {primary, total}) {
    if (size->actual_shape == OperandShape::Register)
      continue;
    if (size->actual_shape != OperandShape::Immediate ||
        size->immediate_type != ScalarType::U32 || !size->immediate_bits) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(size->locations, context),
          .message = fmt::format(
              "createpolicy {} must be a 32-bit register or immediate.",
              size->field_id),
      }});
    }
    if (*size->immediate_bits > UINT32_MAX) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::ImmediateValueMismatch,
          .range = diagnostic_range(size->locations, context),
          .message = fmt::format("createpolicy {} immediate exceeds 32 bits.",
                                 size->field_id),
      }});
    }
  }
  if (primary->actual_shape == OperandShape::Register ||
      total->actual_shape == OperandShape::Register)
    return {};
  if (*primary->immediate_bits <= *total->immediate_bits)
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::ImmediateValueMismatch,
      .range = diagnostic_range(primary->locations, context),
      .message = "createpolicy primary size exceeds total size.",
  }});
}

/** Apply encoded-field and source-versus-use rules to one tensor-map update. */
CheckResult check_tensor_map_replace_rule(std::span<const FieldView> fields,
                                          std::span<const OperandView> operands,
                                          const Context& context) {
  constexpr std::array field_ids{
      std::pair{std::string_view{"field_global_address"},
                TensorMapReplaceField::GlobalAddress},
      std::pair{std::string_view{"field_rank"}, TensorMapReplaceField::Rank},
      std::pair{std::string_view{"field_box_dim"},
                TensorMapReplaceField::BoxDim},
      std::pair{std::string_view{"field_global_dim"},
                TensorMapReplaceField::GlobalDim},
      std::pair{std::string_view{"field_global_stride"},
                TensorMapReplaceField::GlobalStride},
      std::pair{std::string_view{"field_element_stride"},
                TensorMapReplaceField::ElementStride},
      std::pair{std::string_view{"field_elemtype"},
                TensorMapReplaceField::Elemtype},
      std::pair{std::string_view{"field_interleave_layout"},
                TensorMapReplaceField::InterleaveLayout},
      std::pair{std::string_view{"field_swizzle_mode"},
                TensorMapReplaceField::SwizzleMode},
      std::pair{std::string_view{"field_swizzle_atomicity"},
                TensorMapReplaceField::SwizzleAtomicity},
      std::pair{std::string_view{"field_fill_mode"},
                TensorMapReplaceField::FillMode},
  };
  std::optional<TensorMapReplaceField> replacement_field;
  for (const auto& [name, field] : field_ids) {
    const FieldView* selected = find_field(fields, name);
    if (selected == nullptr)
      continue;
    if (!selected->bool_value.value_or(false) || replacement_field)
      return cvt_rule_violation(context, "Invalid tensor-map field identity.");
    replacement_field = field;
  }
  if (!replacement_field)
    return cvt_rule_violation(context, "Missing tensor-map field identity.");

  const OperandView* address = find_operand(operands, "tensor_map");
  const FieldView* space = find_field(fields, "state_space");
  if (!address || !space || !space->memory_state_space)
    return cvt_rule_violation(context, "Missing tensor-map address or space.");
  if (space->memory_state_space != MemoryStateSpace::Generic &&
      address->address_state_space &&
      *space->memory_state_space != *address->address_state_space)
    return cvt_rule_violation(
        context, "Tensor-map address and explicit state space differ.");

  const auto valid_immediate = [](const OperandView& value,
                                  ScalarType type) noexcept {
    const uint64_t mask = type == ScalarType::B32 || type == ScalarType::U32
                              ? uint64_t{0xffffffff}
                              : ~uint64_t{0};
    return value.actual_shape == OperandShape::Immediate &&
           value.immediate_type == type && value.immediate_bits &&
           value.integer_source_bits &&
           *value.immediate_bits == (*value.integer_source_bits & mask);
  };
  if (const OperandView* ordinal = find_operand(operands, "ord")) {
    if (!valid_immediate(*ordinal, ScalarType::U32) ||
        ordinal->immediate_is_negative.value_or(false) ||
        *ordinal->integer_source_bits > 4)
      return cvt_rule_violation(context, "Tensor-map ordinal must be 0..4.",
                                CheckDiagnosticKind::ImmediateValueMismatch);
  }
  const OperandView* value = find_operand(operands, "new_val");
  if (!value)
    return cvt_rule_violation(context, "Missing tensor-map replacement value.");
  const bool wide =
      *replacement_field == TensorMapReplaceField::GlobalAddress ||
      *replacement_field == TensorMapReplaceField::GlobalStride;
  const ScalarType type = wide ? ScalarType::B64 : ScalarType::B32;
  const bool field3 =
      *replacement_field == TensorMapReplaceField::Elemtype ||
      *replacement_field == TensorMapReplaceField::InterleaveLayout ||
      *replacement_field == TensorMapReplaceField::SwizzleMode ||
      *replacement_field == TensorMapReplaceField::SwizzleAtomicity ||
      *replacement_field == TensorMapReplaceField::FillMode;
  if (value->actual_shape == OperandShape::Register) {
    if (!field3)
      return {};
    return cvt_rule_violation(context,
                              "Tensor-map field code must be an immediate.");
  }
  if (!valid_immediate(*value, type))
    return cvt_rule_violation(
        context, "Tensor-map replacement immediate has invalid owned bits.");
  if (*replacement_field == TensorMapReplaceField::Rank) {
    if (*value->immediate_bits <= 4)
      return {};
    return cvt_rule_violation(context, "Encoded tensor rank must be 0..4.",
                              CheckDiagnosticKind::ImmediateValueMismatch);
  }
  if (*replacement_field == TensorMapReplaceField::GlobalAddress ||
      *replacement_field == TensorMapReplaceField::BoxDim ||
      *replacement_field == TensorMapReplaceField::GlobalDim ||
      *replacement_field == TensorMapReplaceField::GlobalStride ||
      *replacement_field == TensorMapReplaceField::ElementStride)
    return {};

  const ResolvedImmediate encoded{
      .bits = *value->immediate_bits,
      .type = type,
      .is_negative = value->immediate_is_negative.value_or(false),
      .integer_source_bits = value->integer_source_bits};
  const auto code = tensor_map_encoded_code(*replacement_field, encoded);
  if (!code)
    return cvt_rule_violation(context, "Invalid tensor-map field code.",
                              CheckDiagnosticKind::ImmediateValueMismatch);
  const auto exact_90a = context.target.identity &&
                         context.target.identity->architecture.number == 90 &&
                         context.target.identity->flavor ==
                             base::TargetFlavor::ArchitectureSpecific;
  if ((*replacement_field == TensorMapReplaceField::SwizzleAtomicity &&
       (context.target.ptx_version < PtxVersion{8, 6} || exact_90a)) ||
      (*replacement_field == TensorMapReplaceField::Elemtype &&
       code->code >= 13 &&
       (context.target.ptx_version < PtxVersion{8, 7} || exact_90a)))
    return cvt_rule_violation(
        context, "Tensor-map field code is unavailable on this target.");
  if (*replacement_field == TensorMapReplaceField::SwizzleMode &&
      code->code == 4 &&
      (context.target.ptx_version < PtxVersion{8, 8} ||
       !context.target.identity ||
       context.target.identity->architecture.number != 103 ||
       context.target.identity->flavor !=
           base::TargetFlavor::ArchitectureSpecific))
    return cvt_rule_violation(context,
                              "96B swizzle requires PTX 8.8 and sm_103a.");
  return {};
}

/** Recheck owned source provenance for the fixed tensor-map proxy-copy size. */
CheckResult check_tensor_map_cp_fenceproxy_rule(
    std::span<const OperandView> operands, const Context& context) {
  const OperandView* size = find_operand(operands, "size");
  if (!size || size->actual_shape != OperandShape::Immediate ||
      size->immediate_type != ScalarType::U32 || !size->immediate_bits ||
      !size->integer_source_bits ||
      size->immediate_is_negative.value_or(false) ||
      *size->immediate_bits != uint64_t{128} ||
      *size->integer_source_bits != uint64_t{128})
    return cvt_rule_violation(
        context, "Tensor-map proxy copy requires source-exact 128-byte size.",
        CheckDiagnosticKind::ImmediateValueMismatch);
  return {};
}

/** Check a tensor-map address without changing ordinary address semantics. */
CheckResult check_tensor_map_address_register_width(
    const WithLocs<ResolvedAddress>& address, const Context& context) {
  const auto* reg = std::get_if<ResolvedRegisterRef>(&address.value.base);
  if (!reg)
    return {};
  const bool invalid = reg->register_class != ResolvedRegisterClass::General ||
                       reg->vector_width.has_value() ||
                       (reg->symbol_id && !reg->declared_type) ||
                       (reg->declared_type &&
                        (!is_integer_type(*reg->declared_type) ||
                         (base::scalar_size_of(*reg->declared_type) != 4 &&
                          base::scalar_size_of(*reg->declared_type) != 8)));
  if (!invalid)
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::OperandTypeMismatch,
      .range = diagnostic_range(address.locs, context),
      .message =
          "Tensor-map address requires a scalar 32- or 64-bit integer/bit "
          "register.",
  }});
}

CheckResult check_tensor_read_addresses(
    const WithLocs<ResolvedTensorOperand>& tensor, const Context& context) {
  return check_tensor_map_address_register_width(
      WithLocs<ResolvedAddress>{tensor.value.tensor_map.address,
                                tensor.value.tensor_map.range},
      context);
}

CheckResult check_tensor_read_addresses(
    const WithLocs<ResolvedTensorOperand>& tensor,
    const WithLocs<ResolvedAddress>& dst, const WithLocs<ResolvedAddress>& mbar,
    const Context& context) {
  if (auto result = check_tensor_read_addresses(tensor, context); !result)
    return result;
  if (auto result = check_tensor_map_address_register_width(dst, context);
      !result)
    return result;
  return check_tensor_map_address_register_width(mbar, context);
}

CheckResult check_tensor_im2col_info(
    const WithLocs<ResolvedTensorOperand>& tensor,
    const WithLocs<ResolvedTensorIm2colInfo>& info,
    std::span<const uint16_t> maximum_values, const Context& context) {
  const auto mode = tensor.value.mode;
  const size_t rank = static_cast<size_t>(tensor.value.rank);
  const size_t expected =
      mode == TensorAccessMode::Im2col && rank >= 3 && rank <= 5 ? rank - 2
      : (mode == TensorAccessMode::Im2colW ||
         mode == TensorAccessMode::Im2colW128) &&
              rank >= 3 && rank <= 5
          ? 2
          : 0;
  if (expected == 0 || info.value.elements.size() != expected ||
      info.locs.size() != expected || maximum_values.size() != expected ||
      info.value.pack_range == SourceRange{})
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::InvalidVectorOperand,
        .range = info.value.pack_range == SourceRange{}
                     ? context.instruction_range
                     : info.value.pack_range,
        .message = "Im2col information mode, arity, or source ranges are "
                   "invalid."}});
  CheckDiagnostics diagnostics;
  for (size_t index = 0; index < expected; ++index) {
    const auto& element = info.value.elements[index];
    const auto range = info.locs[index];
    if (range == SourceRange{}) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::InvalidVectorOperand,
          .range = info.value.pack_range,
          .message = "Im2col information element has no source range."});
      continue;
    }
    if (const auto* reg = std::get_if<ResolvedRegisterRef>(&element)) {
      const bool invalid =
          reg->register_class != ResolvedRegisterClass::General ||
          reg->vector_width.has_value() ||
          (reg->symbol_id && !reg->declared_type) ||
          (reg->declared_type &&
           (!is_integer_type(*reg->declared_type) ||
            base::scalar_size_of(*reg->declared_type) != 2));
      if (invalid)
        diagnostics.push_back(CheckDiagnostic{
            .kind = CheckDiagnosticKind::OperandTypeMismatch,
            .range = range,
            .message = "Im2col information requires a scalar 16-bit "
                       "integer/bit register."});
      continue;
    }
    const auto& immediate = std::get<ResolvedImmediate>(element);
    if (immediate.type != ScalarType::U16 || !immediate.integer_source_bits ||
        immediate.bits != (*immediate.integer_source_bits & uint64_t{0xffff})) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::ImmediateValueMismatch,
          .range = range,
          .message = "Im2col information immediate lacks consistent U16 "
                     "instruction-use metadata."});
    } else if (immediate.bits > maximum_values[index]) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = range,
          .message = "Im2col information exceeds this mode's unsigned "
                     "16-bit instruction-use bound."});
    }
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

/** Preserve address-register shape and width after syntax ownership ends. */
CheckResult check_tensor_reduction_addresses(
    const WithLocs<ResolvedTensorOperand>& tensor,
    const WithLocs<ResolvedAddress>& src, const Context& context) {
  const auto map_check = check_tensor_map_address_register_width(
      WithLocs<ResolvedAddress>{tensor.value.tensor_map.address,
                                tensor.value.tensor_map.range},
      context);
  if (!map_check)
    return map_check;
  return check_tensor_map_address_register_width(src, context);
}

CheckResult check_tensor_gather_scatter_coordinates(
    const WithLocs<ResolvedTensorOperand>& tensor, const Context& context) {
  const auto& value = tensor.value;
  if ((value.mode != TensorAccessMode::TileGather4 &&
       value.mode != TensorAccessMode::TileScatter4) ||
      value.rank != TensorRank::Two || value.coordinates.elements.size() != 5 ||
      value.coordinate_ranges.size() != 5)
    return std::unexpected(CheckDiagnostics{
        CheckDiagnostic{.kind = CheckDiagnosticKind::RuleViolation,
                        .range = diagnostic_range(tensor.locs, context),
                        .message = "Gather/scatter tensor metadata requires "
                                   "rank two and five coordinates."}});
  CheckDiagnostics diagnostics;
  for (size_t index = 0; index < 5; ++index) {
    const auto& range = value.coordinate_ranges[index];
    const auto& element = value.coordinates.elements[index];
    if (range == SourceRange{}) {
      diagnostics.push_back(CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(tensor.locs, context),
          .message = "Gather/scatter coordinate has no source range."});
      continue;
    }
    const auto* reg = std::get_if<ResolvedRegisterRef>(&element);
    if (!reg)
      continue;
    if (reg->register_class != ResolvedRegisterClass::General ||
        reg->vector_width || (reg->symbol_id && !reg->declared_type) ||
        (reg->declared_type &&
         (!is_integer_type(*reg->declared_type) ||
          base::scalar_size_of(*reg->declared_type) != 4)))
      diagnostics.push_back(
          CheckDiagnostic{.kind = CheckDiagnosticKind::OperandTypeMismatch,
                          .range = range,
                          .message = "Gather/scatter coordinates require "
                                     "scalar 32-bit integer/bit registers."});
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

CheckResult check_cp_async_rule(std::span<const FieldView> fields,
                                std::span<const OperandView> operands,
                                const Context& context) {
  const OperandView* size = find_operand(operands, "cp_size");
  if (size == nullptr || size->actual_shape != OperandShape::Immediate ||
      size->immediate_type != ScalarType::U32 || !size->immediate_bits) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = context.instruction_range,
        .message = "cp.async requires a typed immediate copy size.",
    }});
  }
  const FieldView* hint_field = find_field(fields, "cache_hint");
  const bool has_hint =
      hint_field != nullptr && hint_field->bool_value.value_or(false);
  const OperandView* policy = find_operand(operands, "cache_policy");
  /** Preserve the declared integer/bit register family for cache policies. */
  const auto is_policy_type = [](std::optional<ScalarType> type) {
    return type == ScalarType::B64 || type == ScalarType::U64 ||
           type == ScalarType::S64;
  };
  /** Defer type checking only when no declaration was bound to the operand. */
  const auto unbound_unknown_type = [](const OperandView& operand) {
    return !operand.register_type && !operand.register_symbol_id;
  };
  if (policy != nullptr &&
      (!has_hint || policy->actual_shape != OperandShape::Register ||
       policy->register_class != ResolvedRegisterClass::General ||
       (!is_policy_type(policy->register_type) &&
        !unbound_unknown_type(*policy)))) {
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = diagnostic_range(policy->locations, context),
        .message = "cp.async cache policy requires an L2 cache hint and a "
                   "64-bit integer or bit register.",
    }});
  }
  const OperandView* control = find_operand(operands, "source_control");
  if (control == nullptr)
    return {};
  if (control->cp_async_cache_policy) {
    if (has_hint && policy == nullptr &&
        control->actual_shape == OperandShape::Register &&
        control->register_class == ResolvedRegisterClass::General &&
        (is_policy_type(control->register_type) ||
         unbound_unknown_type(*control)))
      return {};
    return std::unexpected(CheckDiagnostics{CheckDiagnostic{
        .kind = CheckDiagnosticKind::RuleViolation,
        .range = diagnostic_range(control->locations, context),
        .message = "cp.async fourth-operand cache policy requires an L2 cache "
                   "hint and no fifth operand.",
    }});
  }
  if (control->actual_shape == OperandShape::Immediate) {
    if (control->immediate_type != ScalarType::U32 ||
        !control->immediate_bits ||
        control->immediate_is_negative.value_or(false)) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(control->locations, context),
          .message = "cp.async source size must be a 32-bit unsigned integer.",
      }});
    }
    if (*control->immediate_bits >= *size->immediate_bits) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::ImmediateValueMismatch,
          .range = diagnostic_range(control->locations, context),
          .message = "cp.async source size must be smaller than copy size.",
      }});
    }
    return {};
  }
  if (control->actual_shape == OperandShape::Register &&
      control->register_class == ResolvedRegisterClass::General &&
      (control->register_type == ScalarType::U32 ||
       control->register_type == ScalarType::S32 ||
       control->register_type == ScalarType::B32 ||
       unbound_unknown_type(*control)))
    return {};
  if (control->actual_shape == OperandShape::Predicate) {
    if (control->register_class != ResolvedRegisterClass::Predicate ||
        (control->register_type != ScalarType::Pred &&
         !unbound_unknown_type(*control))) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::RuleViolation,
          .range = diagnostic_range(control->locations, context),
          .message =
              "cp.async ignore-source control requires a predicate register.",
      }});
    }
    if (context.target.ptx_version < PtxVersion{7, 5}) {
      return std::unexpected(CheckDiagnostics{CheckDiagnostic{
          .kind = CheckDiagnosticKind::UnsupportedPtxVersion,
          .range = diagnostic_range(control->locations, context),
          .message = "cp.async ignore-source control requires PTX 7.5.",
      }});
    }
    return {};
  }
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::RuleViolation,
      .range = diagnostic_range(control->locations, context),
      .message = "cp.async source size must be a 32-bit integer or bit "
                 "register, or an unsigned immediate.",
  }});
}

/** Check one opaque resource's owned kind and indirect carrier metadata. */
void check_texture_resource_static(const ResolvedOpaqueResourceRef& resource,
                                   CheckDiagnostics& diagnostics) {
  const auto report = [&](std::string message) {
    diagnostics.push_back({
        .kind = CheckDiagnosticKind::OperandLayoutPayloadMismatch,
        .range = resource.source_range,
        .message = std::move(message),
    });
  };
  switch (resource.expected_kind) {
    case base::OpaqueResourceKind::Texture:
    case base::OpaqueResourceKind::Sampler:
    case base::OpaqueResourceKind::Surface:
      break;
    default:
      report("Opaque resource has an invalid expected kind.");
      return;
  }
  if (const auto* direct =
          std::get_if<ResolvedOpaqueSymbolRef>(&resource.value)) {
    if (direct->kind != resource.expected_kind)
      report("Direct opaque resource kind disagrees with its use.");
    return;
  }
  const auto& indirect = std::get<ResolvedRegisterRef>(resource.value);
  if (indirect.register_class != ResolvedRegisterClass::General ||
      indirect.vector_width || !indirect.declared_type ||
      base::scalar_size_of(*indirect.declared_type) != 8 ||
      (base::scalar_kind(*indirect.declared_type) != base::ScalarKind::Bit &&
       base::scalar_kind(*indirect.declared_type) != base::ScalarKind::Signed &&
       base::scalar_kind(*indirect.declared_type) !=
           base::ScalarKind::Unsigned))
    report(
        "Indirect opaque resource requires a scalar 64-bit integer/bit "
        "carrier.");
}

/** Apply the form's canonical indirect-resource feature gate when selected. */
void check_texture_indirect_gate(const TextureInstructionDescriptor& descriptor,
                                 const ResolvedOpaqueResourceRef& resource,
                                 const Context& context,
                                 CheckDiagnostics& diagnostics) {
  if (!std::holds_alternative<ResolvedRegisterRef>(resource.value))
    return;
  if (!descriptor.indirect_availability) {
    diagnostics.push_back({
        .kind = CheckDiagnosticKind::OperandLayoutPayloadMismatch,
        .range = resource.source_range,
        .message = "Texture form lacks an indirect resource feature gate.",
    });
  } else if (!is_available(*descriptor.indirect_availability, context.target)) {
    diagnostics.push_back({
        .kind = CheckDiagnosticKind::UnsupportedAvailability,
        .range = resource.source_range,
        .message = "Indirect texture resource is unavailable for this target.",
    });
  }
}

/** Check texture payload invariants without an owning module symbol table. */
CheckResult check_texture_static_payload(
    const TextureInstructionDescriptor& descriptor,
    TextureSelectedTypes selected_types, const ResolvedTextureAccess& access,
    const ResolvedTextureResult& result, bool residency_required,
    const Context& context) {
  CheckDiagnostics diagnostics;
  const auto report = [&](SourceRange range, std::string message) {
    diagnostics.push_back(
        {.kind = CheckDiagnosticKind::OperandLayoutPayloadMismatch,
         .range = range,
         .message = std::move(message)});
  };
  const auto range = context.instruction_range;
  check_texture_resource_static(access.texture, diagnostics);
  check_texture_indirect_gate(descriptor, access.texture, context, diagnostics);
  if (access.texture.expected_kind != base::OpaqueResourceKind::Texture)
    report(access.texture.source_range,
           "Texture access requires a texture resource kind.");
  if (access.sampler) {
    check_texture_resource_static(*access.sampler, diagnostics);
    check_texture_indirect_gate(descriptor, *access.sampler, context,
                                diagnostics);
    if (access.sampler->expected_kind != base::OpaqueResourceKind::Sampler)
      report(access.sampler->source_range,
             "Texture access sampler has the wrong resource kind.");
  }
  if (!descriptor.geometry) {
    report(range, "Texture access form lacks a geometry.");
    return std::unexpected(std::move(diagnostics));
  }
  const auto geometry = *descriptor.geometry;
  if (!selected_types.result_type || !selected_types.coordinate_type ||
      result.result_type != *selected_types.result_type ||
      access.coordinate_type != *selected_types.coordinate_type)
    report(range,
           "Texture selected type modifiers disagree with owned operands.");
  const size_t standard = geometry == TextureGeometry::OneD ? 1
                          : geometry == TextureGeometry::TwoD ||
                                  geometry == TextureGeometry::ArrayOneD
                              ? 2
                              : 4;
  const bool legacy_four =
      !descriptor.component &&
      (geometry == TextureGeometry::OneD || geometry == TextureGeometry::TwoD ||
       geometry == TextureGeometry::ArrayOneD) &&
      access.coordinates.size() == 4;
  if (access.coordinates.size() != standard && !legacy_four)
    report(range, "Texture coordinate tuple arity disagrees with geometry.");
  if ((!access.bracketed && descriptor.component) ||
      (!access.coordinates_packed && geometry != TextureGeometry::OneD))
    report(range,
           "Texture coordinate source topology is invalid for this form.");
  if ((access.coordinate_type != ScalarType::F32 &&
       access.coordinate_type != ScalarType::S32) ||
      (descriptor.component && access.coordinate_type != ScalarType::F32) ||
      ((geometry == TextureGeometry::Cube ||
        geometry == TextureGeometry::ArrayCube) &&
       access.coordinate_type != ScalarType::F32) ||
      ((geometry == TextureGeometry::TwoDMultisample ||
        geometry == TextureGeometry::ArrayTwoDMultisample) &&
       access.coordinate_type != ScalarType::S32))
    report(range, "Texture spatial coordinate type is invalid.");
  const bool mixed = geometry == TextureGeometry::ArrayOneD ||
                     geometry == TextureGeometry::ArrayTwoD ||
                     geometry == TextureGeometry::ArrayCube ||
                     geometry == TextureGeometry::TwoDMultisample ||
                     geometry == TextureGeometry::ArrayTwoDMultisample;
  for (size_t i = 0; i < access.coordinates.size(); ++i) {
    const auto& lane = access.coordinates[i];
    const auto role = texture_lane_role(geometry, i, access.coordinates.size());
    if (lane.role != role) {
      report(lane.range,
             "Texture coordinate lane role disagrees with geometry.");
      continue;
    }
    const ScalarType use_type =
        role == TextureLaneRole::Spatial ||
                (role == TextureLaneRole::Ignored && !mixed)
            ? access.coordinate_type
            : ScalarType::U32;
    const auto* reg = std::get_if<ResolvedRegisterRef>(&lane.value);
    if (!reg) {
      const auto* immediate = std::get_if<ResolvedImmediate>(&lane.value);
      if (!immediate || immediate->type != use_type)
        report(lane.range,
               "Texture coordinate immediate has the wrong use type.");
      continue;
    }
    if (reg->register_class != ResolvedRegisterClass::General ||
        reg->vector_width || !reg->declared_type ||
        base::scalar_size_of(*reg->declared_type) !=
            base::scalar_size_of(use_type) ||
        (!mixed &&
         !base::scalar_types_compatible(*reg->declared_type, use_type,
                                        base::ScalarTypeSizePolicy::SameWidth)))
      report(lane.range, "Texture coordinate scalar carrier is incompatible.");
  }
  if (result.data.elements.size() != descriptor.result_arity ||
      result.data_ranges.size() != descriptor.result_arity)
    report(range, "Texture result arity or lane locations disagree with form.");
  for (size_t i = 0; i < result.data.elements.size(); ++i) {
    const auto& lane = result.data.elements[i];
    const auto* reg = lane ? &*lane : nullptr;
    const auto lane_range =
        i < result.data_ranges.size() ? result.data_ranges[i] : range;
    if (!reg || reg->register_class != ResolvedRegisterClass::General ||
        reg->vector_width || !reg->declared_type ||
        !base::scalar_types_compatible(*reg->declared_type, result.result_type,
                                       base::ScalarTypeSizePolicy::SameWidth))
      report(lane_range,
             "Texture result lane has an incompatible scalar register.");
    if (!reg)
      continue;
    for (size_t earlier = 0; earlier < i; ++earlier) {
      const auto& prior = result.data.elements[earlier];
      if (prior && ((reg->symbol_id && prior->symbol_id &&
                     reg->symbol_id == prior->symbol_id &&
                     reg->parameterized_index == prior->parameterized_index) ||
                    reg->spelling == prior->spelling)) {
        report(lane_range,
               "Texture result writes the same register more than once.");
        break;
      }
    }
  }
  if (result.residency && !descriptor.allows_residency)
    report(range, "Texture form has a forbidden residency predicate.");
  if (result.residency && result.residency->negated)
    report(result.residency_range,
           "Texture residency destination cannot be negated.");
  if (result.residency) {
    const auto& predicate = result.residency->register_ref;
    if (predicate.register_class != ResolvedRegisterClass::Predicate ||
        predicate.vector_width || !predicate.declared_type ||
        *predicate.declared_type != ScalarType::Pred)
      report(result.residency_range,
             "Texture residency destination requires a scalar predicate "
             "register.");
  }
  if (static_cast<bool>(result.residency) != residency_required)
    report(range, "Texture residency presence disagrees with selected layout.");
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  return {};
}

/** Check a query resource without assuming the module's texturing mode. */
CheckResult check_texture_query_static_payload(
    const TextureInstructionDescriptor& descriptor,
    const ResolvedTextureQueryResource& resource, const Context& context) {
  CheckDiagnostics diagnostics;
  if (!descriptor.query || descriptor.geometry || descriptor.tested_kind)
    diagnostics.push_back({
        .kind = CheckDiagnosticKind::OperandLayoutPayloadMismatch,
        .range = context.instruction_range,
        .message = "Texture query form has an invalid operation descriptor.",
    });
  if (!resource.bracketed)
    diagnostics.push_back({
        .kind = CheckDiagnosticKind::OperandLayoutPayloadMismatch,
        .range = resource.resource.source_range,
        .message = "Texture query resource must retain source brackets.",
    });
  check_texture_resource_static(resource.resource, diagnostics);
  check_texture_indirect_gate(descriptor, resource.resource, context,
                              diagnostics);
  bool kind_allowed = false;
  if (descriptor.query) {
    switch (*descriptor.query) {
      case TextureQuery::Width:
      case TextureQuery::Height:
      case TextureQuery::Depth:
      case TextureQuery::ChannelDataType:
      case TextureQuery::ChannelOrder:
      case TextureQuery::NormalizedCoords:
      case TextureQuery::ArraySize:
      case TextureQuery::NumMipmapLevels:
      case TextureQuery::NumSamples:
        kind_allowed = resource.resource.expected_kind ==
                       base::OpaqueResourceKind::Texture;
        break;
      case TextureQuery::ForceUnnormalizedCoords:
        kind_allowed = resource.resource.expected_kind ==
                       base::OpaqueResourceKind::Sampler;
        break;
      case TextureQuery::FilterMode:
      case TextureQuery::AddressMode0:
      case TextureQuery::AddressMode1:
      case TextureQuery::AddressMode2:
        kind_allowed = resource.resource.expected_kind ==
                           base::OpaqueResourceKind::Texture ||
                       resource.resource.expected_kind ==
                           base::OpaqueResourceKind::Sampler;
        break;
    }
  }
  if (!kind_allowed)
    diagnostics.push_back({
        .kind = CheckDiagnosticKind::OperandLayoutPayloadMismatch,
        .range = resource.resource.source_range,
        .message = "Texture query resource kind disagrees with its query.",
    });
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  return {};
}

}  // namespace ptx_frontend::resolved_ir::checker

namespace ptx_frontend::resolved_ir {

std::optional<TensorGatherScatterCoordinateRole>
tensor_gather_scatter_coordinate_role(const ResolvedTensorOperand& tensor,
                                      size_t index) noexcept {
  if ((tensor.mode != TensorAccessMode::TileGather4 &&
       tensor.mode != TensorAccessMode::TileScatter4) ||
      tensor.rank != TensorRank::Two ||
      tensor.coordinates.elements.size() != 5 ||
      tensor.coordinate_ranges.size() != 5 || index >= 5)
    return std::nullopt;
  return static_cast<TensorGatherScatterCoordinateRole>(index);
}

/** Validate the actual owned scalar source independently of operand views. */
namespace checker {
/** Reject a damaged written group before any generic field view loses location. */
CheckResult check_tensor_cta_group(const WithLocs<TensorCtaGroup>& group,
                                   const Context& context) {
  if (!group.locs.empty() && (group.value == TensorCtaGroup::One ||
                              group.value == TensorCtaGroup::Two))
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::RuleViolation,
      .range = diagnostic_range(group.locs, context),
      .message = "Tensor CTA group requires a located ::1 or ::2 suffix.",
  }});
}

CheckResult check_tensor_multicast_mask(const WithLocs<RegOrImm>& mask,
                                        const Context& context) {
  bool valid = !mask.locs.empty();
  if (const auto* reg = std::get_if<ResolvedRegisterRef>(&mask.value)) {
    valid = valid && reg->register_class == ResolvedRegisterClass::General &&
            !reg->vector_width && (!reg->symbol_id || reg->declared_type) &&
            (!reg->declared_type || (*reg->declared_type == ScalarType::B16 ||
                                     *reg->declared_type == ScalarType::U16 ||
                                     *reg->declared_type == ScalarType::S16));
  } else if (const auto* imm = std::get_if<ResolvedImmediate>(&mask.value)) {
    valid = valid && imm->type == ScalarType::U16 && imm->integer_source_bits &&
            imm->bits == (*imm->integer_source_bits & uint64_t{0xffff});
  } else {
    valid = false;
  }
  if (valid)
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::OperandTypeMismatch,
      .range = diagnostic_range(mask.locs, context),
      .message = "Tensor multicast mask requires a scalar 16-bit integer/bit "
                 "register or a U16-converted integer literal.",
  }});
}
/** Reject an invented or unlocated tensor cache suffix. */
CheckResult check_tensor_cache_hint(const WithLocs<bool>& hint,
                                    const Context& context) {
  if (hint.value && !hint.locs.empty())
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::RuleViolation,
      .range = diagnostic_range(hint.locs, context),
      .message = "Tensor cache hint requires a written .L2::cache_hint suffix.",
  }});
}

/** Keep the original source alternative before operand views erase detail. */
CheckResult check_tensor_cache_policy(const WithLocs<RegOrImm>& policy,
                                      const Context& context) {
  bool valid = !policy.locs.empty();
  if (const auto* reg = std::get_if<ResolvedRegisterRef>(&policy.value)) {
    valid = valid && reg->register_class == ResolvedRegisterClass::General &&
            !reg->vector_width && (!reg->symbol_id || reg->declared_type) &&
            (!reg->declared_type || *reg->declared_type == ScalarType::B64 ||
             *reg->declared_type == ScalarType::U64 ||
             *reg->declared_type == ScalarType::S64);
  } else if (const auto* immediate =
                 std::get_if<ResolvedImmediate>(&policy.value)) {
    valid = valid && immediate->type == ScalarType::B64 &&
            immediate->integer_source_bits &&
            immediate->bits == *immediate->integer_source_bits;
  } else {
    valid = false;
  }
  if (valid)
    return {};
  return std::unexpected(CheckDiagnostics{CheckDiagnostic{
      .kind = CheckDiagnosticKind::OperandTypeMismatch,
      .range = diagnostic_range(policy.locs, context),
      .message = "Tensor cache policy requires a located scalar B64/U64/S64 "
                 "register or a B64-converted integer literal with original "
                 "64-bit source bits.",
  }});
}
}  // namespace checker

std::optional<TensorIm2colInfoRole> tensor_im2col_info_role(
    const ResolvedTensorOperand& tensor, const ResolvedTensorIm2colInfo& info,
    size_t index) {
  const size_t rank = static_cast<size_t>(tensor.rank);
  if (rank < 3 || rank > 5 || index >= info.elements.size() ||
      info.pack_range == SourceRange{})
    return std::nullopt;
  if (tensor.mode == TensorAccessMode::Im2col) {
    if (info.elements.size() != rank - 2)
      return std::nullopt;
    constexpr std::array roles{TensorIm2colInfoRole::OffsetW,
                               TensorIm2colInfoRole::OffsetH,
                               TensorIm2colInfoRole::OffsetD};
    return roles[index];
  }
  if (tensor.mode == TensorAccessMode::Im2colW ||
      tensor.mode == TensorAccessMode::Im2colW128) {
    if (info.elements.size() != 2)
      return std::nullopt;
    return index == 0 ? TensorIm2colInfoRole::Halo
                      : TensorIm2colInfoRole::Offset;
  }
  return std::nullopt;
}

}  // namespace ptx_frontend::resolved_ir
