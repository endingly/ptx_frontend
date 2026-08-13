"""Normalization of PTX ISA YAML into the typed code-generation model."""

from __future__ import annotations

import re
from typing import Any

from code_gen.load_yaml import expand_value_refs
from code_gen.model import (
    InstructionSpec,
    ModifierSpec,
    ModifierValueSpec,
    OperandLayoutSpec,
    OperandSpec,
    OperandTypeExpression,
    OperandTypeExpressionKind,
    VariantSpec,
)


_MODIFIER_TYPE_EXPR = re.compile(
    r"modifier\(([A-Za-z_][A-Za-z0-9_]*)\)"
)
_UNSUPPORTED_TYPE_EXPR_FUNCTIONS = ("same_as", "one_of", "same_size_as")


def normalize_operand(raw: dict[str, Any]) -> OperandSpec:
    """Normalize one operand specification."""

    return OperandSpec(
        name=raw["name"],
        kind=raw["kind"],
        role=raw.get("role"),
        access=raw.get("access"),
        type_expression=_normalize_operand_type_expression(raw.get("type")),
    )


def _normalize_operand_type_expression(
    raw_type: object,
) -> OperandTypeExpression | None:
    """Parse YAML ``type`` into a typed source-model expression."""

    if raw_type is None:
        return None
    if isinstance(raw_type, str):
        return OperandTypeExpression(
            kind=OperandTypeExpressionKind.FIXED_SCALAR,
            scalar_type=raw_type,
        )
    if not isinstance(raw_type, dict):
        raise TypeError("operand type must be a scalar type or type expression")

    expression = raw_type.get("expr")
    if not isinstance(expression, str):
        raise TypeError("type expression must be a string")
    match = _MODIFIER_TYPE_EXPR.fullmatch(expression)
    if match is not None:
        return OperandTypeExpression(
            kind=OperandTypeExpressionKind.MODIFIER,
            modifier_name=match.group(1),
        )

    unsupported = next(
        (
            function
            for function in _UNSUPPORTED_TYPE_EXPR_FUNCTIONS
            if expression.startswith(f"{function}(")
        ),
        None,
    )
    if unsupported is not None:
        raise ValueError(
            f"type expression function {unsupported!r} is not supported yet"
        )
    raise ValueError("unsupported type expression; use modifier(<modifier_name>)")


def normalize_modifier(
    raw: dict[str, Any], reusable_value_sets: dict[str, list[str]]
) -> ModifierSpec:
    """Normalize one modifier and expand its reusable value-set references."""

    raw_values: object = raw.get("values", [])
    if not isinstance(raw_values, list):
        raise TypeError("modifier values must be a list")

    values = _normalize_modifier_values(raw_values, reusable_value_sets)
    _validate_modifier_default(raw, values)

    return ModifierSpec(
        name=raw["name"],
        kind=raw["kind"],
        presence=raw["presence"],
        domain=raw.get("domain"),
        values=values,
        value=raw.get("value"),
        token=raw.get("token"),
        default=raw.get("default"),
    )


def _validate_modifier_default(
    raw: dict[str, Any], values: tuple[ModifierValueSpec, ...]
) -> None:
    """Validate the semantic value used when an optional modifier is omitted."""

    presence = raw["presence"]
    has_default = "default" in raw
    if presence != "optional":
        if has_default:
            raise ValueError(
                f"modifier {raw['name']!r}: default is only valid for optional "
                "modifiers"
            )
        return
    if not has_default:
        raise ValueError(
            f"optional modifier {raw['name']!r} must define default"
        )

    default = raw["default"]
    kind = raw["kind"]
    if kind == "flag":
        if type(default) is not bool:
            raise ValueError(
                f"optional flag modifier {raw['name']!r} must have a boolean "
                "default"
            )
        return
    if kind == "type":
        if not isinstance(default, str):
            raise ValueError(
                f"optional type modifier {raw['name']!r} must have a string "
                "default"
            )
        allowed_values = {value.value for value in values}
        if default not in allowed_values:
            raise ValueError(
                f"optional type modifier {raw['name']!r} has default "
                f"{default!r} outside its allowed values"
            )
        return
    if kind == "rounding":
        if not isinstance(default, str):
            raise ValueError(
                f"optional rounding modifier {raw['name']!r} must have a "
                "string default"
            )
        allowed_values = {value.value for value in values}
        if default not in allowed_values:
            raise ValueError(
                f"optional rounding modifier {raw['name']!r} has default "
                f"{default!r} outside its allowed values"
            )


