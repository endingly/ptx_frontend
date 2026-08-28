"""Python model for generated PTX Resolved IR instruction definitions.

The model is derived from the normalized PTX-facing ``InstructionSpec``.  It
describes the semantic fields that must appear in the generated C++ resolved
instruction structs; it does not describe C++ storage or emitter layout.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any

from base.utils import file_stem_to_pascal_case
from code_gen.cpp_backend import (
    CppDomain,
    cpp_domain,
    cpp_optional_value,
    cpp_value,
)
from code_gen.model import (
    AddressAlignmentConstraint,
    InstructionSpec,
    MemoryConsistencyConstraint,
    MemoryVectorConstraint,
    ModifierSpec,
    ModifierValueSpec,
    OperandParameterConstraint,
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


class ResolvedFieldOrigin(Enum):
    """The PTX specification element that supplies a resolved field."""

    MODIFIER = "modifier"
    OPERAND = "operand"


class ResolvedValueKind(Enum):
    """Runtime C++ value category produced for one resolved field."""

    BOOL = "Bool"
    SCALAR_TYPE = "ScalarType"
    ROUNDING_MODE = "RoundingMode"
    COMPARISON_OPERATOR = "ComparisonOperator"
    BOOLEAN_OPERATOR = "BooleanOperator"
    CACHE_OPERATOR = "CacheOperator"
    EVICTION_PRIORITY = "EvictionPriority"
    MEMORY_CONSISTENCY = "MemoryConsistency"
    MEMORY_SCOPE = "MemoryScope"
    VECTOR_ARITY = "VectorArity"
    MEMORY_STATE_SPACE = "MemoryStateSpace"
    REGISTER = "Register"
    PREDICATE = "Predicate"
    IMMEDIATE = "Immediate"
    REG_OR_IMM = "RegOrImm"
    MOV_SOURCE = "MovSource"
    BRANCH_TARGET = "BranchTarget"
    SPECIAL_REGISTER = "SpecialRegister"
    SYMBOL = "Symbol"
    ADDRESS = "Address"
    REGISTER_VECTOR = "RegisterVector"
    DIRECT_CALL_TARGET = "DirectCallTarget"
    INDIRECT_CALLEE = "IndirectCallee"
    BRANCH_TARGET_SET = "BranchTargetSet"
    CALL_RETURN_PARAMETER = "CallReturnParameter"
    CALL_ARGUMENTS = "CallArguments"


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
    state_space_field_id: str | None = None


@dataclass(frozen=True)
class ResolvedAddressAlignmentConstraint:
    """Generated field identities for one natural address-alignment rule."""

    address_field_id: str
    type_field_id: str
    vector_field_id: str | None = None


@dataclass(frozen=True)
class ResolvedMemoryVectorConstraint:
    """Generated field identities for the PTX 8.8 vector cross-rule."""

    type_field_id: str
    vector_field_id: str
    address_field_id: str
    availability: tuple[tuple[str, Any], ...]
    state_space_field_id: str | None = None


@dataclass(frozen=True)
class ResolvedField:
    """One provenance-carrying field in a resolved variant struct."""

    name: str
    value_cpp_type: str
    origin: ResolvedFieldOrigin
    source_name: str
    operand_role: ResolvedOperandRole | None = None
    operand_access: ResolvedOperandAccess | None = None
    allowed_operand_shapes: tuple[ResolvedOperandShape, ...] = ()
    storage: ResolvedFieldStorage = ResolvedFieldStorage.INSTANCE
    constant_value: str | bool | int | None = None

    @property
    def value_kind(self) -> ResolvedValueKind:
        """Return the generic resolver category for this output field."""

        for kind in ResolvedValueKind:
            if (
                cpp_value(CppDomain.RESOLVED_VALUE_CPP_TYPES, kind.value)
                == self.value_cpp_type
            ):
                return kind
        raise ValueError(
            f"C++ backend does not assign resolved value kind to "
            f"{self.value_cpp_type!r}"
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
        if self.value_cpp_type == "bool" and isinstance(self.constant_value, bool):
            return "true" if self.constant_value else "false"
        if self.value_cpp_type == "ScalarType" and isinstance(self.constant_value, str):
            return cpp_value(CppDomain.SCALAR_TYPES, self.constant_value)
        if self.value_cpp_type == "RoundingMode" and isinstance(
            self.constant_value, str
        ):
            return cpp_value(CppDomain.ROUNDING_MODES, self.constant_value)
        if self.value_cpp_type == "ComparisonOperator" and isinstance(
            self.constant_value, str
        ):
            return cpp_value(CppDomain.COMPARISON_OPERATORS, self.constant_value)
        if self.value_cpp_type == "BooleanOperator" and isinstance(
            self.constant_value, str
        ):
            return cpp_value(CppDomain.BOOLEAN_OPERATORS, self.constant_value)
        if self.value_cpp_type == "CacheOperator" and isinstance(
            self.constant_value, str
        ):
            return cpp_value(CppDomain.CACHE_OPERATORS, self.constant_value)
        if self.value_cpp_type == "MemoryStateSpace" and isinstance(
            self.constant_value, str
        ):
            return cpp_value(CppDomain.MEMORY_STATE_SPACES, self.constant_value)
        if self.value_cpp_type == "MemoryConsistency" and isinstance(
            self.constant_value, str
        ):
            return cpp_value(CppDomain.MEMORY_CONSISTENCIES, self.constant_value)
        if self.value_cpp_type == "MemoryScope" and isinstance(self.constant_value, str):
            return cpp_value(CppDomain.MEMORY_SCOPES, self.constant_value)
        raise ValueError(
            f"field {self.name!r}: unsupported fixed value "
            f"{self.constant_value!r} for {self.value_cpp_type}"
        )


@dataclass(frozen=True)
class ResolvedVariant:
    """One alternative of an opcode's generated C++ ``Variant`` type."""

    variant_id: str
    cpp_name: str
    modifier_fields: tuple[ResolvedField, ...]
    modifier_bindings: tuple["ResolvedModifierBinding", ...]
    operand_layouts: tuple["ResolvedOperandLayout", ...]
    modifier_value_availabilities: tuple["ResolvedModifierValueAvailability", ...]
    operand_type_compatibilities: tuple["ResolvedOperandTypeCompatibility", ...]
    memory_consistency: ResolvedMemoryConsistencyConstraint | None
    address_alignment: ResolvedAddressAlignmentConstraint | None
    memory_vector: ResolvedMemoryVectorConstraint | None
    availability: tuple[tuple[str, Any], ...]
    rule: str | None

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

    value_cpp_type: str
    value: str | bool | int


