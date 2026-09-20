"""Typed normalized models for PTX ISA and C++ backend YAML specifications."""

from dataclasses import dataclass, field
from enum import Enum
from typing import Any


class ConditionCodeEffect(Enum):
    """Implicit CC.CF interpretation and access when an instruction executes."""

    NONE = "none"
    CARRY_OUT = "carry_out"
    CARRY_IN = "carry_in"
    CARRY_IN_OUT = "carry_in_out"
    BORROW_OUT = "borrow_out"
    BORROW_IN = "borrow_in"
    BORROW_IN_OUT = "borrow_in_out"


class _SemanticToken(Enum):
    """Strict semantic enum with stable YAML-facing formatting."""

    def __str__(self) -> str:
        """Return the spelling retained in diagnostics and generated identifiers."""

        return self.value


class ModifierKind(_SemanticToken):
    """PTX semantic category selected by a modifier declaration."""

    FLAG = "flag"
    TYPE = "type"
    ENUM = "enum"
    STATE_SPACE = "state_space"
    SCOPE = "scope"
    SEMANTICS = "semantics"
    CACHE = "cache"
    VECTOR = "vector"
    EVICTION_PRIORITY = "eviction_priority"
    PREFETCH_SIZE = "prefetch_size"
    ROUNDING = "rounding"
    PREDICATE = "predicate"
    COMPARISON = "comparison"
    BOOLEAN_OP = "boolean_op"
    SHAPE = "shape"
    LAYOUT = "layout"
    PHASE_TYPE = "phase_type"
    MBARRIER_LAYOUT = "mbarrier_layout"
    MEMORY_ORDER = "memory_order"
    PROXY = "proxy"
    PROXY_PAIR = "proxy_pair"
    TENSOR_MAP = "tensor_map"
    MATRIX = "matrix"
    CUSTOM = "custom"


class ModifierPresence(_SemanticToken):
    """Source presence contract for one modifier slot."""

    REQUIRED = "required"
    OPTIONAL = "optional"
    FIXED = "fixed"
    ABSENT = "absent"


class OperandKind(_SemanticToken):
    """PTX semantic category selected by an operand declaration."""

    REGISTER = "reg"
    PREDICATE = "pred"
    PREDICATE_OR_SINK = "pred_or_sink"
    PREDICATE_SOURCE = "pred_source"
    PREDICATE_OR_SPECIAL_REGISTER = "pred_or_sreg"
    PREDICATE_OR_NOT = "pred_or_not"
    SPECIAL_REGISTER = "sreg"
    ADDRESS = "addr"
    IMMEDIATE = "imm"
    CONSTANT_EXPRESSION = "const_expr"
    LABEL = "label"
    LABEL_OR_REGISTER = "label_or_reg"
    SYMBOL = "symbol"
    FUNCTION = "func"
    REGISTER_OR_IMMEDIATE = "reg_or_imm"
    REGISTER_OR_SINK = "reg_or_sink"
    SHFL_DESTINATION = "shfl_dest"
    PREDICATE_PAIR = "pred_pair"
    PREDICATE_PAIR_OR_SINK = "pred_pair_or_sink"
    MOV_SCALAR_SOURCE = "mov_scalar_src"
    CLUSTER_ADDRESS = "cluster_address"
    VECTOR_REGISTER = "vector_reg"
    VECTOR_SPECIAL_REGISTER = "vector_sreg"
    REGISTER_VECTOR = "reg_vector"
    DIRECT_CALL_TARGET = "direct_call_target"
    INDIRECT_CALL_TARGET = "indirect_call_target"
    INDIRECT_CALL_METADATA = "indirect_call_metadata"
    BRANCH_TARGET_SET = "branch_target_set"
    CALL_RETURN_PARAMETER = "call_return_param"
    CALL_ARGUMENTS = "call_arguments"
    ADDRESS_OR_SYMBOL = "addr_or_symbol"
    REGISTER_LIST = "reg_list"
    PREDICATE_LIST = "pred_list"
    OPERAND_LIST = "operand_list"
    VECTOR = "vector"
    TUPLE = "tuple"
    TENSOR_COORDINATE = "tensor_coordinate"
    MATRIX_FRAGMENT = "matrix_fragment"
    DESCRIPTOR = "descriptor"
    TYPED_TOKEN = "typed_token"
    MBARRIER_STATE_TOKEN = "mbarrier_state_token"
    OPTIONAL_REGISTER = "optional_reg"
    OPTIONAL_PREDICATE = "optional_pred"
    OPTIONAL_IMMEDIATE = "optional_imm"
    OPTIONAL_REGISTER_OR_IMMEDIATE = "optional_reg_or_imm"
    OPTIONAL_REGISTER_LIST = "optional_reg_list"
    OPTIONAL_PREDICATE_LIST = "optional_pred_list"
    OPTIONAL_OPERAND_LIST = "optional_operand_list"


