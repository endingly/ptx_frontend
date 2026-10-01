"""Python model for generated PTX Resolved IR instruction definitions.

The model is derived from the normalized PTX-facing ``InstructionSpec``.  It
describes the semantic fields that must appear in the generated C++ resolved
instruction structs; it does not describe C++ storage or emitter layout.
"""

from dataclasses import dataclass
from enum import Enum
from typing import Any
from typing import overload

from ptx_frontend.base.utils import file_stem_to_pascal_case
from ptx_frontend.spec.model import (
    AsyncCompletionKind,
    AtomicAddressQualifierPolicy,
    ConditionCodeEffect,
    AddressAlignmentConstraint,
    ImmediateMultipleOfConstraint,
    ImmediateRangeConstraint,
    ImmediateValueConstraint,
    InstructionSpec,
    MemoryConsistencyConstraint,
    MemoryVectorConstraint,
    MbarrierStateTokenForm,
    OperandAddressBasePolicy,
    OperandAddressOffsetDomain,
    ModifierKind,
    ModifierPresence,
    ModifierSpec,
    ModifierValueSpec,
    OperandParameterConstraint,
    OperandAccess,
    OperandKind,
    OperandImmediateConversionPolicy,
    OperandRegisterWidthPolicy,
    OperandRole,
    OperandSpec,
    OperandStateSpaceExpression,
    OperandStateSpaceValue,
    OperandTypeCompatibilityValueKind,
    OperandTypeCompatibilitySpec,
    OperandTypeExpression,
    OperandTypeExpressionKind,
    OperandVectorArityExpression,
    OperandVectorTypePolicy,
    SemanticRule,
    VariantSpec,
    modifier_spellings,
)
from ptx_frontend.ir.tensor_reduction import TensorReductionOp
from ptx_frontend.ir.resolved_value_kind import ResolvedValueKind
from ptx_frontend.ir.resolved_value_policy import (
    modifier_value_kind,
    validate_resolved_modifier_value_type,
)
from ptx_frontend.spec.semantic_domains import (
    SemanticDomain,
    is_default_semantic_value,
    is_semantic_value,
    semantic_domain_for_modifier,
)


class ResolvedFieldOrigin(Enum):
    """The PTX specification element that supplies a resolved field."""

    MODIFIER = "modifier"
    OPERAND = "operand"


class AtomicAddressQualifierValue(Enum):
    """Written atomic suffix values supported by the owned IR enum."""

    GENERIC = "generic"
    GLOBAL = "global"
    SHARED = "shared"
    SHARED_CTA = "shared::cta"
    SHARED_CLUSTER = "shared::cluster"


class TensorAccessMode(Enum):
    """Closed instruction-local interpretation of an owned tensor operand."""

    TILED = "tile"
    IM2COL_NO_OFFS = "im2col_no_offs"
    IM2COL = "im2col"
    IM2COL_W = "im2col_w"
    IM2COL_W128 = "im2col_w128"
    TILE_GATHER4 = "tile_gather4"
    TILE_SCATTER4 = "tile_scatter4"


def tensor_im2col_info_contract(
    mode: TensorAccessMode, rank: int,
) -> tuple[tuple[str, int], ...]:
    """Return semantic element roles and inclusive U16-use bounds for a read."""

    if rank not in (3, 4, 5):
        raise ValueError("im2col information requires rank 3, 4, or 5")
    if mode is TensorAccessMode.IM2COL:
        bound = {3: 65535, 4: 255, 5: 31}[rank]
        return tuple((role, bound) for role in ("OffsetW", "OffsetH", "OffsetD")[:rank - 2])
    if mode is TensorAccessMode.IM2COL_W:
        return (("Halo", 511), ("Offset", 31))
    if mode is TensorAccessMode.IM2COL_W128:
        return (("Halo", 31), ("Offset", 31))
    raise ValueError("tensor mode does not carry im2col information")


@dataclass(frozen=True)
class ResolvedAtomicAddressQualifierPolicy:
    """Resolved field identities shared by an instruction's atomic variants."""

    state_space_field_id: str
    address_operand_id: str


_OPERAND_VALUE_KINDS: dict[OperandKind, ResolvedValueKind] = {
    OperandKind.REGISTER: ResolvedValueKind.REGISTER,
    OperandKind.IMMEDIATE: ResolvedValueKind.IMMEDIATE,
    OperandKind.REGISTER_OR_IMMEDIATE: ResolvedValueKind.REG_OR_IMM,
    OperandKind.REGISTER_OR_SINK: ResolvedValueKind.REGISTER_OR_SINK,
    OperandKind.SHFL_DESTINATION: ResolvedValueKind.SHFL_DESTINATION,
    OperandKind.PREDICATE_PAIR: ResolvedValueKind.PREDICATE_PAIR,
    OperandKind.PREDICATE_PAIR_OR_SINK: ResolvedValueKind.PREDICATE_PAIR_OR_SINK,
    OperandKind.MOV_SCALAR_SOURCE: ResolvedValueKind.MOV_SOURCE,
    OperandKind.CP_ASYNC_SOURCE_CONTROL: ResolvedValueKind.CP_ASYNC_SOURCE_CONTROL,
    OperandKind.CLUSTER_ADDRESS: ResolvedValueKind.MOV_SOURCE,
    OperandKind.VECTOR_REGISTER: ResolvedValueKind.VECTOR_REGISTER,
    OperandKind.VECTOR_SPECIAL_REGISTER: ResolvedValueKind.VECTOR_SPECIAL_REGISTER,
    OperandKind.PREDICATE: ResolvedValueKind.PREDICATE,
    OperandKind.PREDICATE_OR_SINK: ResolvedValueKind.PREDICATE_OR_SINK,
    OperandKind.PREDICATE_SOURCE: ResolvedValueKind.PREDICATE_SOURCE,
    OperandKind.PREDICATE_OR_SPECIAL_REGISTER: ResolvedValueKind.PREDICATE_SOURCE,
    OperandKind.PREDICATE_OR_NOT: ResolvedValueKind.PREDICATE,
    OperandKind.LABEL: ResolvedValueKind.BRANCH_TARGET,
    OperandKind.SPECIAL_REGISTER: ResolvedValueKind.SPECIAL_REGISTER,
    OperandKind.SYMBOL: ResolvedValueKind.SYMBOL,
    OperandKind.ADDRESS: ResolvedValueKind.ADDRESS,
    OperandKind.REGISTER_VECTOR: ResolvedValueKind.REGISTER_VECTOR,
    OperandKind.DESCRIPTOR: ResolvedValueKind.REGISTER,
    OperandKind.TYPED_TOKEN: ResolvedValueKind.REGISTER,
    OperandKind.MBARRIER_STATE_TOKEN: ResolvedValueKind.MBARRIER_STATE_TOKEN,
    OperandKind.TENSOR_COORDINATE: ResolvedValueKind.TENSOR_COORDINATE,
    OperandKind.TENSOR_IM2COL_INFO: ResolvedValueKind.TENSOR_IM2COL_INFO,
    OperandKind.TENSOR_OPERAND: ResolvedValueKind.TENSOR_OPERAND,
    OperandKind.MATRIX_FRAGMENT: ResolvedValueKind.REGISTER_VECTOR,
    OperandKind.DIRECT_CALL_TARGET: ResolvedValueKind.DIRECT_CALL_TARGET,
    OperandKind.INDIRECT_CALL_TARGET: ResolvedValueKind.INDIRECT_CALLEE,
    OperandKind.INDIRECT_CALL_METADATA: ResolvedValueKind.INDIRECT_CALLEE,
    OperandKind.BRANCH_TARGET_SET: ResolvedValueKind.BRANCH_TARGET_SET,
    OperandKind.CALL_RETURN_PARAMETER: ResolvedValueKind.CALL_RETURN_PARAMETER,
    OperandKind.CALL_ARGUMENTS: ResolvedValueKind.CALL_ARGUMENTS,
}


class ResolvedFieldStorage(Enum):
    """Whether a field is stored per instruction or fixed by its variant."""

    INSTANCE = "Instance"
    STATIC_CONSTANT = "StaticConstant"


class ResolvedOperandRole(Enum):
    """Semantic role of one resolved operand."""

    DESTINATION = "Destination"
    SOURCE = "Source"
    ADDRESS = "Address"
    PREDICATE = "Predicate"
    BRANCH_TARGET = "BranchTarget"
    BARRIER = "Barrier"
    THREAD_COUNT = "ThreadCount"


class ResolvedOperandAccess(Enum):
    """Access mode of one resolved operand."""

    READ = "Read"
    WRITE = "Write"
    READ_WRITE = "ReadWrite"
    CONTROL = "Control"


class ResolvedOperandShape(Enum):
    """Allowed resolved value shapes for one operand position."""

    REGISTER = "Register"
    PREDICATE = "Predicate"
    IMMEDIATE = "Immediate"
    ADDRESS = "Address"
    SYMBOL = "Symbol"
    VECTOR = "Vector"
    BRANCH_TARGET = "BranchTarget"
    SPECIAL_REGISTER = "SpecialRegister"
    DIRECT_CALL_TARGET = "DirectCallTarget"
    INDIRECT_CALLEE = "IndirectCallee"
    BRANCH_TARGET_SET = "BranchTargetSet"
    CALL_RETURN_PARAMETER = "CallReturnParameter"
    CALL_ARGUMENTS = "CallArguments"
    SHFL_DESTINATION = "ShflDestination"
    PREDICATE_PAIR = "PredicatePair"
    TENSOR_OPERAND = "TensorOperand"


