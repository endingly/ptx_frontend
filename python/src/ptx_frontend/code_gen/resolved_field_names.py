"""Backend-only C++ field-name projection for the semantic resolved IR."""

from dataclasses import replace

from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_optional_value, cpp_value
from ptx_frontend.spec.model import CodegenUnit
from ptx_frontend.ir.resolved_ir import (
    ResolvedField,
    ResolvedFieldStorage,
    ResolvedInstruction,
    ResolvedVariant,
)
from ptx_frontend.code_gen.resolved_value_traits import modifier_value_cpp_expr
from ptx_frontend.base.utils import file_stem_to_pascal_case
from ptx_frontend.spec.model import ConditionCodeEffect


def field_value_cpp_type(field: ResolvedField, *, backend: CodegenUnit) -> str:
    """Return the backend C++ payload type for one semantic resolved field."""

    return cpp_value(CppDomain.RESOLVED_VALUE_CPP_TYPES, field.value_kind.value, backend=backend)


def field_cpp_type(field: ResolvedField, *, backend: CodegenUnit) -> str:
    """Return the emitted member type, including location storage when needed."""

    value_type = field_value_cpp_type(field, backend=backend)
    return value_type if field.storage is ResolvedFieldStorage.STATIC_CONSTANT else f"WithLocs<{value_type}>"


def field_cpp_constant_expr(field: ResolvedField, *, backend: CodegenUnit) -> str:
    """Return the C++ expression for a fixed semantic modifier field."""

    if field.storage is not ResolvedFieldStorage.STATIC_CONSTANT or field.constant_value is None:
        raise ValueError(f"field {field.name!r} has no fixed C++ constant")
    return modifier_value_cpp_expr(field.value_kind, field.constant_value, backend=backend)


def condition_code_cpp_value(effect: ConditionCodeEffect) -> str:
    """Return the backend C++ spelling for one semantic CC effect."""

    return "ConditionCodeEffect::" + file_stem_to_pascal_case(effect.value)


def with_cpp_backend_field_names(
    instruction: ResolvedInstruction,
    backend: CodegenUnit,
) -> ResolvedInstruction:
    """Apply backend member aliases after semantic resolved-IR construction.

    Resolved IR retains PTX field identities so semantic validation does not
    require a configured C++ backend. Generators use this projection only when
    emitting C++ identifiers and descriptor field IDs.
    """

    return replace(
        instruction,
        variants=tuple(
            _with_cpp_backend_variant_field_names(variant, backend)
            for variant in instruction.variants
        ),
    )


def _with_cpp_backend_variant_field_names(
    variant: ResolvedVariant,
    backend: CodegenUnit,
) -> ResolvedVariant:
    """Project one variant's modifier field identities into backend aliases."""

    field_ids = {
        field.name: cpp_optional_value(
            CppDomain.MODIFIER_FIELD_NAMES, field.name, backend=backend
        )
        or field.name
        for field in variant.modifier_fields
    }

    def rename(field_id: str) -> str:
        """Return the emitted C++ identifier for one semantic field identity."""

        return field_ids.get(field_id, field_id)

    layouts = tuple(
        replace(
            layout,
            bindings=tuple(
                replace(
                    binding,
                    type_expression=replace(
                        binding.type_expression,
                        modifier_field_id=(
                            rename(binding.type_expression.modifier_field_id)
                            if binding.type_expression.modifier_field_id is not None
                            else None
                        ),
                    ),
                    state_space_modifier_field_id=(
                        rename(binding.state_space_modifier_field_id)
                        if binding.state_space_modifier_field_id is not None
                        else None
                    ),
                    vector_arity_modifier_field_id=(
                        rename(binding.vector_arity_modifier_field_id)
                        if binding.vector_arity_modifier_field_id is not None
                        else None
                    ),
                )
                for binding in layout.bindings
            ),
        )
        for layout in variant.operand_layouts
    )
    return replace(
        variant,
        modifier_fields=tuple(
            replace(field, name=rename(field.name)) for field in variant.modifier_fields
        ),
        modifier_bindings=tuple(
            replace(binding, target_field_id=rename(binding.target_field_id))
            for binding in variant.modifier_bindings
        ),
        operand_layouts=layouts,
        memory_consistency=(
            replace(
                variant.memory_consistency,
                semantics_field_id=rename(variant.memory_consistency.semantics_field_id),
                scope_field_id=rename(variant.memory_consistency.scope_field_id),
                mmio_field_id=rename(variant.memory_consistency.mmio_field_id),
                cache_field_id=rename(variant.memory_consistency.cache_field_id),
                type_field_id=rename(variant.memory_consistency.type_field_id),
                state_space_field_id=(
                    rename(variant.memory_consistency.state_space_field_id)
                    if variant.memory_consistency.state_space_field_id is not None
                    else None
                ),
            )
            if variant.memory_consistency is not None
            else None
        ),
        address_alignments=tuple(
            replace(
                constraint,
                type_field_id=(
                    rename(constraint.type_field_id)
                    if constraint.type_field_id is not None
                    else None
                ),
                vector_field_id=(
                    rename(constraint.vector_field_id)
                    if constraint.vector_field_id is not None
                    else None
                ),
            )
            for constraint in variant.address_alignments
        ),
        memory_vector=(
            replace(
                variant.memory_vector,
                type_field_id=rename(variant.memory_vector.type_field_id),
                state_space_field_id=(
                    rename(variant.memory_vector.state_space_field_id)
                    if variant.memory_vector.state_space_field_id is not None
                    else None
                ),
            )
            if variant.memory_vector is not None
            else None
        ),
    )
