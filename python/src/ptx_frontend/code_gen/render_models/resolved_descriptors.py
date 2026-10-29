"""Typed resolved descriptor ownership and backend expression preparation."""

from __future__ import annotations

from dataclasses import dataclass

from ptx_frontend.base.utils import to_file_stem
from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_default, cpp_value
from ptx_frontend.code_gen.render_models.availability import AvailabilityModel, build_availability
from ptx_frontend.code_gen.render_models.matrix import MatrixDescriptorModel, build_matrix_descriptor
from ptx_frontend.spec.model import CodegenUnit, MbarrierStateTokenForm, TextureResourceRole
from ptx_frontend.ir.resolved_ir import (
    ResolvedAddressStateSpace,
    ResolvedField,
    ResolvedInstruction,
    ResolvedModifierBinding,
    ResolvedOperandBinding,
    ResolvedOperandTypeExpression,
    ResolvedOperandTypeExpressionKind,
    TensorAccessMode,
)
from ptx_frontend.code_gen.resolved_field_names import (
    condition_code_cpp_value,
)
from ptx_frontend.code_gen.resolved_value_traits import (
    modifier_default_cpp_expr,
    resolved_modifier_value_traits,
)

# Fixed public enum spellings; normalization has already selected the access mode.
_TENSOR_ACCESS_MODE_CPP = {
    TensorAccessMode.TILED: "Tiled",
    TensorAccessMode.IM2COL_NO_OFFS: "Im2colNoOffs",
    TensorAccessMode.IM2COL: "Im2col",
    TensorAccessMode.IM2COL_W: "Im2colW",
    TensorAccessMode.IM2COL_W128: "Im2colW128",
    TensorAccessMode.TILE_GATHER4: "TileGather4",
    TensorAccessMode.TILE_SCATTER4: "TileScatter4",
}

# Existing public geometry names shared by surface and texture binding facts.
_TEXTURE_GEOMETRY_CPP = {
    "1d": "OneD", "2d": "TwoD", "3d": "ThreeD",
    "a1d": "ArrayOneD", "a2d": "ArrayTwoD", "cube": "Cube",
    "acube": "ArrayCube", "2dms": "TwoDMultisample",
    "a2dms": "ArrayTwoDMultisample",
}


@dataclass(frozen=True)
class ResolvedBindingMemberModel:
    """One optional scalar/expression member of a resolved operand binding."""

    name: str
    expression: str

@dataclass(frozen=True)
class ResolvedTypeExpressionModel:
    """Final discriminant and scalar/field alternatives of a type expression."""

    kind: str
    fixed_scalar_type: str
    modifier_field_id: str

@dataclass(frozen=True)
class ResolvedParameterConstraintModel:
    """A parameter direction and its nested availability constraint."""

    direction: str
    availability: AvailabilityModel
    entry_only: str

@dataclass(frozen=True)
class ResolvedVideoOperandModel:
    """Final video topology names and operand field references."""

    lanes: str
    operation: str
    position: str
    selector: str
    type_use: str
    type_field_id: str
    sat_field_id: str
    po_field_id: str
    allow_immediate: str
    allow_negate: str

@dataclass(frozen=True)
class ResolvedOperandBindingModel:
    """A binding with ordered optional scalar rows and typed nested aggregates."""

    target_field_id: str
    type_expression: ResolvedTypeExpressionModel
    register_width_policy: str
    role: str
    access: str
    allowed_shapes: str
    vector_members: tuple[ResolvedBindingMemberModel, ...]
    signed_vector_range: tuple[int, int] | None
    sink_members: tuple[ResolvedBindingMemberModel, ...]
    sink_availability: AvailabilityModel | None
    symbol_members: tuple[ResolvedBindingMemberModel, ...]
    preserve_parameter_address_space: str
    address_members: tuple[ResolvedBindingMemberModel, ...]
    parameter_constraint: ResolvedParameterConstraintModel | None
    topology_members: tuple[ResolvedBindingMemberModel, ...]
    immediate_conversion_policy: str
    video: ResolvedVideoOperandModel | None