class OperandRole(_SemanticToken):
    """Semantic role declared for one PTX operand position."""

    DESTINATION = "dst"
    SOURCE = "src"
    SOURCE_1 = "src1"
    SOURCE_2 = "src2"
    SOURCE_3 = "src3"
    SOURCE_4 = "src4"
    ADDRESS = "addr"
    PREDICATE = "predicate"
    GUARD = "guard"
    LABEL = "label"
    MASK = "mask"
    METADATA = "metadata"
    DESCRIPTOR = "descriptor"
    BARRIER = "barrier"
    THREAD_COUNT = "thread_count"
    IMMEDIATE = "immediate"
    SHAPE = "shape"
    LAYOUT = "layout"
    OTHER = "other"


class OperandAccess(_SemanticToken):
    """Access intent declared for one PTX operand position."""

    READ = "read"
    WRITE = "write"
    READ_WRITE = "read_write"
    ADDRESS = "address"
    CONTROL = "control"
    METADATA = "metadata"


class OperandTypeCompatibilityValueKind(_SemanticToken):
    """Semantic value category used by a contextual operand type rule."""

    SPECIAL_REGISTER = "special_register"


class OperandTypeExpressionKind(Enum):
    """The supported source-level ways to determine an operand scalar type."""

    FIXED_SCALAR = "fixed_scalar"
    MODIFIER = "modifier"


class OperandRegisterWidthPolicy(_SemanticToken):
    """Register-width relation accepted by an operand type constraint."""

    EXACT = "exact"
    SAME_WIDTH = "same_width"
    EQUAL_OR_WIDER = "equal_or_wider"


class OperandImmediateConversionPolicy(_SemanticToken):
    """How a decoded integer immediate is converted at one operand use."""

    NARROW = "narrow"
    REQUIRE_TARGET_RANGE = "require_target_range"


class OperandVectorTypePolicy(_SemanticToken):
    """How an instruction type maps onto a register-vector operand."""

    AGGREGATE = "aggregate"
    ELEMENT = "element"


class MbarrierStateTokenForm(_SemanticToken):
    """Whether an mbarrier state-token operand permits the ``_`` sink."""

    REGISTER = "register"
    REGISTER_OR_SINK = "register_or_sink"
    SINK = "sink"


class OperandLayoutKind(_SemanticToken):
    """Matching algorithm selected by one operand layout."""

    FLAT = "flat"
    CALL = "call"
    INDIRECT_CALL = "indirect_call"


@dataclass(frozen=True)
class OperandVectorArityExpression:
    """A parsed register-vector arity expression from the YAML specification."""

    modifier_name: str


@dataclass(frozen=True)
class OperandStateSpaceExpression:
    """A parsed operand state-space expression from the YAML specification."""

    modifier_name: str


@dataclass(frozen=True)
class OperandStateSpaceValue:
    """One statically allowed operand state space and its target requirement."""

    value: str
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class OperandParameterConstraint:
    """Direction and function-specific availability for a .param address."""

    direction: str
    function_availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class MemoryConsistencyConstraint:
    """Typed cross-modifier rule for a family of ld/st variants."""

    semantics_modifier: str
    scope_modifier: str
    cache_modifier: str | None
    address_operand: str
    type_modifier: str
    mmio_modifier: str | None = None
    state_space_modifier: str | None = None
    mmio_semantics: tuple[ModifierValueSpec, ...] = ()


