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
from ptx_frontend.code_gen.cpp_backend import (
    CppDomain,
    cpp_domain,
    cpp_optional_value,
    cpp_value,
)
from ptx_frontend.spec.model import (
    ConditionCodeEffect,
    AddressAlignmentConstraint,
    ImmediateMultipleOfConstraint,
    ImmediateRangeConstraint,
    ImmediateValueConstraint,
    InstructionSpec,
    MemoryConsistencyConstraint,
    MemoryVectorConstraint,
    MbarrierStateTokenForm,
    ModifierSpec,
    ModifierValueSpec,
    OperandParameterConstraint,
    OperandImmediateConversionPolicy,
    OperandRegisterWidthPolicy,
    OperandSpec,
    OperandStateSpaceExpression,
    OperandStateSpaceValue,
    OperandTypeCompatibilitySpec,
    OperandTypeExpression,
    OperandTypeExpressionKind,
    OperandVectorArityExpression,
    OperandVectorTypePolicy,
    VariantSpec,
)
from ptx_frontend.ir.resolved_value_kind import ResolvedValueKind
from ptx_frontend.ir.resolved_value_policy import (
    modifier_value_kind,
    resolved_modifier_value_policy,
    unsupported_resolved_modifier_value_error,
    validate_resolved_modifier_value_type,
)
from ptx_frontend.code_gen.resolved_value_traits import (
    modifier_value_cpp_expr,
    resolved_modifier_value_traits,
)


class ResolvedFieldOrigin(Enum):
    """The PTX specification element that supplies a resolved field."""

    MODIFIER = "modifier"
    OPERAND = "operand"


