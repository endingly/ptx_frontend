"""Typed syntax storage models, including canonical alias references."""

from __future__ import annotations

from dataclasses import dataclass

from ptx_frontend.base.utils import file_stem_to_pascal_case, to_file_stem
from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_value
from ptx_frontend.code_gen.cpp_literals import cpp_string
from ptx_frontend.spec.model import CodegenUnit
from ptx_frontend.ir.syntax_ast import (
    OperandSyntaxShape, SyntaxInstructionDescriptor, SyntaxVariantDescriptor,
    SyntaxModifierDescriptor, SyntaxOperandSlotDescriptor,
)


@dataclass(frozen=True)
class SyntaxModifierModel:
    """One modifier with a reference into canonical static value storage."""

    allowed_values: str
    presence: str
    kind_id: str


@dataclass(frozen=True)
class SyntaxModifierArrayModel:
    """A complete canonical or historical modifier order."""

    name: str
    modifiers: tuple[SyntaxModifierModel, ...]


@dataclass(frozen=True)
class SyntaxAllowedValuesModel:
    """Static string storage borrowed by a canonical modifier and its aliases."""

    name: str
    values: tuple[str, ...]


@dataclass(frozen=True)
class SyntaxOperandSlotModel:
    """Final operand syntax constraints with optional cardinality and shape facts."""

    allowed_shapes: str
    presence: str
    type_tag: str | None
    minimum_elements: int | None
    maximum_elements: int | None
    element_shapes: str | None
    texture_unbracketed: bool


@dataclass(frozen=True)
class SyntaxOperandLayoutModel:
    """One layout and the slots its span borrows."""

    slots_name: str
    layout_id: str
    kind: str
    slots: tuple[SyntaxOperandSlotModel, ...]


@dataclass(frozen=True)
class SyntaxVariantModel:
    """Ordered syntax storage and final references for one variant."""

    name: str
    variant_name: str
    allowed_values: tuple[SyntaxAllowedValuesModel, ...]
    modifiers: SyntaxModifierArrayModel
    aliases: tuple[SyntaxModifierArrayModel, ...]
    layouts: tuple[SyntaxOperandLayoutModel, ...]
    alias_reference: str


@dataclass(frozen=True)
class SyntaxDescriptorStorageModel:
    """Instruction-local static arrays whose lifetime backs all syntax spans."""

    storage_name: str
    opcode: str
    variants: tuple[SyntaxVariantModel, ...]


@dataclass(frozen=True)
class SyntaxDescriptorGetterModel:
    """Public getter and its static storage owner."""

    cpp_name: str
    storage_name: str


@dataclass(frozen=True)
class SyntaxDescriptorCategoryModel:
    """Category source retaining storage/getter output order."""

    generated_at: str
    category: str
    storages: tuple[SyntaxDescriptorStorageModel, ...]
    getters: tuple[SyntaxDescriptorGetterModel, ...]


@dataclass(frozen=True)
class SyntaxDescriptorImplementationModel:
    """A standalone storage/getter implementation used by fragment consumers."""

    storage: SyntaxDescriptorStorageModel
    getter: SyntaxDescriptorGetterModel


def _cpp_operand_syntax_shape(shape: OperandSyntaxShape, backend: CodegenUnit) -> str:
    """Map a nonempty bitset to its final backend OR expression."""

    values = [cpp_value(CppDomain.SYNTAX_OPERAND_SHAPES, flag.name, backend=backend)
              for flag in OperandSyntaxShape if shape & flag and flag.name is not None]
    if not values:
        raise ValueError("operand syntax shape must not be empty")
    return " | ".join(values)


def _cpp_variant_name(opcode: str, variant_id: str) -> str:
    """Validate the opcode prefix before deriving the public variant name."""

    prefix = f"{opcode}_"
    if not variant_id.startswith(prefix):
        raise ValueError(f"variant {variant_id!r} does not start with opcode prefix {prefix!r}")
    return file_stem_to_pascal_case(variant_id.removeprefix(prefix))


