from typing import Mapping

from ptx_frontend.spec.model import *
from .availability import normalize_availability, UINT64_MAX
from .modifiers import _normalize_modifier_values


def _normalize_operand_type_compatibilities(
    raw_variant: dict[str, Any], layouts: tuple[OperandLayoutSpec, ...]
) -> tuple[OperandTypeCompatibilitySpec, ...]:
    """Normalize value-dependent type rules and validate their operand names."""

    operand_names = {operand.name for layout in layouts for operand in layout.operands}
    result: list[OperandTypeCompatibilitySpec] = []
    seen: set[tuple[str, str, str, int]] = set()
    for raw in raw_variant.get("operand_type_compatibilities", ()):
        operand = raw["operand"]
        if operand not in operand_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: operand type compatibility "
                f"references unknown operand {operand!r}"
            )
        for value in raw["values"]:
            key = (operand, raw["value_kind"], value, raw["instruction_width"])
            if key in seen:
                raise ValueError(
                    f"variant {raw_variant['name']!r}: duplicate operand type "
                    f"compatibility {key!r}"
                )
            seen.add(key)
        result.append(
            OperandTypeCompatibilitySpec(
                operand=operand,
                value_kind=raw["value_kind"],
                values=tuple(raw["values"]),
                instruction_width=raw["instruction_width"],
                effective_type=raw["effective_type"],
                availability=normalize_availability(raw["availability"]),
            )
        )
    return tuple(result)


def _normalize_unified_address_access(raw_variant: Mapping[str, Any]) -> str:
    """Validate and return the declaration-level unified-address policy."""
    access = raw_variant.get("unified_address_access", "none")
    if access not in {"none", "read", "write"}:
        raise ValueError(
            "unified_address_access must be one of 'none', 'read', or 'write'"
        )
    return str(access)


def _normalize_memory_consistency_constraint(
    raw_variant: dict[str, Any],
    modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...],
) -> MemoryConsistencyConstraint | None:
    """Lower the one typed ld/st cross-modifier constraint, if present."""

    matches = [
        item
        for item in raw_variant.get("constraints", ())
        if item.get("kind") == "memory_consistency"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one memory_consistency "
            "constraint is supported"
        )
    raw = matches[0]
    required = {
        "semantics_modifier",
        "scope_modifier",
        "address_operand",
        "type_modifier",
    }
    missing = required - raw.keys()
    if missing:
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency constraint "
            f"is missing {sorted(missing)}"
        )
    modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
    modifier_names = set(modifiers_by_name)
    for key in required - {"address_operand"}:
        value = raw[key]
        if value not in modifier_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency {key} "
                f"references inactive modifier {value!r}"
            )
    operand_names = {operand.name for layout in layouts for operand in layout.operands}
    if raw["address_operand"] not in operand_names:
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency address "
            f"references unknown operand {raw['address_operand']!r}"
        )
    if any(
        operand.name == raw["address_operand"] and operand.kind != "addr"
        for layout in layouts
        for operand in layout.operands
    ):
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency address "
            "operand must have kind 'addr'"
        )
    expected_kinds = {
        "semantics_modifier": "semantics",
        "scope_modifier": "scope",
        "type_modifier": "type",
    }
    for key, expected_kind in expected_kinds.items():
        if modifiers_by_name[raw[key]].kind != expected_kind:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency {key} "
                f"must name a {expected_kind!r} modifier"
            )
    cache_modifier = raw.get("cache_modifier")
    if cache_modifier is not None:
        if cache_modifier not in modifier_names or (
            modifiers_by_name[cache_modifier].kind != "cache"
        ):
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency "
                "cache_modifier must name an active 'cache' modifier"
            )
    mmio_modifier = raw.get("mmio_modifier")
    if mmio_modifier is not None:
        if mmio_modifier not in modifier_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency "
                f"mmio_modifier references inactive modifier {mmio_modifier!r}"
            )
        if modifiers_by_name[mmio_modifier].kind != "flag":
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency "
                "mmio_modifier must name a 'flag' modifier"
            )
    for key in ("state_space_modifier",):
        value = raw.get(key)
        if value is not None and value not in modifier_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency {key} "
                f"references inactive modifier {value!r}"
            )
    if raw.get("state_space_modifier") is not None and (
        modifiers_by_name[raw["state_space_modifier"]].kind != "state_space"
    ):
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency "
            "state_space_modifier must name a state_space modifier"
        )
    raw_mmio_semantics = raw.get("mmio_semantics", ())
    if raw_mmio_semantics and mmio_modifier is None:
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency "
            "mmio_semantics requires mmio_modifier"
        )
    if not isinstance(raw_mmio_semantics, (list, tuple)):
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency "
            "mmio_semantics must be a list"
        )
    mmio_semantics = _normalize_modifier_values(list(raw_mmio_semantics), {})
    semantic_values = {
        value.value for value in modifiers_by_name[raw["semantics_modifier"]].values
    }
    if any(value.value not in semantic_values for value in mmio_semantics):
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency "
            "mmio_semantics must be admitted by semantics_modifier"
        )
    return MemoryConsistencyConstraint(
        semantics_modifier=raw["semantics_modifier"],
        scope_modifier=raw["scope_modifier"],
        cache_modifier=cache_modifier,
        address_operand=raw["address_operand"],
        type_modifier=raw["type_modifier"],
        mmio_modifier=mmio_modifier,
        state_space_modifier=raw.get("state_space_modifier"),
        mmio_semantics=mmio_semantics,
    )