_OPERAND_VALUE_KINDS: dict[str, ResolvedValueKind] = {
    "reg": ResolvedValueKind.REGISTER,
    "imm": ResolvedValueKind.IMMEDIATE,
    "reg_or_imm": ResolvedValueKind.REG_OR_IMM,
    "reg_or_sink": ResolvedValueKind.REGISTER_OR_SINK,
    "shfl_dest": ResolvedValueKind.SHFL_DESTINATION,
    "pred_pair": ResolvedValueKind.PREDICATE_PAIR,
    "pred_pair_or_sink": ResolvedValueKind.PREDICATE_PAIR_OR_SINK,
    "mov_scalar_src": ResolvedValueKind.MOV_SOURCE,
    "cluster_address": ResolvedValueKind.MOV_SOURCE,
    "vector_reg": ResolvedValueKind.VECTOR_REGISTER,
    "vector_sreg": ResolvedValueKind.VECTOR_SPECIAL_REGISTER,
    "pred": ResolvedValueKind.PREDICATE,
    "pred_or_sink": ResolvedValueKind.PREDICATE_OR_SINK,
    "pred_source": ResolvedValueKind.PREDICATE_SOURCE,
    "pred_or_sreg": ResolvedValueKind.PREDICATE_SOURCE,
    "pred_or_not": ResolvedValueKind.PREDICATE,
    "label": ResolvedValueKind.BRANCH_TARGET,
    "sreg": ResolvedValueKind.SPECIAL_REGISTER,
    "symbol": ResolvedValueKind.SYMBOL,
    "addr": ResolvedValueKind.ADDRESS,
    "reg_vector": ResolvedValueKind.REGISTER_VECTOR,
    "descriptor": ResolvedValueKind.REGISTER,
    "typed_token": ResolvedValueKind.REGISTER,
    "mbarrier_state_token": ResolvedValueKind.MBARRIER_STATE_TOKEN,
    "tensor_coordinate": ResolvedValueKind.TENSOR_COORDINATE,
    "matrix_fragment": ResolvedValueKind.REGISTER_VECTOR,
    "direct_call_target": ResolvedValueKind.DIRECT_CALL_TARGET,
    "indirect_call_target": ResolvedValueKind.INDIRECT_CALLEE,
    "indirect_call_metadata": ResolvedValueKind.INDIRECT_CALLEE,
    "branch_target_set": ResolvedValueKind.BRANCH_TARGET_SET,
    "call_return_param": ResolvedValueKind.CALL_RETURN_PARAMETER,
    "call_arguments": ResolvedValueKind.CALL_ARGUMENTS,
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

    @property
    def value_cpp_type(self) -> str:
        """Return the backend-selected C++ type for this semantic value kind."""

        return cpp_value(
            CppDomain.RESOLVED_VALUE_CPP_TYPES,
            self.value_kind.value,
        )

    @property
    def cpp_type(self) -> str:
        """The C++ member type used by generated resolved instruction structs."""

        if self.storage is ResolvedFieldStorage.STATIC_CONSTANT:
            return self.value_cpp_type
        return f"WithLocs<{self.value_cpp_type}>"

    @property
    def cpp_constant_expr(self) -> str:
        """Return the generated C++ expression for a fixed modifier value."""

        if self.storage is not ResolvedFieldStorage.STATIC_CONSTANT:
            raise ValueError("only static resolved fields have constant expressions")
        if self.constant_value is None:
            raise ValueError(f"field {self.name!r}: fixed field has no constant value")
        try:
            return modifier_value_cpp_expr(
                self.value_kind,
                self.constant_value,
            )
        except ValueError as error:
            raise ValueError(
                f"field {self.name!r}: unsupported fixed value "
                f"{self.constant_value!r} for {self.value_kind.value}"
            ) from error


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
    rule: str | None

    condition_code_effect: ConditionCodeEffect = ConditionCodeEffect.NONE

    @property
    def condition_code_cpp_value(self) -> str:
        """Qualified C++ enumerator for this variant's canonical CC effect."""

        return "ConditionCodeEffect::" + file_stem_to_pascal_case(
            self.condition_code_effect.value
        )

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
    parameter_constraint: ResolvedParameterAddressConstraint | None = None
    allowed_vector_arities: tuple[int, ...] = ()
    vector_arity_modifier_field_id: str | None = None
    vector_type_policy: ResolvedVectorTypePolicy = ResolvedVectorTypePolicy.AGGREGATE
    allow_vector_sink: bool = False
    vector_sink_payload_bits: int = 0
    allow_destination_sink: bool = False
    allow_predicate_sink: bool = False
    mbarrier_state_token_form: MbarrierStateTokenForm = MbarrierStateTokenForm.REGISTER
    sink_availability: tuple[tuple[str, Any], ...] = ()
    allow_function_symbol: bool = False
    type_tag: str | None = None
    minimum_elements: int | None = None
    maximum_elements: int | None = None
    allowed_element_shapes: tuple[ResolvedOperandShape, ...] = ()


@dataclass(frozen=True)
class ResolvedOperandLayout:
    """One resolved-field binding layout paired by index with syntax layouts."""

    layout_id: str
    cpp_name: str
    fields: tuple[ResolvedField, ...]
    bindings: tuple[ResolvedOperandBinding, ...]
    availability: tuple[tuple[str, Any], ...]


@dataclass(frozen=True)
class ResolvedInstruction:
    """Resolved IR definition for one PTX opcode, such as ``Add``."""

    opcode: str
    cpp_name: str
    variants: tuple[ResolvedVariant, ...]


_OPERAND_ALLOWED_SHAPES = {
    "reg": (ResolvedOperandShape.REGISTER,),
    "imm": (ResolvedOperandShape.IMMEDIATE,),
    "reg_or_imm": (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.IMMEDIATE,
    ),
    "reg_or_sink": (ResolvedOperandShape.REGISTER,),
    "shfl_dest": (ResolvedOperandShape.SHFL_DESTINATION,),
    "pred_pair": (ResolvedOperandShape.PREDICATE_PAIR,),
    "pred_pair_or_sink": (ResolvedOperandShape.PREDICATE_PAIR,),
    "mov_scalar_src": (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.IMMEDIATE,
        ResolvedOperandShape.SPECIAL_REGISTER,
        ResolvedOperandShape.SYMBOL,
        ResolvedOperandShape.ADDRESS,
    ),
    "cluster_address": (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.SYMBOL,
        ResolvedOperandShape.ADDRESS,
    ),
    "vector_reg": (ResolvedOperandShape.VECTOR,),
    "vector_sreg": (ResolvedOperandShape.VECTOR,),
    "pred": (ResolvedOperandShape.PREDICATE,),
    "pred_or_sink": (ResolvedOperandShape.PREDICATE,),
    "pred_source": (
        ResolvedOperandShape.PREDICATE,
        ResolvedOperandShape.IMMEDIATE,
    ),
    "pred_or_sreg": (
        ResolvedOperandShape.PREDICATE,
        ResolvedOperandShape.IMMEDIATE,
        ResolvedOperandShape.SPECIAL_REGISTER,
    ),
    "pred_or_not": (ResolvedOperandShape.PREDICATE,),
    "label": (ResolvedOperandShape.BRANCH_TARGET,),
    "sreg": (ResolvedOperandShape.SPECIAL_REGISTER,),
    "symbol": (ResolvedOperandShape.SYMBOL,),
    "addr": (ResolvedOperandShape.ADDRESS,),
    "reg_vector": (ResolvedOperandShape.VECTOR,),
    "descriptor": (ResolvedOperandShape.REGISTER,),
    "typed_token": (ResolvedOperandShape.REGISTER,),
    "mbarrier_state_token": (ResolvedOperandShape.REGISTER,),
    "tensor_coordinate": (ResolvedOperandShape.VECTOR,),
    "matrix_fragment": (ResolvedOperandShape.VECTOR,),
    "direct_call_target": (ResolvedOperandShape.DIRECT_CALL_TARGET,),
    "indirect_call_target": (ResolvedOperandShape.INDIRECT_CALLEE,),
    "indirect_call_metadata": (ResolvedOperandShape.INDIRECT_CALLEE,),
    "branch_target_set": (ResolvedOperandShape.BRANCH_TARGET_SET,),
    "call_return_param": (ResolvedOperandShape.CALL_RETURN_PARAMETER,),
    "call_arguments": (ResolvedOperandShape.CALL_ARGUMENTS,),
}

_OPERAND_ROLES = {
    "dst": ResolvedOperandRole.DESTINATION,
    "src": ResolvedOperandRole.SOURCE,
    "src1": ResolvedOperandRole.SOURCE,
    "src2": ResolvedOperandRole.SOURCE,
    "src3": ResolvedOperandRole.SOURCE,
    "addr": ResolvedOperandRole.ADDRESS,
    "address": ResolvedOperandRole.ADDRESS,
    "predicate": ResolvedOperandRole.PREDICATE,
    "branch_target": ResolvedOperandRole.BRANCH_TARGET,
    "label": ResolvedOperandRole.BRANCH_TARGET,
    "barrier": ResolvedOperandRole.BARRIER,
    "thread_count": ResolvedOperandRole.THREAD_COUNT,
}

_OPERAND_ACCESS = {
    "read": ResolvedOperandAccess.READ,
    "write": ResolvedOperandAccess.WRITE,
    "readwrite": ResolvedOperandAccess.READ_WRITE,
    "control": ResolvedOperandAccess.CONTROL,
}


def from_instruction_spec(spec: InstructionSpec) -> ResolvedInstruction:
    """Build the resolved instruction model from one normalized PTX spec."""

    return ResolvedInstruction(
        opcode=spec.opcode,
        cpp_name=file_stem_to_pascal_case(spec.opcode),
        variants=tuple(
            _build_variant(spec.opcode, variant) for variant in spec.variants
        ),
    )


def _build_variant(opcode: str, variant: VariantSpec) -> ResolvedVariant:
    active_modifiers = tuple(
        modifier for modifier in variant.modifiers if modifier.presence != "absent"
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
        )
        for layout in variant.operand_layouts
    )

    return ResolvedVariant(
        variant_id=variant.name,
        condition_code_effect=variant.condition_code_effect,
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
    )