@dataclass(frozen=True)
class ResolvedModifierValueAvailability:
    """Target requirement attached to one dynamic semantic modifier value."""

    source_kind_id: str
    value_cpp_type: str
    value: str | bool | int
    availability: tuple[tuple[str, Any], ...]


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
    "mov_scalar_src": (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.IMMEDIATE,
        ResolvedOperandShape.SPECIAL_REGISTER,
        ResolvedOperandShape.SYMBOL,
        ResolvedOperandShape.ADDRESS,
    ),
    "pred": (ResolvedOperandShape.PREDICATE,),
    "pred_or_not": (ResolvedOperandShape.PREDICATE,),
    "label": (ResolvedOperandShape.BRANCH_TARGET,),
    "sreg": (ResolvedOperandShape.SPECIAL_REGISTER,),
    "symbol": (ResolvedOperandShape.SYMBOL,),
    "addr": (ResolvedOperandShape.ADDRESS,),
    "reg_vector": (ResolvedOperandShape.VECTOR,),
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
        _build_modifier_field(modifier)
        for modifier in active_modifiers
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
        cpp_name=_variant_cpp_name(opcode, variant.name),
        modifier_fields=modifier_fields,
        modifier_bindings=tuple(
            ResolvedModifierBinding(
                source_kind_id=field.source_name,
                target_field_id=field.name,
                default_value=_build_modifier_default(modifier),
            )
            for modifier, field in zip(
                active_modifiers, modifier_fields, strict=True
            )
        ),
        operand_layouts=operand_layouts,
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
        address_alignment=_build_address_alignment_constraint(
            variant.address_alignment,
            {field.source_name: field.name for field in modifier_fields},
        ),
        memory_vector=_build_memory_vector_constraint(
            variant.memory_vector,
            {field.source_name: field.name for field in modifier_fields},
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
        cache_field_id=modifier_field_ids[constraint.cache_modifier],
        address_field_id=constraint.address_operand,
        state_space_field_id=(
            modifier_field_ids[constraint.state_space_modifier]
            if constraint.state_space_modifier is not None else None
        ),
    )


