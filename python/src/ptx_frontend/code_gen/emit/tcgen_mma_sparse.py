"""Render fixed sparse MMA fragments from typed output models."""

from ptx_frontend.code_gen.render_models.tcgen_mma_sparse import build_sparse
from ptx_frontend.code_gen.render_models.tcgen_mma_rows import StaticMmaModel
from ptx_frontend.code_gen.rendering import render_template


def render_sparse_header() -> str:
    """Render the supported sparse header fragment."""

    return render_template("tcgen/mma/sparse.header.j2", StaticMmaModel())


def render_sparse_source() -> str:
    """Render the supported sparse source fragment."""

    return render_template("tcgen/mma/sparse.source.j2", build_sparse())
