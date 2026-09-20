"""Shared C++ checker operand and modifier view emitters."""

from __future__ import annotations

from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_default, cpp_value
from ptx_frontend.code_gen.resolved_value_traits import modifier_default_cpp_expr, modifier_descriptor_members
from ptx_frontend.ir.resolved_ir import (
    ResolvedField, ResolvedFieldOrigin, ResolvedFieldStorage, ResolvedInstruction,
    ResolvedModifierDefault, ResolvedVariant, ResolvedValueKind,
)
from ptx_frontend.spec.model import CodegenUnit


def _cpp(backend: CodegenUnit, domain: CppDomain, value: str) -> str:
    """Map an emitted semantic operand value through this run's backend."""

    return cpp_value(domain, value, backend=backend)


def _cpp_default(backend: CodegenUnit, domain: CppDomain) -> str:
    """Return an emitted C++ default from this run's semantic domain."""

    return cpp_default(domain, backend=backend)

def emit_check_modifier_view(
    instruction: ResolvedInstruction,
    variant: ResolvedVariant,
    field: ResolvedField,
    backend: CodegenUnit,
) -> str:
    """Emit one checker field view with only the selected value engaged."""

    if field.storage is ResolvedFieldStorage.STATIC_CONSTANT:
        value_expr = f"{instruction.cpp_name}::{variant.cpp_name}::{field.name}"
        locations = "std::span<const SourceRange>{}"
    else:
        value_expr = f"selected.{field.name}.value"
        locations = f"selected.{field.name}.locs"

    members = modifier_descriptor_members(
        field.value_kind,
        value_expr,
        unselected_value_expr="std::nullopt",
        backend=backend,
    )
    return f"""              FieldView{{
                  .field_id = "{field.name}",
                  .bool_value = {members[ResolvedValueKind.BOOL]},
                  .cache_operator = {members[ResolvedValueKind.CACHE_OPERATOR]},
                  .eviction_priority = {members[ResolvedValueKind.EVICTION_PRIORITY]},
                  .prefetch_size = {members[ResolvedValueKind.PREFETCH_SIZE]},
                  .scalar_type = {members[ResolvedValueKind.SCALAR_TYPE]},
                  .comparison_operator = {members[ResolvedValueKind.COMPARISON_OPERATOR]},
                  .boolean_operator = {members[ResolvedValueKind.BOOLEAN_OPERATOR]},
                  .vector_arity = {members[ResolvedValueKind.VECTOR_ARITY]},
                  .memory_state_space = {members[ResolvedValueKind.MEMORY_STATE_SPACE]},
                  .memory_consistency = {members[ResolvedValueKind.MEMORY_CONSISTENCY]},
                  .memory_scope = {members[ResolvedValueKind.MEMORY_SCOPE]},
                  .mbarrier_phase_type = {members[ResolvedValueKind.MBARRIER_PHASE_TYPE]},
                  .mbarrier_layout = {members[ResolvedValueKind.MBARRIER_LAYOUT]},
                  .async_proxy_kind = {members[ResolvedValueKind.ASYNC_PROXY_KIND]},
                  .proxy_kind_pair = {members[ResolvedValueKind.PROXY_KIND_PAIR]},
                  .locations = {locations},
              }}"""


def _modifier_default_cpp_value(
    default: ResolvedModifierDefault,
    backend: CodegenUnit,
) -> str:
    """Return the normalized C++ expression for an optional modifier default."""

    return modifier_default_cpp_expr(
        default.value_kind,
        default.value, backend=backend,
    )