def _build_address_alignment_constraint(
    constraint: AddressAlignmentConstraint | None,
    modifier_field_ids: dict[str, str],
) -> ResolvedAddressAlignmentConstraint | None:
    if constraint is None:
        return None
    return ResolvedAddressAlignmentConstraint(
        address_field_id=constraint.address_operand,
        type_field_id=modifier_field_ids[constraint.type_modifier],
        vector_field_id=(
            modifier_field_ids[constraint.vector_modifier]
            if constraint.vector_modifier is not None else None
        ),
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
            if constraint.state_space_modifier is not None else None
        ),
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
        value_cpp_type = cpp_value(
            CppDomain.MODIFIER_VALUE_CPP_TYPES, modifier.kind
        )
    except ValueError as error:
        raise ValueError(
            f"optional modifier {modifier.name!r}: unsupported default for "
            f"modifier kind {modifier.kind!r}"
        ) from error

    if value_cpp_type == "bool" and type(modifier.default) is not bool:
        raise ValueError(
            f"optional flag modifier {modifier.name!r} must have a boolean "
            "default"
        )
    if value_cpp_type == "ScalarType":
        if not isinstance(modifier.default, str):
            raise ValueError(
                f"optional type modifier {modifier.name!r} must have a string "
                "default"
            )
        if modifier.default not in cpp_domain(CppDomain.SCALAR_TYPES).values:
            raise ValueError(
                f"optional type modifier {modifier.name!r} has unsupported "
                f"default {modifier.default!r}"
            )
    if value_cpp_type == "RoundingMode":
        if not isinstance(modifier.default, str):
            raise ValueError(
                f"optional rounding modifier {modifier.name!r} must have a "
                "string default"
            )
        if modifier.default not in cpp_domain(CppDomain.ROUNDING_MODES).values:
            raise ValueError(
                f"optional rounding modifier {modifier.name!r} has unsupported "
                f"default {modifier.default!r}"
            )
    if value_cpp_type == "ComparisonOperator":
        raise ValueError(
            f"optional comparison modifier {modifier.name!r} is unsupported"
        )
    if value_cpp_type == "BooleanOperator":
        raise ValueError(
            f"optional boolean modifier {modifier.name!r} is unsupported"
        )
    if value_cpp_type == "CacheOperator":
        if not isinstance(modifier.default, str):
            raise ValueError(
                f"optional cache modifier {modifier.name!r} must have a "
                "string default"
            )
        if modifier.default not in cpp_domain(CppDomain.CACHE_OPERATORS).values:
            raise ValueError(
                f"optional cache modifier {modifier.name!r} has unsupported "
                f"default {modifier.default!r}"
            )
    if value_cpp_type == "MemoryStateSpace":
        if not isinstance(modifier.default, str):
            raise ValueError(
                f"optional state-space modifier {modifier.name!r} must have a "
                "string default"
            )
        if modifier.default not in cpp_domain(CppDomain.MEMORY_STATE_SPACES).values:
            raise ValueError(
                f"optional state-space modifier {modifier.name!r} has "
                f"unsupported default {modifier.default!r}"
            )
    if value_cpp_type == "MemoryConsistency":
        if not isinstance(modifier.default, str) or modifier.default not in cpp_domain(
            CppDomain.MEMORY_CONSISTENCIES
        ).values:
            raise ValueError(
                f"optional semantics modifier {modifier.name!r} has unsupported "
                f"default {modifier.default!r}"
            )
    if value_cpp_type == "MemoryScope":
        if not isinstance(modifier.default, str) or modifier.default not in cpp_domain(
            CppDomain.MEMORY_SCOPES
        ).values:
            raise ValueError(
                f"optional scope modifier {modifier.name!r} has unsupported "
                f"default {modifier.default!r}"
            )
    return ResolvedModifierDefault(
        value_cpp_type=value_cpp_type,
        value=modifier.default,
    )


