from typing import Any
from ptx_frontend.spec.model import (
    ConditionCodeEffect,
    InstructionSpec,
    SemanticRule,
    VariantSpec,
)
from .constraints import (
    _normalize_operand_type_compatibilities,
    _normalize_memory_consistency_constraint,
    _normalize_unified_address_access,
    _normalize_address_alignment_constraints,
    _normalize_memory_vector_constraint,
    _normalize_immediate_value_constraint,
    _normalize_immediate_range_constraints,
    _normalize_immediate_multiple_of_constraint,
)
from .availability import normalize_availability
from .layout import normalize_operand_layouts
from .modifiers import (
    normalize_modifier,
    normalize_modifier_order_aliases,
)
from .validation import (
    _validate_modifier_state_space_expressions,
    _validate_modifier_type_expressions,
)


def normalize_instruction_spec(spec: dict[str, Any]) -> tuple[InstructionSpec, ...]:
    """Normalize all instruction definitions in one PTX ISA YAML file."""

    source_category = spec.get("category")
    codegen_category = spec.get("codegen_category")
    if not isinstance(source_category, str):
        raise ValueError("PTX spec file must define top-level category")
    if not isinstance(codegen_category, str):
        raise ValueError("PTX spec file must define top-level codegen_category")

    type_sets = spec.get("type_sets", {})
    value_sets = spec.get("value_sets", {})
    duplicate_set_names = set(type_sets) & set(value_sets)
    if duplicate_set_names:
        raise ValueError(
            "type_sets and value_sets define the same names: "
            f"{sorted(duplicate_set_names)}"
        )
    reusable_value_sets = {**type_sets, **value_sets}
    operand_patterns = spec.get("operand_patterns", {})
    instructions: list[InstructionSpec] = []

    for raw_instruction in spec["instructions"]:
        default_operands = raw_instruction.get("operands")
        variants: list[VariantSpec] = []

        for raw_variant in raw_instruction["variants"]:
            modifiers = tuple(
                normalize_modifier(modifier, reusable_value_sets)
                for modifier in raw_variant.get("modifiers", ())
            )
            modifier_order_aliases = normalize_modifier_order_aliases(
                raw_variant, modifiers
            )
            operand_layouts = normalize_operand_layouts(
                raw_variant, default_operands, operand_patterns
            )
            _validate_modifier_type_expressions(modifiers, operand_layouts)
            _validate_modifier_state_space_expressions(modifiers, operand_layouts)
            variants.append(
                VariantSpec(
                    name=raw_variant["name"],
                    condition_code_effect=ConditionCodeEffect(
                        raw_variant.get("condition_code_effect", "none")
                    ),
                    availability=normalize_availability(raw_variant["availability"]),
                    modifiers=modifiers,
                    operand_layouts=operand_layouts,
                    modifier_order_aliases=modifier_order_aliases,
                    rule=_normalize_semantic_rule(raw_variant.get("rule")),
                    operand_type_compatibilities=(
                        _normalize_operand_type_compatibilities(
                            raw_variant, operand_layouts
                        )
                    ),
                    memory_consistency=_normalize_memory_consistency_constraint(
                        raw_variant, modifiers, operand_layouts
                    ),
                    permits_unified_address=bool(
                        raw_variant.get("permits_unified_address", False)
                    ),
                    unified_address_access=_normalize_unified_address_access(
                        raw_variant
                    ),
                    address_alignments=_normalize_address_alignment_constraints(
                        raw_variant, modifiers, operand_layouts
                    ),
                    memory_vector=_normalize_memory_vector_constraint(
                        raw_variant, modifiers, operand_layouts
                    ),
                    immediate_value=_normalize_immediate_value_constraint(
                        raw_variant, operand_layouts
                    ),
                    immediate_ranges=_normalize_immediate_range_constraints(
                        raw_variant, operand_layouts
                    ),
                    immediate_multiple_of=_normalize_immediate_multiple_of_constraint(
                        raw_variant, operand_layouts
                    ),
                )
            )

        instructions.append(
            InstructionSpec(
                opcode=raw_instruction["opcode"],
                variants=tuple(variants),
                syntax_forms=(
                    (raw_instruction["syntax"],) if "syntax" in raw_instruction else ()
                ),
                source_categories=(source_category,),
                codegen_category=codegen_category,
            )
        )

    return tuple(instructions)


def _normalize_semantic_rule(raw_rule: object) -> SemanticRule | None:
    """Convert an optional external rule spelling into its closed identity."""

    if raw_rule is None:
        return None
    if not isinstance(raw_rule, str):
        raise ValueError("semantic rule must be a string")
    try:
        return SemanticRule(raw_rule)
    except ValueError as error:
        raise ValueError(f"unknown semantic rule {raw_rule!r}") from error