def _build_memory_consistency_constraint(
    constraint: MemoryConsistencyConstraint | None,
    modifier_field_ids: dict[str, str],
) -> ResolvedMemoryConsistencyConstraint | None:
    if constraint is None:
        return None
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
            (str(value.value), tuple(value.availability.items()))
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
    """Validate one modifier semantic value against its resolved backend domain."""

    validate_resolved_modifier_value_type(
        value_kind,
        value,
        modifier_name=modifier.name,
        default=default,
    )

    policy = resolved_modifier_value_policy(value_kind)
    if policy.python_type is bool:
        return

    traits = resolved_modifier_value_traits(value_kind)

    if traits.cpp_domain is None:
        raise AssertionError(f"{value_kind.value} has no configured C++ domain")

    if value not in cpp_domain(traits.cpp_domain).values:
        raise unsupported_resolved_modifier_value_error(
            value_kind,
            value,
            modifier_name=modifier.name,
            default=default,
        )


def _build_modifier_default(
    modifier: ModifierSpec,
) -> ResolvedModifierDefault | None:
    if modifier.presence != "optional":
        return None

    if modifier.default is None:
        raise ValueError(
            f"optional modifier {modifier.name!r} has no normalized default"
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
        default=False,
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
        elif modifier.kind == "flag":
            values.append(ModifierValueSpec(value=True))
    if modifier.presence == "optional":
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

    if compatibility.value_kind != "special_register":
        raise ValueError(
            f"unsupported operand compatibility value kind "
            f"{compatibility.value_kind!r}"
        )
    if value not in cpp_domain(CppDomain.SPECIAL_REGISTER_KINDS).values:
        raise ValueError(f"unsupported special-register compatibility value {value!r}")
    if compatibility.effective_type not in cpp_domain(CppDomain.SCALAR_TYPES).values:
        raise ValueError(
            f"unsupported effective scalar type {compatibility.effective_type!r}"
        )
    return ResolvedOperandTypeCompatibility(
        target_field_id=compatibility.operand,
        special_register_kind=value,
        instruction_width=compatibility.instruction_width,
        effective_type=compatibility.effective_type,
        availability=tuple(compatibility.availability.items()),
    )


def _build_operand_layout(
    layout_id: str,
    operands: tuple[OperandSpec, ...],
    availability: dict[str, Any],
    modifier_field_ids: dict[str, str],
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
                allow_destination_sink=operand.allow_destination_sink,
                allow_predicate_sink=operand.allow_predicate_sink,
                mbarrier_state_token_form=operand.mbarrier_state_token_form,
                sink_availability=tuple(operand.sink_availability.items()),
                allow_function_symbol=operand.kind == "mov_scalar_src",
                type_tag=operand.type_tag,
                minimum_elements=operand.minimum_elements,
                maximum_elements=operand.maximum_elements,
                allowed_element_shapes=tuple(
                    (
                        ResolvedOperandShape.REGISTER
                        if kind == "reg"
                        else ResolvedOperandShape.IMMEDIATE
                    )
                    for kind in operand.element_kinds
                ),
            )
            for operand, field in zip(operands, fields, strict=True)
        ),
        availability=tuple(availability.items()),
    )


