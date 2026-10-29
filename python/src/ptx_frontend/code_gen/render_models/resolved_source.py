"""Pure typed source-assembly facts for direct resolved instruction methods."""
from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Iterable
from ptx_frontend.code_gen.context import GenerationContext, GenerationInstruction
from ptx_frontend.code_gen.artifact_layout import INCLUDE_ROOT, form_shards
from ptx_frontend.code_gen.cpp_literals import cpp_string
from ptx_frontend.code_gen.cpp_names import form_name, method_name
from ptx_frontend.code_gen.selection_index import modifier_candidate_rows
from ptx_frontend.code_gen.resolved_layout import operand_slot_for_field, operand_slots
from ptx_frontend.code_gen.reference_policy import REFERENCE_VALUE_KINDS
from ptx_frontend.code_gen.resolved_field_names import field_value_cpp_type
from ptx_frontend.spec.model import CodegenUnit
from ptx_frontend.ir.resolved_ir import ResolvedVariant, ResolvedField, ResolvedFieldStorage, ResolvedOperandLayout, ResolvedValueKind
from ptx_frontend.ir.syntax_ast import from_InstructionSpec
from ptx_frontend.code_gen.render_models.resolved_checks import CheckModel, build_check, named_array_policy
from ptx_frontend.code_gen.render_models.syntax_descriptors import build_syntax_descriptor_storage, SyntaxDescriptorStorageModel
from ptx_frontend.code_gen.render_models.resolved_descriptors import build_resolved_descriptor_storage, ResolvedDescriptorStorageModel
from ptx_frontend.code_gen.render_models.checker_descriptors import build_checker_descriptor_storage, CheckerDescriptorStorageModel

@dataclass(frozen=True)
class ReferenceCallback:
    """One synchronous reference observer call, with its binding policy."""
    method: str
    member: str
    policy: str

@dataclass(frozen=True)
class VisitLayout:
    """Selected layout guard expressions and ordered reference callbacks."""
    index: int
    validity: tuple[str, ...]
    callbacks: tuple[ReferenceCallback, ...]

@dataclass(frozen=True)
class VisitModel:
    """Exact-form reference visitation over validated layout alternatives."""
    name: str
    layouts: tuple[VisitLayout, ...]

@dataclass(frozen=True)
class FieldAssignment:
    """A typed field move from the caller-owned resolution scratch record."""
    member: str
    cpp_type: str
    source_name: str

@dataclass(frozen=True)
class ConstructionLayout:
    """One layout alternative with diagnostic operand names and assignments."""
    index: int
    operand_names: str
    assignments: tuple[FieldAssignment, ...]

@dataclass(frozen=True)
class ConstructionCase:
    """Exact final class and the fields populated before returning ownership."""
    name: str
    modifiers: tuple[FieldAssignment, ...]
    layouts: tuple[ConstructionLayout, ...]

@dataclass(frozen=True)
class ConstructionModel:
    """Construction switch alternatives sharing one scratch access mode."""
    field_access: str
    field_argument: str
    atomic: bool
    cases: tuple[ConstructionCase, ...]

@dataclass(frozen=True)
class WgmmaHelperModel:
    """One physical WGMMA methods shard's private constructor."""
    name: str
    cpp_name: str
    construction: ConstructionModel

@dataclass(frozen=True)
class SelectionForm:
    """Canonical local ordinal and context-validated public form identity."""
    ordinal: str
    name: str
    identity: int
    named_array_policy: str
    helper: str | None

@dataclass(frozen=True)
class CandidateRow:
    """Already selected modifier spelling and known-domain flag."""
    spelling: str
    known: str

@dataclass(frozen=True)
class ResolveModel:
    """Immutable selection storage and the selected construction strategy."""
    prefix: str
    opcode: str
    cpp_name: str
    forms: tuple[SelectionForm, ...]
    rows: tuple[CandidateRow, ...]
    words: tuple[str, ...]
    word_count: int
    construction: ConstructionModel | None
    helpers: tuple[str, ...]