class ResolvedOperandTypeExpressionKind(Enum):
    """C++ descriptor representation of an operand scalar-type source."""

    NONE = "None"
    FIXED_SCALAR = "FixedScalar"
    MODIFIER_FIELD = "ModifierField"


class ResolvedRegisterWidthPolicy(Enum):
    """Descriptor-facing register-size relation for a typed operand."""

    EXACT = "exact"
    SAME_WIDTH = "same_width"
    EQUAL_OR_WIDER = "equal_or_wider"
    WORD_OR_DOUBLEWORD = "word_or_doubleword"


class ResolvedImmediateConversionPolicy(Enum):
    """Descriptor-facing integer conversion behavior for an operand use."""

    NARROW = "Narrow"
    REQUIRE_TARGET_RANGE = "RequireTargetRange"


class ResolvedVectorTypePolicy(Enum):
    """Descriptor-facing interpretation of an instruction type for vectors."""

    AGGREGATE = "Aggregate"
    ELEMENT = "Element"


@dataclass(frozen=True)
class ResolvedOperandTypeExpression:
    """A resolved, descriptor-ready operand scalar-type expression."""

    kind: ResolvedOperandTypeExpressionKind
    scalar_type: str | None = None
    modifier_field_id: str | None = None


@dataclass(frozen=True)
class ResolvedAddressStateSpace:
    """One statically accepted effective address space and its availability."""

    value: str
    availability: tuple[tuple[str, Any], ...]


@dataclass(frozen=True)
class ResolvedParameterAddressConstraint:
    """Descriptor-ready direction and function availability for .param."""

    direction: str
    function_availability: tuple[tuple[str, Any], ...]


@dataclass(frozen=True)
class ResolvedMemoryConsistencyConstraint:
    """Generated field identities for the typed ld/st cross-rule checker."""

    semantics_field_id: str
    scope_field_id: str
    mmio_field_id: str
    cache_field_id: str
    address_field_id: str
    type_field_id: str
    state_space_field_id: str | None = None
    mmio_semantics: tuple[tuple[str, tuple[tuple[str, Any], ...]], ...] = ()


@dataclass(frozen=True)
class ResolvedAddressAlignmentConstraint:
    """Generated field identities for one address-alignment rule."""

    address_field_ids: tuple[str, ...]
    type_field_id: str | None = None
    vector_field_id: str | None = None
    immediate_operand_field_id: str | None = None
    alignment: int | None = None


@dataclass(frozen=True)
class ResolvedMemoryVectorConstraint:
    """Generated field identities for the PTX 8.8 vector cross-rule."""

    type_field_id: str
    vector_field_id: str
    address_field_id: str
    availability: tuple[tuple[str, Any], ...]
    state_space_field_id: str | None = None
    require_modern: bool = False


@dataclass(frozen=True)
class ResolvedImmediateValueConstraint:
    """Generated field identity and integer allowlist for one immediate."""

    operand_field_id: str
    values: tuple[int, ...]


@dataclass(frozen=True)
class ResolvedImmediateRangeConstraint:
    """Generated field identity and inclusive bounds for one immediate."""

    operand_field_id: str
    minimum: int
    maximum: int | None = None


@dataclass(frozen=True)
class ResolvedImmediateMultipleOfConstraint:
    """Generated field identity and positive divisor for one immediate."""

    operand_field_id: str
    divisor: int


@dataclass(frozen=True)
class ResolvedField:
    """One provenance-carrying field in a resolved variant struct."""

    name: str
    value_kind: ResolvedValueKind
    origin: ResolvedFieldOrigin
    source_name: str
    operand_role: ResolvedOperandRole | None = None
    operand_access: ResolvedOperandAccess | None = None
    allowed_operand_shapes: tuple[ResolvedOperandShape, ...] = ()
    storage: ResolvedFieldStorage = ResolvedFieldStorage.INSTANCE
    constant_value: str | bool | int | None = None



@dataclass(frozen=True)
class ResolvedVariant:
    """One alternative of an opcode's generated C++ ``Variant`` type."""

    variant_id: str
    cpp_name: str
    modifier_fields: tuple[ResolvedField, ...]
    modifier_bindings: tuple["ResolvedModifierBinding", ...]
    operand_layouts: tuple["ResolvedOperandLayout", ...]
    modifier_value_domains: tuple["ResolvedModifierValueDomain", ...]
    modifier_value_availabilities: tuple["ResolvedModifierValueAvailability", ...]
    operand_type_compatibilities: tuple["ResolvedOperandTypeCompatibility", ...]
    memory_consistency: ResolvedMemoryConsistencyConstraint | None
    permits_unified_address: bool
    unified_address_access: str
    address_alignments: tuple[ResolvedAddressAlignmentConstraint, ...]
    memory_vector: ResolvedMemoryVectorConstraint | None
    immediate_value: ResolvedImmediateValueConstraint | None
    immediate_ranges: tuple[ResolvedImmediateRangeConstraint, ...]
    immediate_multiple_of: ResolvedImmediateMultipleOfConstraint | None
    availability: tuple[tuple[str, Any], ...]
    rule: SemanticRule | None

    condition_code_effect: ConditionCodeEffect = ConditionCodeEffect.NONE
    completion_kind: AsyncCompletionKind = AsyncCompletionKind.NONE
    atomic_address_qualifier_domain: tuple[AtomicAddressQualifierValue, ...] = ()
    tensor_reduction_op: TensorReductionOp | None = None
    tensor_access_mode: TensorAccessMode | None = None
    tensor_im2col_info_elements: tuple[tuple[str, int], ...] = ()
    tensor_multicast: bool = False

    @property
    def fields(self) -> tuple[ResolvedField, ...]:
        """All fields declared by the variant, in deterministic layout order.

        Operand payloads are layout-local, so layouts may intentionally reuse a
        semantic field name with different resolved representations.
        """

        fields: list[ResolvedField] = list(self.modifier_fields)
        for layout in self.operand_layouts:
            for field in layout.fields:
                fields.append(field)
        return tuple(fields)


@dataclass(frozen=True)
class ResolvedModifierBinding:
    """Bind one syntax modifier kind to a resolved field."""

    source_kind_id: str
    target_field_id: str
    default_value: "ResolvedModifierDefault | None" = None


@dataclass(frozen=True)
class ResolvedModifierDefault:
    """Typed semantic value used when an optional modifier is omitted."""

    value_kind: ResolvedValueKind
    value: str | bool | int


@dataclass(frozen=True)
class ResolvedModifierValueAvailability:
    """Target requirement attached to one dynamic semantic modifier value."""

    source_kind_id: str
    value_kind: ResolvedValueKind
    value: str | bool | int
    availability: tuple[tuple[str, Any], ...]


@dataclass(frozen=True)
class ResolvedModifierValueDomain:
    """One semantic modifier value admitted by a selected resolved variant.

    This AST-free descriptor deliberately excludes target requirements: domain
    membership answers whether a value belongs to the variant, while the
    separate availability descriptor answers whether that legal value is
    supported by a particular target profile.
    """

    source_kind_id: str
    value_kind: ResolvedValueKind
    value: str | bool | int


@dataclass(frozen=True)
class ResolvedOperandTypeCompatibility:
    """One generated value-dependent operand type-checking rule."""

    target_field_id: str
    special_register_kind: str
    instruction_width: int
    effective_type: str
    availability: tuple[tuple[str, Any], ...]


@dataclass(frozen=True)
class ResolvedOperandBinding:
    """Bind one positional syntax operand to a resolved field."""

    target_field_id: str
    type_expression: ResolvedOperandTypeExpression
    register_width_policy: ResolvedRegisterWidthPolicy
    immediate_conversion_policy: ResolvedImmediateConversionPolicy
    role: ResolvedOperandRole
    access: ResolvedOperandAccess
    allowed_shapes: tuple[ResolvedOperandShape, ...]
    allowed_address_state_spaces: tuple[ResolvedAddressStateSpace, ...] = ()
    state_space_modifier_field_id: str | None = None
    address_base_policy: OperandAddressBasePolicy = OperandAddressBasePolicy.ANY
    address_offset_domain: OperandAddressOffsetDomain = OperandAddressOffsetDomain.UNRESTRICTED
    parameter_constraint: ResolvedParameterAddressConstraint | None = None
    allowed_vector_arities: tuple[int, ...] = ()
    vector_arity_modifier_field_id: str | None = None
    vector_type_policy: ResolvedVectorTypePolicy = ResolvedVectorTypePolicy.AGGREGATE
    allow_vector_sink: bool = False
    vector_sink_payload_bits: int = 0
    allowed_vector_register_types: tuple[str, ...] = ()
    require_uniform_vector_register_family: bool = False
    allow_destination_sink: bool = False
    allow_predicate_sink: bool = False
    mbarrier_state_token_form: MbarrierStateTokenForm = MbarrierStateTokenForm.REGISTER
    sink_availability: tuple[tuple[str, Any], ...] = ()
    allow_function_symbol: bool = False
    preserve_parameter_address_space: bool = False
    type_tag: str | None = None
    minimum_elements: int | None = None
    maximum_elements: int | None = None
    allowed_element_shapes: tuple[ResolvedOperandShape, ...] = ()
    tensor_access_mode: TensorAccessMode | None = None
    expected_tensor_rank: int | None = None
    tensor_cta_mask: bool = False