def _normalize_address_alignment_constraints(
    raw_variant: dict[str, Any],
    modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...],
) -> tuple[AddressAlignmentConstraint, ...]:
    """Lower data-driven natural-address-alignment rules."""

    matches = [
        item
        for item in raw_variant.get("constraints", ())
        if item.get("kind") == "address_alignment"
    ]
    if not matches:
        return ()
    normalized: list[AddressAlignmentConstraint] = []
    for raw in matches:
        has_singular = "address_operand" in raw
        has_plural = "address_operands" in raw
        if has_singular == has_plural:
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment constraint "
                "requires exactly one of address_operand or address_operands"
            )
        address_operands = (
            (raw["address_operand"],)
            if has_singular
            else tuple(raw["address_operands"])
        )
        if (
            not address_operands
            or any(not isinstance(operand, str) for operand in address_operands)
            or len(set(address_operands)) != len(address_operands)
        ):
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment address "
                "operands must be unique non-empty identifiers"
            )
        source_count = sum(
            key in raw for key in ("type_modifier", "immediate_operand", "alignment")
        )
        if source_count != 1:
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment requires "
                "exactly one of type_modifier, immediate_operand, or alignment"
            )
        modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
        for key, expected_kind in (
            ("type_modifier", "type"),
            ("vector_modifier", "vector"),
        ):
            value = raw.get(key)
            if value is None:
                continue
            if value not in modifiers_by_name:
                raise ValueError(
                    f"variant {raw_variant['name']!r}: address_alignment {key} "
                    f"references inactive modifier {value!r}"
                )
            if modifiers_by_name[value].kind != expected_kind:
                raise ValueError(
                    f"variant {raw_variant['name']!r}: address_alignment {key} "
                    f"must name a {expected_kind!r} modifier"
                )
        matching_operands = [
            [operand for operand in layout.operands if operand.name in address_operands]
            for layout in layouts
        ]
        if any(
            len(operands) != len(address_operands)
            or {operand.name for operand in operands} != set(address_operands)
            or any(operand.kind != "addr" for operand in operands)
            for operands in matching_operands
        ):
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment address "
                "operand must name an active kind 'addr' operand"
            )
        immediate_operand = raw.get("immediate_operand")
        if immediate_operand is not None:
            matching_immediates = [
                operand
                for layout in layouts
                for operand in layout.operands
                if operand.name == immediate_operand
            ]
            if not matching_immediates or any(
                operand.kind != "imm" for operand in matching_immediates
            ):
                raise ValueError(
                    f"variant {raw_variant['name']!r}: address_alignment "
                    "immediate_operand must name an active kind 'imm' operand"
                )
        alignment = raw.get("alignment")
        if alignment is not None and (type(alignment) is not int or alignment <= 0):
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment alignment "
                "must be a positive integer"
            )
        normalized.append(
            AddressAlignmentConstraint(
                address_operands=address_operands,
                type_modifier=raw.get("type_modifier"),
                vector_modifier=raw.get("vector_modifier"),
                immediate_operand=immediate_operand,
                alignment=alignment,
            )
        )
    seen_address_operands: set[str] = set()
    for constraint in normalized:
        overlap = seen_address_operands.intersection(constraint.address_operands)
        if overlap:
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment address "
                "operand may appear in at most one constraint"
            )
        seen_address_operands.update(constraint.address_operands)
    return tuple(normalized)


