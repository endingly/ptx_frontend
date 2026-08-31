"""Typed normalized models for PTX ISA and C++ backend YAML specifications."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any


class OperandTypeExpressionKind(Enum):
    """The supported source-level ways to determine an operand scalar type."""

    FIXED_SCALAR = "fixed_scalar"
    MODIFIER = "modifier"


class OperandRegisterWidthPolicy(str, Enum):
    """Register-width relation accepted by an operand type constraint."""

    EXACT = "exact"
    SAME_WIDTH = "same_width"
    EQUAL_OR_WIDER = "equal_or_wider"


class OperandVectorTypePolicy(str, Enum):
    """How an instruction type maps onto a register-vector operand."""

    AGGREGATE = "aggregate"
    ELEMENT = "element"


class OperandLayoutKind(str, Enum):
    """Matching algorithm selected by one operand layout."""

    FLAT = "flat"
    CALL = "call"
    INDIRECT_CALL = "indirect_call"


@dataclass(frozen=True)
class OperandVectorArityExpression:
    """A parsed register-vector arity expression from the YAML specification."""

    modifier_name: str


@dataclass(frozen=True)
class OperandStateSpaceExpression:
    """A parsed operand state-space expression from the YAML specification."""

    modifier_name: str


@dataclass(frozen=True)
class OperandStateSpaceValue:
    """One statically allowed operand state space and its target requirement."""

    value: str
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class OperandParameterConstraint:
    """Direction and function-specific availability for a .param address."""

    direction: str
    function_availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class MemoryConsistencyConstraint:
    """Typed cross-modifier rule for a family of ld/st variants."""

    semantics_modifier: str
    scope_modifier: str
    cache_modifier: str
    address_operand: str
    mmio_modifier: str | None = None
    state_space_modifier: str | None = None


@dataclass(frozen=True)
class AddressAlignmentConstraint:
    """Typed static alignment rule for one or more address operands."""

    address_operands: tuple[str, ...]
    type_modifier: str | None = None
    vector_modifier: str | None = None
    immediate_operand: str | None = None
    alignment: int | None = None


@dataclass(frozen=True)
class MemoryVectorConstraint:
    """Typed PTX 8.8 256-bit ld/st vector cross-rule."""

    type_modifier: str
    vector_operand: str
    address_operand: str
    availability: dict[str, Any] = field(default_factory=dict)
    state_space_modifier: str | None = None


@dataclass(frozen=True)
class ImmediateValueConstraint:
    """Restrict one immediate operand to an explicit integer allowlist."""

    operand: str
    values: tuple[int, ...]


@dataclass(frozen=True)
class ImmediateRangeConstraint:
    """Restrict one immediate operand to an inclusive integer range."""

    operand: str
    minimum: int
    maximum: int | None = None


@dataclass(frozen=True)
class ImmediateMultipleOfConstraint:
    """Require one immediate operand to be divisible by a positive integer."""

    operand: str
    divisor: int


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
    register_width_policy: OperandRegisterWidthPolicy = (
        OperandRegisterWidthPolicy.SAME_WIDTH
    )
    state_space_values: tuple[OperandStateSpaceValue, ...] = ()
    state_space_expression: OperandStateSpaceExpression | None = None
    parameter_constraint: OperandParameterConstraint | None = None
    vector_arities: tuple[int, ...] = ()
    vector_arity_expression: OperandVectorArityExpression | None = None
    vector_type_policy: OperandVectorTypePolicy = OperandVectorTypePolicy.AGGREGATE
    vector_allow_sink: bool = False
    type_tag: str | None = None
    minimum_elements: int | None = None
    maximum_elements: int | None = None
    element_kinds: tuple[str, ...] = ()


@dataclass(frozen=True)
class OperandLayoutSpec:
    """One stable operand layout within a modifier-selected variant."""

    name: str
    operands: tuple[OperandSpec, ...]
    kind: OperandLayoutKind = OperandLayoutKind.FLAT
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
    memory_consistency: MemoryConsistencyConstraint | None = None
    address_alignment: AddressAlignmentConstraint | None = None
    memory_vector: MemoryVectorConstraint | None = None
    immediate_value: ImmediateValueConstraint | None = None
    immediate_ranges: tuple[ImmediateRangeConstraint, ...] = ()
    immediate_multiple_of: ImmediateMultipleOfConstraint | None = None


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