@dataclass(frozen=True)
class ResolvedAddressStateSpaceModel:
    """A state-space value and typed target availability."""

    state_space: str
    availability: AvailabilityModel

@dataclass(frozen=True)
class ResolvedBindingStorageModel:
    """One equality-interned binding group and all arrays borrowed by its spans."""

    name: str
    vector_arities: tuple[tuple[str, tuple[int, ...]], ...]
    register_types: tuple[tuple[str, tuple[str, ...]], ...]
    address_spaces: tuple[tuple[str, tuple[ResolvedAddressStateSpaceModel, ...]], ...]
    bindings: tuple[ResolvedOperandBindingModel, ...]

@dataclass(frozen=True)
class ResolvedFieldModel:
    """One field identifier and final value-kind mapping."""

    field_id: str
    value_kind: str

@dataclass(frozen=True)
class ResolvedModifierDefaultModel:
    """A nonempty typed modifier default's kind, active member, and expression."""

    kind: str
    member: str
    value: str

@dataclass(frozen=True)
class ResolvedModifierBindingModel:
    """A source-to-field binding with an optional typed default."""

    source_kind_id: str
    target_field_id: str
    default: ResolvedModifierDefaultModel | None

@dataclass(frozen=True)
class ResolvedOperandLayoutModel:
    """Layout-local fields borrowing a complete instruction-local binding array."""

    fields_name: str
    layout_id: str
    binding_name: str
    fields: tuple[ResolvedFieldModel, ...]

@dataclass(frozen=True)
class ResolvedVariantModel:
    """Independent variant/layout fields and references to shared binding owners."""

    name: str
    variant_name: str
    condition_code_effect: str
    fields: tuple[ResolvedFieldModel, ...]
    modifier_bindings: tuple[ResolvedModifierBindingModel, ...]
    layouts: tuple[ResolvedOperandLayoutModel, ...]
    matrix: MatrixDescriptorModel | None

@dataclass(frozen=True)
class ResolvedDescriptorStorageModel:
    """Instruction-local owners; binding side arrays precede all variant arrays."""

    storage_name: str
    opcode: str
    binding_groups: tuple[ResolvedBindingStorageModel, ...]
    variants: tuple[ResolvedVariantModel, ...]

@dataclass(frozen=True)
class ResolvedDescriptorGetterModel:
    """Public getter name and generated-detail storage reference."""

    cpp_name: str
    storage_name: str

@dataclass(frozen=True)
class ResolvedDescriptorCategoryModel:
    """Category source with separate ordered storage and getter sections."""

    generated_at: str
    category: str
    storages: tuple[ResolvedDescriptorStorageModel, ...]
    getters: tuple[ResolvedDescriptorGetterModel, ...]


def _build_type_expression(expression: ResolvedOperandTypeExpression, backend: CodegenUnit) -> ResolvedTypeExpressionModel:
    """Select the normalized scalar/field alternative before rendering."""

    fixed = cpp_default(CppDomain.SCALAR_TYPES, backend=backend)
    field = '""'
    if expression.kind is ResolvedOperandTypeExpressionKind.FIXED_SCALAR:
        assert expression.scalar_type is not None
        fixed = cpp_value(CppDomain.SCALAR_TYPES, expression.scalar_type, backend=backend)
    elif expression.kind is ResolvedOperandTypeExpressionKind.MODIFIER_FIELD:
        assert expression.modifier_field_id is not None
        field = f'"{expression.modifier_field_id}"'
    return ResolvedTypeExpressionModel(cpp_value(CppDomain.RESOLVED_OPERAND_TYPE_EXPRESSION_KINDS,
        expression.kind.value, backend=backend), fixed, field)


