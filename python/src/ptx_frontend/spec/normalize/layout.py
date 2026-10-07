from typing import Any
from ptx_frontend.spec.model import (
    OperandKind,
    OperandLayoutSpec,
    OperandLayoutKind,
    OperandSpec,
)
from .availability import normalize_availability
from ptx_frontend.spec.synatax_shapes import OPERAND_SYNTAX_SHAPES
from .operands import normalize_operand


def normalize_operand_layouts(
    raw_variant: dict[str, Any],
    default_operands: Any,
    operand_patterns: dict[str, Any],
) -> tuple[OperandLayoutSpec, ...]:
    """Normalize explicit layouts, or lift the legacy operand list to ``default``."""

    raw_layouts = raw_variant.get("operand_layouts")
    if raw_layouts is None:
        operands = _resolve_operands(
            raw_variant.get("operands", default_operands), operand_patterns
        )
        return (OperandLayoutSpec(name="default", operands=operands),)

    if "operands" in raw_variant:
        raise ValueError(
            f"variant {raw_variant['name']!r} cannot define both operands and "
            "operand_layouts"
        )

    layouts: list[OperandLayoutSpec] = []
    names: set[str] = set()
    for raw_layout in raw_layouts:
        name = raw_layout["name"]
        if name in names:
            raise ValueError(
                f"variant {raw_variant['name']!r} has duplicate operand layout "
                f"name {name!r}"
            )
        names.add(name)
        try:
            kind = OperandLayoutKind(raw_layout.get("kind", "flat"))
        except ValueError as error:
            raise ValueError(
                f"operand layout {name!r}: unsupported kind "
                f"{raw_layout.get('kind')!r}"
            ) from error
        operands = _resolve_operands(raw_layout["operands"], operand_patterns)
        call_operand_kinds = {
            OperandKind.DIRECT_CALL_TARGET,
            OperandKind.INDIRECT_CALL_TARGET,
            OperandKind.INDIRECT_CALL_METADATA,
            OperandKind.CALL_RETURN_PARAMETER,
            OperandKind.CALL_ARGUMENTS,
        }
        if kind not in {
            OperandLayoutKind.CALL,
            OperandLayoutKind.INDIRECT_CALL,
        } and any(operand.kind in call_operand_kinds for operand in operands):
            raise ValueError(
                f"operand layout {name!r}: call operands require kind 'call' "
                "or 'indirect_call'"
            )
        if kind is OperandLayoutKind.CALL:
            call_shapes = {
                (OperandKind.DIRECT_CALL_TARGET,),
                (OperandKind.DIRECT_CALL_TARGET, OperandKind.CALL_ARGUMENTS),
                (
                    OperandKind.CALL_RETURN_PARAMETER,
                    OperandKind.DIRECT_CALL_TARGET,
                    OperandKind.CALL_ARGUMENTS,
                ),
            }
            if tuple(operand.kind for operand in operands) not in call_shapes:
                raise ValueError(
                    f"call operand layout {name!r} must be direct target, "
                    "direct target plus input group, or return group plus "
                    "direct target plus input group"
                )
        if kind is OperandLayoutKind.INDIRECT_CALL:
            call_shapes = {
                (OperandKind.INDIRECT_CALL_TARGET, OperandKind.INDIRECT_CALL_METADATA),
                (
                    OperandKind.INDIRECT_CALL_TARGET,
                    OperandKind.CALL_ARGUMENTS,
                    OperandKind.INDIRECT_CALL_METADATA,
                ),
                (
                    OperandKind.CALL_RETURN_PARAMETER,
                    OperandKind.INDIRECT_CALL_TARGET,
                    OperandKind.CALL_ARGUMENTS,
                    OperandKind.INDIRECT_CALL_METADATA,
                ),
            }
            if tuple(operand.kind for operand in operands) not in call_shapes:
                raise ValueError(
                    f"indirect call operand layout {name!r} must be target plus "
                    "metadata, optionally with input and return groups"
                )
        forbidden_modifiers = tuple(raw_layout.get("forbidden_modifiers", ()))
        _validate_forbidden_modifiers(raw_variant, name, forbidden_modifiers)
        layouts.append(
            OperandLayoutSpec(
                name=name,
                operands=operands,
                kind=kind,
                availability=normalize_availability(raw_layout.get("availability", {})),
                forbidden_modifiers=forbidden_modifiers,
            )
        )
    if not layouts:
        raise ValueError(f"variant {raw_variant['name']!r} has no operand layouts")
    normalized_layouts = tuple(layouts)
    _validate_layout_modifier_reachability(
        raw_variant["name"], raw_variant, normalized_layouts
    )
    _validate_flat_operand_layout_ordering(raw_variant["name"], normalized_layouts)
    return normalized_layouts


def _declared_modifier_presence(raw_variant: dict[str, Any]) -> dict[str, str]:
    """Return each declared modifier slot's presence spelling."""

    return {
        modifier["name"]: modifier["presence"]
        for modifier in raw_variant.get("modifiers", ())
    }


