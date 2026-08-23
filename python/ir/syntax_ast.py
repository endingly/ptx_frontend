"""Python IR model for generated PTX Syntax AST architecture descriptors.

This is not the runtime C++ Syntax AST node model.  It is the declarative,
per-opcode syntax architecture from which C++ descriptor tables are generated.
The model intentionally has no dependency on YAML loading or code generation.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, IntFlag
from code_gen.model import (
    InstructionSpec,
    ModifierSpec,
    OperandSpec,
    VariantSpec,
    modifier_spellings,
)


class ModifierPresence(Enum):
    """Whether one modifier kind occurs in a syntax variant."""

    ABSENT = "Absent"
    OPTIONAL = "Optional"
    REQUIRED = "Required"


class OperandPresence(Enum):
    """Whether one positional operand slot may be omitted."""

    REQUIRED = "Required"
    OPTIONAL = "Optional"


class OperandSyntaxShape(IntFlag):
    """Syntax alternatives of the C++ ``syntax_ast::AstOperand`` variant."""

    IDENTIFIER_REF = 1 << 0
    IMMEDIATE = 1 << 1
    ADDRESS = 1 << 2
    VECTOR_MEMBER = 1 << 3
    VECTOR_PACK = 1 << 4
    PREDICATE = 1 << 5
    CALL_PARAMETER_LIST = 1 << 6
    CALL_TARGET = 1 << 7
    CALL_TARGET_SET = 1 << 8
    BRANCH_TARGET = 1 << 9


class OperandLayoutKind(Enum):
    """Generic algorithm used to match an operand layout."""

    FLAT = "Flat"


@dataclass(frozen=True)
class SyntaxModifierDescriptor:
    """One modifier kind constraint in one syntax variant."""

    kind_id: str
    presence: ModifierPresence
    allowed_spellings: tuple[str, ...]


@dataclass(frozen=True)
class SyntaxOperandSlotDescriptor:
    """One positional AST operand constraint in an operand layout."""

    allowed_syntax_shapes: OperandSyntaxShape
    presence: OperandPresence

    def allows(self, actual_shape: OperandSyntaxShape) -> bool:
        return bool(self.allowed_syntax_shapes & actual_shape)


@dataclass(frozen=True)
class SyntaxOperandLayoutDescriptor:
    """One accepted operand layout inside a syntax variant."""

    layout_id: str
    kind: OperandLayoutKind
    slots: tuple[SyntaxOperandSlotDescriptor, ...]


@dataclass(frozen=True)
class SyntaxVariantDescriptor:
    """Syntax constraints for one stable PTX instruction variant ID."""

    variant_id: str
    modifiers: tuple[SyntaxModifierDescriptor, ...]
    operand_layouts: tuple[SyntaxOperandLayoutDescriptor, ...]


@dataclass(frozen=True)
class SyntaxInstructionDescriptor:
    """The complete syntax architecture descriptor for one opcode."""

    opcode: str
    variants: tuple[SyntaxVariantDescriptor, ...]


# region model.InstructionSpec to SyntaxInstructionDescriptor conversion function sets


def from_InstructionSpec(spec: InstructionSpec) -> SyntaxInstructionDescriptor:
    """Translate one normalized PTX instruction into syntax descriptor data."""
    return SyntaxInstructionDescriptor(
        opcode=spec.opcode,
        variants=tuple(
            _build_variant_descriptor_view(variant) for variant in spec.variants
        ),
    )


_PRESENCE_MAP = {
    "absent": ModifierPresence.ABSENT,
    "optional": ModifierPresence.OPTIONAL,
    "required": ModifierPresence.REQUIRED,
    "fixed": ModifierPresence.REQUIRED,
}

_OPERAND_SYNTAX_SHAPES = {
    "reg": OperandSyntaxShape.IDENTIFIER_REF,
    "imm": OperandSyntaxShape.IMMEDIATE,
    "reg_or_imm": OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
    "mov_data_src": (
        OperandSyntaxShape.IDENTIFIER_REF
        | OperandSyntaxShape.IMMEDIATE
        | OperandSyntaxShape.VECTOR_MEMBER
    ),
    "mov_address_src": (
        OperandSyntaxShape.IDENTIFIER_REF
        | OperandSyntaxShape.IMMEDIATE
        | OperandSyntaxShape.ADDRESS
        | OperandSyntaxShape.VECTOR_MEMBER
    ),
    "pred": OperandSyntaxShape.IDENTIFIER_REF,
    "pred_or_not": OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.PREDICATE,
    "label": OperandSyntaxShape.BRANCH_TARGET,
    "sreg": OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.VECTOR_MEMBER,
    "symbol": OperandSyntaxShape.IDENTIFIER_REF,
    "addr": OperandSyntaxShape.ADDRESS,
}

def _build_variant_descriptor_view(
    variant: VariantSpec,
) -> SyntaxVariantDescriptor:
    return SyntaxVariantDescriptor(
        variant_id=variant.name,
        modifiers=tuple(
            _build_modifier_descriptor_view(modifier) for modifier in variant.modifiers
        ),
        operand_layouts=tuple(
            SyntaxOperandLayoutDescriptor(
                layout_id=layout.name,
                kind=OperandLayoutKind.FLAT,  # TODO: support other layout kinds
                slots=tuple(
                    _build_operand_slot_descriptor_view(operand)
                    for operand in layout.operands
                ),
            )
            for layout in variant.operand_layouts
        ),
    )


def _build_modifier_descriptor_view(
    modifier: ModifierSpec,
) -> SyntaxModifierDescriptor:
    """Return the AST modifier descriptor for one normalized PTX modifier spec."""
    try:
        presence = _PRESENCE_MAP[modifier.presence]
    except KeyError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: unsupported presence "
            f"{modifier.presence!r}"
        ) from error

    return SyntaxModifierDescriptor(
        kind_id=modifier.name,
        presence=presence,
        allowed_spellings=modifier_spellings(modifier),
    )


def _build_operand_slot_descriptor_view(
    operand: OperandSpec,
) -> SyntaxOperandSlotDescriptor:
    """Return the AST operand slot descriptor for one normalized PTX operand spec."""
    try:
        shapes = _OPERAND_SYNTAX_SHAPES[operand.kind]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported syntax operand kind "
            f"{operand.kind!r}"
        ) from error

    return SyntaxOperandSlotDescriptor(
        allowed_syntax_shapes=shapes,
        presence=OperandPresence.REQUIRED,
    )


# endregion