def _build_modifier_value_availability(
    modifier: ModifierSpec, value: ModifierValueSpec
) -> ResolvedModifierValueAvailability:
    try:
        value_cpp_type = cpp_value(
            CppDomain.MODIFIER_VALUE_CPP_TYPES, modifier.kind
        )
    except ValueError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: availability for unsupported modifier "
            f"kind {modifier.kind!r}"
        ) from error
    if value_cpp_type == "ScalarType" and not isinstance(value.value, str):
        raise ValueError(
            f"modifier {modifier.name!r}: scalar-type value must be a string"
        )
    if value_cpp_type == "bool" and not isinstance(value.value, bool):
        raise ValueError(
            f"modifier {modifier.name!r}: flag value must be boolean"
        )
    if value_cpp_type == "RoundingMode":
        if not isinstance(value.value, str):
            raise ValueError(
                f"modifier {modifier.name!r}: rounding value must be a string"
            )
        if value.value not in cpp_domain(CppDomain.ROUNDING_MODES).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported rounding value "
                f"{value.value!r}"
            )
    if value_cpp_type == "ComparisonOperator":
        if not isinstance(value.value, str):
            raise ValueError(
                f"modifier {modifier.name!r}: comparison value must be a string"
            )
        if value.value not in cpp_domain(CppDomain.COMPARISON_OPERATORS).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported comparison value "
                f"{value.value!r}"
            )
    if value_cpp_type == "BooleanOperator":
        if not isinstance(value.value, str):
            raise ValueError(
                f"modifier {modifier.name!r}: boolean value must be a string"
            )
        if value.value not in cpp_domain(CppDomain.BOOLEAN_OPERATORS).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported boolean value "
                f"{value.value!r}"
            )
    if value_cpp_type == "CacheOperator":
        if not isinstance(value.value, str):
            raise ValueError(
                f"modifier {modifier.name!r}: cache value must be a string"
            )
        if value.value not in cpp_domain(CppDomain.CACHE_OPERATORS).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported cache value "
                f"{value.value!r}"
            )
    if value_cpp_type == "VectorArity":
        if not isinstance(value.value, str):
            raise ValueError(
                f"modifier {modifier.name!r}: vector value must be a string"
            )
        if value.value not in cpp_domain(CppDomain.VECTOR_ARITIES).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported vector value "
                f"{value.value!r}"
            )
    if value_cpp_type == "MemoryStateSpace":
        if not isinstance(value.value, str):
            raise ValueError(
                f"modifier {modifier.name!r}: state-space value must be a string"
            )
        if value.value not in cpp_domain(CppDomain.MEMORY_STATE_SPACES).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported state-space value "
                f"{value.value!r}"
            )
    if value_cpp_type == "MemoryConsistency":
        if not isinstance(value.value, str) or value.value not in cpp_domain(
            CppDomain.MEMORY_CONSISTENCIES
        ).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported memory consistency "
                f"value {value.value!r}"
            )
    if value_cpp_type == "MemoryScope":
        if not isinstance(value.value, str) or value.value not in cpp_domain(
            CppDomain.MEMORY_SCOPES
        ).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported memory scope value "
                f"{value.value!r}"
            )
    return ResolvedModifierValueAvailability(
        source_kind_id=modifier.name,
        value_cpp_type=value_cpp_type,
        value=value.value,
        availability=tuple(value.availability.items()),
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
            )
            for operand, field in zip(operands, fields, strict=True)
        ),
        availability=tuple(availability.items()),
    )


def _build_modifier_field(modifier: ModifierSpec) -> ResolvedField:
    try:
        value_cpp_type = cpp_value(
            CppDomain.MODIFIER_VALUE_CPP_TYPES, modifier.kind
        )
    except ValueError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: unsupported resolved modifier kind "
            f"{modifier.kind!r}"
        ) from error

    return ResolvedField(
        name=cpp_optional_value(CppDomain.MODIFIER_FIELD_NAMES, modifier.name)
        or modifier.name,
        value_cpp_type=value_cpp_type,
        origin=ResolvedFieldOrigin.MODIFIER,
        source_name=modifier.name,
        storage=(
            ResolvedFieldStorage.STATIC_CONSTANT
            if modifier.presence == "fixed"
            else ResolvedFieldStorage.INSTANCE
        ),
        constant_value=modifier.value if modifier.presence == "fixed" else None,
    )


def _build_operand_field(operand: OperandSpec) -> ResolvedField:
    try:
        value_cpp_type = cpp_value(
            CppDomain.OPERAND_VALUE_CPP_TYPES, operand.kind
        )
    except ValueError as error:
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
        value_cpp_type=value_cpp_type,
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