def emit_check_modifier_value_view(
    instruction: ResolvedInstruction,
    variant: ResolvedVariant,
    field: ResolvedField,
    backend: CodegenUnit,
) -> str:
    """Emit one checker view of a selected resolved modifier value."""

    value_expr = (
        f"{instruction.cpp_name}::{variant.cpp_name}::{field.name}"
        if field.storage is ResolvedFieldStorage.STATIC_CONSTANT
        else f"selected.{field.name}.value"
    )

    members = modifier_descriptor_members(
        field.value_kind,
        value_expr,
        backend=backend,
    )

    locations = (
        "std::span<const SourceRange>{}"
        if field.storage is ResolvedFieldStorage.STATIC_CONSTANT
        else f"selected.{field.name}.locs"
    )

    if field.storage is ResolvedFieldStorage.STATIC_CONSTANT:
        is_present = "true"
    else:
        is_present = f"!selected.{field.name}.locs.empty()"

        binding = next(
            binding
            for binding in variant.modifier_bindings
            if binding.target_field_id == field.name
        )

        if binding.default_value is not None:
            default_value = _modifier_default_cpp_value(binding.default_value, backend)
            is_present += f" || selected.{field.name}.value != {default_value}"

    value_kind = _cpp(backend,
        CppDomain.CHECKER_MODIFIER_VALUE_KINDS,
        field.value_kind.value,
    )

    return f"""              ModifierValueView{{
                  .kind_id = "{field.source_name}",
                  .value_kind = {value_kind},
                  .bool_value = {members[ResolvedValueKind.BOOL]},
                  .scalar_type = {members[ResolvedValueKind.SCALAR_TYPE]},
                  .rounding_mode = {members[ResolvedValueKind.ROUNDING_MODE]},
                  .comparison_operator = {members[ResolvedValueKind.COMPARISON_OPERATOR]},
                  .boolean_operator = {members[ResolvedValueKind.BOOLEAN_OPERATOR]},
                  .cache_operator = {members[ResolvedValueKind.CACHE_OPERATOR]},
                  .eviction_priority = {members[ResolvedValueKind.EVICTION_PRIORITY]},
                  .prefetch_size = {members[ResolvedValueKind.PREFETCH_SIZE]},
                  .vector_arity = {members[ResolvedValueKind.VECTOR_ARITY]},
                  .memory_state_space = {members[ResolvedValueKind.MEMORY_STATE_SPACE]},
                  .memory_consistency = {members[ResolvedValueKind.MEMORY_CONSISTENCY]},
                  .memory_scope = {members[ResolvedValueKind.MEMORY_SCOPE]},
                  .mbarrier_phase_type = {members[ResolvedValueKind.MBARRIER_PHASE_TYPE]},
                  .mbarrier_layout = {members[ResolvedValueKind.MBARRIER_LAYOUT]},
                  .async_proxy_kind = {members[ResolvedValueKind.ASYNC_PROXY_KIND]},
                  .proxy_kind_pair = {members[ResolvedValueKind.PROXY_KIND_PAIR]},
                  .is_present = {is_present},
                  .locations = {locations},
              }}"""


