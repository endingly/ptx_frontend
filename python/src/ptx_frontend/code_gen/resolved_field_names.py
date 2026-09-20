"""Backend-only C++ field-name projection for the semantic resolved IR."""

from dataclasses import replace

from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_optional_value
from ptx_frontend.ir.resolved_ir import (
    ResolvedInstruction,
    ResolvedVariant,
)


def with_cpp_backend_field_names(
    instruction: ResolvedInstruction,
) -> ResolvedInstruction:
    """Apply backend member aliases after semantic resolved-IR construction.

    Resolved IR retains PTX field identities so semantic validation does not
    require a configured C++ backend. Generators use this projection only when
    emitting C++ identifiers and descriptor field IDs.
    """

    return replace(
        instruction,
        variants=tuple(
            _with_cpp_backend_variant_field_names(variant)
            for variant in instruction.variants
        ),
    )


def _with_cpp_backend_variant_field_names(
    variant: ResolvedVariant,
) -> ResolvedVariant:
    """Project one variant's modifier field identities into backend aliases."""

    field_ids = {
        field.name: cpp_optional_value(CppDomain.MODIFIER_FIELD_NAMES, field.name)
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
