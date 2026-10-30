"""Typed declarations and exact-form contracts for resolved public headers."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any
from ptx_frontend.base.utils import file_stem_to_pascal_case
from ptx_frontend.code_gen.artifact_layout import INCLUDE_ROOT, form_shards
from ptx_frontend.code_gen.cpp_names import REFERENCE_TYPES, form_name, method_name
from ptx_frontend.code_gen.context import GenerationContext, GenerationInstruction
from ptx_frontend.code_gen.resolved_layout import operand_slots
from ptx_frontend.code_gen.resolved_field_names import condition_code_cpp_value, field_cpp_constant_expr, field_cpp_type
from ptx_frontend.code_gen.render_models.availability import AvailabilityModel, build_availability
from ptx_frontend.code_gen.render_models.matrix import MatrixDescriptorModel, build_matrix_descriptor
from ptx_frontend.spec.model import CodegenUnit
from ptx_frontend.ir.resolved_ir import ResolvedVariant, ResolvedField, ResolvedFieldStorage
from ptx_frontend.ir.tensor_reduction import TENSOR_REDUCTION_ELEMENT_TYPES, TensorReductionOp
from ptx_frontend.spec.model import OpaqueResourceKind, SemanticRule, TextureComponent, TextureMipmapMode

@dataclass(frozen=True)
class FabricModel:
    """Closed endpoint, completion, reporting and shared-access obligations."""
    operation: str
    endpoint: str
    shared_access: str
    completion: str
    counted: str
    reports: str
    layout: str

@dataclass(frozen=True)
class SurfaceModel:
    """Immutable surface topology and the selected indirect-resource gate."""
    gate: AvailabilityModel
    geometry: str
    addressing: str
    boundary: str
    operation: str
    query: str
    vector_arity: int

@dataclass(frozen=True)
class StackModel:
    """Static stack operation with width and byte alignment."""
    operation: str
    width: int
    default_alignment: int

@dataclass(frozen=True)
class TextureModel:
    """Exact texture topology, mutable type projection and residency cases."""
    geometry: str
    mipmap: str
    component: str
    query: str
    tested: str
    result_arity: int
    allows_residency: str
    allows_offset: str
    allows_compare: str
    query_level: str
    indirect_gate: AvailabilityModel | None
    selected_types: bool
    residency_layouts: tuple[str, ...] | None

@dataclass(frozen=True)
class CtaGroupModel:
    """Selected tensor CTA-group projection structure and routing names."""
    mode: str
    one: str
    two: str

@dataclass(frozen=True)
class TensorMapReplaceModel:
    """Fixed tensor-map field and its optional encoded value domain."""
    field: str
    value_type: str | None

@dataclass(frozen=True)
class TcgenAllocationModel:
    """Closed allocation action and its local permit effect."""
    action: str
    effect: str

@dataclass(frozen=True)
class TcgenCommitModel:
    """Source address spelling and multicast facts for the fixed commit contract."""
    spelling: str
    multicast: str

@dataclass(frozen=True)
class TcgenCopyPair:
    """One canonical shape/multicast row in static storage."""
    shape: str
    multicast: str

@dataclass(frozen=True)
class TcgenCopyModel:
    """Opaque-copy topology and field-ordered format bitmasks."""
    pairs: tuple[TcgenCopyPair, ...]
    masks: tuple[int, ...]

@dataclass(frozen=True)
class TcgenContractModel:
    """Selected Tensor Memory projections, independent of other form families."""
    allocation: TcgenAllocationModel | None
    commit: TcgenCommitModel | None
    fence: str | None
    copy: TcgenCopyModel | None

@dataclass(frozen=True)
class FormContractModel:
    """Optional exact-form contracts in the established declaration order."""
    reduction: str | None
    cta_group: CtaGroupModel | None
    replacement: TensorMapReplaceModel | None
    matrix: MatrixDescriptorModel | None
    surface: SurfaceModel | None
    video_lanes: str | None
    video_operation: str | None
    texture: TextureModel | None
    stack: StackModel | None
    allocation: TcgenAllocationModel | None
    commit: TcgenCommitModel | None
    fence: str | None
    copy: TcgenCopyModel | None
    floating_point_default: str | None

@dataclass(frozen=True)
class FieldDeclaration:
    """Owned member or static semantic constant with its public spelling."""
    cpp_type: str
    name: str
    source_name: str
    constant: str | None
    optional: bool = False

@dataclass(frozen=True)
class FormDefinition:
    """One exact final instruction class with ordered direct typed members."""
    opcode: str
    variant_name: str
    name: str
    namespace: str
    condition_code: str
    completion: str
    wgmma_action: str
    contract: FormContractModel
    fabric: FabricModel | None
    atomic: bool
    modifiers: tuple[FieldDeclaration, ...]
    operands: tuple[FieldDeclaration, ...]

@dataclass(frozen=True)
class EntrypointsModel:
    """Opcode-level static metadata and resolver declaration spellings."""
    opcode: str
    prefix: str
    cpp_name: str

@dataclass(frozen=True)
class HeaderModel:
    """Opcode header, including an optional aggregate of physical form shards."""
    include_root: str
    category: str
    opcode: str
    shards: tuple[str, ...]
    definitions: tuple[FormDefinition, ...]
    entrypoints: EntrypointsModel | None

@dataclass(frozen=True)
class IdentityForm:
    """Named exact identity with stable hexadecimal encoding."""
    name: str
    value: str

@dataclass(frozen=True)
class IdentityHeaderModel:
    """Opcode-local lightweight typed identities."""
    include_root: str
    namespace: str
    opcode_value: str
    forms: tuple[IdentityForm, ...]

@dataclass(frozen=True)
class CatalogueEntry:
    """One opcode identity include and its exact form names."""
    category: str
    opcode: str
    cpp_name: str
    namespace: str
    forms: tuple[str, ...]

@dataclass(frozen=True)
class CatalogueModel:
    """Canonical include and enum ordering for broad public catalogues."""
    include_root: str
    entries: tuple[CatalogueEntry, ...]

@dataclass(frozen=True)
class ReferenceMethod:
    """Borrowed reference type and its synchronous observer method."""
    cpp_type: str
    name: str

@dataclass(frozen=True)
class ReductionDomainRow:
    """Closed tensor-reduction operation and permitted known element types."""
    name: str
    types: tuple[str, ...]

@dataclass(frozen=True)
class ReductionDomainModel:
    """Static reduction domain shared by public base and fragment adapters."""
    rows: tuple[ReductionDomainRow, ...]

@dataclass(frozen=True)
class BaseHeaderModel:
    """Common polymorphic contracts and optional closed reduction domain."""
    include_root: str
    callbacks: tuple[ReferenceMethod, ...]
    reduction: ReductionDomainModel | None

_TEXTURE_GEOMETRY_CPP = {
    '1d': 'OneD', '2d': 'TwoD', '3d': 'ThreeD', 'a1d': 'ArrayOneD',
    'a2d': 'ArrayTwoD', 'cube': 'Cube', 'acube': 'ArrayCube',
    '2dms': 'TwoDMultisample', 'a2dms': 'ArrayTwoDMultisample',
}


def build_fabric(variant: ResolvedVariant) -> FabricModel | None:
    """Project normalized fabric facts without interpreting the contract again."""
    contract = variant.fabric
    if contract is None:
        return None
    return FabricModel(file_stem_to_pascal_case(contract.operation.value),
        file_stem_to_pascal_case(contract.endpoint.value), file_stem_to_pascal_case(contract.shared_access.value),
        ''.join(part.title() for part in variant.completion_kind.value.split('_')),
        str(contract.counted).lower(), str(contract.reports_fabric).lower(),
        'base::MbarrierLayout::V1' if contract.requires_mbarrier_layout_v1 else 'std::nullopt')


def build_surface(variant: ResolvedVariant) -> SurfaceModel | None:
    """Select optional surface enum expressions and the canonical gate."""
    contract = variant.surface
    if contract is None:
        return None
    def selected(domain: str, value: Any) -> str:
        """Spell a closed enum or an absent exact-form fact."""
        if value is None:
            return 'std::nullopt'
        name = {'b': 'Byte', 'p': 'Sample'}.get(value.value) if domain == 'SurfaceAddressingMode' else None
        if domain == 'SurfaceGeometry':
            name = _TEXTURE_GEOMETRY_CPP[value.value]
        return domain + '::' + (name or file_stem_to_pascal_case(value.value))
    return SurfaceModel(build_availability(contract.indirect_availability),
        selected('SurfaceGeometry', contract.geometry), selected('SurfaceAddressingMode', contract.addressing),
        selected('SurfaceBoundaryMode', contract.boundary), selected('SurfaceReductionOperation', contract.operation),
        selected('SurfaceQuery', contract.query), contract.vector_arity)


def build_stack(variant: ResolvedVariant) -> StackModel | None:
    """Project canonical stack width and default byte alignment."""
    c = variant.stack
    return None if c is None else StackModel(file_stem_to_pascal_case(c.operation.value), c.width, c.default_alignment)


def build_texture(variant: ResolvedVariant) -> TextureModel | None:
    """Project exact texture facts and layout-indexed residency requirements."""
    c = variant.texture
    if c is None:
        return None
    geometry = 'TextureGeometry::' + _TEXTURE_GEOMETRY_CPP[c.geometry.value] if c.geometry is not None else 'std::nullopt'
    mipmap = {TextureMipmapMode.OMITTED: 'Omitted', TextureMipmapMode.BASE: 'Base', TextureMipmapMode.LEVEL: 'Level', TextureMipmapMode.GRADIENT: 'Gradient'}[c.mipmap]
    component = 'TextureComponent::' + {TextureComponent.RED: 'Red', TextureComponent.GREEN: 'Green', TextureComponent.BLUE: 'Blue', TextureComponent.ALPHA: 'Alpha'}[c.component] if c.component is not None else 'std::nullopt'
    query = 'TextureQuery::' + file_stem_to_pascal_case(c.query.value).replace('AddrMode', 'AddressMode') if c.query is not None else 'std::nullopt'
    tested = 'base::OpaqueResourceKind::' + {OpaqueResourceKind.TEXTURE: 'Texture', OpaqueResourceKind.SAMPLER: 'Sampler', OpaqueResourceKind.SURFACE: 'Surface'}[c.tested_kind] if c.tested_kind is not None else 'std::nullopt'
    return TextureModel(geometry, mipmap, component, query, tested, c.result_arity,
        str(c.allows_residency).lower(), str(c.allows_offset).lower(), str(c.allows_compare).lower(), str(c.query_level).lower(),
        None if c.indirect_availability is None else build_availability(c.indirect_availability),
        {field.source_name for field in variant.modifier_fields} >= {'dtype', 'ctype'},
        tuple(str(any(binding.texture_residency_required for binding in layout.bindings)).lower()
            for layout in variant.operand_layouts) if c.geometry is not None else None)


def build_cta_group(variant: ResolvedVariant) -> CtaGroupModel | None:
    """Choose the finite projection structure from normalized CTA-group facts."""
    if variant.tensor_access_mode is None:
        return None
    mode = 'absent' if not variant.tensor_cta_group_applicable else ('written' if any(field.source_name == 'cta_group' for field in variant.modifier_fields) else 'default')
    return CtaGroupModel(mode, 'MulticastDestinations' if variant.tensor_multicast else 'Destination',
        'MulticastParityPeers' if variant.tensor_multicast else 'DestinationOrPeer')


def build_tensor_map_replace(variant: ResolvedVariant) -> TensorMapReplaceModel | None:
    """Validate the sole fixed field and choose its encoded value type."""
    fields = [field.source_name.removeprefix('field_') for field in variant.modifier_fields if field.source_name.startswith('field_')]
    if not fields:
        return None
    if len(fields) != 1:
        raise ValueError('tensor-map replacement must have one fixed field')
    return TensorMapReplaceModel(file_stem_to_pascal_case(fields[0]), {
        'elemtype': 'TensorMapElementType', 'interleave_layout': 'TensorMapInterleaveLayout',
        'swizzle_mode': 'TensorMapSwizzleMode', 'swizzle_atomicity': 'TensorMapSwizzleAtomicity', 'fill_mode': 'TensorMapFillMode',
    }.get(fields[0]))


def build_tcgen_contract(variant: ResolvedVariant) -> TcgenContractModel:
    """Select Tensor Memory contracts without requiring unrelated backend mappings."""
    allocation = {SemanticRule.TENSOR_MEMORY_ALLOC: ('Alloc', 'RequiresPermit'),
        SemanticRule.TENSOR_MEMORY_DEALLOC: ('Dealloc', 'ReleasesAllocation'),
        SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT: ('RelinquishAllocPermit', 'RelinquishesPermit')}
    commit = None
    if variant.rule is SemanticRule.TENSOR_MEMORY_COMMIT:
        if variant.tcgen_commit_address_spelling is None:
            raise ValueError('TCGEN commit lacks address spelling')
        commit = TcgenCommitModel(file_stem_to_pascal_case(variant.tcgen_commit_address_spelling.value), str(variant.tcgen_commit_multicast).lower())
    fence = None
    if variant.rule is SemanticRule.TENSOR_MEMORY_FENCE:
        if variant.tcgen_fence_direction is None:
            raise ValueError('TCGEN fence lacks direction')
        fence = file_stem_to_pascal_case(variant.tcgen_fence_direction.value)
    copy = None
    if variant.rule is SemanticRule.TENSOR_MEMORY_COPY:
        shapes = {'32x32b': 'S32x32b', '16x64b': 'S16x64b', '16x128b': 'S16x128b', '16x256b': 'S16x256b', '16x32bx2': 'S16x32bx2', 's128x256b': 'S128x256b', 's4x256b': 'S4x256b', 's128x128b': 'S128x128b', 's64x128b': 'S64x128b', 's32x128b': 'S32x128b'}
        multicasts = {'none': 'None', 'warpx2_02_13': 'WarpX2_02_13', 'warpx2_01_23': 'WarpX2_01_23', 'warpx4': 'WarpX4'}
        copy = TcgenCopyModel(tuple(TcgenCopyPair(shapes[shape], multicasts[multicast]) for shape, multicast in variant.tcgen_copy_pairs),
            tuple(sum(1 << index for index, present in enumerate(row) if present) for row in variant.tcgen_copy_formats))
    return TcgenContractModel(TcgenAllocationModel(*allocation[variant.rule]) if variant.rule in allocation else None,
        commit, fence, copy)


def build_form_contract(variant: ResolvedVariant, backend: CodegenUnit) -> FormContractModel:
    """Choose existing exact-form projections, preserving declaration order."""
    tcgen = build_tcgen_contract(variant)
    return FormContractModel(None if variant.tensor_reduction_op is None else file_stem_to_pascal_case(variant.tensor_reduction_op.value),
        build_cta_group(variant), build_tensor_map_replace(variant),
        None if variant.matrix is None else build_matrix_descriptor(variant.matrix, backend), build_surface(variant),
        None if variant.video is None else {'scalar': 'Scalar', 'two': 'Two', 'four': 'Four'}[variant.video.lanes.value],
        None if variant.video is None else file_stem_to_pascal_case(variant.video.operation.value),
        build_texture(variant), build_stack(variant), tcgen.allocation, tcgen.commit, tcgen.fence, tcgen.copy,
        None if variant.floating_point_default is None else file_stem_to_pascal_case(variant.floating_point_default.value))


def _identity_namespace(entry: GenerationInstruction) -> str:
    """Return the established lightweight opcode-local namespace."""
    return f'identity::{file_stem_to_pascal_case(entry.specification.codegen_category)}::{entry.cpp_name}'


def build_field(field: ResolvedField, backend: CodegenUnit, name: str, *, optional: bool = False) -> FieldDeclaration:
    """Project a declaration's storage kind, type, name and constant expression."""
    return FieldDeclaration(field_cpp_type(field, backend=backend), name, field.name,
        field_cpp_constant_expr(field, backend=backend) if field.storage is ResolvedFieldStorage.STATIC_CONSTANT else None, optional)