def emit_check_operand_view(
    field: ResolvedField, object_name: str, backend: CodegenUnit
) -> str:
    """Emit the checker view selected by the field's semantic operand kind."""

    if field.origin is not ResolvedFieldOrigin.OPERAND:
        raise ValueError(f"field {field.name!r} is not an operand field")

    if field.value_kind is ResolvedValueKind.DIRECT_CALL_TARGET:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "DirectCallTarget")},
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.INDIRECT_CALLEE:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "IndirectCallee")},
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.CALL_RETURN_PARAMETER:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "CallReturnParameter")},
                  .register_type = {object_name}.{field.name}.value.declared_type,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.CALL_ARGUMENTS:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "CallArguments")},
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.REGISTER_VECTOR:
        return f"""              [&]() -> OperandView {{
                OperandView view{{
                    .field_id = "{field.name}",
                    .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Vector")},
                    .vector_arity = {object_name}.{field.name}.value.elements.size(),
                    .locations = {object_name}.{field.name}.locs,
                }};
                size_t index = 0;
                for (const auto& element :
                     {object_name}.{field.name}.value.elements) {{
                  if (index >= view.vector_element_shapes.size() ||
                      index >= view.vector_element_types.size())
                    break;
                  if (element) {{
                    view.vector_element_shapes[index] =
                        {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Register")};
                    view.vector_element_types[index] =
                        element->declared_type.value_or({_cpp_default(backend, CppDomain.SCALAR_TYPES)});
                  }} else {{
                    ++view.vector_sink_count;
                  }}
                  ++index;
                }}
                return view;
              }}()"""
    if field.value_kind is ResolvedValueKind.TENSOR_COORDINATE:
        return f"""              [&]() -> OperandView {{
                OperandView view{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Vector")},
                  .vector_arity = {object_name}.{field.name}.value.elements.size(),
                  .locations = {object_name}.{field.name}.locs,
                }};
                size_t index = 0;
                for (const auto& element :
                     {object_name}.{field.name}.value.elements) {{
                  if (index >= view.vector_element_shapes.size() ||
                      index >= view.vector_element_types.size())
                    break;
                  if (const auto* register_ref =
                          std::get_if<ResolvedRegisterRef>(&element)) {{
                    view.vector_element_shapes[index] =
                        {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Register")};
                    view.vector_element_types[index] =
                        register_ref->declared_type.value_or({_cpp_default(backend, CppDomain.SCALAR_TYPES)});
                  }} else {{
                    const auto& immediate = std::get<ResolvedImmediate>(element);
                    view.vector_element_shapes[index] =
                        {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Immediate")};
                    view.vector_element_types[index] = immediate.type;
                  }}
                  ++index;
                }}
                return view;
              }}()"""
    if field.value_kind is ResolvedValueKind.VECTOR_REGISTER:
        return f"""              [&]() -> OperandView {{
                const auto& register_ref =
                    {object_name}.{field.name}.value.register_ref;
                OperandView view{{
                    .field_id = "{field.name}",
                    .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Vector")},
                    .vector_arity = static_cast<size_t>(
                        register_ref.vector_width.value_or(0)),
                    .locations = {object_name}.{field.name}.locs,
                }};
                for (size_t index = 0;
                     index < view.vector_arity &&
                     index < view.vector_element_types.size(); ++index)
                  view.vector_element_types[index] =
                      register_ref.declared_type.value_or({_cpp_default(backend, CppDomain.SCALAR_TYPES)});
                return view;
              }}()"""
    if field.value_kind is ResolvedValueKind.VECTOR_SPECIAL_REGISTER:
        return f"""              [&]() -> OperandView {{
                const auto& special_register = {object_name}.{field.name}.value;
                const auto info = base::metadata(special_register.id);
                OperandView view{{
                    .field_id = "{field.name}",
                    .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Vector")},
                    .special_register_type = info.element_type,
                    .special_register_id = special_register.id,
                    .vector_arity = static_cast<size_t>(info.vector_width),
                    .value_availability = special_register_availability(info),
                    .value_name = special_register.spelling,
                    .locations = {object_name}.{field.name}.locs,
                }};
                for (size_t index = 0;
                     index < view.vector_arity &&
                     index < view.vector_element_types.size(); ++index)
                  view.vector_element_types[index] = info.element_type;
                return view;
              }}()"""
    if field.value_kind is ResolvedValueKind.REGISTER:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Register")},
                  .immediate_type = std::nullopt,
                  .register_type = {object_name}.{field.name}.value.declared_type,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.MBARRIER_STATE_TOKEN:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Register")},
                  .immediate_type = std::nullopt,
                  .register_type = {object_name}.{field.name}.value.register_ref
                      ? {object_name}.{field.name}.value.register_ref->declared_type
                      : std::nullopt,
                  .is_sink = !{object_name}.{field.name}.value.register_ref,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.REGISTER_OR_SINK:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Register")},
                  .immediate_type = std::nullopt,
                  .register_type = {object_name}.{field.name}.value.register_ref
                      ? {object_name}.{field.name}.value.register_ref->declared_type
                      : std::nullopt,
                  .is_sink = !{object_name}.{field.name}.value.register_ref,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.SHFL_DESTINATION:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "ShflDestination")},
                  .immediate_type = std::nullopt,
                  .register_type = {object_name}.{field.name}.value.data
                      ? {object_name}.{field.name}.value.data->value.declared_type
                      : std::nullopt,
                  .paired_destination_data_present = static_cast<bool>({object_name}.{field.name}.value.data),
                  .paired_destination_predicate_present = static_cast<bool>({object_name}.{field.name}.value.predicate),
                  .paired_destination_predicate_type = {object_name}.{field.name}.value.predicate
                      ? {object_name}.{field.name}.value.predicate->value.register_ref.declared_type
                      : std::nullopt,
                  .destination_predicate_negated = {object_name}.{field.name}.value.predicate && {object_name}.{field.name}.value.predicate->value.negated,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.PREDICATE_PAIR:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "PredicatePair")},
                  .immediate_type = std::nullopt,
                  .predicate_pair_types = {{{object_name}.{field.name}.value.first.register_ref.declared_type.value_or({_cpp_default(backend, CppDomain.SCALAR_TYPES)}), {object_name}.{field.name}.value.second.register_ref.declared_type.value_or({_cpp_default(backend, CppDomain.SCALAR_TYPES)})}},
                  .destination_predicate_negated = {object_name}.{field.name}.value.first.negated || {object_name}.{field.name}.value.second.negated,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.PREDICATE_PAIR_OR_SINK:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "PredicatePair")},
                  .immediate_type = std::nullopt,
                  .predicate_pair_has_destination = static_cast<bool>({object_name}.{field.name}.value.first) || static_cast<bool>({object_name}.{field.name}.value.second),
                  .predicate_pair_types = {{{object_name}.{field.name}.value.first ? {object_name}.{field.name}.value.first->register_ref.declared_type.value_or({_cpp_default(backend, CppDomain.SCALAR_TYPES)}) : {_cpp_default(backend, CppDomain.SCALAR_TYPES)}, {object_name}.{field.name}.value.second ? {object_name}.{field.name}.value.second->register_ref.declared_type.value_or({_cpp_default(backend, CppDomain.SCALAR_TYPES)}) : {_cpp_default(backend, CppDomain.SCALAR_TYPES)}}},
                  .destination_predicate_negated = ({object_name}.{field.name}.value.first && {object_name}.{field.name}.value.first->negated) || ({object_name}.{field.name}.value.second && {object_name}.{field.name}.value.second->negated),
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.IMMEDIATE:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Immediate")},
                  .immediate_type = {object_name}.{field.name}.value.type,
                  .immediate_bits = {object_name}.{field.name}.value.bits,
                  .immediate_is_negative = {object_name}.{field.name}.value.is_negative,
                  .register_type = std::nullopt,
                  .locations = {object_name}.{field.name}.locs,
                  .integer_source_bits = {object_name}.{field.name}.value.integer_source_bits,
              }}"""
    if field.value_kind is ResolvedValueKind.PREDICATE:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Predicate")},
                  .immediate_type = std::nullopt,
                  .register_type = {object_name}.{field.name}.value.register_ref.declared_type,
                  .destination_predicate_negated = {object_name}.{field.name}.value.negated,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.PREDICATE_OR_SINK:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Predicate")},
                  .immediate_type = std::nullopt,
                  .register_type = {object_name}.{field.name}.value.predicate ? {object_name}.{field.name}.value.predicate->register_ref.declared_type : std::nullopt,
                  .is_sink = !{object_name}.{field.name}.value.predicate,
                  .destination_predicate_negated = {object_name}.{field.name}.value.predicate && {object_name}.{field.name}.value.predicate->negated,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.PREDICATE_SOURCE:
        return f"""              [&]() -> OperandView {{
                const auto& source = {object_name}.{field.name}.value;
                if (const auto* special =
                        std::get_if<ResolvedPredicateSpecialRegister>(&source)) {{
                  const auto info = base::metadata(special->register_ref.id);
                  return OperandView{{
                      .field_id = "{field.name}",
                      .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "SpecialRegister")},
                      .special_register_type = info.element_type,
                      .special_register_id = special->register_ref.id,
                      .value_availability = special_register_availability(info),
                      .value_name = special->register_ref.spelling,
                      .locations = {object_name}.{field.name}.locs,
                  }};
                }}
                if (std::holds_alternative<ResolvedPredicateConstant>(source)) {{
                  return OperandView{{
                      .field_id = "{field.name}",
                      .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Immediate")},
                      .immediate_type = {_cpp(backend, CppDomain.SCALAR_TYPES, "pred")},
                      .locations = {object_name}.{field.name}.locs,
                  }};
                }}
                const auto& predicate = std::get<ResolvedPredicate>(source);
                return OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Predicate")},
                  .immediate_type = std::nullopt,
                  .register_type = predicate.register_ref.declared_type,
                  .locations = {object_name}.{field.name}.locs,
                }};
              }}()"""
    if field.value_kind is ResolvedValueKind.BRANCH_TARGET:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "BranchTarget")},
                  .immediate_type = std::nullopt,
                  .register_type = std::nullopt,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.BRANCH_TARGET_SET:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "BranchTargetSet")},
                  .immediate_type = std::nullopt,
                  .register_type = std::nullopt,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.SPECIAL_REGISTER:
        return f"""              [&]() -> OperandView {{
                const auto info = base::metadata(
                    {object_name}.{field.name}.value.id);
                return OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "SpecialRegister")},
                  .immediate_type = std::nullopt,
                  .register_type = std::nullopt,
                  .special_register_type = info.element_type,
                  .special_register_id = {object_name}.{field.name}.value.id,
                  .value_availability = special_register_availability(info),
                  .value_name = {object_name}.{field.name}.value.spelling,
                  .locations = {object_name}.{field.name}.locs,
                }};
              }}()"""
    if field.value_kind is ResolvedValueKind.SYMBOL:
        return f"""              OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Symbol")},
                  .immediate_type = std::nullopt,
                  .register_type = std::nullopt,
                  .value_availability = {object_name}.{field.name}.value.address_availability,
                  .value_name = {object_name}.{field.name}.value.spelling,
                  .locations = {object_name}.{field.name}.locs,
              }}"""
    if field.value_kind is ResolvedValueKind.ADDRESS:
        return f"""              [&]() -> OperandView {{
                const auto* symbol = std::get_if<ResolvedSymbolRef>(
                    &{object_name}.{field.name}.value.base);
                std::optional<MemoryStateSpace> effective_state_space;
                ParameterDirection parameter_direction = {_cpp_default(backend, CppDomain.PARAMETER_DIRECTIONS)};
                if (symbol != nullptr && symbol->address_state_space) {{
                  // Preserve the declaration-derived effective space. In
                  // particular, device parameters may produce local rather
                  // than declaration-space addresses in other instructions.
                  switch (*symbol->address_state_space) {{
                    case syntax_ast::AstStateSpace::Global:
                      effective_state_space = {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "global")};
                      break;
                    case syntax_ast::AstStateSpace::Shared:
                      effective_state_space = {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "shared")};
                      break;
                    case syntax_ast::AstStateSpace::Local:
                      effective_state_space = {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "local")};
                      break;
                    case syntax_ast::AstStateSpace::Parameter:
                      effective_state_space = {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "param")};
                      break;
                    case syntax_ast::AstStateSpace::Constant:
                      effective_state_space = {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "const")};
                      break;
                    case syntax_ast::AstStateSpace::Register:
                      break;
                  }}
                }}
                if (symbol != nullptr && symbol->declaration_kind) {{
                  if (*symbol->declaration_kind ==
                      binding::SymbolKind::InputParameter) {{
                    parameter_direction = {_cpp(backend, CppDomain.PARAMETER_DIRECTIONS, "input")};
                  }} else if (*symbol->declaration_kind ==
                             binding::SymbolKind::ReturnParameter) {{
                    parameter_direction = {_cpp(backend, CppDomain.PARAMETER_DIRECTIONS, "return")};
                  }} else if (*symbol->declaration_kind ==
                             binding::SymbolKind::CallParameter) {{
                    parameter_direction = ParameterDirection::CallArgument;
                  }}
                }}
                const std::optional<bool> declaration_is_unified =
                    symbol == nullptr ? std::nullopt
                                      : symbol->declaration_is_unified;
                std::optional<uint64_t> address_alignment;
                const auto low_bit = [](uint64_t value) {{
                  return value == 0 ? uint64_t{{0}} : value & (~value + 1);
                }};
                if (symbol != nullptr) {{
                  address_alignment = symbol->address_alignment;
                }} else if (const auto* immediate = std::get_if<ResolvedImmediate>(
                               &{object_name}.{field.name}.value.base)) {{
                  address_alignment = low_bit(immediate->bits);
                }}
                if (address_alignment && {object_name}.{field.name}.value.offset) {{
                  const uint64_t offset_alignment = low_bit(
                      {object_name}.{field.name}.value.offset->value.bits);
                  if (offset_alignment != 0 &&
                      (*address_alignment == 0 || offset_alignment < *address_alignment))
                    address_alignment = offset_alignment;
                }}
                // Register, immediate, and unresolved standalone address
                // bases remain unknown; spelling is not semantic evidence.
                return OperandView{{
                  .field_id = "{field.name}",
                  .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Address")},
                  .immediate_type = std::nullopt,
                  .register_type = std::nullopt,
                  .address_state_space = effective_state_space,
                  .address_alignment = address_alignment,
                  .address_unified = {object_name}.{field.name}.value.unified,
                  .address_declaration_is_unified = declaration_is_unified,
                  .enclosing_function_kind =
                      {object_name}.{field.name}.value.enclosing_function_kind,
                  .parameter_direction = parameter_direction,
                  .parameter_qualifier =
                      {object_name}.{field.name}.value.parameter_qualifier,
                  .locations = {object_name}.{field.name}.locs,
                }};
              }}()"""
    if field.value_kind is ResolvedValueKind.REG_OR_IMM:
        return f"""              [&]() -> OperandView {{
                if (const auto* immediate =
                        std::get_if<ResolvedImmediate>(&{object_name}.{field.name}.value)) {{
                  return OperandView{{
                      .field_id = "{field.name}",
                      .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Immediate")},
                      .immediate_type = immediate->type,
                      .immediate_bits = immediate->bits,
                      .immediate_is_negative = immediate->is_negative,
                      .register_type = std::nullopt,
                      .locations = {object_name}.{field.name}.locs,
                      .integer_source_bits = immediate->integer_source_bits,
                  }};
                }}
                const auto& register_ref =
                    std::get<ResolvedRegisterRef>({object_name}.{field.name}.value);
                return OperandView{{
                    .field_id = "{field.name}",
                    .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Register")},
                    .immediate_type = std::nullopt,
                    .register_type = register_ref.declared_type,
                    .locations = {object_name}.{field.name}.locs,
                }};
              }}()"""
    if field.value_kind is ResolvedValueKind.MOV_SOURCE:
        return f"""              [&]() -> OperandView {{
                const auto state_space_from_symbol =
                    [](const ResolvedSymbolRef* symbol)
                        -> std::optional<MemoryStateSpace> {{
                  if (symbol == nullptr || !symbol->address_state_space)
                    return std::nullopt;
                  switch (*symbol->address_state_space) {{
                    case syntax_ast::AstStateSpace::Global:
                      return {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "global")};
                    case syntax_ast::AstStateSpace::Shared:
                      return {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "shared")};
                    case syntax_ast::AstStateSpace::Local:
                      return {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "local")};
                    case syntax_ast::AstStateSpace::Parameter:
                      return {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "param")};
                    case syntax_ast::AstStateSpace::Constant:
                      return {_cpp(backend, CppDomain.MEMORY_STATE_SPACES, "const")};
                    case syntax_ast::AstStateSpace::Register:
                      return std::nullopt;
                  }}
                  return std::nullopt;
                }};
                if (const auto* immediate =
                        std::get_if<ResolvedImmediate>(&{object_name}.{field.name}.value)) {{
                  return OperandView{{
                      .field_id = "{field.name}",
                      .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Immediate")},
                      .immediate_type = immediate->type,
                      .immediate_bits = immediate->bits,
                      .immediate_is_negative = immediate->is_negative,
                      .register_type = std::nullopt,
                      .locations = {object_name}.{field.name}.locs,
                      .integer_source_bits = immediate->integer_source_bits,
                  }};
                }}
                if (const auto* register_ref =
                        std::get_if<ResolvedRegisterRef>(&{object_name}.{field.name}.value)) {{
                  return OperandView{{
                      .field_id = "{field.name}",
                      .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Register")},
                      .immediate_type = std::nullopt,
                      .register_type = register_ref->declared_type,
                      .locations = {object_name}.{field.name}.locs,
                  }};
                }}
                if (const auto* special_register =
                        std::get_if<ResolvedSpecialRegisterRef>(
                            &{object_name}.{field.name}.value)) {{
                  const auto info =
                      base::metadata(special_register->id);
                  return OperandView{{
                      .field_id = "{field.name}",
                      .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "SpecialRegister")},
                      .immediate_type = std::nullopt,
                      .register_type = std::nullopt,
                      .special_register_type = info.element_type,
                      .special_register_id = special_register->id,
                      .value_availability = special_register_availability(info),
                      .value_name = special_register->spelling,
                      .locations = {object_name}.{field.name}.locs,
                  }};
                }}
                if (const auto* function = std::get_if<ResolvedFunctionRef>(
                        &{object_name}.{field.name}.value)) {{
                  return OperandView{{
                      .field_id = "{field.name}",
                      .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Symbol")},
                      .immediate_type = std::nullopt,
                      .register_type = std::nullopt,
                      .value_availability = function->address_availability,
                      .value_name = function->spelling,
                      .locations = {object_name}.{field.name}.locs,
                  }};
                }}
                if (const auto* symbol = std::get_if<ResolvedSymbolRef>(
                        &{object_name}.{field.name}.value)) {{
                  return OperandView{{
                      .field_id = "{field.name}",
                      .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Symbol")},
                      .immediate_type = std::nullopt,
                      .register_type = std::nullopt,
                      .address_state_space = state_space_from_symbol(symbol),
                      .value_availability = symbol->address_availability,
                      .value_name = symbol->spelling,
                      .locations = {object_name}.{field.name}.locs,
                  }};
                }}
                // An offset address keeps its value requirements on the
                // symbol base, so expose those requirements through the same
                // operand view used for a direct symbol source.
                const auto& address =
                    std::get<ResolvedAddress>({object_name}.{field.name}.value);
                const auto* symbol =
                    std::get_if<ResolvedSymbolRef>(&address.base);
                return OperandView{{
                    .field_id = "{field.name}",
                    .actual_shape = {_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, "Address")},
                    .immediate_type = std::nullopt,
                    .register_type = std::nullopt,
                    .address_state_space = state_space_from_symbol(symbol),
                    .value_availability =
                        symbol == nullptr ? std::nullopt
                                          : symbol->address_availability,
                    .value_name =
                        symbol == nullptr ? std::string_view{{}}
                                          : std::string_view{{symbol->spelling}},
                    .locations = {object_name}.{field.name}.locs,
                }};
              }}()"""
    raise ValueError(
        f"operand field {field.name!r}: unsupported checker view value kind "
        f"{field.value_kind.value!r}"
    )
