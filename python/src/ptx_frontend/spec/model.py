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


class AsyncCompletionKind(Enum):
    """Instruction-local completion identity shared by async instruction families."""

    NONE = "none"
    ASYNC_GROUP = "async_group"
    BULK_GROUP = "bulk_group"
    MBARRIER_COMPLETE_TX_BYTES = "mbarrier_complete_tx_bytes"
    MBARRIER_COMPLETE_TX16B = "mbarrier_complete_tx16b"
    FABRIC_READ_WAIT = "fabric_read_wait"
    WGMMA_GROUP = "wgmma_group"
    TCGEN_LOAD_WAIT = "tcgen_load_wait"
    TCGEN_STORE_WAIT = "tcgen_store_wait"
    TCGEN_MBARRIER_ARRIVE_ONE = "tcgen_mbarrier_arrive_one"


class FabricOperation(Enum):
    """Source-visible CFT action selected by one exact instruction form."""

    TRY_GET = "try_get"
    TRY_PUT = "try_put"
    TRY_RED = "try_red"
    TRY_PULLRED = "try_pullred"
    SUBMIT = "submit"
    WAIT = "wait"


class FabricEndpointKind(Enum):
    """Logical endpoint topology required at runtime, never inferred from a register."""

    NONE = "none"
    UNICAST = "unicast"
    MULTICAST = "multicast"


class FabricSharedAccess(Enum):
    """Direction of the operation's local CTA-shared data access."""

    NONE = "none"
    READ = "read"
    WRITE = "write"


@dataclass(frozen=True)
class FabricInstructionSpec:
    """Immutable per-form CFT protocol requirements carried into generated IR."""

    operation: FabricOperation
    endpoint: FabricEndpointKind
    shared_access: FabricSharedAccess
    counted: bool = False
    reports_fabric: bool = False
    requires_mbarrier_layout_v1: bool = False


class SurfaceGeometry(Enum):
    """Surface coordinate topology, without texture sampling geometries."""

    ONE_D = "1d"
    TWO_D = "2d"
    THREE_D = "3d"
    ARRAY_ONE_D = "a1d"
    ARRAY_TWO_D = "a2d"


class SurfaceAddressingMode(Enum):
    """Whether the x coordinate selects bytes or formatted samples."""

    BYTE = "b"
    SAMPLE = "p"


class SurfaceBoundaryMode(Enum):
    """Source-selected handling of out-of-bounds surface accesses."""

    TRAP = "trap"
    CLAMP = "clamp"
    ZERO = "zero"


class SurfaceReductionOperation(Enum):
    """The five reduction operations admitted by surface Syntax."""

    ADD = "add"
    MIN = "min"
    MAX = "max"
    AND = "and"
    OR = "or"


class SurfaceQuery(Enum):
    """Statically selected property; the returned value remains a runtime fact."""

    WIDTH = "width"
    HEIGHT = "height"
    DEPTH = "depth"
    CHANNEL_DATA_TYPE = "channel_data_type"
    CHANNEL_ORDER = "channel_order"
    ARRAY_SIZE = "array_size"
    MEMORY_LAYOUT = "memory_layout"


@dataclass(frozen=True)
class SurfaceInstructionSpec:
    """Closed exact-form surface semantics from the canonical ISA input."""

    geometry: SurfaceGeometry | None = None
    addressing: SurfaceAddressingMode | None = None
    boundary: SurfaceBoundaryMode | None = None
    operation: SurfaceReductionOperation | None = None
    query: SurfaceQuery | None = None
    vector_arity: int = 1
    indirect_availability: dict[str, Any] = field(default_factory=dict)
class StackOperation(Enum):
    """Static action performed on the current thread stack."""

    SAVE = "save"
    RESTORE = "restore"
    ALLOCATE = "allocate"


@dataclass(frozen=True)
class StackInstructionSpec:
    """Exact stack form width and default minimum byte alignment."""

    operation: StackOperation
    width: int
    default_alignment: int = 8


