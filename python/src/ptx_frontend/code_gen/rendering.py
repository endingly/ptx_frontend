"""Render fixed package templates against concrete, independently built models."""

from __future__ import annotations

from dataclasses import is_dataclass

from jinja2 import Environment, PackageLoader, StrictUndefined

# Compile once per template; render calls own their contexts and share no model state.
_ENVIRONMENT = Environment(
    loader=PackageLoader("ptx_frontend.code_gen", "templates"),
    undefined=StrictUndefined,
    autoescape=False,
    newline_sequence="\n",
    keep_trailing_newline=True,
    trim_blocks=False,
    lstrip_blocks=False,
    cache_size=-1,
    auto_reload=False,
)


def render_template(template_name: str, model: object) -> str:
    """Render a fixed package resource with the typed root variable ``model``.

    Templates preserve literal LF whitespace and their final newline. Missing
    resources and model fields raise Jinja's explicit errors. Only compiled
    templates are cached; each invocation builds an independent render context.
    """
    if not is_dataclass(model) or isinstance(model, type):
        raise TypeError("render_template requires a concrete dataclass model")
    return _ENVIRONMENT.get_template(template_name).render(model=model)
