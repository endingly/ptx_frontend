"""Normalization of PTX ISA YAML into the typed code-generation model."""

from __future__ import annotations

from typing import Any

from code_gen.load_yaml import expand_value_refs
from code_gen.model import InstructionSpec, ModifierSpec, OperandSpec, VariantSpec


def normalize_operand(raw: dict[str, Any]) -> OperandSpec:
    """Normalize one operand specification."""

    raw_type = raw.get("type")
    if isinstance(raw_type, dict):
        type_expr = raw_type.get("expr")
    else:
        type_expr = raw_type

    return OperandSpec(
        name=raw["name"],
        kind=raw["kind"],
        role=raw.get("role"),
        access=raw.get("access"),
        type_expr=type_expr,
    )


def normalize_modifier(
    raw: dict[str, Any], type_sets: dict[str, list[str]]
) -> ModifierSpec:
    """Normalize one modifier and expand its type-set references."""

    return ModifierSpec(
        name=raw["name"],
        kind=raw["kind"],
        presence=raw["presence"],
        domain=raw.get("domain"),
        values=expand_value_refs(raw.get("values", ()), type_sets),
        value=raw.get("value"),
        token=raw.get("token"),
        default=raw.get("default"),
    )


def normalize_instruction_spec(spec: dict[str, Any]) -> tuple[InstructionSpec, ...]:
    """Normalize all instruction definitions in one PTX ISA YAML file."""

    type_sets = spec.get("type_sets", {})
    operand_patterns = spec.get("operand_patterns", {})
    instructions: list[InstructionSpec] = []

    for raw_instruction in spec["instructions"]:
        default_operands = raw_instruction.get("operands")
        variants: list[VariantSpec] = []

        for raw_variant in raw_instruction["variants"]:
            operands_raw = raw_variant.get("operands", default_operands)
            if isinstance(operands_raw, str):
                operands_raw = operand_patterns[operands_raw]

            variants.append(
                VariantSpec(
                    name=raw_variant["name"],
                    availability=raw_variant["availability"],
                    modifiers=tuple(
                        normalize_modifier(modifier, type_sets)
                        for modifier in raw_variant.get("modifiers", ())
                    ),
                    operands=tuple(
                        normalize_operand(operand) for operand in operands_raw
                    ),
                    rule=raw_variant.get("rule"),
                )
            )

        instructions.append(
            InstructionSpec(
                opcode=raw_instruction["opcode"],
                syntax=raw_instruction.get("syntax"),
                variants=tuple(variants),
            )
        )

    return tuple(instructions)