@dataclass(frozen=True)
class ResolvedOperandLayout:
    """One resolved-field binding layout paired by index with syntax layouts."""

    layout_id: str
    cpp_name: str
    fields: tuple[ResolvedField, ...]
    bindings: tuple[ResolvedOperandBinding, ...]
    availability: tuple[tuple[str, Any], ...]
    # Variant modifier slots this layout rejects, as source names for
    # diagnostics and as variant-local slot indexes for the generated checker.
    # The checker reports a spelled slot in this set because layout selection
    # itself is shape-only.
    forbidden_modifiers: tuple[str, ...] = ()
    forbidden_modifier_slots: tuple[int, ...] = ()


@dataclass(frozen=True)
class ResolvedInstruction:
    """Resolved IR definition for one PTX opcode, such as ``Add``."""

    opcode: str
    cpp_name: str
    variants: tuple[ResolvedVariant, ...]
    atomic_address_qualifier: ResolvedAtomicAddressQualifierPolicy | None = None


_OPERAND_ALLOWED_SHAPES: dict[OperandKind, tuple[ResolvedOperandShape, ...]] = {
    OperandKind.REGISTER: (ResolvedOperandShape.REGISTER,),
    OperandKind.IMMEDIATE: (ResolvedOperandShape.IMMEDIATE,),
    OperandKind.REGISTER_OR_IMMEDIATE: (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.IMMEDIATE,
    ),
    OperandKind.REGISTER_OR_SINK: (ResolvedOperandShape.REGISTER,),
    OperandKind.SHFL_DESTINATION: (ResolvedOperandShape.SHFL_DESTINATION,),
    OperandKind.PREDICATE_PAIR: (ResolvedOperandShape.PREDICATE_PAIR,),
    OperandKind.PREDICATE_PAIR_OR_SINK: (ResolvedOperandShape.PREDICATE_PAIR,),
    OperandKind.MOV_SCALAR_SOURCE: (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.IMMEDIATE,
        ResolvedOperandShape.SPECIAL_REGISTER,
        ResolvedOperandShape.SYMBOL,
        ResolvedOperandShape.ADDRESS,
    ),
    OperandKind.CP_ASYNC_SOURCE_CONTROL: (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.IMMEDIATE,
        ResolvedOperandShape.PREDICATE,
    ),
    OperandKind.CLUSTER_ADDRESS: (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.SYMBOL,
        ResolvedOperandShape.ADDRESS,
    ),
    OperandKind.VECTOR_REGISTER: (ResolvedOperandShape.VECTOR,),
    OperandKind.VECTOR_SPECIAL_REGISTER: (ResolvedOperandShape.VECTOR,),
    OperandKind.PREDICATE: (ResolvedOperandShape.PREDICATE,),
    OperandKind.PREDICATE_OR_SINK: (ResolvedOperandShape.PREDICATE,),
    OperandKind.PREDICATE_SOURCE: (
        ResolvedOperandShape.PREDICATE,
        ResolvedOperandShape.IMMEDIATE,
    ),
    OperandKind.PREDICATE_OR_SPECIAL_REGISTER: (
        ResolvedOperandShape.PREDICATE,
        ResolvedOperandShape.IMMEDIATE,
        ResolvedOperandShape.SPECIAL_REGISTER,
    ),
    OperandKind.PREDICATE_OR_NOT: (ResolvedOperandShape.PREDICATE,),
    OperandKind.LABEL: (ResolvedOperandShape.BRANCH_TARGET,),
    OperandKind.SPECIAL_REGISTER: (ResolvedOperandShape.SPECIAL_REGISTER,),
    OperandKind.SYMBOL: (ResolvedOperandShape.SYMBOL,),
    OperandKind.ADDRESS: (ResolvedOperandShape.ADDRESS,),
    OperandKind.REGISTER_VECTOR: (ResolvedOperandShape.VECTOR,),
    OperandKind.DESCRIPTOR: (ResolvedOperandShape.REGISTER,),
    OperandKind.TYPED_TOKEN: (ResolvedOperandShape.REGISTER,),
    OperandKind.MBARRIER_STATE_TOKEN: (ResolvedOperandShape.REGISTER,),
    OperandKind.TENSOR_COORDINATE: (ResolvedOperandShape.VECTOR,),
    OperandKind.TENSOR_IM2COL_INFO: (ResolvedOperandShape.VECTOR,),
    OperandKind.TENSOR_OPERAND: (ResolvedOperandShape.TENSOR_OPERAND,),
    OperandKind.MATRIX_FRAGMENT: (ResolvedOperandShape.VECTOR,),
    OperandKind.DIRECT_CALL_TARGET: (ResolvedOperandShape.DIRECT_CALL_TARGET,),
    OperandKind.INDIRECT_CALL_TARGET: (ResolvedOperandShape.INDIRECT_CALLEE,),
    OperandKind.INDIRECT_CALL_METADATA: (ResolvedOperandShape.INDIRECT_CALLEE,),
    OperandKind.BRANCH_TARGET_SET: (ResolvedOperandShape.BRANCH_TARGET_SET,),
    OperandKind.CALL_RETURN_PARAMETER: (ResolvedOperandShape.CALL_RETURN_PARAMETER,),
    OperandKind.CALL_ARGUMENTS: (ResolvedOperandShape.CALL_ARGUMENTS,),
}

_OPERAND_ROLES = {
    OperandRole.DESTINATION: ResolvedOperandRole.DESTINATION,
    OperandRole.SOURCE: ResolvedOperandRole.SOURCE,
    OperandRole.SOURCE_1: ResolvedOperandRole.SOURCE,
    OperandRole.SOURCE_2: ResolvedOperandRole.SOURCE,
    OperandRole.SOURCE_3: ResolvedOperandRole.SOURCE,
    OperandRole.ADDRESS: ResolvedOperandRole.ADDRESS,
    OperandRole.PREDICATE: ResolvedOperandRole.PREDICATE,
    OperandRole.LABEL: ResolvedOperandRole.BRANCH_TARGET,
    OperandRole.BARRIER: ResolvedOperandRole.BARRIER,
    OperandRole.THREAD_COUNT: ResolvedOperandRole.THREAD_COUNT,
}

_OPERAND_ACCESS = {
    OperandAccess.READ: ResolvedOperandAccess.READ,
    OperandAccess.WRITE: ResolvedOperandAccess.WRITE,
    OperandAccess.READ_WRITE: ResolvedOperandAccess.READ_WRITE,
    OperandAccess.CONTROL: ResolvedOperandAccess.CONTROL,
}


def from_instruction_spec(spec: InstructionSpec) -> ResolvedInstruction:
    """Build the resolved instruction model from one normalized PTX spec."""

    for variant in spec.variants:
        if variant.rule is not None and not isinstance(variant.rule, SemanticRule):
            raise ValueError(
                f"variant {variant.name!r} has a non-normalized semantic rule"
            )

    atomic_policy = spec.atomic_address_qualifier
    return ResolvedInstruction(
        opcode=spec.opcode,
        cpp_name=file_stem_to_pascal_case(spec.opcode),
        variants=tuple(
            _build_variant(spec.opcode, variant, atomic_policy)
            for variant in spec.variants
        ),
        atomic_address_qualifier=(
            ResolvedAtomicAddressQualifierPolicy(
                state_space_field_id=atomic_policy.state_space_modifier,
                address_operand_id=atomic_policy.address_operand,
            )
            if atomic_policy is not None else None
        ),
    )


def _build_atomic_address_qualifier_domain(
    policy: AtomicAddressQualifierPolicy | None, variant: VariantSpec
) -> tuple[AtomicAddressQualifierValue, ...]:
    """Derive written suffixes from one variant's state-space modifier."""

    if policy is None:
        return ()
    modifier = next((item for item in variant.modifiers
                     if item.name == policy.state_space_modifier), None)
    if (modifier is None or modifier.kind is not ModifierKind.STATE_SPACE
            or modifier.presence is ModifierPresence.ABSENT):
        raise ValueError(
            f"variant {variant.name!r}: atomic qualifier requires an active "
            f"state-space modifier {policy.state_space_modifier!r}"
        )
    for layout in variant.operand_layouts:
        address = next((operand for operand in layout.operands
                        if operand.name == policy.address_operand), None)
        if address is None or address.kind is not OperandKind.ADDRESS:
            raise ValueError(
                f"variant {variant.name!r} layout {layout.name!r}: atomic "
                f"qualifier requires address operand {policy.address_operand!r}"
            )
    domain: list[AtomicAddressQualifierValue] = []
    for value in _modifier_domain_values(modifier):
        try:
            qualifier = AtomicAddressQualifierValue(value.value)
        except ValueError as error:
            raise ValueError(
                f"variant {variant.name!r}: unsupported atomic address "
                f"qualifier {value.value!r}"
            ) from error
        if qualifier not in domain:
            domain.append(qualifier)
    return tuple(domain)


