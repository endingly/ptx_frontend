"""Render fixed collector MMA fragments from typed output models."""

from ptx_frontend.code_gen.render_models.tcgen_mma_rows import StaticMmaModel
from ptx_frontend.code_gen.rendering import render_template


def render_collector_header() -> str:
    """Render the supported collector header fragment."""

    return render_template("tcgen/mma/collector.header.j2", StaticMmaModel())


def render_collector_source() -> str:
    """Render the supported collector source fragment."""

    return render_template("tcgen/mma/collector.source.j2", StaticMmaModel())
