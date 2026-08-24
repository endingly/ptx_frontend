"""Typed normalized models for PTX ISA and C++ backend YAML specifications."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any


class OperandTypeExpressionKind(Enum):
    """The supported source-level ways to determine an operand scalar type."""

    FIXED_SCALAR = "fixed_scalar"
    MODIFIER = "modifier"


class RuntimeLookupKind(str, Enum):
    """Runtime C++ lookup forms emitted for backend value domains."""

    PTX_SUFFIX = "ptx_suffix"


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


def modifier_spellings(modifier: ModifierSpec) -> tuple[str, ...]:
    """Return every canonical lexer-facing spelling accepted by a modifier."""

    if modifier.token is not None:
        return (modifier.token,)
    if modifier.values:
        return tuple(
            value.token if value.token is not None else f".{value.value}"
            for value in modifier.values
        )
    if isinstance(modifier.value, str):
        return (f".{modifier.value}",)
    if modifier.value is True:
        return (f".{modifier.name}",)
    return ()


@dataclass(frozen=True)
class OperandSpec:
    """One normalized source-level PTX operand."""

    name: str
    kind: str
    role: str | None = None
    access: str | None = None
    type_expression: OperandTypeExpression | None = None
    vector_arities: tuple[int, ...] = ()


@dataclass(frozen=True)
class OperandLayoutSpec:
    """One stable operand layout within a modifier-selected variant."""

    name: str
    operands: tuple[OperandSpec, ...]
    # Empty means that this layout introduces no target requirement beyond its
    # containing variant's availability.
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class OperandTypeCompatibilitySpec:
    """Contextual operand type accepted by one instruction variant."""

    operand: str
    value_kind: str
    values: tuple[str, ...]
    instruction_width: int
    effective_type: str
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class VariantSpec:
    """One PTX instruction variant."""

    name: str
    availability: dict[str, Any]
    modifiers: tuple[ModifierSpec, ...]
    operand_layouts: tuple[OperandLayoutSpec, ...]
    rule: str | None = None
    operand_type_compatibilities: tuple[OperandTypeCompatibilitySpec, ...] = ()


@dataclass(frozen=True)
class InstructionSpec:
    """All merged YAML definitions and variants for one opcode."""

    opcode: str
    variants: tuple[VariantSpec, ...]
    syntax_forms: tuple[str, ...] = ()
    source_categories: tuple[str, ...] = ()
    codegen_category: str = "uncategorized"


# -----------------------------------------------------------------------------
# C++ backend model
# -----------------------------------------------------------------------------
#
# ``DomainBackend`` is consumed by the current generation path for all
# semantic-value-to-C++ spelling/type mappings.  Per-instruction emit policy
# remains modeled for future consumers, but does not control the current
# resolved-IR structure.


@dataclass(frozen=True)
class DomainBackend:
    """C++ representations for all semantic values in one backend domain."""

    cpp_type: str
    values: dict[str, str]
    default: str | None = None
    runtime_lookup: RuntimeLookupKind | None = None


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
    """Aggregate normalized PTX semantics and C++ backend data.

    The backend loader currently returns a backend-only unit with an empty
    ``instructions`` tuple; the ISA database continues to own merged
    instructions. This keeps C++ mappings typed without coupling PTX database
    discovery to one backend.
    """

    spec_schema: str
    backend_schema: str
    category: str
    namespace: str
    includes: tuple[str, ...] | None
    instructions: tuple[InstructionSpec, ...]
    backends: dict[str, InstructionBackend]
    domains: dict[str, DomainBackend]