class TextureGeometry(Enum):
    """Texture coordinate topology independent of opcode spelling."""

    ONE_D = "1d"
    TWO_D = "2d"
    THREE_D = "3d"
    ARRAY_ONE_D = "a1d"
    ARRAY_TWO_D = "a2d"
    CUBE = "cube"
    ARRAY_CUBE = "acube"
    TWO_D_MULTISAMPLE = "2dms"
    ARRAY_TWO_D_MULTISAMPLE = "a2dms"


class TextureMipmapMode(Enum):
    """Source-visible texture LOD selection after YAML spelling classification."""

    OMITTED = "omitted"
    BASE = "base"
    LEVEL = "level"
    GRADIENT = "gradient"


class TextureComponent(Enum):
    """One gather channel selected by a tld4 form."""

    RED = "r"
    GREEN = "g"
    BLUE = "b"
    ALPHA = "a"


class TextureQuery(Enum):
    """Closed texture or sampler property query domain."""

    WIDTH = "width"
    HEIGHT = "height"
    DEPTH = "depth"
    CHANNEL_DATA_TYPE = "channel_data_type"
    CHANNEL_ORDER = "channel_order"
    NORMALIZED_COORDS = "normalized_coords"
    ARRAY_SIZE = "array_size"
    NUM_MIPMAP_LEVELS = "num_mipmap_levels"
    NUM_SAMPLES = "num_samples"
    FORCE_UNNORMALIZED_COORDS = "force_unnormalized_coords"
    FILTER_MODE = "filter_mode"
    ADDRESS_MODE_0 = "addr_mode_0"
    ADDRESS_MODE_1 = "addr_mode_1"
    ADDRESS_MODE_2 = "addr_mode_2"


class OpaqueResourceKind(Enum):
    """The declared resource identity tested or consumed by texture forms."""

    TEXTURE = "texture"
    SAMPLER = "sampler"
    SURFACE = "surface"


class TextureResourceRole(Enum):
    """Query resource role; mode-dependent sampler properties remain explicit."""

    TEXTURE = "texture"
    SAMPLER = "sampler"
    SAMPLER_BY_MODE = "sampler_by_mode"


@dataclass(frozen=True)
class TextureInstructionSpec:
    """Closed per-form texture semantics independent of source spellings."""

    geometry: TextureGeometry | None = None
    mipmap: TextureMipmapMode = TextureMipmapMode.OMITTED
    component: TextureComponent | None = None
    query: TextureQuery | None = None
    tested_kind: OpaqueResourceKind | None = None
    result_arity: int = 1
    allows_offset: bool = False
    allows_compare: bool = False
    allows_residency: bool = False
    query_level: bool = False
    # Minimum source/target gate for an indirect texture or query resource.
    indirect_availability: dict[str, Any] | None = None


class WgmmaProtocolAction(Enum):
    """Instruction-local action in the independent warpgroup MMA protocol."""

    NONE = "none"
    ISSUE = "issue"
    REGISTER_FENCE = "register_fence"
    COMMIT = "commit"
    WAIT = "wait"


class MatrixFamily(Enum):
    """Instruction family represented by one warp-matrix topology."""

    LDMATRIX = "ldmatrix"
    STMATRIX = "stmatrix"
    MOVMATRIX = "movmatrix"
    MMA = "mma"
    MMA_SPARSE = "mma_sparse"
    WMMA_LOAD = "wmma_load"
    WMMA_STORE = "wmma_store"
    WMMA_MMA = "wmma_mma"
    WGMMA = "wgmma"
    WGMMA_SPARSE = "wgmma_sparse"


class WgmmaSourcePlacement(Enum):
    """Where operand A resides; WGMMA operand B always uses shared memory."""

    NONE = "none"
    SHARED = "shared"
    REGISTER = "register"


class WgmmaSparseMetadataKind(Enum):
    """Shape-specific interpretation of the opaque b32 sparse metadata."""

    NONE = "none"
    TWO_OF_FOUR = "two_of_four"
    ONE_OF_TWO_TF32 = "one_of_two_tf32"


