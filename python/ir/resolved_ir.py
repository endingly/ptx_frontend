"""Python model for generated PTX Resolved IR instruction definitions.

The model is derived from the normalized PTX-facing ``InstructionSpec``.  It
describes the semantic fields that must appear in the generated C++ resolved
instruction structs; it does not describe C++ storage or emitter layout.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any

from code_gen.model import InstructionSpec, ModifierSpec, OperandSpec, VariantSpec
from base.utils import file_stem_to_pascal_case


class ResolvedFieldOrigin(Enum):
    """The PTX specification element that supplies a resolved field."""

    MODIFIER = "modifier"
    OPERAND = "operand"


@dataclass(frozen=True)
class ResolvedField:
    """One provenance-carrying field in a resolved variant struct."""

    name: str
    value_cpp_type: str
    origin: ResolvedFieldOrigin
    source_name: str
    type_expr: str | None = None

    @property
    def cpp_type(self) -> str:
        """The C++ member type used by generated resolved instruction structs."""

        return f"WithLocs<{self.value_cpp_type}>"


@dataclass(frozen=True)
class ResolvedVariant:
    """One alternative of an opcode's generated C++ ``Variant`` type."""

    variant_id: str
    cpp_name: str
    fields: tuple[ResolvedField, ...]
    availability: tuple[tuple[str, Any], ...]
    rule: str | None


@dataclass(frozen=True)
class ResolvedInstruction:
    """Resolved IR definition for one PTX opcode, such as ``Add``."""

    opcode: str
    cpp_name: str
    variants: tuple[ResolvedVariant, ...]


_MODIFIER_VALUE_CPP_TYPES = {
    "flag": "bool",
    "type": "ScalarType",
}

_MODIFIER_FIELD_NAMES = {
    # ``.sat`` is represented semantically as the boolean property rather
    # than as its source spelling.
    "sat": "saturate",
}

_OPERAND_VALUE_CPP_TYPES = {
    "reg": "ResolvedRegisterId",
    "imm": "ResolvedImmediate",
    "reg_or_imm": "RegOrImm",
}


def from_instruction_spec(spec: InstructionSpec) -> ResolvedInstruction:
    """Build the resolved instruction model from one normalized PTX spec."""

    return ResolvedInstruction(
        opcode=spec.opcode,
        cpp_name=file_stem_to_pascal_case(spec.opcode),
        variants=tuple(
            _build_variant(spec.opcode, variant) for variant in spec.variants
        ),
    )


def _build_variant(opcode: str, variant: VariantSpec) -> ResolvedVariant:
    return ResolvedVariant(
        variant_id=variant.name,
        cpp_name=_variant_cpp_name(opcode, variant.name),
        fields=tuple(
            _build_modifier_field(modifier)
            for modifier in variant.modifiers
            if _materializes_resolved_field(modifier)
        )
        + tuple(_build_operand_field(operand) for operand in variant.operands),
        availability=tuple(variant.availability.items()),
        rule=variant.rule,
    )


def _materializes_resolved_field(modifier: ModifierSpec) -> bool:
    """Fixed and absent modifiers are represented by the selected variant."""

    return modifier.presence not in {"absent", "fixed"}


def _build_modifier_field(modifier: ModifierSpec) -> ResolvedField:
    try:
        value_cpp_type = _MODIFIER_VALUE_CPP_TYPES[modifier.kind]
    except KeyError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: unsupported resolved modifier kind "
            f"{modifier.kind!r}"
        ) from error

    return ResolvedField(
        name=_MODIFIER_FIELD_NAMES.get(modifier.name, modifier.name),
        value_cpp_type=value_cpp_type,
        origin=ResolvedFieldOrigin.MODIFIER,
        source_name=modifier.name,
    )


def _build_operand_field(operand: OperandSpec) -> ResolvedField:
    try:
        value_cpp_type = _OPERAND_VALUE_CPP_TYPES[operand.kind]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported resolved operand kind "
            f"{operand.kind!r}"
        ) from error

    return ResolvedField(
        name=operand.name,
        value_cpp_type=value_cpp_type,
        origin=ResolvedFieldOrigin.OPERAND,
        source_name=operand.name,
        type_expr=operand.type_expr,
    )


def _variant_cpp_name(opcode: str, variant_id: str) -> str:
    prefix = f"{opcode}_"
    if not variant_id.startswith(prefix):
        raise ValueError(
            f"variant {variant_id!r} does not start with opcode prefix {prefix!r}"
        )
    return file_stem_to_pascal_case(variant_id.removeprefix(prefix))
