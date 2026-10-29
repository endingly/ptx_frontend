"""Typed availability facts prepared independently of rendering."""

from __future__ import annotations

from collections.abc import Mapping

from ptx_frontend.spec.normalize import (
    parse_availability_target,
    validate_availability_family,
    validate_availability_sm_version,
)

from dataclasses import dataclass


@dataclass(frozen=True)
class AvailabilityClauseModel:
    """One normalized target alternative with final C++ scalar expressions."""

    ptx_major: int
    ptx_minor: int
    sm: int
    family: str
    has_exact_target: str = "false"
    architecture: int = 0
    flavor: str = "Generic"
    capabilities: tuple[str, ...] = ()
    map_f64_to_f32: str = "false"


@dataclass(frozen=True)
class AvailabilityModel:
    """Either a legacy minimum target or an ordered disjunction of targets."""

    minimum: AvailabilityClauseModel | None
    clauses: tuple[AvailabilityClauseModel, ...]


def build_availability(availability: Mapping[str, object]) -> AvailabilityModel:
    """Validate metadata and preserve its exact alternative order."""

    if "any_of" not in availability:
        major, minor = _parse_ptx_version(availability.get("ptx", "0.0"))
        return AvailabilityModel(AvailabilityClauseModel(
            major, minor,
            validate_availability_sm_version(availability.get("sm", 0)),
            validate_availability_family(availability["family"]) if "family" in availability else "",
        ), ())
    clauses = availability["any_of"]
    assert isinstance(clauses, list)
    models = []
    for clause in clauses:
        assert isinstance(clause, dict)
        if "map_f64_to_f32" in clause and clause["map_f64_to_f32"] is not True:
            raise ValueError("availability map_f64_to_f32 must be Boolean true")
        major, minor = _parse_ptx_version(clause.get("ptx", "0.0"))
        target = clause.get("target")
        number, flavor = parse_availability_target(target) if target is not None else (0, "Generic")
        family = validate_availability_family(clause["family"]) if "family" in clause else ""
        capabilities = clause.get("capabilities", [])
        assert isinstance(capabilities, list)
        models.append(AvailabilityClauseModel(
            major, minor, validate_availability_sm_version(clause.get("sm", 0)),
            family,
            str(target is not None).lower(), number, flavor, tuple(capabilities),
            str(clause.get("map_f64_to_f32", False)).lower(),
        ))
    return AvailabilityModel(None, tuple(models))


def _parse_ptx_version(value: object) -> tuple[int, int]:
    """Parse decimal major.minor components, each within the uint16_t range."""

    text = str(value)
    pieces = text.split(".")
    if len(pieces) != 2 or not all(piece.isdecimal() for piece in pieces):
        raise ValueError(f"invalid PTX availability version {value!r}")
    major, minor = (int(piece) for piece in pieces)
    if not (0 <= major <= 0xFFFF and 0 <= minor <= 0xFFFF):
        raise ValueError(f"PTX availability version is out of range: {value!r}")
    return major, minor