class MatrixElementType(Enum):
    """Logical element type, independent of its register packing."""

    B1 = "b1"
    B8 = "b8"
    B8X16 = "b8x16"
    B16 = "b16"
    B4X16_P64 = "b4x16_p64"
    B6X16_P32 = "b6x16_p32"
    F16 = "f16"
    BF16 = "bf16"
    TF32 = "tf32"
    F32 = "f32"
    F64 = "f64"
    S8 = "s8"
    U8 = "u8"
    S32 = "s32"
    S4 = "s4"
    U4 = "u4"
    E4M3 = "e4m3"
    E5M2 = "e5m2"
    E3M2 = "e3m2"
    E2M3 = "e2m3"
    E2M1 = "e2m1"


class MatrixLayout(Enum):
    """Logical row or column placement of a matrix fragment."""

    NONE = "none"
    ROW = "row"
    COL = "col"


class MatrixKind(Enum):
    """Warp MMA numeric format, including block-scaled formats."""

    CLASSIC = "classic"
    F8F6F4 = "f8f6f4"
    MXF8F6F4 = "mxf8f6f4"
    MXF4 = "mxf4"
    MXF4NVF4 = "mxf4nvf4"


class MatrixBitOperation(Enum):
    """Single-bit MMA operation applied before population count."""

    NONE = "none"
    XOR = "xor"
    AND = "and"


class MatrixSparseOrder(Enum):
    """Static ordering contract for sparse metadata bits."""

    NONE = "none"
    NATIVE = "native"
    ORDERED = "ordered"


class MatrixScaleType(Enum):
    """Logical scale-factor representation for block-scaled MMA."""

    NONE = "none"
    UE8M0 = "ue8m0"
    UE4M3 = "ue4m3"


class MatrixAddressQualifier(Enum):
    """Written matrix address-space suffix, distinct from bound provenance."""

    NONE = "none"
    GLOBAL = "global"
    SHARED = "shared"
    SHARED_CTA = "shared::cta"


class MatrixFragmentRole(Enum):
    """A, B, C, or D position of an owned register fragment."""

    D = "d"
    A = "a"
    B = "b"
    C = "c"


@dataclass(frozen=True)
class MatrixShape:
    """Logical M×N×K shape; raw matrix moves use K=0."""

    m: int
    n: int
    k: int


@dataclass(frozen=True)
class MatrixFragmentShape:
    """Canonical register count and packing for one named operand."""

    operand: str
    role: MatrixFragmentRole
    element_type: MatrixElementType
    register_type: str
    register_count: int


@dataclass(frozen=True)
class MatrixScaleSelectorSpec:
    """Named A/B selector tuple and its canonical per-position immediate limits."""

    operand: str
    role: MatrixFragmentRole
    byte_mask: int
    thread_max: int


@dataclass(frozen=True)
class MatrixSpec:
    """Typed instruction-local matrix topology shared by generators."""

    family: MatrixFamily
    shape: MatrixShape
    a_layout: MatrixLayout
    b_layout: MatrixLayout
    c_layout: MatrixLayout
    d_layout: MatrixLayout
    elements: tuple[tuple[MatrixFragmentRole, MatrixElementType], ...]
    fragments: tuple[MatrixFragmentShape, ...]
    kind: MatrixKind = MatrixKind.CLASSIC
    bit_operation: MatrixBitOperation = MatrixBitOperation.NONE
    scale_type: MatrixScaleType = MatrixScaleType.NONE
    source_packing: MatrixElementType | None = None
    destination_packing: MatrixElementType | None = None
    address_qualifier: MatrixAddressQualifier = MatrixAddressQualifier.NONE
    transpose: bool = False
    matrix_count: int = 0
    scale_vector_size: int = 0
    sparse_order: MatrixSparseOrder = MatrixSparseOrder.NONE
    scale_selectors: tuple[MatrixScaleSelectorSpec, ...] = ()
    source_placement: WgmmaSourcePlacement = WgmmaSourcePlacement.NONE
    sparse_metadata_kind: WgmmaSparseMetadataKind = WgmmaSparseMetadataKind.NONE