def _build_modifier(name: str, index: int, modifier: SyntaxModifierDescriptor,
                    backend: CodegenUnit) -> SyntaxModifierModel:
    """Bind historical orders to canonical allowed-value array names."""

    return SyntaxModifierModel(
        f"{name}_modifier_{index}_allowed_values" if modifier.allowed_spellings else "{}",
        cpp_value(CppDomain.SYNTAX_MODIFIER_PRESENCE, modifier.presence.value, backend=backend),
        cpp_string(modifier.kind_id),
    )


def _build_slot(slot: SyntaxOperandSlotDescriptor, backend: CodegenUnit) -> SyntaxOperandSlotModel:
    """Prepare optional syntax fields without emitting member declarations."""

    allowed_shapes = _cpp_operand_syntax_shape(slot.allowed_syntax_shapes, backend)
    type_tag = cpp_string(slot.type_tag) if slot.type_tag is not None else None
    element_shapes = _cpp_operand_syntax_shape(slot.allowed_element_shapes, backend) if slot.allowed_element_shapes else None
    return SyntaxOperandSlotModel(
        allowed_shapes,
        cpp_value(CppDomain.SYNTAX_OPERAND_PRESENCE, slot.presence.value, backend=backend),
        type_tag, slot.minimum_elements, slot.maximum_elements, element_shapes,
        slot.texture_unbracketed,
    )


def _build_variant(variant: SyntaxVariantDescriptor, opcode: str,
                   backend: CodegenUnit) -> SyntaxVariantModel:
    """Preserve canonical slot indexes, historical orders, and layout order."""

    name = to_file_stem(variant.variant_id)
    canonical_indexes = {modifier.kind_id: index for index, modifier in enumerate(variant.modifiers)}
    return SyntaxVariantModel(
        name, cpp_string(_cpp_variant_name(opcode, variant.variant_id)),
        tuple(SyntaxAllowedValuesModel(f"{name}_modifier_{index}_allowed_values",
            tuple(cpp_string(value) for value in modifier.allowed_spellings))
            for index, modifier in enumerate(variant.modifiers) if modifier.allowed_spellings),
        SyntaxModifierArrayModel(f"{name}_modifiers", tuple(
            _build_modifier(name, index, modifier, backend) for index, modifier in enumerate(variant.modifiers))),
        tuple(SyntaxModifierArrayModel(f"{name}_modifier_order_alias_{alias_index}", tuple(
            _build_modifier(name, canonical_indexes[modifier.kind_id], modifier, backend) for modifier in alias))
            for alias_index, alias in enumerate(variant.modifier_order_aliases)),
        tuple(SyntaxOperandLayoutModel(f"{name}_layout_{index}_slots", cpp_string(layout.layout_id),
            cpp_value(CppDomain.SYNTAX_OPERAND_LAYOUT_KINDS, layout.kind.value, backend=backend),
            tuple(_build_slot(slot, backend) for slot in layout.slots))
            for index, layout in enumerate(variant.operand_layouts)),
        f"{name}_modifier_order_aliases" if variant.modifier_order_aliases else "{}",
    )


def build_syntax_descriptor_storage(descriptor: SyntaxInstructionDescriptor, backend: CodegenUnit,
                                    *, cpp_name: str) -> SyntaxDescriptorStorageModel:
    """Build static syntax ownership facts with no rendered child fragments."""

    return SyntaxDescriptorStorageModel(f"{cpp_name}DescriptorStorage", cpp_string(descriptor.opcode),
        tuple(_build_variant(variant, descriptor.opcode, backend) for variant in descriptor.variants))


def build_syntax_descriptor_getter(*, cpp_name: str) -> SyntaxDescriptorGetterModel:
    """Prepare the stable public getter's owner reference."""

    return SyntaxDescriptorGetterModel(cpp_name, f"{cpp_name}DescriptorStorage")