def build_entrypoints(entry: GenerationInstruction) -> EntrypointsModel:
    """Name public opcode-level metadata and resolver entrypoints."""
    return EntrypointsModel(entry.specification.opcode, entry.specification.opcode.replace('.', '_').replace('-', '_'), entry.cpp_name)


def build_opcode_header(context: GenerationContext, *, category: str, opcode: str, include_entrypoints: bool = True,
    form_indices: tuple[int, ...] | None = None) -> HeaderModel:
    """Compose exact classes or the existing aggregate of form-shard headers."""
    entries = tuple(entry for entry in context.entries if entry.specification.codegen_category == category and entry.specification.opcode == opcode)
    if len(entries) != 1:
        raise ValueError(f'expected one {category}/{opcode} entry')
    entry = entries[0]
    shards = form_shards(entry)
    if shards and form_indices is None:
        return HeaderModel(INCLUDE_ROOT, category, opcode, tuple(f'{index:03d}' for index in range(len(shards))), (), build_entrypoints(entry))
    definitions = []
    for index in range(len(entry.resolved.variants)) if form_indices is None else form_indices:
        variant = entry.resolved.variants[index]
        definitions.append(FormDefinition(opcode, variant.cpp_name, form_name(entry, variant), _identity_namespace(entry),
            condition_code_cpp_value(variant.condition_code_effect), ''.join(part.title() for part in variant.completion_kind.value.split('_')),
            ''.join(part.title() for part in variant.wgmma_protocol_action.value.split('_')), build_form_contract(variant, context.backend),
            build_fabric(variant), entry.resolved.atomic_address_qualifier is not None,
            tuple(build_field(field, context.backend, field.name) for field in variant.modifier_fields),
            tuple(build_field(slot.field, context.backend, slot.member_name, optional=slot.optional) for slot in operand_slots(variant, context.backend))))
    return HeaderModel(INCLUDE_ROOT, category, opcode, (), tuple(definitions), build_entrypoints(entry) if include_entrypoints else None)