def _build_variant(
    opcode: str, variant: VariantSpec,
    atomic_policy: AtomicAddressQualifierPolicy | None,
) -> ResolvedVariant:
    tensor_access_mode = _build_tensor_access_mode(opcode, variant)
    tensor_multicast = _build_tensor_multicast(variant, tensor_access_mode)
    expected_tensor_rank = _build_expected_tensor_rank(variant, tensor_access_mode)
    active_modifiers = tuple(
        modifier
        for modifier in variant.modifiers
        if modifier.presence != ModifierPresence.ABSENT
    )
    modifier_fields = tuple(
        _build_modifier_field(modifier) for modifier in active_modifiers
    )
    operand_layouts = tuple(
        _build_operand_layout(
            layout.name,
            layout.operands,
            layout.availability,
            {field.source_name: field.name for field in modifier_fields},
            layout.forbidden_modifiers,
            _resolve_forbidden_modifier_slots(
                layout.name,
                layout.forbidden_modifiers,
                {field.source_name: index for index, field in enumerate(modifier_fields)},
            ),
            tensor_access_mode,
            expected_tensor_rank,
            tensor_multicast,
        )
        for layout in variant.operand_layouts
    )

    return ResolvedVariant(
        variant_id=variant.name,
        condition_code_effect=variant.condition_code_effect,
        completion_kind=variant.completion_kind,
        cpp_name=_variant_cpp_name(opcode, variant.name),
        modifier_fields=modifier_fields,
        modifier_bindings=tuple(
            ResolvedModifierBinding(
                source_kind_id=field.source_name,
                target_field_id=field.name,
                default_value=_build_modifier_default(modifier),
            )
            for modifier, field in zip(active_modifiers, modifier_fields, strict=True)
        ),
        operand_layouts=operand_layouts,
        atomic_address_qualifier_domain=(
            _build_atomic_address_qualifier_domain(atomic_policy, variant)
        ),
        modifier_value_domains=tuple(
            _build_modifier_value_domain(modifier, value)
            for modifier in active_modifiers
            for value in _modifier_domain_values(modifier)
        ),
        modifier_value_availabilities=tuple(
            _build_modifier_value_availability(modifier, value)
            for modifier in variant.modifiers
            for value in modifier.values
            if value.availability
        ),
        operand_type_compatibilities=tuple(
            _build_operand_type_compatibility(compatibility, value)
            for compatibility in variant.operand_type_compatibilities
            for value in compatibility.values
        ),
        memory_consistency=_build_memory_consistency_constraint(
            variant.memory_consistency,
            {field.source_name: field.name for field in modifier_fields},
        ),
        permits_unified_address=variant.permits_unified_address,
        unified_address_access=variant.unified_address_access,
        address_alignments=tuple(
            _build_address_alignment_constraint(
                constraint,
                {field.source_name: field.name for field in modifier_fields},
            )
            for constraint in variant.address_alignments
        ),
        memory_vector=_build_memory_vector_constraint(
            variant.memory_vector,
            {field.source_name: field.name for field in modifier_fields},
        ),
        immediate_value=_build_immediate_value_constraint(
            variant.immediate_value,
        ),
        immediate_ranges=_build_immediate_range_constraints(
            variant.immediate_ranges,
        ),
        immediate_multiple_of=_build_immediate_multiple_of_constraint(
            variant.immediate_multiple_of,
        ),
        availability=tuple(variant.availability.items()),
        rule=variant.rule,
        tensor_reduction_op=_build_tensor_reduction_op(
            opcode, variant, tensor_access_mode
        ),
        tensor_access_mode=tensor_access_mode,
        tensor_multicast=tensor_multicast,
        tensor_im2col_info_elements=(
            tensor_im2col_info_contract(
                tensor_access_mode,
                expected_tensor_rank,
            )
            if tensor_access_mode in {
                TensorAccessMode.IM2COL, TensorAccessMode.IM2COL_W,
                TensorAccessMode.IM2COL_W128,
            } else ()
        ),
    )


