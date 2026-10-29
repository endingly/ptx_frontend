"""Typed checker descriptor arrays and nested policy aggregates."""

from __future__ import annotations

from dataclasses import dataclass
from collections.abc import Mapping

from ptx_frontend.code_gen.render_models.availability import AvailabilityModel, build_availability
from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_value
from ptx_frontend.spec.model import CodegenUnit
from ptx_frontend.ir.resolved_ir import (
    ResolvedInstruction,
    ResolvedModifierValueDomain,
    ResolvedModifierValueAvailability,
    ResolvedVariant,
    ResolvedValueKind,
)
from ptx_frontend.code_gen.resolved_value_traits import (
    modifier_descriptor_default_members,
    modifier_value_cpp_expr,
)


@dataclass(frozen=True)
class CheckerModifierMemberModel:
    """One fixed typed value member's final backend expression."""

    name: str
    expression: str

@dataclass(frozen=True)
class CheckerModifierValueModel:
    """Typed modifier value plus optional target availability."""

    descriptor_type: str
    kind_id: str
    value_kind: str
    members: tuple[CheckerModifierMemberModel, ...]
    availability: AvailabilityModel | None

@dataclass(frozen=True)
class CheckerLayoutModel:
    """Layout availability and static slot-index storage references."""

    name: str
    layout_name: str
    availability: AvailabilityModel
    forbidden_slots: tuple[int, ...]
    forbidden_reference: str

@dataclass(frozen=True)
class CheckerTypeCompatibilityModel:
    """A normalized special-register compatibility alternative."""

    target_field_id: str
    special_register_kind: str
    instruction_width: int
    effective_type: str
    availability: AvailabilityModel

@dataclass(frozen=True)
class CheckerImmediateRangeModel:
    """Portable exact unsigned immediate bounds."""

    operand_field_id: str
    minimum: str
    has_maximum: str
    maximum: str

@dataclass(frozen=True)
class CheckerAddressAlignmentModel:
    """Alignment constraint and the field-name array its span borrows."""

    name: str
    address_fields: tuple[str, ...]
    type_field_id: str
    vector_field_id: str
    immediate_operand_field_id: str
    alignment: int

@dataclass(frozen=True)
class CheckerMmioSemanticModel:
    """A final MMIO semantic value and nested availability."""

    semantics: str
    availability: AvailabilityModel

@dataclass(frozen=True)
class CheckerMemoryConsistencyModel:
    """Memory consistency field references and ordered MMIO alternatives."""

    semantics_field_id: str
    scope_field_id: str
    mmio_field_id: str
    cache_field_id: str
    address_field_id: str
    type_field_id: str
    state_space_field_id: str
    mmio_name: str
    mmio_semantics: tuple[CheckerMmioSemanticModel, ...]

@dataclass(frozen=True)
class CheckerMemoryVectorModel:
    """Vector memory field references with modern-target policy."""

    type_field_id: str
    vector_field_id: str
    address_field_id: str
    state_space_field_id: str
    availability: AvailabilityModel
    require_modern: str

@dataclass(frozen=True)
class CheckerImmediateValueModel:
    """An operand's exact immediate domain and its static array owner."""

    operand_field_id: str
    values: tuple[str, ...]

@dataclass(frozen=True)
class CheckerImmediateMultipleModel:
    """An immediate divisibility constraint."""

    operand_field_id: str
    divisor: str

@dataclass(frozen=True)
class CheckerAtomicAddressModel:
    """Atomic field references and the variant's exact written suffix domain."""

    state_space_field_id: str
    address_operand_id: str
    values: tuple[str, ...]

@dataclass(frozen=True)
class CheckerVariantModel:
    """Ordered checker arrays and typed optional variant constraint aggregates."""

    name: str
    availability: AvailabilityModel
    value_availabilities: tuple[CheckerModifierValueModel, ...]
    value_domains: tuple[CheckerModifierValueModel, ...]
    layouts: tuple[CheckerLayoutModel, ...]
    type_compatibilities: tuple[CheckerTypeCompatibilityModel, ...]
    rule_id: str
    permits_unified_address: str
    unified_address_access: str
    memory_consistency: CheckerMemoryConsistencyModel | None
    memory_vector: CheckerMemoryVectorModel | None
    immediate_value: CheckerImmediateValueModel | None
    immediate_ranges: tuple[CheckerImmediateRangeModel, ...]
    immediate_multiple: CheckerImmediateMultipleModel | None
    address_alignments: tuple[CheckerAddressAlignmentModel, ...]
    atomic_address: CheckerAtomicAddressModel | None

@dataclass(frozen=True)
class CheckerDescriptorStorageModel:
    """Instruction-local checker arrays backing every descriptor span."""

    storage_name: str
    opcode: str
    variants: tuple[CheckerVariantModel, ...]