def _validate_forbidden_modifiers(
    raw_variant: dict[str, Any], layout_name: str, forbidden: tuple[str, ...]
) -> None:
    """Require forbidden slots to be declared and still selectable elsewhere."""

    presence_by_slot = _declared_modifier_presence(raw_variant)
    unknown = [slot for slot in forbidden if slot not in presence_by_slot]
    if unknown:
        raise ValueError(
            f"operand layout {layout_name!r}: forbidden_modifiers names "
            f"undeclared modifier slots {sorted(unknown)}"
        )
    # Forbidden references name active optional slots. A fixed or required slot
    # cannot be omitted, so forbidding it would leave the layout unreachable; an
    # absent slot is redundant and cannot be lowered, because the resolved IR
    # drops absent modifiers before slot indexing.
    unsupported = [
        slot for slot in forbidden if presence_by_slot[slot] != "optional"
    ]
    if unsupported:
        raise ValueError(
            f"operand layout {layout_name!r}: forbidden_modifiers may only name "
            f"optional slots, got {sorted(unsupported)}"
        )


def _validate_layout_modifier_reachability(
    variant_name: str,
    raw_variant: dict[str, Any],
    layouts: tuple[OperandLayoutSpec, ...],
) -> None:
    """Require every optional slot to stay spellable through some layout."""

    for slot, presence in _declared_modifier_presence(raw_variant).items():
        if presence != "optional":
            continue
        if all(slot in layout.forbidden_modifiers for layout in layouts):
            raise ValueError(
                f"variant {variant_name!r}: optional modifier slot {slot!r} is "
                "forbidden by every operand layout"
            )


def _modern_pack_interval(operand: OperandSpec) -> tuple[int, int] | None:
    if operand.minimum_elements is None or operand.maximum_elements is None:
        return None  # TODO
    return operand.minimum_elements, operand.maximum_elements


def _flat_slot_overlap(left: OperandSpec, right: OperandSpec) -> bool:
    if not (OPERAND_SYNTAX_SHAPES[left.kind] & OPERAND_SYNTAX_SHAPES[right.kind]):
        return False
    left_interval = _modern_pack_interval(left)
    right_interval = _modern_pack_interval(right)
    if left_interval is None and right_interval is None:
        return True
    if left_interval is not None and right_interval is not None:
        if max(left_interval[0], right_interval[0]) > min(
            left_interval[1], right_interval[1]
        ):
            return False
        return bool(set(left.element_kinds) & set(right.element_kinds))
    return True


def _flat_slot_is_subset(candidate: OperandSpec, other: OperandSpec) -> bool:
    candidate_shapes = OPERAND_SYNTAX_SHAPES[candidate.kind]
    other_shapes = OPERAND_SYNTAX_SHAPES[other.kind]
    if (candidate_shapes & other_shapes) != candidate_shapes:
        return False
    candidate_interval = _modern_pack_interval(candidate)
    other_interval = _modern_pack_interval(other)
    if candidate_interval is None:
        return other_interval is None
    if other_interval is None:
        return True
    return (
        candidate_interval[0] >= other_interval[0]
        and candidate_interval[1] <= other_interval[1]
        and set(candidate.element_kinds) <= set(other.element_kinds)
    )


def _validate_flat_operand_layout_ordering(
    variant_name: str, layouts: tuple[OperandLayoutSpec, ...]
) -> None:
    flat_layouts = tuple(
        layout for layout in layouts if layout.kind is OperandLayoutKind.FLAT
    )
    for left_index, left in enumerate(flat_layouts):
        for right in flat_layouts[left_index + 1 :]:
            if len(left.operands) != len(right.operands):
                continue
            if not all(
                _flat_slot_overlap(left_operand, right_operand)
                for left_operand, right_operand in zip(
                    left.operands, right.operands, strict=True
                )
            ):
                continue
            left_subset = all(
                _flat_slot_is_subset(left_operand, right_operand)
                for left_operand, right_operand in zip(
                    left.operands, right.operands, strict=True
                )
            )
            right_subset = all(
                _flat_slot_is_subset(right_operand, left_operand)
                for left_operand, right_operand in zip(
                    left.operands, right.operands, strict=True
                )
            )
            if left_subset == right_subset:
                raise ValueError(
                    f"variant {variant_name!r}: flat operand layouts {left.name!r} and "
                    f"{right.name!r} accept overlapping syntax without a unique "
                    "most-specific layout"
                )


def _resolve_operands(
    raw_operands: Any, operand_patterns: dict[str, Any]
) -> tuple[OperandSpec, ...]:
    if raw_operands is None:
        raise ValueError(
            "variant has neither operands nor inherited instruction operands"
        )
    if isinstance(raw_operands, str):
        if not raw_operands.startswith("$"):
            raise ValueError(
                "operand-pattern references must use the '$name' form; "
                "use an explicit operand list for inline operands"
            )
        pattern_name = raw_operands[1:]
        try:
            raw_operands = operand_patterns[pattern_name]
        except KeyError as error:
            raise ValueError(f"unknown operand pattern: {pattern_name}") from error
    if not isinstance(raw_operands, list):
        raise TypeError("operands must be an explicit list or a '$name' reference")
    return tuple(normalize_operand(operand) for operand in raw_operands)
