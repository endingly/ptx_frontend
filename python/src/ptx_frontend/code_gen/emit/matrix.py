"""Render typed matrix descriptors through a fixed aggregate template."""

from ptx_frontend.code_gen.render_models.matrix import build_matrix_descriptor
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.spec.model import CodegenUnit, MatrixSpec


def emit_matrix_descriptor(matrix: MatrixSpec, backend: CodegenUnit) -> str:
    """Render one value-owning descriptor with canonical fragment counts."""

    return render_template("fragments/matrix.j2", build_matrix_descriptor(matrix, backend))
