"""Typed normalized models for PTX ISA and C++ backend YAML specifications."""

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
    category: str = "uncategorized"


# -----------------------------------------------------------------------------
# Reserved C++ backend model
# -----------------------------------------------------------------------------
#
# These types intentionally are not consumed by the current generation path.
# They preserve the typed boundary for a future loader of
# instructions/ptx_cpp_backend_spec, where C++-only spelling tables and emit
# policy can be moved out of the Python emitters without entering the PTX ISA
# semantic model above.


@dataclass(frozen=True)
class DomainBackend:
    """C++ representations for all semantic values in one backend domain."""

    cpp_type: str
    values: dict[str, str]
    default: str | None = None


@dataclass(frozen=True)
class ModifierBackend:
    """C++ field and value-domain mapping for one instruction modifier."""

    field: str
    cpp_type: str | None = None
    domain: str | None = None
    default: str | None = None


@dataclass(frozen=True)
class OperandBackend:
    """C++ field and type mapping for one instruction operand."""

    field: str
    cpp_type: str


@dataclass(frozen=True)
class EmitAlternativeBackend:
    """One nested C++ representation and the PTX variants assigned to it."""

    name: str
    variants: tuple[str, ...] = ()


@dataclass(frozen=True)
class EmitBackend:
    """C++ storage-shape policy for one generated instruction."""

    kind: str
    instance: str | None = None
    type: str | None = None
    alternatives: tuple[EmitAlternativeBackend, ...] = ()


@dataclass(frozen=True)
class InstructionBackend:
    """Complete C++ backend mapping for one PTX opcode."""

    opcode: str
    cpp: str
    emit: EmitBackend
    modifiers: dict[str, ModifierBackend]
    operands: dict[str, OperandBackend]
    type_checker_rule: str | None = None
    visitor_name: str | None = None
    modifier_order: tuple[str, ...] = ()
    operand_order: tuple[str, ...] = ()


@dataclass(frozen=True)
class CodegenUnit:
    """Detached aggregate of normalized PTX semantics and C++ backend data.

    This restores the pre-refactor model contract for future backend work.  The
    active generators continue to consume :class:`CodegenDatabase` and do not
    construct this aggregate yet.
    """

    spec_schema: str
    backend_schema: str
    category: str
    namespace: str
    includes: tuple[str, ...] | None
    instructions: tuple[InstructionSpec, ...]
    backends: dict[str, InstructionBackend]
    domains: dict[str, DomainBackend]