@dataclass(frozen=True)
class CheckerDescriptorGetterModel:
    """Public getter name and its generated-detail static owner."""

    cpp_name: str
    storage_name: str

@dataclass(frozen=True)
class CheckerDescriptorCategoryModel:
    """Category source with ordered static storage and public getters."""

    generated_at: str
    category: str
    storages: tuple[CheckerDescriptorStorageModel, ...]
    getters: tuple[CheckerDescriptorGetterModel, ...]


def _cpp_uint64(value: int) -> str:
    """Prepare a portable exact uint64_t initializer."""

    return f"uint64_t{{{value}ULL}}"


def _build_modifier_value(entry: ResolvedModifierValueAvailability | ResolvedModifierValueDomain,
                          backend: CodegenUnit,
                          defaults: Mapping[ResolvedValueKind, str] | None = None) -> CheckerModifierValueModel:
    """Prepare ordered typed value members using the existing backend traits."""

    selected_value = modifier_value_cpp_expr(entry.value_kind, entry.value, backend=backend)
    if defaults is None:
        defaults = modifier_descriptor_default_members(backend=backend)
    members = (
        ("bool_value", ResolvedValueKind.BOOL), ("scalar_type", ResolvedValueKind.SCALAR_TYPE),
        ("rounding_mode", ResolvedValueKind.ROUNDING_MODE), ("comparison_operator", ResolvedValueKind.COMPARISON_OPERATOR),
        ("test_property", ResolvedValueKind.TEST_PROPERTY), ("boolean_operator", ResolvedValueKind.BOOLEAN_OPERATOR),
        ("cache_operator", ResolvedValueKind.CACHE_OPERATOR), ("eviction_priority", ResolvedValueKind.EVICTION_PRIORITY),
        ("prefetch_size", ResolvedValueKind.PREFETCH_SIZE), ("vector_arity", ResolvedValueKind.VECTOR_ARITY),
        ("memory_state_space", ResolvedValueKind.MEMORY_STATE_SPACE), ("memory_consistency", ResolvedValueKind.MEMORY_CONSISTENCY),
        ("memory_scope", ResolvedValueKind.MEMORY_SCOPE), ("mbarrier_phase_type", ResolvedValueKind.MBARRIER_PHASE_TYPE),
        ("mbarrier_layout", ResolvedValueKind.MBARRIER_LAYOUT), ("tcgen_cta_group", ResolvedValueKind.CTA_GROUP),
        ("tcgen_scale_vector_size", ResolvedValueKind.TCGEN_SCALE_VECTOR_SIZE), ("tcgen_collector", ResolvedValueKind.TCGEN_COLLECTOR),
        ("tcgen_shape", ResolvedValueKind.TCGEN_SHAPE), ("tcgen_repeat", ResolvedValueKind.TCGEN_NUM),
        ("tcgen_reduction_op", ResolvedValueKind.TCGEN_RED_OP), ("tcgen_wait_class", ResolvedValueKind.TCGEN_WAIT),
        ("video_scale", ResolvedValueKind.VIDEO_SCALE), ("video_shift_mode", ResolvedValueKind.VIDEO_SHIFT_MODE),
        ("video_secondary_op", ResolvedValueKind.VIDEO_SECONDARY_OP), ("video_type", ResolvedValueKind.VIDEO_TYPE),
        ("async_proxy_kind", ResolvedValueKind.ASYNC_PROXY_KIND), ("proxy_kind_pair", ResolvedValueKind.PROXY_KIND_PAIR),
    )
    availability = build_availability(dict(entry.availability)) if isinstance(entry, ResolvedModifierValueAvailability) else None
    return CheckerModifierValueModel("checker::ModifierValueAvailabilityDescriptor" if availability is not None
        else "checker::ModifierValueDomainDescriptor", entry.source_kind_id,
        cpp_value(CppDomain.CHECKER_MODIFIER_VALUE_KINDS, entry.value_kind.value, backend=backend),
        tuple(CheckerModifierMemberModel(name, selected_value if kind is entry.value_kind else defaults[kind])
              for name, kind in members), availability)


