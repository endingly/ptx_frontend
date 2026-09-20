"""Shared C++ availability descriptor rendering."""

from __future__ import annotations

from collections.abc import Mapping

from ptx_frontend.spec.normalize import (
    parse_availability_target,
    validate_availability_family,
    validate_availability_sm_version,
)

def emit_availability(availability: Mapping[str, object]) -> str:
    """Render one normalized availability expression for C++ descriptors."""
    if "any_of" not in availability:
        minimum_ptx = _parse_ptx_version(availability.get("ptx", "0.0"))
        return f"""{{
                  .minimum_ptx_version = {{{minimum_ptx[0]}, {minimum_ptx[1]}}},
                  .minimum_sm_version = {validate_availability_sm_version(availability.get("sm", 0))},
                  .required_family = "{validate_availability_family(availability["family"]) if "family" in availability else ""}",
              }}"""

    clauses = availability["any_of"]
    assert isinstance(clauses, list)
    emitted = []
    for clause in clauses:
        assert isinstance(clause, dict)
        minimum_ptx = _parse_ptx_version(clause.get("ptx", "0.0"))
        target = clause.get("target")
        number, flavor = (
            parse_availability_target(target) if target is not None else (0, "Generic")
        )
        family = (
            validate_availability_family(clause["family"]) if "family" in clause else ""
        )
        capabilities = clause.get("capabilities", [])
        assert isinstance(capabilities, list)
        capability_values = ", ".join(f'"{value}"' for value in capabilities)
        emitted.append(f"""checker::AvailabilityClause{{
                      .minimum_ptx_version = {{{minimum_ptx[0]}, {minimum_ptx[1]}}},
                      .minimum_sm_version = {validate_availability_sm_version(clause.get("sm", 0))},
                      .has_exact_target = {str(target is not None).lower()},
                      .exact_target_architecture = {{{number}}},
                      .exact_target_flavor = base::TargetFlavor::{flavor},
                      .required_family = "{family}",
                      .capabilities = {{{{{capability_values}}}}},
                      .capability_count = {len(capabilities)},
                  }}""")
    return f'''{{
                  .any_of = {{{{
                      {",\n                      ".join(emitted)}
                  }}}},
                  .any_of_count = {len(clauses)},
              }}'''


def _parse_ptx_version(value: object) -> tuple[int, int]:
    text = str(value)
    pieces = text.split(".")
    if len(pieces) != 2 or not all(piece.isdecimal() for piece in pieces):
        raise ValueError(f"invalid PTX availability version {value!r}")
    major, minor = (int(piece) for piece in pieces)
    if not (0 <= major <= 0xFFFF and 0 <= minor <= 0xFFFF):
        raise ValueError(f"PTX availability version is out of range: {value!r}")
    return major, minor