class _SemanticToken(Enum):
    """Strict semantic enum with stable YAML-facing formatting."""

    def __str__(self) -> str:
        """Return the spelling retained in diagnostics and generated identifiers."""

        return self.value


class SemanticRule(_SemanticToken):
    """Closed semantic checker identities accepted from instruction YAML."""

    CONTROL_FLOW_BRA = "control_flow.bra"
    CONTROL_FLOW_BRX_IDX = "control_flow.brx_idx"
    DATA_MOVEMENT_APPLYPRIORITY = "data_movement.applypriority"
    DATA_MOVEMENT_CP_ASYNC = "data_movement.cp_async"
    DATA_MOVEMENT_CP_ASYNC_COMMIT_GROUP = "data_movement.cp_async_commit_group"
    DATA_MOVEMENT_CP_ASYNC_MBARRIER_ARRIVE = "data_movement.cp_async_mbarrier_arrive"
    DATA_MOVEMENT_CP_ASYNC_WAIT_ALL = "data_movement.cp_async_wait_all"
    DATA_MOVEMENT_CP_ASYNC_WAIT_GROUP = "data_movement.cp_async_wait_group"
    DATA_MOVEMENT_CREATEPOLICY = "data_movement.createpolicy"
    DATA_MOVEMENT_CVT = "data_movement.cvt"
    DATA_MOVEMENT_DISCARD = "data_movement.discard"
    DATA_MOVEMENT_LD_EXPLICIT = "data_movement.ld_explicit"
    DATA_MOVEMENT_LD_GENERIC = "data_movement.ld_generic"
    DATA_MOVEMENT_LDMATRIX = "data_movement.ldmatrix"
    DATA_MOVEMENT_MOV = "data_movement.mov"
    DATA_MOVEMENT_PREFETCH = "data_movement.prefetch"
    DATA_MOVEMENT_ST_EXPLICIT = "data_movement.st_explicit"
    DATA_MOVEMENT_ST_GENERIC = "data_movement.st_generic"
    DATA_MOVEMENT_ST_BULK = "data_movement.st_bulk"
    DATA_MOVEMENT_TENSORMAP_REPLACE = "data_movement.tensormap_replace"
    DATA_MOVEMENT_TENSORMAP_CP_FENCEPROXY = "data_movement.tensormap_cp_fenceproxy"
    DATA_MOVEMENT_TENSOR_REDUCTION = "data_movement.tensor_reduction"
    FLOATING_POINT_ADD = "floating_point.add"
    FLOATING_POINT_ADD_BFLOAT = "floating_point.add_bfloat"
    FLOATING_POINT_ADD_HALF = "floating_point.add_half"
    FLOATING_POINT_SUB = "floating_point.sub"
    FLOATING_POINT_SUB_BFLOAT = "floating_point.sub_bfloat"
    FLOATING_POINT_SUB_HALF = "floating_point.sub_half"
    INTEGER_ARITH_ADD = "integer_arith.add"
    INTEGER_ARITH_ADD_PACKED = "integer_arith.add_packed"
    INTEGER_ARITH_ADD_SAT = "integer_arith.add_sat"
    INTEGER_ARITH_SUB = "integer_arith.sub"
    INTEGER_ARITH_SUB_SAT = "integer_arith.sub_sat"
    MATRIX_MMA = "matrix.mma"
    MATRIX_WGMMA_SCALE = "matrix.wgmma_scale"
    TENSOR_MEMORY_ALLOC = "tensor_memory.alloc"
    TENSOR_MEMORY_DEALLOC = "tensor_memory.dealloc"
    TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT = "tensor_memory.relinquish_alloc_permit"
    TENSOR_MEMORY_LOAD = "tensor_memory.load"
    TENSOR_MEMORY_STORE = "tensor_memory.store"
    TENSOR_MEMORY_LOAD_REDUCTION = "tensor_memory.load_reduction"
    TENSOR_MEMORY_WAIT = "tensor_memory.wait"
    TENSOR_MEMORY_COMMIT = "tensor_memory.commit"
    TENSOR_MEMORY_FENCE = "tensor_memory.fence"
    TENSOR_MEMORY_COPY = "tensor_memory.copy"
    TENSOR_MEMORY_SHIFT = "tensor_memory.shift"
    TENSOR_MEMORY_MMA = "tensor_memory.mma"
    MIXED_PRECISION_ADD = "mixed_precision.add"
    MIXED_PRECISION_SUB = "mixed_precision.sub"
    PARALLEL_SYNC_AND_COMMUNICATION_ACTIVEMASK = "parallel_sync_and_communication.activemask"
    PARALLEL_SYNC_AND_COMMUNICATION_ATOM = "parallel_sync_and_communication.atom"
    PARALLEL_SYNC_AND_COMMUNICATION_BAR_ARRIVE = "parallel_sync_and_communication.bar_arrive"
    PARALLEL_SYNC_AND_COMMUNICATION_BAR_RED_AND = "parallel_sync_and_communication.bar_red_and"
    PARALLEL_SYNC_AND_COMMUNICATION_BAR_RED_OR = "parallel_sync_and_communication.bar_red_or"
    PARALLEL_SYNC_AND_COMMUNICATION_BAR_RED_POPC = "parallel_sync_and_communication.bar_red_popc"
    PARALLEL_SYNC_AND_COMMUNICATION_BAR_SYNC = "parallel_sync_and_communication.bar_sync"
    PARALLEL_SYNC_AND_COMMUNICATION_FENCE = "parallel_sync_and_communication.fence"
    PARALLEL_SYNC_AND_COMMUNICATION_MEMBAR = "parallel_sync_and_communication.membar"
    PARALLEL_SYNC_AND_COMMUNICATION_RED = "parallel_sync_and_communication.red"
    PARALLEL_SYNC_AND_COMMUNICATION_RED_ASYNC_RELEASE = "parallel_sync_and_communication.red_async_release"
    PARALLEL_SYNC_AND_COMMUNICATION_SHFL = "parallel_sync_and_communication.shfl"
    PARALLEL_SYNC_AND_COMMUNICATION_VOTE = "parallel_sync_and_communication.vote"


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
    TEST_PROPERTY = "test_property"
    BOOLEAN_OP = "boolean_op"
    SHAPE = "shape"
    LAYOUT = "layout"
    PHASE_TYPE = "phase_type"
    MBARRIER_LAYOUT = "mbarrier_layout"
    CTA_GROUP = "cta_group"
    TCGEN_SCALE_VECTOR_SIZE = "tcgen_scale_vector_size"
    TCGEN_COLLECTOR = "tcgen_collector"
    TCGEN_SHAPE = "tcgen_shape"
    TCGEN_NUM = "tcgen_num"
    TCGEN_RED_OP = "tcgen_red_op"
    TCGEN_WAIT = "tcgen_wait"
    VIDEO_SCALE = "video_scale"
    VIDEO_SHIFT_MODE = "video_shift_mode"
    VIDEO_SECONDARY_OP = "video_secondary_op"
    VIDEO_TYPE = "video_type"
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
    MOV_VECTOR_SOURCE = "mov_vector_src"
    CP_ASYNC_SOURCE_CONTROL = "cp_async_source_control"
    CLUSTER_ADDRESS = "cluster_address"
    VECTOR_REGISTER = "vector_reg"
    VECTOR_SPECIAL_REGISTER = "vector_sreg"
    REGISTER_VECTOR = "reg_vector"
    VALUE_VECTOR = "value_vector"
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
    TENSOR_IM2COL_INFO = "tensor_im2col_info"
    TENSOR_OPERAND = "tensor_operand"
    FABRIC_HANDLE = "fabric_handle"
    SURFACE_ACCESS = "surface_access"
    SURFACE_QUERY_RESOURCE = "surface_query_resource"
    STACK_TOKEN = "stack_token"
    LOCAL_ALLOCATION_RESULT = "local_allocation_result"
    VIDEO_OPERAND = "video_operand"
    TEXTURE_ACCESS = "texture_access"
    TEXTURE_QUERY_RESOURCE = "texture_query_resource"
    TEXTURE_RESULT = "texture_result"
    TEXTURE_RESULT_WITH_PREDICATE = "texture_result_with_predicate"
    TENSOR_MEMORY_ADDRESS = "tensor_memory_address"
    TENSOR_MEMORY_ADDRESS_BRACKET = "tensor_memory_address_bracket"
    TCGEN_HALF_SPLIT_OFFSET = "tcgen_half_split_offset"
    MATRIX_FRAGMENT = "matrix_fragment"
    MATRIX_SCALE_SELECTOR = "matrix_scale_selector"
    SHARED_MATRIX_DESCRIPTOR = "shared_matrix_descriptor"
    WGMMA_SCALE_D = "wgmma_scale_d"
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
    WORD_OR_DOUBLEWORD = "word_or_doubleword"