def _normalize_memory_vector_constraint(
    raw_variant: dict[str, Any],
    modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...],
) -> MemoryVectorConstraint | None:
    """Lower the typed PTX 8.8 256-bit ld/st vector rule, if present."""

    matches = [
        item
        for item in raw_variant.get("constraints", ())
        if item.get("kind") == "memory_vector"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one memory_vector "
            "constraint is supported"
        )
    raw = matches[0]
    required = {"type_modifier", "vector_operand", "address_operand", "availability"}
    missing = required - raw.keys()
    if missing:
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_vector constraint "
            f"is missing {sorted(missing)}"
        )
    modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
    for key, expected_kind in (("type_modifier", "type"),):
        value = raw[key]
        if value not in modifiers_by_name:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector {key} "
                f"references inactive modifier {value!r}"
            )
        if modifiers_by_name[value].kind != expected_kind:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector {key} "
                f"must name a {expected_kind!r} modifier"
            )
    operand_by_name = {
        operand.name: operand for layout in layouts for operand in layout.operands
    }
    for key, expected_kind in (
        ("vector_operand", "reg_vector"),
        ("address_operand", "addr"),
    ):
        value = raw[key]
        operand = operand_by_name.get(value)
        if operand is None:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector {key} "
                f"references unknown operand {value!r}"
            )
        if operand.kind != expected_kind:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector {key} "
                f"must name a {expected_kind!r} operand"
            )
    state_space_modifier = raw.get("state_space_modifier")
    if state_space_modifier is not None:
        modifier = modifiers_by_name.get(state_space_modifier)
        if modifier is None:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector state_space_modifier "
                f"references inactive modifier {state_space_modifier!r}"
            )
        if modifier.kind != "state_space":
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector state_space_modifier "
                "must name a 'state_space' modifier"
            )
    availability = raw["availability"]
    if not isinstance(availability, dict):
        raise TypeError("memory_vector availability must be an object")
    return MemoryVectorConstraint(
        type_modifier=raw["type_modifier"],
        vector_operand=raw["vector_operand"],
        address_operand=raw["address_operand"],
        availability=normalize_availability(availability),
        state_space_modifier=state_space_modifier,
        require_modern=bool(raw.get("require_modern", False)),
    )


def _normalize_immediate_value_constraint(
    raw_variant: dict[str, Any], layouts: tuple[OperandLayoutSpec, ...]
) -> ImmediateValueConstraint | None:
    """Lower one exact integer allowlist for an immediate operand."""

    matches = [
        item
        for item in raw_variant.get("constraints", ())
        if item.get("kind") == "immediate_value"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one immediate_value "
            "constraint is supported"
        )
    raw = matches[0]
    if set(raw) != {"kind", "operand", "values"}:
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_value constraint "
            "requires only operand and values"
        )
    operand_name = raw["operand"]
    values = raw["values"]
    _validate_immediate_constraint_operand(
        raw_variant, layouts, operand_name, "immediate_value"
    )
    if not isinstance(values, list) or not values:
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_value values must "
            "be unique non-negative integers"
        )
    for index, value in enumerate(values):
        _validate_uint64(raw_variant, "immediate_value", f"values[{index}]", value)
    if len(set(values)) != len(values):
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_value values must "
            "be unique non-negative integers"
        )
    return ImmediateValueConstraint(operand=operand_name, values=tuple(values))