@dataclass(frozen=True)
class AddressAlignmentConstraint:
    """Typed static alignment rule for one or more address operands."""

    address_operands: tuple[str, ...]
    type_modifier: str | None = None
    vector_modifier: str | None = None
    immediate_operand: str | None = None
    alignment: int | None = None


@dataclass(frozen=True)
class MemoryVectorConstraint:
    """Typed PTX 8.8 256-bit ld/st vector cross-rule."""

    type_modifier: str
    vector_operand: str
    address_operand: str
    availability: dict[str, Any] = field(default_factory=dict)
    state_space_modifier: str | None = None
    require_modern: bool = False


@dataclass(frozen=True)
class ImmediateValueConstraint:
    """Restrict one immediate operand to an explicit integer allowlist."""

    operand: str
    values: tuple[int, ...]


@dataclass(frozen=True)
class ImmediateRangeConstraint:
    """Restrict one immediate operand to an inclusive integer range."""

    operand: str
    minimum: int
    maximum: int | None = None


@dataclass(frozen=True)
class ImmediateMultipleOfConstraint:
    """Require one immediate operand to be divisible by a positive integer."""

    operand: str
    divisor: int


class RuntimeLookupKind(str, Enum):
    """Runtime C++ lookup forms emitted for backend value domains."""

    PTX_SUFFIX = "ptx_suffix"


@dataclass(frozen=True)
class OperandTypeExpression:
    """A parsed operand type expression from the YAML syntax specification."""

    kind: OperandTypeExpressionKind
    scalar_type: str | None = None
    modifier_name: str | None = None


@dataclass(frozen=True)
class ModifierValueSpec:
    """One legal semantic modifier value and its optional target requirement."""

    value: str | bool | int
    token: str | None = None
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class ModifierSpec:
    """One normalized PTX modifier in an instruction variant."""

    name: str
    kind: ModifierKind
    presence: ModifierPresence
    domain: str | None = None
    values: tuple[ModifierValueSpec, ...] = ()
    value: str | bool | int | None = None
    token: str | None = None
    default: str | bool | int | None = None


def modifier_spellings(modifier: ModifierSpec) -> tuple[str, ...]:
    """Return every canonical lexer-facing spelling accepted by a modifier."""

    if modifier.token is not None:
        return (modifier.token,)
    if modifier.values:
        return tuple(
            value.token if value.token is not None else f".{value.value}"
            for value in modifier.values
        )
    if isinstance(modifier.value, str):
        return (f".{modifier.value}",)
    if modifier.value is True:
        return (f".{modifier.name}",)
    return ()


@dataclass(frozen=True)
class OperandSpec:
    """One normalized source-level PTX operand."""

    name: str
    kind: OperandKind
    role: OperandRole | None = None
    access: OperandAccess | None = None
    type_expression: OperandTypeExpression | None = None
    register_width_policy: OperandRegisterWidthPolicy = (
        OperandRegisterWidthPolicy.SAME_WIDTH
    )
    immediate_conversion_policy: OperandImmediateConversionPolicy = (
        OperandImmediateConversionPolicy.NARROW
    )
    state_space_values: tuple[OperandStateSpaceValue, ...] = ()
    state_space_expression: OperandStateSpaceExpression | None = None
    parameter_constraint: OperandParameterConstraint | None = None
    vector_arities: tuple[int, ...] = ()
    vector_arity_expression: OperandVectorArityExpression | None = None
    vector_type_policy: OperandVectorTypePolicy = OperandVectorTypePolicy.AGGREGATE
    vector_allow_sink: bool = False
    vector_sink_payload_bits: int = 0
    allow_destination_sink: bool = False
    allow_predicate_sink: bool = False
    mbarrier_state_token_form: MbarrierStateTokenForm = MbarrierStateTokenForm.REGISTER
    sink_availability: dict[str, Any] = field(default_factory=dict)
    type_tag: str | None = None
    minimum_elements: int | None = None
    maximum_elements: int | None = None
    element_kinds: tuple[OperandKind, ...] = ()


