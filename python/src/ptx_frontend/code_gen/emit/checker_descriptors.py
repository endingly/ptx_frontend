"""Thin checker descriptor adapters over typed storage and fixed templates."""

from pathlib import Path

from ptx_frontend.base.utils import generated_at_comment
from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.render_models.checker_descriptors import (
    CheckerDescriptorCategoryModel, build_checker_descriptor_storage,
    build_checker_descriptor_getter, _cpp_uint64,
)
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.ir.resolved_ir import ResolvedInstruction, ResolvedVariant
from ptx_frontend.spec.model import CodegenUnit


def generate_resolved_checker_descriptor_source(context: GenerationContext, *, category: str, output_path: Path) -> None:
    """Generate category-local checker arrays and their public getters."""

    instructions = tuple(entry.resolved for entry in context.entries
                         if entry.specification.codegen_category == category)
    if not instructions:
        raise ValueError(f"instruction category {category!r} is empty")
    model = CheckerDescriptorCategoryModel(generated_at_comment(), category,
        tuple(build_checker_descriptor_storage(instruction, context.backend) for instruction in instructions),
        tuple(build_checker_descriptor_getter(instruction) for instruction in instructions))
    content = render_template("descriptors/checker_category.cpp.j2", model)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def _emit_instruction_descriptor_storage(instruction: ResolvedInstruction, backend: CodegenUnit) -> str:
    """Render instruction-local checker owners and borrowed spans."""

    return render_template("descriptors/checker.j2", build_checker_descriptor_storage(instruction, backend))


def _emit_instruction_descriptor_getter(instruction: ResolvedInstruction) -> str:
    """Render one public checker descriptor getter."""

    return render_template("descriptors/checker_getter.j2", build_checker_descriptor_getter(instruction))


def _emit_modifier_value_domain_descriptor(entry, *, backend: CodegenUnit) -> str:
    """Render the supported target-independent modifier-value fragment."""

    from ptx_frontend.code_gen.render_models.checker_descriptors import _build_modifier_value
    return render_template("descriptors/checker_modifier_value.j2", _build_modifier_value(entry, backend))


def _emit_variant_modifier_value_domain_descriptors(variant: ResolvedVariant, backend: CodegenUnit) -> str:
    """Render a supported complete variant-local typed modifier-domain array."""

    from ptx_frontend.code_gen.render_models.checker_descriptors import build_checker_modifier_value_domains
    return render_template("descriptors/checker_modifier_domains.j2", build_checker_modifier_value_domains(variant, backend))