class OperandAddressBasePolicy(_SemanticToken):
    """Allowed base form of a bracketed address operand."""

    ANY = "any"
    REGISTER = "register"


class OperandAddressOffsetDomain(_SemanticToken):
    """Allowed signed source domain for a bracketed address offset."""

    UNRESTRICTED = "unrestricted"
    SIGNED32 = "signed32"


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


class VideoLanes(_SemanticToken):
    """Static packed video lane topology, independent of carrier declarations."""

    SCALAR = "scalar"
    TWO = "two"
    FOUR = "four"


class VideoOperation(_SemanticToken):
    """Instruction-local video obligations used by generated descriptors."""

    ARITHMETIC = "arithmetic"
    SHIFT = "shift"
    COMPARE = "compare"
    MAD = "mad"


class VideoOperandPosition(_SemanticToken):
    """Logical video position defining type use and omitted selector defaults."""

    DESTINATION = "destination"
    A = "a"
    B = "b"
    C = "c"


class VideoSelectorPolicy(_SemanticToken):
    """Closed source-selection or destination-mask domain for one slot."""

    NONE = "none"
    OPTIONAL_SCALAR = "optional_scalar"
    REQUIRED_SCALAR = "required_scalar"
    HALF_SWIZZLE = "half_swizzle"
    BYTE_SWIZZLE = "byte_swizzle"
    HALF_MASK = "half_mask"
    BYTE_MASK = "byte_mask"


