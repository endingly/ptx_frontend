"""Render the fixed TCGEN known-word API from typed catalogue projections."""

from pathlib import Path

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.render_models.tcgen_descriptor_domains import (
    build_tcgen_descriptor_header,
    build_tcgen_descriptor_source,
)
from ptx_frontend.code_gen.rendering import render_template


def generate_tcgen_descriptor_header(
    _context: GenerationContext, *, output_path: Path,
) -> None:
    """Write public value, field, report and layout declarations."""

    content = render_template("tcgen/domains/header.hpp.j2", build_tcgen_descriptor_header())
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def generate_tcgen_descriptor_source(
    _context: GenerationContext, *, output_path: Path,
) -> None:
    """Write pure defined-field algorithms and catalogue-derived static rows."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(_source_text(), encoding="utf-8")


def _source_text() -> str:
    """Render all encoded facts from the immutable Python catalogue."""

    return render_template("tcgen/domains/source.cpp.j2", build_tcgen_descriptor_source())