def _build_tensor_access_mode(
    opcode: str, variant: VariantSpec,
) -> TensorAccessMode | None:
    """Lower a checked canonical mode for each tensor operand binding."""

    has_tensor = any(
        operand.kind is OperandKind.TENSOR_OPERAND
        for layout in variant.operand_layouts for operand in layout.operands
    )
    has_no_offsets = any(
        modifier.name == "im2col_no_offs" for modifier in variant.modifiers
    )
    has_read_im2col = any(
        modifier.name in {"im2col", "im2col_w", "im2col_w128"}
        for modifier in variant.modifiers
    )
    has_gather_scatter = any(
        modifier.name in {"tile_gather4", "tile_scatter4"}
        for modifier in variant.modifiers
    )
    if not has_tensor:
        if has_no_offsets or has_read_im2col or has_gather_scatter:
            raise ValueError(f"variant {variant.name!r}: tensor mode lacks tensor operand")
        return None
    if opcode != "cp":
        raise ValueError(f"variant {variant.name!r}: unsupported tensor opcode")
    modifiers = {modifier.name: modifier for modifier in variant.modifiers}
    if len(modifiers) != len(variant.modifiers):
        raise ValueError(f"variant {variant.name!r}: duplicate tensor mode/flag")
    tile = modifiers.get("tile")
    no_offsets = modifiers.get("im2col_no_offs")
    read_modes = {
        "im2col": TensorAccessMode.IM2COL,
        "im2col_w": TensorAccessMode.IM2COL_W,
        "im2col_w128": TensorAccessMode.IM2COL_W128,
    }
    gather_modes = {
        "tile_gather4": TensorAccessMode.TILE_GATHER4,
        "tile_scatter4": TensorAccessMode.TILE_SCATTER4,
    }
    selected_modes = [name for name in ("tile", "im2col_no_offs", *read_modes,
                                      *gather_modes)
                      if name in modifiers]
    if len(selected_modes) != 1:
        raise ValueError(f"variant {variant.name!r}: expected one tensor mode")
    if tile is not None:
        if (tile.kind is not ModifierKind.FLAG
                or tile.presence is not ModifierPresence.OPTIONAL
                or tile.default is not False
                or modifier_spellings(tile) != (".tile",)):
            raise ValueError(f"variant {variant.name!r}: invalid tile mode")
        return TensorAccessMode.TILED

    if has_gather_scatter:
        mode_name = selected_modes[0]
        mode = gather_modes[mode_name]
        mode_modifier = modifiers[mode_name]
        token = (".tile::gather4" if mode is TensorAccessMode.TILE_GATHER4
                 else ".tile::scatter4")
        if (mode_modifier.kind is not ModifierKind.FLAG
                or mode_modifier.presence is not ModifierPresence.FIXED
                or mode_modifier.value is not True
                or modifier_spellings(mode_modifier) != (token,)):
            raise ValueError(f"variant {variant.name!r}: invalid gather/scatter mode")
        gather = mode is TensorAccessMode.TILE_GATHER4
        prefetch = "prefetch" in modifiers
        topology = {
            "async": ".async", "bulk": ".bulk",
            "tensor_qualifier": ".tensor", "rank": ".2d",
        }
        if gather and prefetch:
            topology.update({"prefetch": ".prefetch", "level": ".L2",
                             "src_space": ".global"})
            completion = AsyncCompletionKind.NONE
            expected = (("tensor", OperandKind.TENSOR_OPERAND),)
        elif gather:
            destination = modifiers.get("dst_space")
            if destination is None or modifier_spellings(destination) not in (
                    (".shared::cta",), (".shared::cluster",)):
                raise ValueError(f"variant {variant.name!r}: invalid gather destination")
            topology.update({"dst_space": modifier_spellings(destination)[0],
                             "src_space": ".global",
                             "completion": ".mbarrier::complete_tx::bytes"})
            completion = AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES
            expected = (("dst", OperandKind.ADDRESS),
                        ("tensor", OperandKind.TENSOR_OPERAND),
                        ("mbar", OperandKind.ADDRESS))
            if "multicast" in modifiers:
                expected += (("cta_mask", OperandKind.REGISTER_OR_IMMEDIATE),)
        else:
            topology.update({"dst_space": ".global",
                             "src_space": ".shared::cta",
                             "completion": ".bulk_group"})
            completion = AsyncCompletionKind.BULK_GROUP
            expected = (("tensor", OperandKind.TENSOR_OPERAND),
                        ("src", OperandKind.ADDRESS))
        if (set(modifiers) != set(topology) | {mode_name} | (
                {"multicast"} if "multicast" in modifiers else set())
                or variant.completion_kind is not completion
                or variant.rule is not None
                or len(variant.operand_layouts) != 1):
            raise ValueError(f"variant {variant.name!r}: invalid gather/scatter topology")
        for name, spelling in topology.items():
            modifier = modifiers[name]
            if (modifier.kind is not ModifierKind.FLAG
                    or modifier.presence is not ModifierPresence.FIXED
                    or modifier.value is not True
                    or modifier_spellings(modifier) != (spelling,)):
                raise ValueError(f"variant {variant.name!r}: invalid {name} flag")
        operands = variant.operand_layouts[0].operands
        if tuple((operand.name, operand.kind) for operand in operands) != expected:
            raise ValueError(f"variant {variant.name!r}: invalid gather/scatter operands")
        tensor = operands[0 if prefetch or not gather else 1]
        if (tensor.access is not OperandAccess.READ
                or tensor.minimum_elements != 5
                or tensor.maximum_elements != 5
                or tensor.immediate_conversion_policy is not
                OperandImmediateConversionPolicy.NARROW
                or tensor.type_expression is None
                or tensor.type_expression.kind is not
                OperandTypeExpressionKind.FIXED_SCALAR
                or tensor.type_expression.scalar_type != "s32"
                or {space.value for space in tensor.state_space_values}
                != {"param", "const", "global"}
                or set(tensor.element_kinds) !=
                {OperandKind.REGISTER, OperandKind.IMMEDIATE}):
            raise ValueError(f"variant {variant.name!r}: invalid gather/scatter coordinates")
        if not prefetch:
            address = operands[-1] if not gather else operands[0]
            if {space.value for space in address.state_space_values} != {"shared"}:
                raise ValueError(f"variant {variant.name!r}: invalid shared address")
        return mode

    if has_read_im2col:
        mode_name = selected_modes[0]
        mode = read_modes[mode_name]
        mode_modifier = modifiers[mode_name]
        token = {
            TensorAccessMode.IM2COL: ".im2col",
            TensorAccessMode.IM2COL_W: ".im2col::w",
            TensorAccessMode.IM2COL_W128: ".im2col::w::128",
        }[mode]
        if (mode_modifier.kind is not ModifierKind.FLAG
                or mode_modifier.presence is not ModifierPresence.FIXED
                or mode_modifier.value is not True
                or modifier_spellings(mode_modifier) != (token,)):
            raise ValueError(f"variant {variant.name!r}: invalid im2col mode flag")
        rank_modifier = modifiers.get("rank")
        ranks = {f".{count}d": count for count in (3, 4, 5)}
        if (rank_modifier is None or rank_modifier.kind is not ModifierKind.FLAG
                or rank_modifier.presence is not ModifierPresence.FIXED
                or rank_modifier.value is not True
                or modifier_spellings(rank_modifier) not in
                tuple((spelling,) for spelling in ranks)):
            raise ValueError(f"variant {variant.name!r}: invalid im2col rank")
        rank = ranks[modifier_spellings(rank_modifier)[0]]
        prefetch = "prefetch" in modifiers
        topology = (
            {"async": ".async", "bulk": ".bulk", "prefetch": ".prefetch",
             "tensor_qualifier": ".tensor", "level": ".L2",
             "src_space": ".global"}
            if prefetch else
            {"async": ".async", "bulk": ".bulk",
             "tensor_qualifier": ".tensor", "src_space": ".global",
             "completion": ".mbarrier::complete_tx::bytes"}
        )
        if not prefetch:
            destination = modifiers.get("dst_space")
            if destination is None or modifier_spellings(destination) not in (
                    (".shared::cta",), (".shared::cluster",)):
                raise ValueError(f"variant {variant.name!r}: invalid im2col destination")
            topology["dst_space"] = modifier_spellings(destination)[0]
        if set(modifiers) != set(topology) | {"rank", mode_name} | (
                {"multicast"} if "multicast" in modifiers else set()):
            raise ValueError(f"variant {variant.name!r}: invalid im2col topology")
        for name, spelling in topology.items():
            modifier = modifiers[name]
            if (modifier.kind is not ModifierKind.FLAG
                    or modifier.presence is not ModifierPresence.FIXED
                    or modifier.value is not True
                    or modifier_spellings(modifier) != (spelling,)):
                raise ValueError(f"variant {variant.name!r}: invalid {name} flag")
        if prefetch:
            if variant.completion_kind is not AsyncCompletionKind.NONE:
                raise ValueError(f"variant {variant.name!r}: prefetch has completion")
        elif variant.completion_kind is not AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES:
            raise ValueError(f"variant {variant.name!r}: invalid im2col completion")
        if variant.rule is not None or len(variant.operand_layouts) != 2:
            raise ValueError(f"variant {variant.name!r}: invalid im2col rule/layout count")
        required_layouts = {"without_info", "with_info"}
        if {layout.name for layout in variant.operand_layouts} != required_layouts:
            raise ValueError(f"variant {variant.name!r}: invalid info layout names")
        expected_prefix = (
            (("tensor", OperandKind.TENSOR_OPERAND),) if prefetch else
            (("dst", OperandKind.ADDRESS),
             ("tensor", OperandKind.TENSOR_OPERAND),
             ("mbar", OperandKind.ADDRESS))
        )
        expected_arity = len(tensor_im2col_info_contract(mode, rank))
        for layout in variant.operand_layouts:
            operands = layout.operands
            expected = expected_prefix + (
                (("im2col_info", OperandKind.TENSOR_IM2COL_INFO),)
                if layout.name == "with_info" else ()
            )
            if "multicast" in modifiers:
                expected += (("cta_mask", OperandKind.REGISTER_OR_IMMEDIATE),)
            if tuple((operand.name, operand.kind) for operand in operands) != expected:
                raise ValueError(f"variant {variant.name!r}: invalid im2col operands")
            tensor = operands[0 if prefetch else 1]
            if (tensor.access is not OperandAccess.READ
                    or tensor.minimum_elements != rank
                    or tensor.maximum_elements != rank
                    or tensor.immediate_conversion_policy is not
                    OperandImmediateConversionPolicy.NARROW
                    or tensor.type_expression is None
                    or tensor.type_expression.kind is not
                    OperandTypeExpressionKind.FIXED_SCALAR
                    or tensor.type_expression.scalar_type != "s32"
                    or {space.value for space in tensor.state_space_values}
                    != {"param", "const", "global"}):
                raise ValueError(f"variant {variant.name!r}: invalid tensor read")
            if layout.name == "with_info":
                info = operands[-2] if "multicast" in modifiers else operands[-1]
                if (info.access is not OperandAccess.READ
                        or info.minimum_elements != expected_arity
                        or info.maximum_elements != expected_arity
                        or info.immediate_conversion_policy is not
                        OperandImmediateConversionPolicy.NARROW
                        or info.type_expression is None
                        or info.type_expression.kind is not
                        OperandTypeExpressionKind.FIXED_SCALAR
                        or info.type_expression.scalar_type != "u16"
                        or set(info.element_kinds) !=
                        {OperandKind.REGISTER, OperandKind.IMMEDIATE}):
                    raise ValueError(f"variant {variant.name!r}: invalid info binding")
        return mode

    assert no_offsets is not None
    if (no_offsets.kind is not ModifierKind.FLAG
            or no_offsets.presence is not ModifierPresence.FIXED
            or no_offsets.value is not True
            or modifier_spellings(no_offsets) != (".im2col_no_offs",)
            or variant.completion_kind is not AsyncCompletionKind.BULK_GROUP):
        raise ValueError(f"variant {variant.name!r}: invalid no-offset mode")
    required = {
        "async": ".async", "bulk": ".bulk", "tensor_qualifier": ".tensor",
        "dst_space": ".global", "src_space": ".shared::cta",
        "completion": ".bulk_group",
    }
    for name, spelling in required.items():
        modifier = modifiers.get(name)
        if (modifier is None or modifier.kind is not ModifierKind.FLAG
                or modifier.presence is not ModifierPresence.FIXED
                or modifier.value is not True
                or modifier_spellings(modifier) != (spelling,)):
            raise ValueError(f"variant {variant.name!r}: invalid {name} topology")
    rank = modifiers.get("rank")
    ranks = {f".{value}d": value for value in range(3, 6)}
    if (rank is None or rank.kind is not ModifierKind.FLAG
            or rank.presence is not ModifierPresence.FIXED
            or rank.value is not True
            or modifier_spellings(rank) not in tuple((name,) for name in ranks)):
        raise ValueError(f"variant {variant.name!r}: invalid no-offset rank")
    count = ranks[modifier_spellings(rank)[0]]
    if len(variant.operand_layouts) != 1:
        raise ValueError(f"variant {variant.name!r}: invalid no-offset layout")
    operands = variant.operand_layouts[0].operands
    if (len(operands) != 2
            or (operands[0].name, operands[0].kind) !=
            ("tensor", OperandKind.TENSOR_OPERAND)
            or (operands[1].name, operands[1].kind) !=
            ("src", OperandKind.ADDRESS)
            or operands[0].access is not OperandAccess.READ
            or operands[1].access is not OperandAccess.READ
            or operands[0].minimum_elements != count
            or operands[0].maximum_elements != count
            or operands[0].immediate_conversion_policy is not
            OperandImmediateConversionPolicy.NARROW
            or operands[0].type_expression is None
            or operands[0].type_expression.kind is not
            OperandTypeExpressionKind.FIXED_SCALAR
            or operands[0].type_expression.scalar_type != "s32"
            or {space.value for space in operands[0].state_space_values}
            != {"param", "const", "global"}
            or {space.value for space in operands[1].state_space_values}
            != {"shared"}):
        raise ValueError(f"variant {variant.name!r}: invalid no-offset operands")
    core = set(required) | {"rank", "im2col_no_offs"}
    if variant.rule is SemanticRule.DATA_MOVEMENT_TENSOR_REDUCTION:
        reduction_flags = {name for name in modifiers if name.startswith("reduction_")}
        if (len(reduction_flags) != 1 or "reduce" not in modifiers
                or set(modifiers) != core | {"reduce"} | reduction_flags):
            raise ValueError(f"variant {variant.name!r}: invalid reduction mode flags")
    elif variant.rule is None:
        if set(modifiers) != core:
            raise ValueError(f"variant {variant.name!r}: invalid store mode flags")
    else:
        raise ValueError(f"variant {variant.name!r}: invalid no-offset rule")
    return TensorAccessMode.IM2COL_NO_OFFS


