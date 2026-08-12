"""Typed normalized model for PTX ISA YAML specifications."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class ModifierSpec:
    """One normalized PTX modifier in an instruction variant."""

    name: str
    kind: str
    presence: str
    domain: str | None = None
    values: tuple[str, ...] = ()
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
    type_expr: str | None = None


@dataclass(frozen=True)
class OperandLayoutSpec:
    """One stable operand layout within a modifier-selected variant."""

    name: str
    operands: tuple[OperandSpec, ...]


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
