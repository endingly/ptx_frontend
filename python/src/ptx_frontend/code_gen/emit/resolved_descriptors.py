"""Thin adapters for typed resolved descriptor storage and getter templates."""

from pathlib import Path

from ptx_frontend.base.utils import generated_at_comment
from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.render_models.resolved_descriptors import (
    ResolvedDescriptorCategoryModel, build_resolved_descriptor_storage,
    build_resolved_descriptor_getter,
)
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.ir.resolved_ir import ResolvedInstruction
from ptx_frontend.spec.model import CodegenUnit


def generate_resolved_descriptor_source(context: GenerationContext, *, category: str, output_path: Path) -> None:
    """Generate category-local owners followed by their public getters."""

    instructions = tuple(entry.resolved for entry in context.entries
                         if entry.specification.codegen_category == category)
    if not instructions:
        raise ValueError(f"instruction category {category!r} is empty")
    model = ResolvedDescriptorCategoryModel(generated_at_comment(), category,
        tuple(build_resolved_descriptor_storage(instruction, context.backend) for instruction in instructions),
        tuple(build_resolved_descriptor_getter(instruction) for instruction in instructions))
    content = render_template("descriptors/resolved_category.cpp.j2", model)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def _emit_resolved_descriptor_storage(instruction: ResolvedInstruction, backend: CodegenUnit) -> str:
    """Render complete equal binding groups once per instruction-local owner."""

    return render_template("descriptors/resolved.j2", build_resolved_descriptor_storage(instruction, backend))


def _emit_resolved_descriptor_getter(instruction: ResolvedInstruction) -> str:
    """Render the public getter outside generated_detail."""

    return render_template("descriptors/resolved_getter.j2", build_resolved_descriptor_getter(instruction))


def _emit_modifier_default_descriptor(binding, backend: CodegenUnit) -> str:
    """Render the supported modifier-default aggregate fragment."""

    from ptx_frontend.code_gen.render_models.resolved_descriptors import _build_modifier_binding
    return render_template("descriptors/resolved_default.j2", _build_modifier_binding(binding, backend))


def _emit_operand_binding_descriptor(binding, vector_arities_name: str, address_state_spaces_name: str,
        backend: CodegenUnit, register_types_name: str = "") -> str:
    """Render a binding fragment with explicit externally owned side-array names."""

    from ptx_frontend.code_gen.render_models.resolved_descriptors import _build_operand_binding
    model = _build_operand_binding(binding, "", backend, vector_arities_name=vector_arities_name,
        address_state_spaces_name=address_state_spaces_name, register_types_name=register_types_name)
    return render_template("descriptors/resolved_binding.j2", model)


def _emit_address_state_spaces(entries, backend: CodegenUnit) -> str:
    """Render supported ordered address-state-space descriptor fragments."""

    from ptx_frontend.code_gen.render_models.resolved_descriptors import build_address_spaces_fragment
    return render_template("descriptors/resolved_address_spaces.j2", build_address_spaces_fragment(entries, backend))