def _normalize_immediate_range_constraints(
    raw_variant: dict[str, Any], layouts: tuple[OperandLayoutSpec, ...]
) -> tuple[ImmediateRangeConstraint, ...]:
    """Lower inclusive non-negative ranges, one for each immediate operand."""

    matches = [
        item
        for item in raw_variant.get("constraints", ())
        if item.get("kind") == "immediate_range"
    ]
    if not matches:
        return ()
    ranges = []
    operands = set()
    for raw in matches:
        if set(raw) not in (
            {"kind", "operand", "minimum"},
            {"kind", "operand", "minimum", "maximum"},
        ):
            raise ValueError(
                f"variant {raw_variant['name']!r}: immediate_range constraint "
                "requires operand, minimum, and optional maximum"
            )
        operand_name, minimum, maximum = (
            raw["operand"],
            raw["minimum"],
            raw.get("maximum"),
        )
        if operand_name in operands:
            raise ValueError(
                f"variant {raw_variant['name']!r}: duplicate immediate_range "
                f"operand {operand_name!r}"
            )
        operands.add(operand_name)
        _validate_immediate_constraint_operand(
            raw_variant,
            layouts,
            operand_name,
            "immediate_range",
            allowed_kinds=("imm", "reg_or_imm"),
        )
        _validate_uint64(raw_variant, "immediate_range", "minimum", minimum)
        if maximum is not None:
            _validate_uint64(raw_variant, "immediate_range", "maximum", maximum)
        if maximum is not None and maximum < minimum:
            raise ValueError(
                f"variant {raw_variant['name']!r}: immediate_range bounds must "
                "be non-negative integers with optional maximum >= minimum"
            )
        ranges.append(
            ImmediateRangeConstraint(
                operand=operand_name, minimum=minimum, maximum=maximum
            )
        )
    return tuple(ranges)


def _normalize_immediate_multiple_of_constraint(
    raw_variant: dict[str, Any], layouts: tuple[OperandLayoutSpec, ...]
) -> ImmediateMultipleOfConstraint | None:
    """Lower one positive-divisor constraint for an immediate operand."""

    matches = [
        item
        for item in raw_variant.get("constraints", ())
        if item.get("kind") == "immediate_multiple_of"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one immediate_multiple_of "
            "constraint is supported"
        )
    raw = matches[0]
    if set(raw) != {"kind", "operand", "divisor"}:
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_multiple_of constraint "
            "requires only operand and divisor"
        )
    operand_name, divisor = raw["operand"], raw["divisor"]
    _validate_immediate_constraint_operand(
        raw_variant,
        layouts,
        operand_name,
        "immediate_multiple_of",
        allowed_kinds=("imm", "reg_or_imm"),
    )
    _validate_uint64(raw_variant, "immediate_multiple_of", "divisor", divisor)
    if divisor <= 0:
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_multiple_of divisor must "
            "be a positive integer"
        )
    return ImmediateMultipleOfConstraint(operand=operand_name, divisor=divisor)


def _validate_immediate_constraint_operand(
    raw_variant: dict[str, Any],
    layouts: tuple[OperandLayoutSpec, ...],
    operand_name: str,
    constraint_kind: str,
    *,
    allowed_kinds: tuple[str, ...] = ("imm",),
) -> None:
    """Require one occurrence and validate every layout where it is present."""

    found = False
    for layout in layouts:
        matching = tuple(
            operand for operand in layout.operands if operand.name == operand_name
        )
        if not matching:
            continue
        found = True
        disallowed = next(
            (operand for operand in matching if operand.kind not in allowed_kinds), None
        )
        if disallowed is not None:
            raise ValueError(
                f"variant {raw_variant['name']!r}: {constraint_kind} operand "
                f"{operand_name!r} in operand layout {layout.name!r} must have "
                f"kind {' or '.join(repr(kind) for kind in allowed_kinds)}, not "
                f"{disallowed.kind!r}"
            )
    if not found:
        raise ValueError(
            f"variant {raw_variant['name']!r}: {constraint_kind} operand "
            f"{operand_name!r} must exist in at least one operand layout as kind "
            f"{' or '.join(repr(kind) for kind in allowed_kinds)}"
        )


def _validate_uint64(
    raw_variant: dict[str, Any], constraint_kind: str, field: str, value: object
) -> None:
    """Require one immediate constraint value to fit the generated uint64_t."""

    if type(value) is int and 0 <= value <= UINT64_MAX:
        return
    requirement = (
        "a positive integer representable as uint64"
        if constraint_kind == "immediate_multiple_of" and field == "divisor"
        else "non-negative uint64 integers"
    )
    raise ValueError(
        f"variant {raw_variant['name']!r}: {constraint_kind} constraint field "
        f"{field!r} value {value!r} must be {requirement}"
    )
