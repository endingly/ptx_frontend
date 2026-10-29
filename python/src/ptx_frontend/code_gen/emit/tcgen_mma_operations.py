"""Render planned dense, sparse, collector and WS MMA artifacts."""
from pathlib import Path

from ptx_frontend.code_gen.render_models.tcgen_mma_operations import build_mma, build_mma_header
from ptx_frontend.code_gen.rendering import render_template

HEADER_NAME = "tcgen_mma_operations.gen.hpp"
SOURCE_NAME = "resolved_ir_tcgen_mma_operations.gen.cpp"


def render_tcgen_mma_header() -> str:
    """Render public declarations from a fixed typed fragment composition."""

    return render_template("tcgen/mma/operations.header.j2", build_mma_header())


def render_tcgen_mma_source() -> str:
    """Render catalogue-selected static rows and conditional operation queries."""

    return render_template("tcgen/mma/operations.source.j2", build_mma())


def generate_tcgen_mma_header(_context: object, *, output_path: Path) -> None:
    """Write the planned public operational rows."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_tcgen_mma_header(), encoding="utf-8")


def generate_tcgen_mma_source(_context: object, *, output_path: Path) -> None:
    """Write the planned private operational row backing."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_tcgen_mma_source(), encoding="utf-8")
