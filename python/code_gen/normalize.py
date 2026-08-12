"""Normalization of PTX ISA YAML into the typed code-generation model."""

from __future__ import annotations

from typing import Any

from code_gen.load_yaml import expand_value_refs
from code_gen.model import (
    InstructionSpec,
    ModifierSpec,
    OperandLayoutSpec,
    OperandSpec,
    VariantSpec,
)


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


def normalize_operand_layouts(
    raw_variant: dict[str, Any],
    default_operands: Any,
    operand_patterns: dict[str, Any],
) -> tuple[OperandLayoutSpec, ...]:
    """Normalize explicit layouts, or lift the legacy operand list to ``default``."""

    raw_layouts = raw_variant.get("operand_layouts")
    if raw_layouts is None:
        operands = _resolve_operands(
            raw_variant.get("operands", default_operands), operand_patterns
        )
        return (OperandLayoutSpec(name="default", operands=operands),)

    if "operands" in raw_variant:
        raise ValueError(
            f"variant {raw_variant['name']!r} cannot define both operands and "
            "operand_layouts"
        )

    layouts: list[OperandLayoutSpec] = []
    names: set[str] = set()
    for raw_layout in raw_layouts:
        name = raw_layout["name"]
        if name in names:
            raise ValueError(
                f"variant {raw_variant['name']!r} has duplicate operand layout "
                f"name {name!r}"
            )
        names.add(name)
        layouts.append(
            OperandLayoutSpec(
                name=name,
                operands=_resolve_operands(raw_layout["operands"], operand_patterns),
            )
        )
    if not layouts:
        raise ValueError(f"variant {raw_variant['name']!r} has no operand layouts")
    return tuple(layouts)


def _resolve_operands(
    raw_operands: Any, operand_patterns: dict[str, Any]
) -> tuple[OperandSpec, ...]:
    if raw_operands is None:
        raise ValueError("variant has neither operands nor inherited instruction operands")
    if isinstance(raw_operands, str):
        raw_operands = operand_patterns[raw_operands]
    return tuple(normalize_operand(operand) for operand in raw_operands)


def normalize_instruction_spec(spec: dict[str, Any]) -> tuple[InstructionSpec, ...]:
    """Normalize all instruction definitions in one PTX ISA YAML file."""

    type_sets = spec.get("type_sets", {})
    operand_patterns = spec.get("operand_patterns", {})
    instructions: list[InstructionSpec] = []

    for raw_instruction in spec["instructions"]:
        default_operands = raw_instruction.get("operands")
        variants: list[VariantSpec] = []

        for raw_variant in raw_instruction["variants"]:
            variants.append(
                VariantSpec(
                    name=raw_variant["name"],
                    availability=raw_variant["availability"],
                    modifiers=tuple(
                        normalize_modifier(modifier, type_sets)
                        for modifier in raw_variant.get("modifiers", ())
                    ),
                    operand_layouts=normalize_operand_layouts(
                        raw_variant, default_operands, operand_patterns
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