def _build_operand_binding(binding: ResolvedOperandBinding, name: str, backend: CodegenUnit, *,
                           vector_arities_name: str | None = None, address_state_spaces_name: str | None = None,
                           register_types_name: str | None = None) -> ResolvedOperandBindingModel:
    """Preserve designated-member order and typed nested operand constraints."""

    member = ResolvedBindingMemberModel
    vector: list[ResolvedBindingMemberModel] = []
    if binding.allowed_vector_arities:
        vector.append(member("allowed_vector_arities", vector_arities_name if vector_arities_name is not None else f"{name}_vector_arities"))
    if binding.vector_arity_modifier_field_id is not None:
        vector.append(member("vector_arity_modifier_field_id", f'"{binding.vector_arity_modifier_field_id}"'))
    if binding.allowed_vector_arities or binding.vector_arity_modifier_field_id is not None:
        vector.extend((member("vector_type_policy", f"checker::VectorTypePolicy::{binding.vector_type_policy.value}"),
                       member("allow_vector_sink", str(binding.allow_vector_sink).lower())))
    if binding.allow_named_vector:
        vector.append(member("allow_named_vector", "true"))
    if binding.vector_sink_payload_bits:
        vector.append(member("vector_sink_payload_bits", str(binding.vector_sink_payload_bits)))
    if binding.allowed_vector_register_types:
        vector.append(member("allowed_register_types", register_types_name if register_types_name is not None else f"{name}_register_types"))
    if binding.require_uniform_vector_register_family:
        vector.append(member("require_uniform_register_family", "true"))
    if binding.source_value_vector:
        vector.append(member("source_value_vector", "true"))
    sink: list[ResolvedBindingMemberModel] = []
    if binding.allow_destination_sink:
        sink.append(member("allow_destination_sink", "true"))
    if binding.allow_predicate_sink:
        sink.append(member("allow_predicate_sink", "true"))
    if binding.mbarrier_state_token_form is not MbarrierStateTokenForm.REGISTER:
        sink.append(member("mbarrier_state_token_form", "checker::MbarrierStateTokenForm::" +
            ''.join(part.capitalize() for part in binding.mbarrier_state_token_form.value.split('_'))))
    symbol: list[ResolvedBindingMemberModel] = []
    if binding.allow_function_symbol:
        symbol.append(member("allow_function_symbol", "true"))
    if binding.allow_call_table:
        symbol.append(member("allow_call_table", "true"))
    address: list[ResolvedBindingMemberModel] = []
    if binding.type_tag is not None:
        address.append(member("type_tag", f'"{binding.type_tag}"'))
    if binding.minimum_elements is not None:
        address.extend((member("minimum_elements", str(binding.minimum_elements)), member("maximum_elements", str(binding.maximum_elements))))
    if binding.allowed_element_shapes:
        address.append(member("allowed_element_shapes", " | ".join(cpp_value(CppDomain.RESOLVED_OPERAND_SHAPES,
            shape.value, backend=backend) for shape in binding.allowed_element_shapes)))
    if binding.allowed_address_state_spaces:
        address.append(member("allowed_address_state_spaces", address_state_spaces_name if address_state_spaces_name is not None else f"{name}_address_state_spaces"))
    if binding.state_space_modifier_field_id is not None:
        address.append(member("state_space_modifier_field_id", f'"{binding.state_space_modifier_field_id}"'))
    if binding.address_base_policy.value == "register":
        address.append(member("address_base_policy", "checker::AddressBasePolicy::Register"))
    if binding.address_offset_domain.value == "signed32":
        address.append(member("address_offset_domain", "checker::AddressOffsetDomain::Signed32"))
    parameter = None
    if binding.parameter_constraint is not None:
        policy = binding.parameter_constraint
        parameter = ResolvedParameterConstraintModel(cpp_value(CppDomain.PARAMETER_DIRECTIONS, policy.direction,
            backend=backend), build_availability(dict(policy.function_availability)), str(policy.entry_only).lower())
    topology: list[ResolvedBindingMemberModel] = []
    if binding.tensor_access_mode is not None:
        topology.append(member("expected_tensor_mode", f"TensorAccessMode::{_TENSOR_ACCESS_MODE_CPP[binding.tensor_access_mode]}"))
    if binding.expected_tensor_rank is not None:
        topology.append(member("expected_tensor_rank", "TensorRank::" + ('One', 'Two', 'Three', 'Four', 'Five')[binding.expected_tensor_rank - 1]))
    if binding.surface_geometry is not None:
        topology.append(member("surface_geometry", "SurfaceGeometry::" + _TEXTURE_GEOMETRY_CPP[binding.surface_geometry.value]))
    if binding.texture_geometry is not None:
        topology.append(member("texture_geometry", "TextureGeometry::" + _TEXTURE_GEOMETRY_CPP[binding.texture_geometry.value]))
    if binding.texture_legacy_v4_coordinates:
        topology.append(member("texture_legacy_v4_coordinates", "true"))
    if binding.texture_unbracketed:
        topology.append(member("texture_unbracketed", "true"))
    if binding.texture_resource_kind in {TextureResourceRole.TEXTURE, TextureResourceRole.SAMPLER}:
        topology.append(member("texture_resource_kind", "base::OpaqueResourceKind::" + {
            TextureResourceRole.TEXTURE: "Texture", TextureResourceRole.SAMPLER: "Sampler"}[binding.texture_resource_kind]))
    if binding.texture_resource_kind is TextureResourceRole.SAMPLER_BY_MODE:
        topology.append(member("texture_query_sampler_by_mode", "true"))
    if binding.texture_residency_required:
        topology.append(member("texture_residency_required", "true"))
    if binding.tensor_cta_mask:
        topology.append(member("tensor_cta_mask_role", "TensorCtaMaskRole::MulticastCluster"))
    video = None
    if binding.video is not None:
        slot, instruction = binding.video, binding.video_instruction
        assert instruction is not None
        def cpp(value: str) -> str:
            """Map normalized video names to their established enum spelling."""
            return ''.join(part.title() for part in value.split('_'))
        video = ResolvedVideoOperandModel(cpp(instruction.lanes.value), cpp(instruction.operation.value),
            cpp(slot.position.value), cpp(slot.selector.value), cpp(slot.type_use.value), slot.type_modifier or "",
            instruction.sat_modifier or "", instruction.po_modifier or "", str(slot.allow_immediate).lower(), str(slot.allow_negate).lower())
    return ResolvedOperandBindingModel(binding.target_field_id, _build_type_expression(binding.type_expression, backend),
        cpp_value(CppDomain.REGISTER_WIDTH_POLICIES, binding.register_width_policy.value, backend=backend),
        cpp_value(CppDomain.RESOLVED_OPERAND_ROLES, binding.role.value, backend=backend),
        cpp_value(CppDomain.RESOLVED_OPERAND_ACCESS, binding.access.value, backend=backend),
        " | ".join(cpp_value(CppDomain.RESOLVED_OPERAND_SHAPES, shape.value, backend=backend) for shape in binding.allowed_shapes),
        tuple(vector), binding.vector_signed_immediate_range, tuple(sink),
        build_availability(dict(binding.sink_availability)) if binding.sink_availability else None,
        tuple(symbol), str(binding.preserve_parameter_address_space).lower(), tuple(address), parameter,
        tuple(topology), cpp_value(CppDomain.IMMEDIATE_CONVERSION_POLICIES,
            binding.immediate_conversion_policy.value, backend=backend), video)


