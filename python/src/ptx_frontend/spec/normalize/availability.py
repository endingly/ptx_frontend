import ctypes
import re
from typing import Any

UINT64_MAX = ctypes.c_uint64(-1).value
UINT32_MAX = ctypes.c_uint32(-1).value

_AVAILABILITY_TARGET = re.compile(r"sm_([1-9][0-9]*)([af]?)$")
_AVAILABILITY_FAMILY = re.compile(r"sm_[1-9][0-9]*f$")


def validate_availability_sm_version(
    value: object, *, field: str = "availability SM version"
) -> int:
    """Return one SM version representable by the generated C++ descriptor."""

    if not isinstance(value, int) or not 0 <= value <= UINT32_MAX:
        raise ValueError(f"{field} must be a uint32")

    return value


def parse_availability_target(target: object) -> tuple[int, str]:
    """Parse an exact availability target into its architecture and flavor."""

    match = _AVAILABILITY_TARGET.fullmatch(target) if isinstance(target, str) else None
    if match is None:
        raise ValueError("availability target must be an sm_<number>[a|f] spelling")
    return (
        validate_availability_sm_version(
            int(match.group(1)), field="availability target architecture"
        ),
        {
            "": "Generic",
            "a": "ArchitectureSpecific",
            "f": "FamilySpecific",
        }[match.group(2)],
    )


def validate_availability_family(family: object) -> str:
    """Return one YAML minimum family-specific feature target spelling."""

    if not isinstance(family, str) or _AVAILABILITY_FAMILY.fullmatch(family) is None:
        raise ValueError("availability family must be an sm_<number>f spelling")
    return family


def normalize_availability(raw: object) -> dict[str, Any]:
    """Validate the legacy availability form or its bounded DNF replacement."""

    if not isinstance(raw, dict):
        raise TypeError("availability must be an object")
    if not raw:
        return {}
    if "any_of" not in raw:
        allowed = {"ptx", "sm", "family", "deprecated", "removed", "notes"}
        if set(raw) - allowed or not set(raw) & {"ptx", "sm", "family"}:
            raise ValueError("availability must contain a legacy requirement or any_of")
        if "sm" in raw:
            validate_availability_sm_version(raw["sm"])
        if "family" in raw:
            validate_availability_family(raw["family"])
        return dict(raw)
    if set(raw) != {"any_of"}:
        raise ValueError("any_of availability cannot mix with legacy fields")
    clauses = raw["any_of"]
    if not isinstance(clauses, list) or not 1 <= len(clauses) <= 5:
        raise ValueError("availability any_of must contain one to five clauses")
    normalized: list[dict[str, Any]] = []
    for clause in clauses:
        if not isinstance(clause, dict):
            raise TypeError("availability any_of clauses must be objects")
        if (
            set(clause) - {"ptx", "sm", "target", "family", "capabilities"}
            or not clause
        ):
            raise ValueError("availability any_of clause has invalid fields")
        if "sm" in clause:
            validate_availability_sm_version(clause["sm"])
        if "target" in clause:
            parse_availability_target(clause["target"])
        if "family" in clause:
            validate_availability_family(clause["family"])
        capabilities = clause.get("capabilities")
        if capabilities is not None:
            if (
                not isinstance(capabilities, list)
                or not 1 <= len(capabilities) <= 4
                or len(capabilities) != len(set(capabilities))
                or not all(isinstance(item, str) and item for item in capabilities)
            ):
                raise ValueError(
                    "availability capabilities must be one to four unique names"
                )
        normalized.append(dict(clause))
    return {"any_of": normalized}
