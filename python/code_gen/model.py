"""Typed normalized model for PTX ISA YAML specifications."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any


class OperandTypeExpressionKind(Enum):
    """The supported source-level ways to determine an operand scalar type."""

    FIXED_SCALAR = "fixed_scalar"
    MODIFIER = "modifier"


@dataclass(frozen=True)
class OperandTypeExpression:
    """A parsed operand type expression from the YAML syntax specification."""

    kind: OperandTypeExpressionKind
    scalar_type: str | None = None
    modifier_name: str | None = None


@dataclass(frozen=True)
class ModifierValueSpec:
    """One legal semantic modifier value and its optional target requirement."""

    value: str | bool | int
    token: str | None = None
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class ModifierSpec:
    """One normalized PTX modifier in an instruction variant."""

    name: str
    kind: str
    presence: str
    domain: str | None = None
    values: tuple[ModifierValueSpec, ...] = ()
    value: str | bool | int | None = None
    token: str | None = None
    default: str | bool | int | None = None


@dataclass(frozen=True)
class OperandSpec:
    """One normalized source-level PTX operand."""

    name: str
    kind: str
    role: str | None = None
    access: str | None = None
    type_expression: OperandTypeExpression | None = None


@dataclass(frozen=True)
class OperandLayoutSpec:
    """One stable operand layout within a modifier-selected variant."""

    name: str
    operands: tuple[OperandSpec, ...]
    # Empty means that this layout introduces no target requirement beyond its
    # containing variant's availability.
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class VariantSpec:
    """One PTX instruction variant."""

    name: str
    availability: dict[str, Any]
    modifiers: tuple[ModifierSpec, ...]
    operand_layouts: tuple[OperandLayoutSpec, ...]
    rule: str | None = None


@dataclass(frozen=True)
class InstructionSpec:
    """All PTX variants for one opcode."""

    opcode: str
    syntax: str | None
    variants: tuple[VariantSpec, ...]
