"""Render fixed sparse_mx MMA fragments from typed output models."""

from ptx_frontend.code_gen.render_models.tcgen_mma_sparse_mx import build_sparse_mx
from ptx_frontend.code_gen.render_models.tcgen_mma_rows import StaticMmaModel
from ptx_frontend.code_gen.rendering import render_template


def render_sparse_mx_header() -> str:
    """Render the supported sparse_mx header fragment."""

    return render_template("tcgen/mma/sparse_mx.header.j2", StaticMmaModel())


def render_sparse_mx_source() -> str:
    """Render the supported sparse_mx source fragment."""

    return render_template("tcgen/mma/sparse_mx.source.j2", build_sparse_mx())