def build_identity_header(context: GenerationContext, *, category: str, opcode: str) -> IdentityHeaderModel:
    """Prepare stable opcode-local named identity declarations."""
    entry = next(item for item in context.entries if item.specification.codegen_category == category and item.specification.opcode == opcode)
    return IdentityHeaderModel(INCLUDE_ROOT, _identity_namespace(entry), f'{context.identities.opcode_value(entry):08x}',
        tuple(IdentityForm(form_name(entry, variant), f'{context.identities.form_value(entry, variant):08x}') for variant in entry.resolved.variants))


def build_catalogue(context: GenerationContext) -> CatalogueModel:
    """Prepare complete catalogue and umbrella includes in context order."""
    return CatalogueModel(INCLUDE_ROOT, tuple(CatalogueEntry(entry.specification.codegen_category, entry.specification.opcode,
        entry.cpp_name, _identity_namespace(entry), tuple(form_name(entry, variant) for variant in entry.resolved.variants)) for entry in context.entries))


def build_reduction_domain() -> ReductionDomainModel:
    """Project the closed operation/type table into static output rows."""
    return ReductionDomainModel(tuple(ReductionDomainRow(file_stem_to_pascal_case(op.value), tuple(scalar.upper() for scalar in TENSOR_REDUCTION_ELEMENT_TYPES[op])) for op in TensorReductionOp))


def build_base_header(context: GenerationContext) -> BaseHeaderModel:
    """Validate reference type support and describe the common public base."""
    from ptx_frontend.code_gen.reference_policy import REFERENCE_VALUE_KINDS
    from ptx_frontend.code_gen.resolved_field_names import field_value_cpp_type
    for entry in context.entries:
        for variant in entry.resolved.variants:
            for layout in variant.operand_layouts:
                for field in layout.fields:
                    if field.value_kind in REFERENCE_VALUE_KINDS:
                        method_name(field_value_cpp_type(field, backend=context.backend))
    return BaseHeaderModel(INCLUDE_ROOT, tuple(ReferenceMethod(cpp_type, method_name(cpp_type)) for cpp_type in REFERENCE_TYPES),
        build_reduction_domain() if any(variant.tensor_reduction_op is not None for entry in context.entries for variant in entry.resolved.variants) else None)