@dataclass(frozen=True)
class VideoInstructionSpec:
    """Typed static video topology and instruction-local checker obligations."""

    lanes: VideoLanes
    operation: VideoOperation
    sat_modifier: str | None = None
    po_modifier: str | None = None


class VideoOperandTypeUse(_SemanticToken):
    """Scalar constant coercion without conflating video arithmetic semantics."""

    UNSIGNED = "unsigned"
    MODIFIER_FIELD = "modifier_field"
    BIT_CARRIER = "bit_carrier"


@dataclass(frozen=True)
class VideoOperandSpec:
    """Source-faithful video slot contract, separate from ordinary integers."""

    position: VideoOperandPosition
    selector: VideoSelectorPolicy
    type_use: VideoOperandTypeUse = VideoOperandTypeUse.UNSIGNED
    type_modifier: str | None = None
    allow_immediate: bool = False
    allow_negate: bool = False


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
    address_base_policy: OperandAddressBasePolicy = OperandAddressBasePolicy.ANY
    address_offset_domain: OperandAddressOffsetDomain = OperandAddressOffsetDomain.UNRESTRICTED
    vector_arities: tuple[int, ...] = ()
    vector_arity_expression: OperandVectorArityExpression | None = None
    vector_type_policy: OperandVectorTypePolicy = OperandVectorTypePolicy.AGGREGATE
    vector_allow_sink: bool = False
    vector_allow_named: bool = False
    vector_sink_payload_bits: int = 0
    vector_allowed_register_types: tuple[str, ...] = ()
    vector_require_uniform_register_family: bool = False
    vector_signed_immediate_range: tuple[int, int] | None = None
    allow_destination_sink: bool = False
    allow_predicate_sink: bool = False
    mbarrier_state_token_form: MbarrierStateTokenForm = MbarrierStateTokenForm.REGISTER
    sink_availability: dict[str, Any] = field(default_factory=dict)
    type_tag: str | None = None
    minimum_elements: int | None = None
    maximum_elements: int | None = None
    element_kinds: tuple[OperandKind, ...] = ()
    surface_geometry: SurfaceGeometry | None = None
    texture_geometry: TextureGeometry | None = None
    texture_legacy_v4_coordinates: bool = False
    texture_unbracketed: bool = False
    texture_resource_kind: TextureResourceRole | None = None
    video: VideoOperandSpec | None = None


