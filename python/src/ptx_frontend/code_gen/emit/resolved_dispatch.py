"""Write global identity and resolver dispatch from fixed package templates."""
from pathlib import Path
from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.code_gen.render_models.resolved_dispatch import build_dispatch


def generate_resolved_dispatch_source(context: GenerationContext, *, output_path: Path) -> None:
    """Write prefix decoding and opcode dispatch without all form headers."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_template('resolved/dispatch.cpp.j2', build_dispatch(context)), encoding='utf-8')