def _build_tensor_multicast(
    variant: VariantSpec, mode: TensorAccessMode | None,
) -> bool:
    """Lower a paired cluster-load multicast qualifier and mask role."""

    if mode is None:
        return False
    modifiers = {modifier.name: modifier for modifier in variant.modifiers}
    multicast = modifiers.get("multicast")
    has_mask = any(operand.name == "cta_mask"
                   for layout in variant.operand_layouts
                   for operand in layout.operands)
    if multicast is None:
        if has_mask:
            raise ValueError(f"variant {variant.name!r}: mask without multicast")
        return False
    destination = modifiers.get("dst_space")
    if (mode not in {TensorAccessMode.TILED, TensorAccessMode.TILE_GATHER4,
                     TensorAccessMode.IM2COL, TensorAccessMode.IM2COL_W,
                     TensorAccessMode.IM2COL_W128}
            or variant.completion_kind is not
            AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES
            or multicast.kind is not ModifierKind.FLAG
            or multicast.presence is not ModifierPresence.FIXED
            or multicast.value is not True
            or modifier_spellings(multicast) != (".multicast::cluster",)
            or destination is None
            or modifier_spellings(destination) != (".shared::cluster",)):
        raise ValueError(f"variant {variant.name!r}: invalid multicast topology")
    for layout in variant.operand_layouts:
        if not layout.operands or layout.operands[-1].name != "cta_mask":
            raise ValueError(f"variant {variant.name!r}: multicast mask ordering")
        mask = layout.operands[-1]
        if (mask.kind is not OperandKind.REGISTER_OR_IMMEDIATE
                or mask.access is not OperandAccess.READ
                or mask.immediate_conversion_policy is not
                OperandImmediateConversionPolicy.NARROW
                or mask.type_expression is None
                or mask.type_expression.kind is not
                OperandTypeExpressionKind.FIXED_SCALAR
                or mask.type_expression.scalar_type != "u16"):
            raise ValueError(f"variant {variant.name!r}: invalid multicast mask")
    if mode is TensorAccessMode.TILED:
        required = {"async": ".async", "bulk": ".bulk",
                    "tensor_qualifier": ".tensor",
                    "dst_space": ".shared::cluster", "src_space": ".global",
                    "completion": ".mbarrier::complete_tx::bytes"}
        if set(modifiers) != set(required) | {"rank", "tile", "multicast"}:
            raise ValueError(f"variant {variant.name!r}: extra multicast control")
        for name, spelling in required.items():
            modifier = modifiers[name]
            if (modifier.kind is not ModifierKind.FLAG
                    or modifier.presence is not ModifierPresence.FIXED
                    or modifier.value is not True
                    or modifier_spellings(modifier) != (spelling,)):
                raise ValueError(f"variant {variant.name!r}: invalid {name} flag")
        if len(variant.operand_layouts) != 1 or tuple(
                (operand.name, operand.kind)
                for operand in variant.operand_layouts[0].operands) != (
                    ("dst", OperandKind.ADDRESS),
                    ("tensor", OperandKind.TENSOR_OPERAND),
                    ("mbar", OperandKind.ADDRESS),
                    ("cta_mask", OperandKind.REGISTER_OR_IMMEDIATE),
                ):
            raise ValueError(f"variant {variant.name!r}: tiled multicast operands")
    return True


def _build_expected_tensor_rank(
    variant: VariantSpec, mode: TensorAccessMode | None,
) -> int | None:
    """Lower the fixed dimension token independently of coordinate arity."""

    if mode is None:
        return None
    ranks = {f".{rank}d": rank for rank in range(1, 6)}
    rank_flags = [modifier for modifier in variant.modifiers
                  if modifier.name == "rank"]
    if len(rank_flags) != 1:
        raise ValueError(f"variant {variant.name!r}: expected one tensor rank")
    rank_flag = rank_flags[0]
    spelling = modifier_spellings(rank_flag)
    if (rank_flag.kind is not ModifierKind.FLAG
            or rank_flag.presence is not ModifierPresence.FIXED
            or rank_flag.value is not True
            or len(spelling) != 1 or spelling[0] not in ranks):
        raise ValueError(f"variant {variant.name!r}: invalid tensor rank flag")
    rank = ranks[spelling[0]]
    if mode in {TensorAccessMode.TILE_GATHER4, TensorAccessMode.TILE_SCATTER4}:
        if rank != 2:
            raise ValueError(f"variant {variant.name!r}: gather/scatter rank must be two")
    else:
        for layout in variant.operand_layouts:
            tensors = [operand for operand in layout.operands
                       if operand.kind is OperandKind.TENSOR_OPERAND]
            if (len(tensors) != 1 or tensors[0].minimum_elements != rank
                    or tensors[0].maximum_elements != rank):
                raise ValueError(f"variant {variant.name!r}: tensor rank/arity mismatch")
    return rank


def _build_tensor_reduction_op(
    opcode: str, variant: VariantSpec,
    tensor_access_mode: TensorAccessMode | None,
) -> TensorReductionOp | None:
    """Lower one checked fixed tiled-reduction operation from canonical flags."""

    if variant.rule is not SemanticRule.DATA_MOVEMENT_TENSOR_REDUCTION:
        return None
    if opcode != "cp" or variant.completion_kind is not AsyncCompletionKind.BULK_GROUP:
        raise ValueError(f"variant {variant.name!r}: invalid tensor reduction topology")
    modifiers = {modifier.name: modifier for modifier in variant.modifiers}
    required = {
        "reduce": ".reduce", "async": ".async", "bulk": ".bulk",
        "tensor_qualifier": ".tensor", "dst_space": ".global",
        "src_space": ".shared::cta", "completion": ".bulk_group",
    }
    operation_flags = [modifier for modifier in variant.modifiers
                       if modifier.name.startswith("reduction_")]
    if len(operation_flags) != 1:
        raise ValueError(f"variant {variant.name!r}: expected one reduction operation")
    operation = operation_flags[0]
    mode_name = (
        "tile" if tensor_access_mode is TensorAccessMode.TILED
        else "im2col_no_offs" if tensor_access_mode is TensorAccessMode.IM2COL_NO_OFFS
        else ""
    )
    if (not mode_name or len(modifiers) != len(variant.modifiers)
            or set(modifiers) != set(required) | {"rank", mode_name, operation.name}):
        raise ValueError(f"variant {variant.name!r}: invalid tensor reduction flags")
    for name, spelling in required.items():
        modifier = modifiers[name]
        if (modifier.kind is not ModifierKind.FLAG
                or modifier.presence is not ModifierPresence.FIXED
                or modifier.value is not True
                or modifier_spellings(modifier) != (spelling,)):
            raise ValueError(f"variant {variant.name!r}: invalid {name} flag")
    rank = modifiers["rank"]
    if (rank.kind is not ModifierKind.FLAG
            or rank.presence is not ModifierPresence.FIXED
            or rank.value is not True
            or modifier_spellings(rank) not in tuple((f".{n}d",) for n in range(1, 6))):
        raise ValueError(f"variant {variant.name!r}: invalid tensor reduction rank")
    if tensor_access_mode is TensorAccessMode.TILED:
        tile = modifiers["tile"]
        if (tile.kind is not ModifierKind.FLAG
                or tile.presence is not ModifierPresence.OPTIONAL
                or tile.default is not False
                or modifier_spellings(tile) != (".tile",)):
            raise ValueError(f"variant {variant.name!r}: invalid tile flag")
    if (operation.kind is not ModifierKind.FLAG
            or operation.presence is not ModifierPresence.FIXED
            or operation.value is not True):
        raise ValueError(f"variant {variant.name!r}: reduction operation must be fixed true")
    matches = [op for op in TensorReductionOp
               if operation.name == f"reduction_{op.value}"
               and modifier_spellings(operation) == (f".{op.value}",)]
    if len(matches) != 1:
        raise ValueError(f"variant {variant.name!r}: unknown reduction operation")
    if len(variant.operand_layouts) != 1:
        raise ValueError(f"variant {variant.name!r}: invalid reduction operand layouts")
    operands = variant.operand_layouts[0].operands
    if (len(operands) != 2
            or (operands[0].name, operands[0].kind) != ("tensor", OperandKind.TENSOR_OPERAND)
            or (operands[1].name, operands[1].kind) != ("src", OperandKind.ADDRESS)):
        raise ValueError(f"variant {variant.name!r}: invalid reduction operands")
    tensor, src = operands
    if (tensor.access is not OperandAccess.READ
            or src.access is not OperandAccess.READ
            or tensor.immediate_conversion_policy is not OperandImmediateConversionPolicy.NARROW
            or tensor.type_expression is None
            or tensor.type_expression.kind is not OperandTypeExpressionKind.FIXED_SCALAR
            or tensor.type_expression.scalar_type != "s32"
            or {space.value for space in tensor.state_space_values}
            != {"param", "const", "global"}
            or {space.value for space in src.state_space_values} != {"shared"}):
        raise ValueError(f"variant {variant.name!r}: invalid reduction operand roles")
    return matches[0]