@dataclass(frozen=True)
class FormMethodsModel:
    """Exact class methods with nested checker and reference visitor facts."""
    name: str
    check: CheckModel
    visit: VisitModel

@dataclass(frozen=True)
class DescriptorDomain:
    """One descriptor catalog's type and public opcode member spelling."""
    name: str
    type_name: str
    root_type: str
    opcode_member: str

@dataclass(frozen=True)
class DescriptorGettersModel:
    """Static direct getters or immutable concatenation of canonical shards."""
    prefix: str
    opcode: str
    cpp_name: str
    count: int
    shard_indices: tuple[str, ...]
    domains: tuple[DescriptorDomain, ...]

@dataclass(frozen=True)
class SourceModel:
    """One opcode or shard source with nested descriptor storage models."""
    include_root: str
    category: str
    opcode: str
    prefix: str
    cpp_name: str
    shard: str | None
    syntax_storage: SyntaxDescriptorStorageModel | None
    resolved_storage: ResolvedDescriptorStorageModel | None
    checker_storage: CheckerDescriptorStorageModel | None
    getters: DescriptorGettersModel | None
    methods: tuple[FormMethodsModel, ...]
    resolver: ResolveModel | None
    wgmma_helper: WgmmaHelperModel | None

def _address_symbol_resolution_policy(
    field: ResolvedField, layout: ResolvedOperandLayout
) -> str:
    """Return the immutable address-symbol binding policy for one field."""

    if field.value_kind is not ResolvedValueKind.MOV_SOURCE:
        return "checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace"
    binding = next(
        (item for item in layout.bindings if item.target_field_id == field.name),
        None,
    )
    if binding is None:
        raise ValueError(
            f"layout {layout.layout_id!r} lacks a MOV_SOURCE binding for "
            f"field {field.name!r}"
        )
    if binding.preserve_parameter_address_space:
        return "checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace"
    return "checker::AddressSymbolResolutionPolicy::MaterializeDeviceParameter"


def _wgmma_construction_helper_name(shard_index: int) -> str:
    """Name one private WGMMA construction helper by canonical physical shard."""

    return f"wgmma_construct_{shard_index:03d}"


def build_visit(entry: GenerationInstruction, variant: ResolvedVariant, backend: CodegenUnit) -> VisitModel:
    """Project ordered reference observer calls without rendering source."""
    slots = operand_slots(variant, backend)
    layouts = []
    for index, layout in enumerate(variant.operand_layouts):
        validity = tuple(f"{slot.member_name}.has_value() == {'true' if index in slot.layout_indices else 'false'}"
                         for slot in slots if slot.optional)
        callbacks = []
        for field in layout.fields:
            if field.value_kind not in REFERENCE_VALUE_KINDS:
                continue
            slot = operand_slot_for_field(slots, field, backend)
            callbacks.append(ReferenceCallback(method_name(field_value_cpp_type(field, backend=backend)),
                f"(*{slot.member_name})" if slot.optional else slot.member_name,
                _address_symbol_resolution_policy(field, layout)))
        layouts.append(VisitLayout(index, validity, tuple(callbacks)))
    return VisitModel(form_name(entry, variant), tuple(layouts))