def _build_binding_storage(name: str, bindings: tuple[ResolvedOperandBinding, ...], backend: CodegenUnit) -> ResolvedBindingStorageModel:
    """Prepare side-array owners before the binding array that borrows them."""

    return ResolvedBindingStorageModel(name,
        tuple((f"{name}_binding_{index}_vector_arities", binding.allowed_vector_arities)
            for index, binding in enumerate(bindings) if binding.allowed_vector_arities),
        tuple((f"{name}_binding_{index}_register_types", tuple(cpp_value(CppDomain.SCALAR_TYPES,
            value, backend=backend) for value in binding.allowed_vector_register_types))
            for index, binding in enumerate(bindings) if binding.allowed_vector_register_types),
        tuple((f"{name}_binding_{index}_address_state_spaces", tuple(ResolvedAddressStateSpaceModel(
            cpp_value(CppDomain.MEMORY_STATE_SPACES, entry.value, backend=backend), build_availability(dict(entry.availability)))
            for entry in binding.allowed_address_state_spaces))
            for index, binding in enumerate(bindings) if binding.allowed_address_state_spaces),
        tuple(_build_operand_binding(binding, f"{name}_binding_{index}", backend) for index, binding in enumerate(bindings)))


def _build_field(field: ResolvedField, backend: CodegenUnit) -> ResolvedFieldModel:
    """Prepare a field's final backend kind mapping."""

    return ResolvedFieldModel(field.name, cpp_value(CppDomain.RESOLVED_VALUE_KINDS, field.value_kind.value, backend=backend))


