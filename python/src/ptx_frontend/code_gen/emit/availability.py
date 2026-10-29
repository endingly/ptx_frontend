"""Render typed availability descriptors through the shared template engine."""

from collections.abc import Mapping

from ptx_frontend.code_gen.render_models.availability import build_availability, _parse_ptx_version
from ptx_frontend.code_gen.rendering import render_template


def emit_availability(availability: Mapping[str, object]) -> str:
    """Render one validated normalized availability descriptor."""

    return render_template("fragments/availability.j2", build_availability(availability))
