"""Render the known-fact metadata, evaluator, and owned selected adapter."""

from pathlib import Path

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.render_models.tensor_map_known_facts import (
    TensorKnownFactQueryModel,
    build_tensor_map_known_fact_rules,
    build_tensor_known_fact_source,
    build_tensor_known_selected_adapter,
)
from ptx_frontend.code_gen.rendering import render_template


def render_tensor_map_known_fact_rules() -> str:
    """Render closed public IDs and dependency metadata from the rule catalog."""

    return render_template("tensor/known_fact_rules.hpp.j2", build_tensor_map_known_fact_rules())


def render_tensor_map_known_fact_query() -> str:
    """Render the out-of-line caller-fact evaluator."""

    return render_template("tensor/known_fact_query.cpp.j2", TensorKnownFactQueryModel())


def _render_selected_adapter(context: GenerationContext) -> str:
    """Render exact Cp metadata and guarded direct-field projections."""

    return render_template("tensor/known_fact_selected_adapter.cpp.j2",
                           build_tensor_known_selected_adapter(context))


def generate_tensor_map_known_fact_rules(
    context: GenerationContext, *, output_path: Path,
) -> None:
    """Write the public deterministic catalog artifact in the standard plan."""

    del context
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_tensor_map_known_fact_rules(), encoding="utf-8")


def generate_tensor_map_known_fact_query(
    context: GenerationContext, *, output_path: Path,
) -> None:
    """Write the private query and owned selected-form adapter together."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        render_template("tensor/known_fact_source.cpp.j2", build_tensor_known_fact_source(context)),
        encoding="utf-8",
    )
