"""Render fixed ws MMA fragments from typed output models."""

from ptx_frontend.code_gen.render_models.tcgen_mma_ws import build_ws
from ptx_frontend.code_gen.render_models.tcgen_mma_rows import StaticMmaModel
from ptx_frontend.code_gen.rendering import render_template


def render_ws_header() -> str:
    """Render the supported ws header fragment."""

    return render_template("tcgen/mma/ws.header.j2", StaticMmaModel())


def render_ws_source() -> str:
    """Render the supported ws source fragment."""

    return render_template("tcgen/mma/ws.source.j2", build_ws())
