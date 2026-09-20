"""Python IR model for generated PTX Syntax AST architecture descriptors.

This is not the runtime C++ Syntax AST node model.  It is the declarative,
per-opcode syntax architecture from which C++ descriptor tables are generated.
The model intentionally has no dependency on YAML loading or code generation.
"""

from dataclasses import dataclass
from enum import Enum, IntFlag
from ptx_frontend.spec.model import (
    InstructionSpec,
    ModifierSpec,
    ModifierPresence as ModelModifierPresence,
    OperandKind,
    OperandLayoutKind as ModelOperandLayoutKind,
    OperandSpec,
    VariantSpec,
    modifier_spellings,
)
from ptx_frontend.spec.synatax_shapes import OperandSyntaxShape, OPERAND_SYNTAX_SHAPES


class ModifierPresence(Enum):
    """Whether one modifier kind occurs in a syntax variant."""

    ABSENT = "Absent"
    OPTIONAL = "Optional"
    REQUIRED = "Required"


class OperandPresence(Enum):
    """Whether one positional operand slot may be omitted."""

    REQUIRED = "Required"
    OPTIONAL = "Optional"


class OperandLayoutKind(Enum):
    """Generic algorithm used to match an operand layout."""

    FLAT = "Flat"
    CALL = "Call"
    INDIRECT_CALL = "IndirectCall"


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
    type_tag: str | None = None
    minimum_elements: int | None = None
    maximum_elements: int | None = None
    allowed_element_shapes: OperandSyntaxShape = OperandSyntaxShape(0)

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
    modifier_order_aliases: tuple[tuple[SyntaxModifierDescriptor, ...], ...] = ()


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
    ModelModifierPresence.ABSENT: ModifierPresence.ABSENT,
    ModelModifierPresence.OPTIONAL: ModifierPresence.OPTIONAL,
    ModelModifierPresence.REQUIRED: ModifierPresence.REQUIRED,
    ModelModifierPresence.FIXED: ModifierPresence.REQUIRED,
}


def _build_variant_descriptor_view(
    variant: VariantSpec,
) -> SyntaxVariantDescriptor:
    modifiers = tuple(
        _build_modifier_descriptor_view(modifier) for modifier in variant.modifiers
    )
    modifiers_by_name = {modifier.kind_id: modifier for modifier in modifiers}
    return SyntaxVariantDescriptor(
        variant_id=variant.name,
        modifiers=modifiers,
        operand_layouts=tuple(
            SyntaxOperandLayoutDescriptor(
                layout_id=layout.name,
                kind=OperandLayoutKind(
                    "Call"
                    if layout.kind is ModelOperandLayoutKind.CALL
                    else (
                        "IndirectCall"
                        if layout.kind is ModelOperandLayoutKind.INDIRECT_CALL
                        else "Flat"
                    )
                ),
                slots=tuple(
                    _build_operand_slot_descriptor_view(operand)
                    for operand in layout.operands
                ),
            )
            for layout in variant.operand_layouts
        ),
        modifier_order_aliases=tuple(
            tuple(modifiers_by_name[slot_name] for slot_name in alias)
            for alias in variant.modifier_order_aliases
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
        shapes = OPERAND_SYNTAX_SHAPES[operand.kind]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported syntax operand kind "
            f"{operand.kind!r}"
        ) from error

    return SyntaxOperandSlotDescriptor(
        allowed_syntax_shapes=shapes,
        presence=OperandPresence.REQUIRED,
        type_tag=operand.type_tag,
        minimum_elements=operand.minimum_elements,
        maximum_elements=operand.maximum_elements,
        allowed_element_shapes=sum(
            (
                (
                    OperandSyntaxShape.IDENTIFIER_REF
                    if kind is OperandKind.REGISTER
                    else OperandSyntaxShape.IMMEDIATE
                )
                for kind in operand.element_kinds
            ),
            OperandSyntaxShape(0),
        ),
    )


# endregion