@dataclass(frozen=True)
class OperandLayoutSpec:
    """One stable operand layout within a modifier-selected variant."""

    name: str
    operands: tuple[OperandSpec, ...]
    kind: OperandLayoutKind = OperandLayoutKind.FLAT
    # Empty means that this layout introduces no target requirement beyond its
    # containing variant's availability.
    availability: dict[str, Any] = field(default_factory=dict)
    # Modifier slots the containing variant admits but this layout rejects.
    # Selection is by operand shape only, so a spelling outside this set is
    # reported by the checker rather than being an ambiguity.
    forbidden_modifiers: tuple[str, ...] = ()


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
    matrix: MatrixSpec | None = None
    condition_code_effect: ConditionCodeEffect = ConditionCodeEffect.NONE
    completion_kind: AsyncCompletionKind = AsyncCompletionKind.NONE
    fabric: FabricInstructionSpec | None = None
    surface: SurfaceInstructionSpec | None = None
    stack: StackInstructionSpec | None = None
    texture: TextureInstructionSpec | None = None
    video: VideoInstructionSpec | None = None
    wgmma_protocol_action: WgmmaProtocolAction = WgmmaProtocolAction.NONE
    rule: SemanticRule | None = None
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
    tcgen_copy_pairs: tuple[tuple[str, str], ...] = ()
    tcgen_copy_formats: tuple[tuple[bool, bool, bool], ...] = ()


@dataclass(frozen=True)
class AtomicAddressQualifierPolicy:
    """Source slots defining a written atomic address qualifier."""

    state_space_modifier: str
    address_operand: str


@dataclass(frozen=True)
class InstructionSpec:
    """All merged YAML definitions and variants for one opcode."""

    opcode: str
    variants: tuple[VariantSpec, ...]
    syntax_forms: tuple[str, ...] = ()
    source_categories: tuple[str, ...] = ()
    codegen_category: str = "uncategorized"
    atomic_address_qualifier: AtomicAddressQualifierPolicy | None = None


# -----------------------------------------------------------------------------
# C++ backend model
# -----------------------------------------------------------------------------
#
# ``DomainBackend`` is consumed by the current generation path for semantic
# value-to-C++ spelling mappings. Its ``cpp_type`` determines a generated
# runtime lookup table's value type when ``runtime_lookup`` is set; otherwise
# it is type metadata and never controls an instruction field layout.


@dataclass(frozen=True)
class DomainBackend:
    """C++ representations for all semantic values in one backend domain."""

    cpp_type: str
    values: dict[str, str]
    default: str | None = None
    runtime_lookup: RuntimeLookupKind | None = None


@dataclass(frozen=True)
class InstructionIdentityBackend:
    """Stable 8-bit category allocations; opcode ordinals are derived."""

    categories: dict[str, int]


@dataclass(frozen=True)
class CodegenUnit:
    """Normalized C++ mappings bound to one PTX ISA schema version."""

    spec_schema: str
    backend_schema: str
    domains: dict[str, DomainBackend]
    instruction_identity: InstructionIdentityBackend | None = None