def _build_memory_consistency_constraint(
    constraint: MemoryConsistencyConstraint | None,
    modifier_field_ids: dict[str, str],
) -> ResolvedMemoryConsistencyConstraint | None:
    if constraint is None:
        return None
    for value in constraint.mmio_semantics:
        if not is_semantic_value(SemanticDomain.MEMORY_CONSISTENCY, value.value):
            raise ValueError(
                f"unsupported semantic memory consistency value {value.value!r}"
            )
    return ResolvedMemoryConsistencyConstraint(
        semantics_field_id=modifier_field_ids[constraint.semantics_modifier],
        scope_field_id=modifier_field_ids[constraint.scope_modifier],
        mmio_field_id=(
            modifier_field_ids[constraint.mmio_modifier]
            if constraint.mmio_modifier is not None
            else ""
        ),
        cache_field_id=(
            modifier_field_ids[constraint.cache_modifier]
            if constraint.cache_modifier is not None
            else ""
        ),
        address_field_id=constraint.address_operand,
        type_field_id=modifier_field_ids[constraint.type_modifier],
        state_space_field_id=(
            modifier_field_ids[constraint.state_space_modifier]
            if constraint.state_space_modifier is not None
            else None
        ),
        mmio_semantics=tuple(
            (value.value, tuple(value.availability.items()))
            for value in constraint.mmio_semantics
        ),
    )


@overload
def _build_address_alignment_constraint(
    constraint: AddressAlignmentConstraint,
    modifier_field_ids: dict[str, str],
) -> ResolvedAddressAlignmentConstraint: ...


@overload
def _build_address_alignment_constraint(
    constraint: None,
    modifier_field_ids: dict[str, str],
) -> None: ...


def _build_address_alignment_constraint(
    constraint: AddressAlignmentConstraint | None,
    modifier_field_ids: dict[str, str],
) -> ResolvedAddressAlignmentConstraint | None:
    if constraint is None:
        return None
    return ResolvedAddressAlignmentConstraint(
        address_field_ids=constraint.address_operands,
        type_field_id=(
            modifier_field_ids[constraint.type_modifier]
            if constraint.type_modifier is not None
            else None
        ),
        vector_field_id=(
            modifier_field_ids[constraint.vector_modifier]
            if constraint.vector_modifier is not None
            else None
        ),
        immediate_operand_field_id=constraint.immediate_operand,
        alignment=constraint.alignment,
    )


def _build_memory_vector_constraint(
    constraint: MemoryVectorConstraint | None,
    modifier_field_ids: dict[str, str],
) -> ResolvedMemoryVectorConstraint | None:
    if constraint is None:
        return None
    return ResolvedMemoryVectorConstraint(
        type_field_id=modifier_field_ids[constraint.type_modifier],
        vector_field_id=constraint.vector_operand,
        address_field_id=constraint.address_operand,
        availability=tuple(constraint.availability.items()),
        state_space_field_id=(
            modifier_field_ids[constraint.state_space_modifier]
            if constraint.state_space_modifier is not None
            else None
        ),
        require_modern=constraint.require_modern,
    )


def _build_immediate_value_constraint(
    constraint: ImmediateValueConstraint | None,
) -> ResolvedImmediateValueConstraint | None:
    if constraint is None:
        return None
    return ResolvedImmediateValueConstraint(
        operand_field_id=constraint.operand,
        values=constraint.values,
    )


def _build_immediate_range_constraints(
    constraints: tuple[ImmediateRangeConstraint, ...],
) -> tuple[ResolvedImmediateRangeConstraint, ...]:
    return tuple(
        ResolvedImmediateRangeConstraint(
            operand_field_id=constraint.operand,
            minimum=constraint.minimum,
            maximum=constraint.maximum,
        )
        for constraint in constraints
    )


def _build_immediate_multiple_of_constraint(
    constraint: ImmediateMultipleOfConstraint | None,
) -> ResolvedImmediateMultipleOfConstraint | None:
    if constraint is None:
        return None
    return ResolvedImmediateMultipleOfConstraint(
        operand_field_id=constraint.operand,
        divisor=constraint.divisor,
    )


def _validate_resolved_modifier_value(
    modifier: ModifierSpec,
    value_kind: ResolvedValueKind,
    value: str | bool | int,
    *,
    default: bool,
) -> None:
    """Validate one modifier semantic value without consulting C++ mappings."""

    validate_resolved_modifier_value_type(
        value_kind,
        value,
        modifier_name=modifier.name,
        default=default,
    )
    domain = semantic_domain_for_modifier(modifier.kind)
    if domain is not None and not (
        is_semantic_value(domain, value)
        or (default and is_default_semantic_value(domain, value))
    ):
        raise ValueError(
            f"modifier {modifier.name!r}: unsupported semantic "
            f"{domain.value} value {value!r}"
        )



def _build_modifier_default(
    modifier: ModifierSpec,
) -> ResolvedModifierDefault | None:
    if modifier.presence != ModifierPresence.OPTIONAL:
        return None

    if modifier.default is None:
        raise ValueError(
            f"optional modifier {modifier.name!r} has no normalized default"
        )

    domain = semantic_domain_for_modifier(modifier.kind)
    if modifier.values and modifier.default not in {
        value.value for value in modifier.values
    } and not (
        domain is not None and is_default_semantic_value(domain, modifier.default)
    ):
        raise ValueError(
            f"optional {modifier.kind.value} modifier {modifier.name!r} has default "
            f"{modifier.default!r} outside its allowed values"
        )

    try:
        value_kind = modifier_value_kind(modifier.kind)
    except ValueError as error:
        raise ValueError(
            f"optional modifier {modifier.name!r}: unsupported default for "
            f"modifier kind {modifier.kind!r}"
        ) from error
    _validate_resolved_modifier_value(
        modifier,
        value_kind,
        modifier.default,
        default=True,
    )
    return ResolvedModifierDefault(
        value_kind=value_kind,
        value=modifier.default,
    )


def _build_modifier_value_availability(
    modifier: ModifierSpec,
    value: ModifierValueSpec,
) -> ResolvedModifierValueAvailability:
    try:
        value_kind = modifier_value_kind(modifier.kind)
    except ValueError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: availability for unsupported modifier "
            f"kind {modifier.kind!r}"
        ) from error

    _validate_resolved_modifier_value(
        modifier,
        value_kind,
        value.value,
        default=(
            modifier.presence is ModifierPresence.OPTIONAL
            and value.value == modifier.default
            and all(candidate.value != value.value for candidate in modifier.values)
        ),
    )

    return ResolvedModifierValueAvailability(
        source_kind_id=modifier.name,
        value_kind=value_kind,
        value=value.value,
        availability=tuple(value.availability.items()),
    )


def _modifier_domain_values(
    modifier: ModifierSpec,
) -> tuple[ModifierValueSpec, ...]:
    """Return all semantic values admitted by one normalized modifier field.

    YAML ``values`` define spelling-selectable values. A flag's spelling
    implies ``true`` even though it has no value list, and an optional field's
    normalized default is also a legal resolved value when syntax omits it.
    """

    values = list(modifier.values)
    if not values:
        if modifier.value is not None:
            values.append(ModifierValueSpec(value=modifier.value))
        elif modifier.kind == ModifierKind.FLAG:
            values.append(ModifierValueSpec(value=True))
    if modifier.presence == ModifierPresence.OPTIONAL:
        if modifier.default is None:
            raise ValueError(
                f"optional modifier {modifier.name!r} has no normalized default"
            )
        if all(value.value != modifier.default for value in values):
            values.append(ModifierValueSpec(value=modifier.default))
    return tuple(values)


def _build_modifier_value_domain(
    modifier: ModifierSpec, value: ModifierValueSpec
) -> ResolvedModifierValueDomain:
    """Build one typed semantic-domain entry using common value validation."""

    availability = _build_modifier_value_availability(modifier, value)
    return ResolvedModifierValueDomain(
        source_kind_id=availability.source_kind_id,
        value_kind=availability.value_kind,
        value=availability.value,
    )


def _build_operand_type_compatibility(
    compatibility: OperandTypeCompatibilitySpec, value: str
) -> ResolvedOperandTypeCompatibility:
    """Validate and lower one expanded contextual operand type rule."""

    if (
        compatibility.value_kind
        is not OperandTypeCompatibilityValueKind.SPECIAL_REGISTER
    ):
        raise ValueError(
            f"unsupported operand compatibility value kind "
            f"{compatibility.value_kind!r}"
        )
    if not isinstance(value, str):
        raise ValueError("special-register compatibility value must be a string")
    if not is_semantic_value(SemanticDomain.SPECIAL_REGISTER, value):
        raise ValueError(f"unsupported semantic special-register value {value!r}")
    if not is_semantic_value(SemanticDomain.SCALAR_TYPE, compatibility.effective_type):
        raise ValueError(
            f"unsupported semantic scalar type {compatibility.effective_type!r}"
        )
    return ResolvedOperandTypeCompatibility(
        target_field_id=compatibility.operand,
        special_register_kind=value,
        instruction_width=compatibility.instruction_width,
        effective_type=compatibility.effective_type,
        availability=tuple(compatibility.availability.items()),
    )


def _resolve_forbidden_modifier_slots(
    layout_name: str,
    forbidden_modifiers: tuple[str, ...],
    slot_indexes: dict[str, int],
) -> tuple[int, ...]:
    """Lower forbidden modifier names to variant-local slot indexes."""

    slots: list[int] = []
    for name in forbidden_modifiers:
        if name not in slot_indexes:
            raise ValueError(
                f"operand layout {layout_name!r}: forbidden modifier {name!r} is "
                "not an active modifier slot of its variant"
            )
        slots.append(slot_indexes[name])
    return tuple(slots)


