"""Direct typed operand members shared by resolved IR model and emitters."""

from __future__ import annotations

from dataclasses import dataclass
import re

from ptx_frontend.code_gen.resolved_field_names import field_cpp_type
from ptx_frontend.ir.resolved_ir import ResolvedField, ResolvedVariant
from ptx_frontend.spec.model import CodegenUnit


@dataclass(frozen=True)
class OperandSlot:
    """One direct member and the layouts in which its value must be present."""

    field: ResolvedField
    member_name: str
    layout_indices: tuple[int, ...]
    optional: bool


def operand_slots(
    variant: ResolvedVariant, backend: CodegenUnit
) -> tuple[OperandSlot, ...]:
    """Unify equal typed fields and distinguish overloaded field identities."""

    appearances: dict[tuple[str, str], list[int]] = {}
    examples: dict[tuple[str, str], ResolvedField] = {}
    name_types: dict[str, set[str]] = {}
    for index, layout in enumerate(variant.operand_layouts):
        seen_in_layout: set[tuple[str, str]] = set()
        for field in layout.fields:
            key = (field.name, field_cpp_type(field, backend=backend))
            if key in seen_in_layout:
                raise ValueError(
                    f"{variant.cpp_name}: duplicate typed operand {key!r} "
                    f"in layout {layout.layout_id!r}"
                )
            seen_in_layout.add(key)
            appearances.setdefault(key, []).append(index)
            examples.setdefault(key, field)
            name_types.setdefault(field.name, set()).add(key[1])

    slots: list[OperandSlot] = []
    used_members: set[str] = set()
    for key, indices in appearances.items():
        field = examples[key]
        if len(name_types[field.name]) == 1:
            member_name = field.name
        else:
            kind = re.sub(r"(?<!^)(?=[A-Z])", "_", field.value_kind.value)
            kind = re.sub(r"[^a-zA-Z0-9]+", "_", kind).strip("_").lower()
            member_name = f"{field.name}_{kind}"
        if member_name in used_members:
            raise ValueError(
                f"{variant.cpp_name}: operand members collide at {member_name!r}"
            )
        used_members.add(member_name)
        slots.append(
            OperandSlot(
                field=field,
                member_name=member_name,
                layout_indices=tuple(indices),
                optional=set(indices) != set(range(len(variant.operand_layouts))),
            )
        )
    return tuple(slots)


def operand_slot_for_field(
    slots: tuple[OperandSlot, ...], field: ResolvedField, backend: CodegenUnit
) -> OperandSlot:
    """Return the direct member for a layout field with its exact C++ type."""

    key = (field.name, field_cpp_type(field, backend=backend))
    for slot in slots:
        if (slot.field.name, field_cpp_type(slot.field, backend=backend)) == key:
            return slot
    raise ValueError(f"missing direct member for operand field {key!r}")
