"""Write resolved source files through typed assembly models and fixed templates."""
from __future__ import annotations

from pathlib import Path
from typing import Any, Iterable

from ptx_frontend.code_gen.context import GenerationContext, GenerationInstruction
from ptx_frontend.code_gen.artifact_layout import form_shards
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.code_gen.render_models.resolved_source import (
    _address_symbol_resolution_policy, _wgmma_construction_helper_name,
    MethodsGroupModel, build_wgmma_declarations, build_construction, build_descriptor_getters,
    build_descriptor_shard_source, build_form_methods, build_form_shard_source,
    build_opcode_source, build_resolve, build_visit, build_wgmma_helper,
)


def _write(output_path: Path, template: str, model: Any) -> None:
    """Write UTF-8 text to the existing planned output location."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_template(template, model), encoding='utf-8')


def _emit_visit(entry: GenerationInstruction, variant: Any, backend: Any) -> str:
    """Render the supported ordered reference visitor fragment."""
    return render_template('resolved/fragment_visit.j2', build_visit(entry, variant, backend))


def _emit_construction_cases(entry: GenerationInstruction, variants: Iterable[Any], backend: Any, *, borrowed_fields: bool) -> str:
    """Render construction alternatives using the caller's scratch access mode."""
    return render_template('resolved/fragment_construction.j2', build_construction(entry, variants, backend, borrowed_fields=borrowed_fields))


def _emit_wgmma_construction_helper(entry: GenerationInstruction, shard_index: int, backend: Any) -> str:
    """Render the constructor owned by an existing physical WGMMA shard."""
    return render_template('resolved/fragment_wgmma_helper.j2', build_wgmma_helper(entry, shard_index, backend))


def _emit_wgmma_construction_declarations(entry: GenerationInstruction) -> str:
    """Render private shard-builder declarations at the WGMMA entrypoint."""
    return render_template('resolved/fragment_wgmma_declarations.j2', build_wgmma_declarations(entry))


def _emit_resolve(entry: GenerationInstruction, backend: Any, context: GenerationContext) -> str:
    """Render conservative selection and direct construction dispatch."""
    return render_template('resolved/fragment_resolve.j2', build_resolve(entry, backend, context))


def _emit_sharded_descriptor_getters(entry: GenerationInstruction, shard_count: int) -> str:
    """Render immutable contiguous catalogs assembled from private shards."""
    return render_template('resolved/fragment_getters.j2', build_descriptor_getters(entry, shard_count))


def _emit_form_methods(entry: GenerationInstruction, indexed_variants: Iterable[tuple[int, Any]], backend: Any) -> str:
    """Render exact methods while retaining global descriptor indices."""
    return render_template('resolved/fragment_methods.j2', MethodsGroupModel(build_form_methods(entry, indexed_variants, backend)))


def generate_resolved_opcode_source(context: GenerationContext, *, category: str, opcode: str, output_path: Path) -> None:
    """Write an opcode's nested descriptors, methods and direct resolver."""
    _write(output_path, 'resolved/opcode.cpp.j2', build_opcode_source(context, category=category, opcode=opcode))


def generate_resolved_descriptor_shard_source(context: GenerationContext, *, category: str, opcode: str,
    shard_index: int, output_path: Path) -> None:
    """Write a bounded canonical slice of private descriptor rows."""
    _write(output_path, 'resolved/descriptor_shard.cpp.j2', build_descriptor_shard_source(context,
        category=category, opcode=opcode, shard_index=shard_index))


def generate_resolved_form_shard_source(context: GenerationContext, *, category: str, opcode: str,
    shard_index: int, output_path: Path) -> None:
    """Write a bounded canonical slice of exact methods and shard constructor."""
    _write(output_path, 'resolved/form_shard.cpp.j2', build_form_shard_source(context,
        category=category, opcode=opcode, shard_index=shard_index))