def build_construction(entry: GenerationInstruction, variants: Iterable[ResolvedVariant], backend: CodegenUnit, *, borrowed_fields: bool) -> ConstructionModel:
    """Select final fields, preserving canonical modifier and layout order."""
    cases = []
    for variant in variants:
        slots = operand_slots(variant, backend)
        modifiers = tuple(FieldAssignment(field.name, field_value_cpp_type(field, backend=backend), field.name)
            for field in variant.modifier_fields if field.storage is ResolvedFieldStorage.INSTANCE)
        layouts = tuple(ConstructionLayout(index, ', '.join(field.name for field in layout.fields) or '(none)',
            tuple(FieldAssignment(operand_slot_for_field(slots, field, backend).member_name,
                field_value_cpp_type(field, backend=backend), field.name)
                for field in layout.fields if field.storage is ResolvedFieldStorage.INSTANCE))
            for index, layout in enumerate(variant.operand_layouts))
        cases.append(ConstructionCase(form_name(entry, variant), modifiers, layouts))
    return ConstructionModel('fields.' if borrowed_fields else 'fields->',
        'fields' if borrowed_fields else '*fields', entry.resolved.atomic_address_qualifier is not None, tuple(cases))


def build_wgmma_helper(entry: GenerationInstruction, shard_index: int, backend: CodegenUnit) -> WgmmaHelperModel:
    """Build the constructor owned by the existing physical methods shard."""
    if entry.resolved.atomic_address_qualifier is not None:
        raise ValueError('WGMMA shard construction cannot access an AST qualifier')
    variants = tuple(entry.resolved.variants[index] for index in form_shards(entry)[shard_index])
    return WgmmaHelperModel(_wgmma_construction_helper_name(shard_index), entry.cpp_name,
        build_construction(entry, variants, backend, borrowed_fields=True))


def build_resolve(entry: GenerationInstruction, backend: CodegenUnit, context: GenerationContext) -> ResolveModel:
    """Prepare validated selection tables and direct or sharded constructors."""
    syntax = from_InstructionSpec(entry.specification)
    if len(syntax.variants) != len(entry.resolved.variants) or any(
        left.variant_id != right.variant_id for left, right in zip(syntax.variants, entry.resolved.variants, strict=True)):
        raise ValueError('selection syntax/resolved catalog order mismatch')
    rows = modifier_candidate_rows(syntax)
    word_count = (len(syntax.variants) + 63) // 64
    sharded = entry.specification.opcode == 'wgmma' and bool(form_shards(entry))
    helper_map = {index: _wgmma_construction_helper_name(shard_index)
        for shard_index, indices in enumerate(form_shards(entry)) for index in indices} if sharded else {}
    forms = tuple(SelectionForm(variant.cpp_name, form_name(entry, variant),
        context.identities.form_value(entry, variant), named_array_policy(entry, variant), helper_map.get(index))
        for index, variant in enumerate(entry.resolved.variants))
    return ResolveModel(entry.specification.opcode.replace('.', '_'), entry.resolved.opcode, entry.cpp_name,
        forms, tuple(CandidateRow(cpp_string(spelling), str(known).lower()) for spelling, known, _ in rows),
        tuple(f"0x{(mask >> (word * 64)) & ((1 << 64) - 1):016x}ULL"
            for _, _, mask in rows for word in range(word_count)), word_count,
        None if sharded else build_construction(entry, entry.resolved.variants, backend, borrowed_fields=False),
        tuple(_wgmma_construction_helper_name(index) for index in range(len(form_shards(entry)))) if sharded else ())


def build_form_methods(entry: GenerationInstruction, indexed_variants: Iterable[tuple[int, ResolvedVariant]], backend: CodegenUnit) -> tuple[FormMethodsModel, ...]:
    """Preserve global descriptor indices while composing exact-form methods."""
    return tuple(FormMethodsModel(form_name(entry, variant), build_check(entry, variant, index, backend),
        build_visit(entry, variant, backend)) for index, variant in indexed_variants)


def build_descriptor_getters(entry: GenerationInstruction, shard_count: int) -> DescriptorGettersModel:
    """Describe contiguous catalogs using existing shard ownership and order."""
    domains = (DescriptorDomain('syntax', 'check_end::SyntaxVariantDescriptor', 'check_end::SyntaxInstructionDescriptor', 'Opcode_name'),
        DescriptorDomain('resolved', 'check_end::ResolvedVariantDescriptor', 'check_end::ResolvedInstructionDescriptor', 'opcode_name'),
        DescriptorDomain('checker', 'checker::VariantDescriptor', 'checker::InstructionDescriptor', 'opcode_name'))
    return DescriptorGettersModel(entry.specification.opcode.replace('.', '_'), entry.specification.opcode,
        entry.cpp_name, len(entry.resolved.variants), tuple(f'{index:03d}' for index in range(shard_count)), domains)