def _build_modifier_field(modifier: ModifierSpec) -> ResolvedField:
    try:
        value_kind = modifier_value_kind(modifier.kind)
    except ValueError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: unsupported resolved modifier kind "
            f"{modifier.kind!r}"
        ) from error

    return ResolvedField(
        name=cpp_optional_value(
            CppDomain.MODIFIER_FIELD_NAMES,
            modifier.name,
        )
        or modifier.name,
        value_kind=value_kind,
        origin=ResolvedFieldOrigin.MODIFIER,
        source_name=modifier.name,
        storage=(
            ResolvedFieldStorage.STATIC_CONSTANT
            if modifier.presence == "fixed"
            else ResolvedFieldStorage.INSTANCE
        ),
        constant_value=(modifier.value if modifier.presence == "fixed" else None),
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
        role = _OPERAND_ROLES[operand.role or ""]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported resolved operand role "
            f"{operand.role!r}"
        ) from error

    try:
        access = _OPERAND_ACCESS[operand.access or ""]
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
        if expression.scalar_type not in cpp_domain(CppDomain.SCALAR_TYPES).values:
            raise ValueError(
                f"unsupported fixed operand scalar type {expression.scalar_type!r}"
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
    """Validate static state spaces against the semantic C++ value domain."""

    supported = cpp_domain(CppDomain.MEMORY_STATE_SPACES).values
    result: list[ResolvedAddressStateSpace] = []
    for value in values:
        if value.value not in supported:
            raise ValueError(
                f"unsupported resolved operand state space {value.value!r}"
            )
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
    if constraint.direction not in cpp_domain(CppDomain.PARAMETER_DIRECTIONS).values:
        raise ValueError(
            f"unsupported resolved parameter direction {constraint.direction!r}"
        )
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
