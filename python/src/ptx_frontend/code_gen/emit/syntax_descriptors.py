"""Thin syntax descriptor rendering adapters."""

from pathlib import Path

from ptx_frontend.base.utils import generated_at_comment
from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.cpp_literals import cpp_string as _cpp_string
from ptx_frontend.code_gen.render_models.syntax_descriptors import (
    SyntaxDescriptorCategoryModel, SyntaxDescriptorImplementationModel,
    build_syntax_descriptor_storage, build_syntax_descriptor_getter,
    _cpp_operand_syntax_shape, _cpp_variant_name,
)
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.ir.syntax_ast import SyntaxInstructionDescriptor, from_InstructionSpec
from ptx_frontend.spec.model import CodegenUnit


def generate_syntax_descriptor_source(context: GenerationContext, *, category: str, output_path: Path) -> None:
    """Generate one category using typed storage/getter models."""

    entries = tuple(entry for entry in context.entries if entry.specification.codegen_category == category)
    model = SyntaxDescriptorCategoryModel(generated_at_comment(), category,
        tuple(build_syntax_descriptor_storage(from_InstructionSpec(entry.specification), context.backend,
              cpp_name=entry.cpp_name) for entry in entries),
        tuple(build_syntax_descriptor_getter(cpp_name=entry.cpp_name) for entry in entries))
    content = render_template("descriptors/syntax_category.cpp.j2", model)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def emit_check_end_instruction_descriptor_implementation(descriptor: SyntaxInstructionDescriptor,
        backend: CodegenUnit, *, cpp_name: str) -> str:
    """Render storage whose arrays outlive the public descriptor spans."""

    return render_template("descriptors/syntax_implementation.j2", SyntaxDescriptorImplementationModel(
        build_syntax_descriptor_storage(descriptor, backend, cpp_name=cpp_name),
        build_syntax_descriptor_getter(cpp_name=cpp_name)))


def _emit_instruction_descriptor_storage(descriptor: SyntaxInstructionDescriptor,
        backend: CodegenUnit, *, cpp_name: str) -> str:
    """Render instruction-local syntax storage."""

    return render_template("descriptors/syntax.j2", build_syntax_descriptor_storage(descriptor, backend, cpp_name=cpp_name))


def _emit_instruction_descriptor_getter(*, cpp_name: str) -> str:
    """Render one stable public getter."""

    return render_template("descriptors/syntax_getter.j2", build_syntax_descriptor_getter(cpp_name=cpp_name))
