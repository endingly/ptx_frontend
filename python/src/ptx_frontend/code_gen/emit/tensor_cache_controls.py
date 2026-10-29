"""Render the owned tensor cache query for exact direct instruction classes."""

from pathlib import Path

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.render_models.tensor_cache_controls import build_tensor_cache_controls
from ptx_frontend.code_gen.rendering import render_template


def render_tensor_cache_controls_query(context: GenerationContext) -> str:
    """Render selected-form dispatch without decoding opaque tensor-map bytes."""

    return render_template("tensor/cache_controls.cpp.j2", build_tensor_cache_controls(context))


def generate_tensor_cache_controls_query(
    context: GenerationContext, *, output_path: Path,
) -> None:
    """Write the private query through the standard global artifact plan."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_tensor_cache_controls_query(context), encoding="utf-8")