def _build_variant(instruction: ResolvedInstruction, variant: ResolvedVariant, backend: CodegenUnit,
                   defaults: Mapping[ResolvedValueKind, str]) -> CheckerVariantModel:
    """Select optional policies and preserve existing array/member output order."""

    name = variant.cpp_name
    consistency = None
    if variant.memory_consistency is not None:
        policy = variant.memory_consistency
        consistency = CheckerMemoryConsistencyModel(policy.semantics_field_id, policy.scope_field_id,
            policy.mmio_field_id, policy.cache_field_id, policy.address_field_id, policy.type_field_id,
            policy.state_space_field_id or "", f"{name}_mmio_semantics",
            tuple(CheckerMmioSemanticModel(cpp_value(CppDomain.MEMORY_CONSISTENCIES, value, backend=backend),
                build_availability(dict(availability))) for value, availability in policy.mmio_semantics))
    vector = None
    if variant.memory_vector is not None:
        policy = variant.memory_vector
        vector = CheckerMemoryVectorModel(policy.type_field_id, policy.vector_field_id, policy.address_field_id,
            policy.state_space_field_id or "", build_availability(dict(policy.availability)), str(policy.require_modern).lower())
    immediate = None
    if variant.immediate_value is not None:
        immediate = CheckerImmediateValueModel(variant.immediate_value.operand_field_id,
            tuple(_cpp_uint64(value) for value in variant.immediate_value.values))
    multiple = None
    if variant.immediate_multiple_of is not None:
        multiple = CheckerImmediateMultipleModel(variant.immediate_multiple_of.operand_field_id,
            _cpp_uint64(variant.immediate_multiple_of.divisor))
    atomic = None
    if instruction.atomic_address_qualifier is not None:
        policy = instruction.atomic_address_qualifier
        atomic = CheckerAtomicAddressModel(policy.state_space_field_id, policy.address_operand_id,
            tuple(f"AtomicAddressQualifier::{value.name.title().replace('_', '')}" for value in variant.atomic_address_qualifier_domain))
    return CheckerVariantModel(name, build_availability(dict(variant.availability)),
        tuple(_build_modifier_value(entry, backend, defaults) for entry in variant.modifier_value_availabilities),
        tuple(_build_modifier_value(entry, backend, defaults) for entry in variant.modifier_value_domains),
        tuple(CheckerLayoutModel(f"{name}_operand_layout_{index}_forbidden_modifiers", layout.layout_id,
            build_availability(dict(layout.availability)), layout.forbidden_modifier_slots,
            f"{name}_operand_layout_{index}_forbidden_modifiers" if layout.forbidden_modifier_slots else "{}")
            for index, layout in enumerate(variant.operand_layouts)),
        tuple(CheckerTypeCompatibilityModel(entry.target_field_id,
            cpp_value(CppDomain.SPECIAL_REGISTER_KINDS, entry.special_register_kind, backend=backend),
            entry.instruction_width, cpp_value(CppDomain.SCALAR_TYPES, entry.effective_type, backend=backend),
            build_availability(dict(entry.availability))) for entry in variant.operand_type_compatibilities),
        variant.rule.value if variant.rule is not None else "", str(variant.permits_unified_address).lower(),
        variant.unified_address_access.title(), consistency, vector, immediate,
        tuple(CheckerImmediateRangeModel(constraint.operand_field_id, _cpp_uint64(constraint.minimum),
            str(constraint.maximum is not None).lower(), _cpp_uint64(constraint.maximum) if constraint.maximum is not None else "~uint64_t{0}")
            for constraint in variant.immediate_ranges), multiple,
        tuple(CheckerAddressAlignmentModel(f"{name}_address_alignment_{index}_address_fields", alignment.address_field_ids,
            alignment.type_field_id or "", alignment.vector_field_id or "", alignment.immediate_operand_field_id or "", alignment.alignment or 0)
            for index, alignment in enumerate(variant.address_alignments)), atomic)


def build_checker_descriptor_storage(instruction: ResolvedInstruction, backend: CodegenUnit) -> CheckerDescriptorStorageModel:
    """Build all checker storage without rendered child source strings."""

    # Neutral expressions depend only on this build's backend, not the row value.
    defaults = modifier_descriptor_default_members(backend=backend) if any(
        variant.modifier_value_availabilities or variant.modifier_value_domains
        for variant in instruction.variants
    ) else {}
    return CheckerDescriptorStorageModel(f"{instruction.cpp_name}CheckerDescriptorStorage", instruction.opcode,
        tuple(_build_variant(instruction, variant, backend, defaults) for variant in instruction.variants))


def build_checker_descriptor_getter(instruction: ResolvedInstruction) -> CheckerDescriptorGetterModel:
    """Prepare the stable public getter's static storage reference."""

    return CheckerDescriptorGetterModel(instruction.cpp_name, f"{instruction.cpp_name}CheckerDescriptorStorage")


@dataclass(frozen=True)
class CheckerModifierValueDomainsModel:
    """A variant-local modifier-domain array for supported fragment consumers."""

    name: str
    value_domains: tuple[CheckerModifierValueModel, ...]


def build_checker_modifier_value_domains(variant: ResolvedVariant, backend: CodegenUnit) -> CheckerModifierValueDomainsModel:
    """Prepare the complete ordered typed modifier domain of one variant."""

    defaults = modifier_descriptor_default_members(backend=backend) if variant.modifier_value_domains else {}
    return CheckerModifierValueDomainsModel(variant.cpp_name,
        tuple(_build_modifier_value(entry, backend, defaults) for entry in variant.modifier_value_domains))
