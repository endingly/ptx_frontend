"""
This module defines the data models used for code generation of PTX instructions in the C++ backend.
It includes the `ModifierSpec` dataclass, which encapsulates the specifications for instruction modifiers, such as their name, kind, presence, domain, possible values, default value, and associated token.
This model is essential for accurately representing the modifiers that can be applied to PTX instructions during code generation.
"""

from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class ModifierSpec:
    """
    Data model representing the specification of a PTX instruction modifier for the C++ backend code generation.
    see instructions/schemas/ptx-instr-v1.schema.yaml for more details on the fields and their meanings.
    """

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
    name: str
    kind: str
    role: str | None = None
    access: str | None = None
    type_expr: str | None = None


@dataclass(frozen=True)
class VariantSpec:
    name: str
    availability: dict[str, Any]
    modifiers: tuple[ModifierSpec, ...]
    operands: tuple[OperandSpec, ...]
    rule: str | None = None


@dataclass(frozen=True)
class InstructionSpec:
    opcode: str
    syntax: str | None
    variants: tuple[VariantSpec, ...]


#################################################################################################################
# The following data models are used for representing the backend-specific mappings of PTX instruction modifiers.
#################################################################################################################


@dataclass(frozen=True)
class DomainBackend:
    cpp_type: str
    values: dict[str, str]
    default: str | None = None


@dataclass(frozen=True)
class ModifierBackend:
    field: str
    cpp_type: str | None = None
    domain: str | None = None
    default: str | None = None
    optional_policy: str | None = None


@dataclass(frozen=True)
class OperandBackend:
    field: str
    cpp_type: str


@dataclass(frozen=True)
class EmitBackend:
    kind: str
    instance: str | None = None
    type: str | None = None


@dataclass(frozen=True)
class InstructionBackend:
    opcode: str
    cpp: str
    emit: EmitBackend
    modifiers: dict[str, ModifierBackend]
    operands: dict[str, OperandBackend]
    type_checker_rule: str | None = None
    visitor_name: str | None = None
    modifier_order: tuple[str, ...] = ()
    operand_order: tuple[str, ...] = ()


#################################################################################################################
# The following data models are used for representing the complete mapping of PTX instructions to their C++ backend implementations,
# including the instruction specifications and the backend-specific mappings.
#################################################################################################################


@dataclass(frozen=True)
class CodegenUnit:
    namespace: str
    instructions: tuple[InstructionSpec, ...]
    backends: dict[str, InstructionBackend]
    domains: dict[str, DomainBackend]
