"""Render private constexpr resolver value-domain lookup tables."""

from pathlib import Path

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.render_models.value_domains import build_value_domain_header, build_ptx_suffix_table
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.spec.model import DomainBackend


def generate_resolved_value_domain_header(context: GenerationContext, *, output_path: Path) -> None:
    """Generate suffix lookup tables selected by backend runtime_lookup metadata."""

    content = render_template("value_domains/header.hpp.j2", build_value_domain_header(context))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def _emit_ptx_suffix_table(name: str, domain: DomainBackend) -> str:
    """Render one stable backend-owned suffix table."""

    return render_template("value_domains/table.j2", build_ptx_suffix_table(name, domain))
