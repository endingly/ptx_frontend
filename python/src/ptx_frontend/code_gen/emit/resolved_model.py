"""Write resolved public headers through typed models and fixed templates."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from ptx_frontend.code_gen.context import GenerationContext, GenerationInstruction
from ptx_frontend.code_gen.artifact_layout import form_shards
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.code_gen.render_models.resolved_header import (
    _identity_namespace, build_base_header, build_catalogue, build_cta_group,
    build_entrypoints, build_fabric, build_field, build_form_contract, build_identity_header,
    build_opcode_header, build_reduction_domain, build_stack, build_surface,
    build_tensor_map_replace, build_texture, build_tcgen_contract,
)
from ptx_frontend.ir.resolved_ir import ResolvedField


def _write(output_path: Path, template: str, model: Any) -> None:
    """Write UTF-8 generated text to the already planned artifact path."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_template(template, model), encoding='utf-8')


def _fabric_descriptor(variant: Any) -> str:
    """Render the supported exact-form fabric contract fragment."""
    model = build_fabric(variant)
    return '' if model is None else render_template('resolved/fragment_fabric.j2', model)


def _surface_descriptor(variant: Any) -> str:
    """Render the supported exact-form surface contract fragment."""
    model = build_surface(variant)
    return '' if model is None else render_template('resolved/fragment_surface.j2', model)


def _stack_descriptor(variant: Any) -> str:
    """Render the supported exact-form stack contract fragment."""
    model = build_stack(variant)
    return '' if model is None else render_template('resolved/fragment_stack.j2', model)


def _texture_descriptor(variant: Any) -> str:
    """Render the supported exact-form texture contract fragment."""
    model = build_texture(variant)
    return '' if model is None else render_template('resolved/fragment_texture.j2', model)


def _field_declaration(field: ResolvedField, backend: Any, name: str) -> str:
    """Render one direct member with the established ownership documentation."""
    return render_template('resolved/fragment_field.j2', build_field(field, backend, name))


def _tensor_cta_group_contract(variant: Any) -> str:
    """Render a copied written/effective CTA-group role."""
    model = build_cta_group(variant)
    return '' if model is None else render_template('resolved/fragment_cta_group.j2', model)


def _tensor_map_replace_contract(variant: Any) -> str:
    """Render the fixed replacement-field and encoded-value projection."""
    model = build_tensor_map_replace(variant)
    return '' if model is None else render_template('resolved/fragment_replacement.j2', model)


def _form_contract(variant: Any, backend: Any) -> str:
    """Render all normalized immutable contracts for one exact form."""
    return render_template('resolved/fragment_contract.j2', build_form_contract(variant, backend))


def _tcgen_form_contract(variant: Any) -> list[str]:
    """Retain the supported ordered Tensor Memory contract fragment API."""
    model = build_tcgen_contract(variant)
    result = []
    if model.allocation is not None:
        result.append(render_template('resolved/fragment_allocation.j2', model.allocation))
    if model.commit is not None:
        result.append(render_template('resolved/fragment_commit.j2', model.commit))
    if model.fence is not None:
        # The fixed fence fragment reads a typed contract root.
        result.append(render_template('resolved/fragment_fence_contract.j2', model))
    if model.copy is not None:
        result.append(render_template('resolved/fragment_copy.j2', model.copy))
        result.append(render_template('resolved/fragment_copy_view.j2', model.copy))
    return result


def _opcode_entrypoints(entry: GenerationInstruction) -> str:
    """Render public metadata and resolver declarations for one opcode."""
    return render_template('resolved/fragment_entrypoints.j2', build_entrypoints(entry))


def _emit_tensor_reduction_domain() -> str:
    """Render the closed reduction operation/type query fragment."""
    return render_template('resolved/fragment_reduction.j2', build_reduction_domain())


def generate_resolved_base_header(context: GenerationContext, *, output_path: Path) -> None:
    """Write common polymorphic and borrowed-reference contracts."""
    _write(output_path, 'resolved/base.hpp.j2', build_base_header(context))


def generate_resolved_opcode_identity_header(context: GenerationContext, *, category: str, opcode: str, output_path: Path) -> None:
    """Write lightweight opcode-local identities without exact classes."""
    _write(output_path, 'resolved/identity.hpp.j2', build_identity_header(context, category=category, opcode=opcode))


def generate_resolved_identity_catalogue_header(context: GenerationContext, *, output_path: Path) -> None:
    """Write complete public enum catalogues in canonical context order."""
    _write(output_path, 'resolved/catalogue.hpp.j2', build_catalogue(context))


def generate_resolved_opcode_header(context: GenerationContext, *, category: str, opcode: str, output_path: Path,
    include_entrypoints: bool = True, form_indices: tuple[int, ...] | None = None) -> None:
    """Write selected exact classes or their existing form-shard aggregate."""
    _write(output_path, 'resolved/opcode.hpp.j2', build_opcode_header(context, category=category, opcode=opcode,
        include_entrypoints=include_entrypoints, form_indices=form_indices))


def generate_resolved_form_shard_header(context: GenerationContext, *, category: str, opcode: str,
    shard_index: int, output_path: Path) -> None:
    """Write one existing canonical slice of exact public classes."""
    entry = next(item for item in context.entries if item.specification.codegen_category == category and item.specification.opcode == opcode)
    generate_resolved_opcode_header(context, category=category, opcode=opcode, output_path=output_path,
        include_entrypoints=False, form_indices=form_shards(entry)[shard_index])


def generate_resolved_umbrella_header(context: GenerationContext, *, output_path: Path) -> None:
    """Write the installed broad aggregate of generated final forms."""
    _write(output_path, 'resolved/umbrella.hpp.j2', build_catalogue(context))
