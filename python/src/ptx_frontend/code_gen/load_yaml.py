from pathlib import Path
from typing import Any
import yaml


def load_yaml(path: Path) -> dict[str, Any]:
    """Load a YAML file and return its contents as a dictionary."""
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise TypeError(f"{path}: expected YAML mapping")
    return data


def expand_value_refs(
    values: list[Any], value_sets: dict[str, list[str]]
) -> tuple[str, ...]:
    """Recursively expand ``$name`` references to named value sets."""

    def expand_one(value: Any, active_sets: tuple[str, ...]) -> list[str]:
        if not isinstance(value, str) or not value.startswith("$"):
            return [str(value)]

        set_name = value[1:]
        if set_name not in value_sets:
            raise ValueError(f"unknown value set: {set_name}")
        if set_name in active_sets:
            cycle = " -> ".join((*active_sets, set_name))
            raise ValueError(f"cyclic value-set reference: {cycle}")

        return [
            expanded
            for member in value_sets[set_name]
            for expanded in expand_one(member, (*active_sets, set_name))
        ]

    return tuple(
        expanded
        for value in values
        for expanded in expand_one(value, ())
    )