@dataclass(frozen=True)
class OperandLayoutSpec:
    """One stable operand layout within a modifier-selected variant."""

    name: str
    operands: tuple[OperandSpec, ...]
    kind: OperandLayoutKind = OperandLayoutKind.FLAT
    # Empty means that this layout introduces no target requirement beyond its
    # containing variant's availability.
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class OperandTypeCompatibilitySpec:
    """Contextual operand type accepted by one instruction variant."""

    operand: str
    value_kind: OperandTypeCompatibilityValueKind
    values: tuple[str, ...]
    instruction_width: int
    effective_type: str
    availability: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class VariantSpec:
    """One PTX instruction variant."""

    name: str
    availability: dict[str, Any]
    modifiers: tuple[ModifierSpec, ...]
    operand_layouts: tuple[OperandLayoutSpec, ...]
    condition_code_effect: ConditionCodeEffect = ConditionCodeEffect.NONE
    rule: str | None = None
    operand_type_compatibilities: tuple[OperandTypeCompatibilitySpec, ...] = ()
    memory_consistency: MemoryConsistencyConstraint | None = None
    permits_unified_address: bool = False
    unified_address_access: str = "none"
    address_alignments: tuple[AddressAlignmentConstraint, ...] = ()
    memory_vector: MemoryVectorConstraint | None = None
    immediate_value: ImmediateValueConstraint | None = None
    immediate_ranges: tuple[ImmediateRangeConstraint, ...] = ()
    immediate_multiple_of: ImmediateMultipleOfConstraint | None = None
    # Complete historical source orders for the variant-local modifier slots.
    # The canonical order remains ``modifiers``; each alias includes absent
    # slots so it can be validated as a permutation of that order.
    modifier_order_aliases: tuple[tuple[str, ...], ...] = ()


@dataclass(frozen=True)
class InstructionSpec:
    """All merged YAML definitions and variants for one opcode."""

    opcode: str
    variants: tuple[VariantSpec, ...]
    syntax_forms: tuple[str, ...] = ()
    source_categories: tuple[str, ...] = ()
    codegen_category: str = "uncategorized"


# -----------------------------------------------------------------------------
# C++ backend model
# -----------------------------------------------------------------------------
#
# ``DomainBackend`` is consumed by the current generation path for all
# semantic-value-to-C++ spelling/type mappings.  Per-instruction emit policy
# remains modeled for future consumers, but does not control the current
# resolved-IR structure.


@dataclass(frozen=True)
class DomainBackend:
    """C++ representations for all semantic values in one backend domain."""

    cpp_type: str
    values: dict[str, str]
    default: str | None = None
    runtime_lookup: RuntimeLookupKind | None = None


@dataclass(frozen=True)
class ModifierBackend:
    """C++ field and value-domain mapping for one instruction modifier."""

    field: str
    cpp_type: str | None = None
    domain: str | None = None
    default: str | None = None


@dataclass(frozen=True)
class OperandBackend:
    """C++ field and type mapping for one instruction operand."""

    field: str
    cpp_type: str


@dataclass(frozen=True)
class EmitAlternativeBackend:
    """One nested C++ representation and the PTX variants assigned to it."""

    name: str
    variants: tuple[str, ...] = ()


@dataclass(frozen=True)
class EmitBackend:
    """C++ storage-shape policy for one generated instruction."""

    kind: str
    instance: str | None = None
    type: str | None = None
    alternatives: tuple[EmitAlternativeBackend, ...] = ()


@dataclass(frozen=True)
class InstructionBackend:
    """Complete C++ backend mapping for one PTX opcode."""

    opcode: str
    cpp: str
    emit: EmitBackend
    modifiers: dict[str, ModifierBackend]
    operands: dict[str, OperandBackend]
    type_checker_rule: str | None = None
    visitor_name: str | None = None
    modifier_order: tuple[str, ...] = ()
    operand_order: tuple[str, ...] = ()


@dataclass(frozen=True)
class CodegenUnit:
    """Aggregate normalized PTX semantics and C++ backend data.

    The backend loader currently returns a backend-only unit with an empty
    ``instructions`` tuple; the ISA database continues to own merged
    instructions. This keeps C++ mappings typed without coupling PTX database
    discovery to one backend.
    """

    spec_schema: str
    backend_schema: str
    category: str
    namespace: str
    includes: tuple[str, ...] | None
    instructions: tuple[InstructionSpec, ...]
    backends: dict[str, InstructionBackend]
    domains: dict[str, DomainBackend]