def _entry(context: GenerationContext, category: str, opcode: str) -> GenerationInstruction:
    """Look up the unique generation entry for the caller-owned output path."""
    entries = tuple(item for item in context.entries if item.specification.codegen_category == category and item.specification.opcode == opcode)
    if len(entries) != 1:
        raise ValueError(f'expected one {category}/{opcode} entry')
    return entries[0]


def build_opcode_source(context: GenerationContext, *, category: str, opcode: str) -> SourceModel:
    """Compose a complete opcode source without intermediate C++ strings."""
    entry = _entry(context, category, opcode)
    shards = form_shards(entry)
    return SourceModel(INCLUDE_ROOT, category, opcode, opcode.replace('.', '_'), entry.cpp_name, None,
        None if shards else build_syntax_descriptor_storage(from_InstructionSpec(entry.specification), context.backend, cpp_name=entry.cpp_name),
        None if shards else build_resolved_descriptor_storage(entry.resolved, context.backend),
        None if shards else build_checker_descriptor_storage(entry.resolved, context.backend),
        build_descriptor_getters(entry, len(shards)),
        () if shards else build_form_methods(entry, enumerate(entry.resolved.variants), context.backend),
        build_resolve(entry, context.backend, context), None)


def build_descriptor_shard_source(context: GenerationContext, *, category: str, opcode: str, shard_index: int) -> SourceModel:
    """Compose private descriptor rows for a bounded canonical form slice."""
    entry = _entry(context, category, opcode)
    indices = form_shards(entry)[shard_index]
    resolved = replace(entry.resolved, variants=tuple(entry.resolved.variants[index] for index in indices))
    syntax = from_InstructionSpec(replace(entry.specification,
        variants=tuple(entry.specification.variants[index] for index in indices)))
    return SourceModel(INCLUDE_ROOT, category, opcode, opcode.replace('.', '_'), entry.cpp_name, f'{shard_index:03d}',
        build_syntax_descriptor_storage(syntax, context.backend, cpp_name=entry.cpp_name),
        build_resolved_descriptor_storage(resolved, context.backend), build_checker_descriptor_storage(resolved, context.backend),
        None, (), None, None)


def build_form_shard_source(context: GenerationContext, *, category: str, opcode: str, shard_index: int) -> SourceModel:
    """Compose bounded methods and optional WGMMA construction ownership."""
    entry = _entry(context, category, opcode)
    indices = form_shards(entry)[shard_index]
    return SourceModel(INCLUDE_ROOT, category, opcode, opcode.replace('.', '_').replace('-', '_'), entry.cpp_name,
        f'{shard_index:03d}', None, None, None, None,
        build_form_methods(entry, ((index, entry.resolved.variants[index]) for index in indices), context.backend), None,
        build_wgmma_helper(entry, shard_index, context.backend) if opcode == 'wgmma' else None)


@dataclass(frozen=True)
class MethodsGroupModel:
    """Typed root for the supported group-of-methods fragment API."""
    methods: tuple[FormMethodsModel, ...]


@dataclass(frozen=True)
class WgmmaDeclarationsModel:
    """Typed root for private WGMMA construction helper declarations."""
    helpers: tuple[str, ...]


def build_wgmma_declarations(entry: GenerationInstruction) -> WgmmaDeclarationsModel:
    """Prepare declarations following the existing physical shard order."""
    return WgmmaDeclarationsModel(tuple(_wgmma_construction_helper_name(index) for index in range(len(form_shards(entry)))))