def _build_modifier_binding(binding: ResolvedModifierBinding, backend: CodegenUnit) -> ResolvedModifierBindingModel:
    """Select the active default member without pre-rendering its aggregate."""

    default = binding.default_value
    model = None
    if default is not None:
        traits = resolved_modifier_value_traits(default.value_kind)
        value = modifier_default_cpp_expr(default.value_kind, default.value, backend=backend)
        kind = cpp_value(CppDomain.RESOLVED_MODIFIER_DEFAULT_KINDS, default.value_kind.value, backend=backend)
        model = ResolvedModifierDefaultModel(kind, traits.descriptor_member, value)
    return ResolvedModifierBindingModel(binding.source_kind_id, binding.target_field_id, model)


def build_resolved_descriptor_storage(instruction: ResolvedInstruction, backend: CodegenUnit) -> ResolvedDescriptorStorageModel:
    """Intern complete equal binding tuples locally, preserving first-owner order."""

    groups: list[tuple[tuple[ResolvedOperandBinding, ...], str]] = []
    variants: list[ResolvedVariantModel] = []
    for variant in instruction.variants:
        name = to_file_stem(variant.variant_id)
        layouts: list[ResolvedOperandLayoutModel] = []
        for index, layout in enumerate(variant.operand_layouts):
            prefix = next((owner for bindings, owner in groups if bindings == layout.bindings), None)
            if prefix is None:
                prefix = f"{name}_operand_layout_{index}"
                groups.append((layout.bindings, prefix))
            layouts.append(ResolvedOperandLayoutModel(f"{name}_operand_layout_{index}_fields", layout.layout_id,
                f"{prefix}_bindings", tuple(_build_field(field, backend) for field in layout.fields)))
        variants.append(ResolvedVariantModel(name, variant.cpp_name, condition_code_cpp_value(variant.condition_code_effect),
            tuple(_build_field(field, backend) for field in variant.modifier_fields),
            tuple(_build_modifier_binding(binding, backend) for binding in variant.modifier_bindings), tuple(layouts),
            build_matrix_descriptor(variant.matrix, backend) if variant.matrix is not None else None))
    return ResolvedDescriptorStorageModel(f"{instruction.cpp_name}ResolvedDescriptorStorage", instruction.opcode,
        tuple(_build_binding_storage(prefix, bindings, backend) for bindings, prefix in groups), tuple(variants))


def build_resolved_descriptor_getter(instruction: ResolvedInstruction) -> ResolvedDescriptorGetterModel:
    """Prepare the public getter's instruction-local storage reference."""

    return ResolvedDescriptorGetterModel(instruction.cpp_name, f"{instruction.cpp_name}ResolvedDescriptorStorage")

@dataclass(frozen=True)
class ResolvedAddressSpacesFragmentModel:
    """Ordered state-space rows for supported descriptor fragment consumers."""

    entries: tuple[ResolvedAddressStateSpaceModel, ...]


def build_address_spaces_fragment(entries: tuple[ResolvedAddressStateSpace, ...], backend: CodegenUnit) -> ResolvedAddressSpacesFragmentModel:
    """Prepare state-space availability rows independently of renderer state."""

    return ResolvedAddressSpacesFragmentModel(tuple(ResolvedAddressStateSpaceModel(
        cpp_value(CppDomain.MEMORY_STATE_SPACES, entry.value, backend=backend), build_availability(dict(entry.availability)))
        for entry in entries))
