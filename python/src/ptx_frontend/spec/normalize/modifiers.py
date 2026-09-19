from ptx_frontend.spec.load_yaml import expand_value_refs
from typing import Any
from ptx_frontend.spec.model import ModifierSpec, ModifierValueSpec
from .availability import normalize_availability


def normalize_modifier(
    raw: dict[str, Any], reusable_value_sets: dict[str, list[str]]
) -> ModifierSpec:
    """Normalize one modifier and expand its reusable value-set references."""

    raw_values: object = raw.get("values", [])
    if not isinstance(raw_values, list):
        raise TypeError("modifier values must be a list")

    values = _normalize_modifier_values(raw_values, reusable_value_sets)
    if raw["kind"] == "cache" and any(value.value == "unspecified" for value in values):
        raise ValueError(
            f"modifier {raw['name']!r}: cache sentinel 'unspecified' is not a "
            "syntax value"
        )
    _validate_modifier_default(raw, values)

    return ModifierSpec(
        name=raw["name"],
        kind=raw["kind"],
        presence=raw["presence"],
        domain=raw.get("domain"),
        values=values,
        value=raw.get("value"),
        token=raw.get("token"),
        default=raw.get("default"),
    )


def _validate_modifier_default(
    raw: dict[str, Any], values: tuple[ModifierValueSpec, ...]
) -> None:
    """Validate the semantic value used when an optional modifier is omitted."""

    presence = raw["presence"]
    has_default = "default" in raw
    if presence != "optional":
        if has_default:
            raise ValueError(
                f"modifier {raw['name']!r}: default is only valid for optional "
                "modifiers"
            )
        return
    if not has_default:
        raise ValueError(f"optional modifier {raw['name']!r} must define default")

    default = raw["default"]
    kind = raw["kind"]
    if kind == "flag":
        if type(default) is not bool:
            raise ValueError(
                f"optional flag modifier {raw['name']!r} must have a boolean " "default"
            )
        return
    if kind == "type":
        if not isinstance(default, str):
            raise ValueError(
                f"optional type modifier {raw['name']!r} must have a string " "default"
            )
        allowed_values = {value.value for value in values}
        if default not in allowed_values:
            raise ValueError(
                f"optional type modifier {raw['name']!r} has default "
                f"{default!r} outside its allowed values"
            )
        return
    if kind == "rounding":
        if not isinstance(default, str):
            raise ValueError(
                f"optional rounding modifier {raw['name']!r} must have a "
                "string default"
            )
        allowed_values = {value.value for value in values}
        if default not in allowed_values:
            raise ValueError(
                f"optional rounding modifier {raw['name']!r} has default "
                f"{default!r} outside its allowed values"
            )
        return
    if kind == "cache":
        if default != "unspecified":
            raise ValueError(
                f"optional cache modifier {raw['name']!r} must use semantic "
                "default 'unspecified'"
            )
        return
    if kind in {"semantics", "scope"}:
        if not isinstance(default, str):
            raise ValueError(
                f"optional {kind} modifier {raw['name']!r} must have a string "
                "default"
            )
        allowed_values = {value.value for value in values}
        # Omission sentinels intentionally are not spellable modifier values.
        sentinel = "omitted" if kind == "semantics" else "none"
        if default != sentinel and default not in allowed_values:
            raise ValueError(
                f"optional {kind} modifier {raw['name']!r} has default "
                f"{default!r} outside its allowed values"
            )
        return


def _normalize_modifier_values(
    raw_values: list[Any], reusable_value_sets: dict[str, list[str]]
) -> tuple[ModifierValueSpec, ...]:
    """Expand value-set references while preserving per-value availability."""

    values: list[ModifierValueSpec] = []
    seen: set[str | bool | int] = set()
    for raw_value in raw_values:
        if isinstance(raw_value, dict):
            raw_semantic_value = raw_value["value"]
            token = raw_value.get("token")
            availability = normalize_availability(raw_value.get("availability", {}))
        else:
            raw_semantic_value = raw_value
            token = None
            availability = {}

        if isinstance(raw_semantic_value, str) and raw_semantic_value.startswith("$"):
            if token is not None:
                raise ValueError(
                    "a modifier value-set reference cannot define one token "
                    "override for multiple expanded values"
                )
            expanded_values: tuple[str | bool | int, ...] = expand_value_refs(
                [raw_semantic_value], reusable_value_sets
            )
        else:
            expanded_values = (raw_semantic_value,)
        for value in expanded_values:
            if value in seen:
                raise ValueError(f"duplicate modifier value {value!r}")
            seen.add(value)
            values.append(
                ModifierValueSpec(
                    value=value,
                    token=token,
                    availability=availability,
                )
            )
    return tuple(values)


def normalize_modifier_order_aliases(
    raw_variant: dict[str, Any], modifiers: tuple[ModifierSpec, ...]
) -> tuple[tuple[str, ...], ...]:
    """Validate complete historical orders for one variant's modifier slots."""

    raw_aliases = raw_variant.get("modifier_order_aliases", [])
    if not isinstance(raw_aliases, list):
        raise TypeError("modifier_order_aliases must be a list")

    canonical_order = tuple(modifier.name for modifier in modifiers)
    slot_names = set(canonical_order)
    aliases: list[tuple[str, ...]] = []
    for raw_alias in raw_aliases:
        if not isinstance(raw_alias, list) or not all(
            isinstance(slot, str) for slot in raw_alias
        ):
            raise TypeError("modifier_order_aliases entries must be slot-name lists")
        alias = tuple(raw_alias)
        unknown_slots = set(alias) - slot_names
        if unknown_slots:
            raise ValueError(
                f"variant {raw_variant['name']!r}: modifier order alias names "
                f"unknown slots {sorted(unknown_slots)!r}"
            )
        if len(set(alias)) != len(alias):
            raise ValueError(
                f"variant {raw_variant['name']!r}: modifier order alias "
                "repeats a modifier slot"
            )
        if len(alias) != len(canonical_order) or set(alias) != slot_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: modifier order alias must "
                "contain every modifier slot"
            )
        if alias == canonical_order:
            raise ValueError(
                f"variant {raw_variant['name']!r}: modifier order alias "
                "duplicates the canonical modifier order"
            )
        if alias in aliases:
            raise ValueError(
                f"variant {raw_variant['name']!r}: duplicate modifier order alias"
            )
        aliases.append(alias)
    return tuple(aliases)