def _build_operand_layout(
    layout_id: str,
    operands: tuple[OperandSpec, ...],
    availability: dict[str, Any],
    modifier_field_ids: dict[str, str],
    forbidden_modifiers: tuple[str, ...] = (),
    forbidden_modifier_slots: tuple[int, ...] = (),
    tensor_access_mode: TensorAccessMode | None = None,
    expected_tensor_rank: int | None = None,
    tensor_multicast: bool = False,
) -> ResolvedOperandLayout:
    fields = tuple(_build_operand_field(operand) for operand in operands)
    return ResolvedOperandLayout(
        layout_id=layout_id,
        cpp_name=file_stem_to_pascal_case(layout_id),
        fields=fields,
        bindings=tuple(
            ResolvedOperandBinding(
                target_field_id=field.name,
                type_expression=_resolve_operand_type_expression(
                    operand.type_expression,
                    modifier_field_ids,
                ),
                register_width_policy=ResolvedRegisterWidthPolicy(
                    operand.register_width_policy.value
                ),
                immediate_conversion_policy=ResolvedImmediateConversionPolicy(
                    "".join(
                        part.title()
                        for part in operand.immediate_conversion_policy.value.split("_")
                    )
                ),
                role=_require_operand_role(field),
                access=_require_operand_access(field),
                allowed_shapes=field.allowed_operand_shapes,
                allowed_address_state_spaces=_resolve_operand_state_spaces(
                    operand.state_space_values
                ),
                state_space_modifier_field_id=(
                    _resolve_operand_state_space_expression(
                        operand.state_space_expression,
                        modifier_field_ids,
                    )
                ),
                address_base_policy=operand.address_base_policy,
                address_offset_domain=operand.address_offset_domain,
                parameter_constraint=_resolve_parameter_address_constraint(
                    operand.parameter_constraint
                ),
                allowed_vector_arities=operand.vector_arities,
                vector_arity_modifier_field_id=(
                    _resolve_operand_vector_arity_expression(
                        operand.vector_arity_expression,
                        modifier_field_ids,
                    )
                ),
                vector_type_policy=ResolvedVectorTypePolicy(
                    operand.vector_type_policy.value.capitalize()
                ),
                allow_vector_sink=operand.vector_allow_sink,
                vector_sink_payload_bits=operand.vector_sink_payload_bits,
                allowed_vector_register_types=operand.vector_allowed_register_types,
                require_uniform_vector_register_family=(
                    operand.vector_require_uniform_register_family
                ),
                allow_destination_sink=operand.allow_destination_sink,
                allow_predicate_sink=operand.allow_predicate_sink,
                mbarrier_state_token_form=operand.mbarrier_state_token_form,
                sink_availability=tuple(operand.sink_availability.items()),
                allow_function_symbol=operand.kind == OperandKind.MOV_SCALAR_SOURCE,
                preserve_parameter_address_space=(
                    operand.kind == OperandKind.CLUSTER_ADDRESS
                ),
                type_tag=operand.type_tag,
                minimum_elements=operand.minimum_elements,
                maximum_elements=operand.maximum_elements,
                allowed_element_shapes=tuple(
                    (
                        ResolvedOperandShape.REGISTER
                        if kind is OperandKind.REGISTER
                        else ResolvedOperandShape.IMMEDIATE
                    )
                    for kind in operand.element_kinds
                ),
                tensor_access_mode=(
                    tensor_access_mode
                    if operand.kind is OperandKind.TENSOR_OPERAND else None
                ),
                expected_tensor_rank=(
                    expected_tensor_rank
                    if operand.kind is OperandKind.TENSOR_OPERAND else None
                ),
                tensor_cta_mask=(tensor_multicast and operand.name == "cta_mask"),
            )
            for operand, field in zip(operands, fields, strict=True)
        ),
        availability=tuple(availability.items()),
        forbidden_modifiers=forbidden_modifiers,
        forbidden_modifier_slots=forbidden_modifier_slots,
    )


def _build_modifier_field(modifier: ModifierSpec) -> ResolvedField:
    try:
        value_kind = modifier_value_kind(modifier.kind)
    except ValueError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: unsupported resolved modifier kind "
            f"{modifier.kind!r}"
        ) from error
    if modifier.presence is ModifierPresence.FIXED:
        if modifier.value is None:
            raise ValueError(f"fixed modifier {modifier.name!r} has no value")
        _validate_resolved_modifier_value(
            modifier,
            value_kind,
            modifier.value,
            default=False,
        )

    return ResolvedField(
        name=modifier.name,
        value_kind=value_kind,
        origin=ResolvedFieldOrigin.MODIFIER,
        source_name=modifier.name,
        storage=(
            ResolvedFieldStorage.STATIC_CONSTANT
            if modifier.presence == ModifierPresence.FIXED
            else ResolvedFieldStorage.INSTANCE
        ),
        constant_value=(
            modifier.value
            if modifier.presence == ModifierPresence.FIXED
            else None
        ),
    )


def _build_operand_field(operand: OperandSpec) -> ResolvedField:
    try:
        value_kind = _OPERAND_VALUE_KINDS[operand.kind]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported resolved operand kind "
            f"{operand.kind!r}"
        ) from error

    try:
        role = _OPERAND_ROLES[operand.role]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported resolved operand role "
            f"{operand.role!r}"
        ) from error

    try:
        access = _OPERAND_ACCESS[operand.access]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported resolved operand access "
            f"{operand.access!r}"
        ) from error

    return ResolvedField(
        name=operand.name,
        value_kind=value_kind,
        origin=ResolvedFieldOrigin.OPERAND,
        source_name=operand.name,
        operand_role=role,
        operand_access=access,
        allowed_operand_shapes=_OPERAND_ALLOWED_SHAPES[operand.kind],
    )


def _resolve_operand_type_expression(
    expression: OperandTypeExpression | None,
    modifier_field_ids: dict[str, str],
) -> ResolvedOperandTypeExpression:
    """Map a source-model type expression to its resolved descriptor form."""

    if expression is None:
        return ResolvedOperandTypeExpression(
            kind=ResolvedOperandTypeExpressionKind.NONE,
        )
    if expression.kind is OperandTypeExpressionKind.FIXED_SCALAR:
        assert expression.scalar_type is not None
        if not is_semantic_value(SemanticDomain.SCALAR_TYPE, expression.scalar_type):
            raise ValueError(
                f"unsupported semantic operand scalar type {expression.scalar_type!r}"
            )
        return ResolvedOperandTypeExpression(
            kind=ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
            scalar_type=expression.scalar_type,
        )
    if expression.kind is OperandTypeExpressionKind.MODIFIER:
        assert expression.modifier_name is not None
        try:
            field_id = modifier_field_ids[expression.modifier_name]
        except KeyError as error:
            raise ValueError(
                f"operand type expression references unresolved modifier "
                f"{expression.modifier_name!r}"
            ) from error
        return ResolvedOperandTypeExpression(
            kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
            modifier_field_id=field_id,
        )
    raise AssertionError(f"unhandled operand type expression: {expression.kind}")


def _resolve_operand_state_space_expression(
    expression: OperandStateSpaceExpression | None,
    modifier_field_ids: dict[str, str],
) -> str | None:
    """Map ``modifier(name)`` to its generated resolved field identifier."""

    if expression is None:
        return None
    try:
        return modifier_field_ids[expression.modifier_name]
    except KeyError as error:
        raise ValueError(
            "operand state-space expression references unresolved modifier "
            f"{expression.modifier_name!r}"
        ) from error


def _resolve_operand_vector_arity_expression(
    expression: OperandVectorArityExpression | None,
    modifier_field_ids: dict[str, str],
) -> str | None:
    """Map a vector arity expression to its generated resolved field id."""

    if expression is None:
        return None
    try:
        return modifier_field_ids[expression.modifier_name]
    except KeyError as error:
        raise ValueError(
            "operand vector arity expression references unresolved modifier "
            f"{expression.modifier_name!r}"
        ) from error


def _resolve_operand_state_spaces(
    values: tuple[OperandStateSpaceValue, ...],
) -> tuple[ResolvedAddressStateSpace, ...]:
    """Lower static PTX state-space values normalized at the input boundary."""

    result: list[ResolvedAddressStateSpace] = []
    for value in values:
        if not is_semantic_value(SemanticDomain.MEMORY_STATE_SPACE, value.value):
            raise ValueError(f"unknown operand state space {value.value!r}")
        result.append(
            ResolvedAddressStateSpace(
                value=value.value,
                availability=tuple(value.availability.items()),
            )
        )
    return tuple(result)


def _resolve_parameter_address_constraint(
    constraint: OperandParameterConstraint | None,
) -> ResolvedParameterAddressConstraint | None:
    if constraint is None:
        return None
    return ResolvedParameterAddressConstraint(
        direction=constraint.direction,
        function_availability=tuple(constraint.function_availability.items()),
    )


def _require_operand_role(field: ResolvedField) -> ResolvedOperandRole:
    if field.operand_role is None:
        raise ValueError(f"operand field {field.name!r} has no semantic role")
    return field.operand_role


def _require_operand_access(field: ResolvedField) -> ResolvedOperandAccess:
    if field.operand_access is None:
        raise ValueError(f"operand field {field.name!r} has no access mode")
    return field.operand_access


def _variant_cpp_name(opcode: str, variant_id: str) -> str:
    prefix = f"{opcode}_"
    if not variant_id.startswith(prefix):
        raise ValueError(
            f"variant {variant_id!r} does not start with opcode prefix {prefix!r}"
        )
    return file_stem_to_pascal_case(variant_id.removeprefix(prefix))
