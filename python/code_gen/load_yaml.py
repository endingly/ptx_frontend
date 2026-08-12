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
    values: list[Any], type_sets: dict[str, list[str]]
) -> tuple[str, ...]:
    """Expand value references in a list of values."""
    result: list[str] = []

    for value in values:
        if isinstance(value, str) and value.startswith("$"):
            set_name = value[1:]
            if set_name not in type_sets:
                raise ValueError(f"unknown type set: {set_name}")
            result.extend(type_sets[set_name])
        else:
            result.append(str(value))

    return tuple(result)