def _normalize_modifier_values(
    raw_values: list[Any], reusable_value_sets: dict[str, list[str]]
) -> tuple[ModifierValueSpec, ...]:
    """Expand value-set references while preserving per-value availability."""

    values: list[ModifierValueSpec] = []
    seen: set[str | bool | int] = set()
    for raw_value in raw_values:
        if isinstance(raw_value, dict):
            raw_semantic_value = raw_value["value"]
            token = raw_value.get("token")
            availability = dict(raw_value.get("availability", {}))
        else:
            raw_semantic_value = raw_value
            token = None
            availability = {}

        if isinstance(raw_semantic_value, str) and raw_semantic_value.startswith("$"):
            if token is not None:
                raise ValueError(
                    "a modifier value-set reference cannot define one token "
                    "override for multiple expanded values"
                )
            expanded_values: tuple[str | bool | int, ...] = expand_value_refs(
                [raw_semantic_value], reusable_value_sets
            )
        else:
            expanded_values = (raw_semantic_value,)
        for value in expanded_values:
            if value in seen:
                raise ValueError(f"duplicate modifier value {value!r}")
            seen.add(value)
            values.append(
                ModifierValueSpec(
                    value=value,
                    token=token,
                    availability=availability,
                )
            )
    return tuple(values)


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
                availability=dict(raw_layout.get("availability", {})),
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
        if not raw_operands.startswith("$"):
            raise ValueError(
                "operand-pattern references must use the '$name' form; "
                "use an explicit operand list for inline operands"
            )
        pattern_name = raw_operands[1:]
        try:
            raw_operands = operand_patterns[pattern_name]
        except KeyError as error:
            raise ValueError(f"unknown operand pattern: {pattern_name}") from error
    if not isinstance(raw_operands, list):
        raise TypeError("operands must be an explicit list or a '$name' reference")
    return tuple(normalize_operand(operand) for operand in raw_operands)


def _validate_modifier_type_expressions(
    modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...],
) -> None:
    """Require ``modifier(name)`` expressions to name an active type modifier."""

    modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
    for layout in layouts:
        for operand in layout.operands:
            expression = operand.type_expression
            if expression is None:
                continue
            if expression.kind is not OperandTypeExpressionKind.MODIFIER:
                continue
            assert expression.modifier_name is not None
            modifier_name = expression.modifier_name
            modifier = modifiers_by_name.get(modifier_name)
            if modifier is None:
                raise ValueError(
                    f"operand {operand.name!r}: type expression references unknown "
                    f"modifier {modifier_name!r}"
                )
            if modifier.kind != "type" or modifier.presence == "absent":
                raise ValueError(
                    f"operand {operand.name!r}: modifier {modifier_name!r} must be "
                    "an active type modifier"
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
            operand_layouts = normalize_operand_layouts(
                raw_variant, default_operands, operand_patterns
            )
            _validate_modifier_type_expressions(modifiers, operand_layouts)
            variants.append(
                VariantSpec(
                    name=raw_variant["name"],
                    availability=raw_variant["availability"],
                    modifiers=modifiers,
                    operand_layouts=operand_layouts,
                    rule=raw_variant.get("rule"),
                )
            )

        instructions.append(
            InstructionSpec(
                opcode=raw_instruction["opcode"],
                variants=tuple(variants),
                syntax_forms=(raw_instruction["syntax"],)
                if "syntax" in raw_instruction
                else (),
                source_categories=(source_category,),
                codegen_category=codegen_category,
            )
        )

    return tuple(instructions)
