#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/base/ptx_ast_types.hpp>
#include <ptx_frontend/base/ptx_special_register.hpp>
#include <ptx_frontend/base/ptx_target.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/common/source_loc.hpp>
#include <ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>
#include <ptx_frontend/semantic/ptx_call_argument_compatibility.hpp>
#include <ptx_frontend/semantic/ptx_constant_value.hpp>

namespace ptx_frontend::resolved_ir {

struct ResolvedRegisterRef;
struct ResolvedRegisterVector;
struct ResolvedVectorSource;
struct ResolvedImmediate;
struct ResolvedTensorOperand;
struct ResolvedVideoOperand;

/** Logical video operand position defining types and omitted selector defaults. */
enum class VideoOperandPosition : uint8_t { Destination, A, B, C };
/** Instruction-local obligations independent of opcode spelling. */
enum class VideoOperation : uint8_t { Arithmetic, Shift, Compare, Mad };
/** Permitted selector family and source-written presence for one operand. */
enum class VideoSelectorPolicy : uint8_t {
  None,
  OptionalScalar,
  RequiredScalar,
  HalfSwizzle,
  ByteSwizzle,
  HalfMask,
  ByteMask
};
/** Scalar constant coercion policy independent of video arithmetic signedness. */
enum class VideoOperandTypeUse : uint8_t {
  Unsigned,
  ModifierField,
  BitCarrier
};
/** Signedness of video arithmetic independently of the 32-bit carrier type. */
enum class VideoType : uint8_t { S32, U32 };
/** Number of packed operations represented by a video instruction form. */
enum class VideoLanes : uint8_t { Scalar = 1, Two = 2, Four = 4 };
/** Optional second arithmetic operation applied to the primary video result. */
enum class VideoSecondaryOp : uint8_t { None, Add, Min, Max };
/** Required scalar video shift-count interpretation. */
enum class VideoShiftMode : uint8_t { Clamp, Wrap };
/** Optional vmad product scaling, preserving an omitted scale. */
enum class VideoScale : uint8_t { None, Shr7, Shr15 };

enum class ResolvedRegisterClass : uint8_t;

/**
 * Implicit CC.CF access for an executed instruction. Predication gates both
 * explicit results and this effect. Incoming CC.CF is not preserved by calls.
 * Carry and borrow interpret the same architectural bit, not separate state.
 */
enum class ConditionCodeEffect : uint8_t {
  None,
  CarryOut,
  CarryIn,
  CarryInOut,
  BorrowOut,
  BorrowIn,
  BorrowInOut,
};

/** State-space identity used by resolved modifiers and effective addresses. */
enum class MemoryStateSpace : uint8_t {
  Invalid,
  Generic,
  Global,
  Shared,
  Local,
  Parameter,
  Constant
};
/** Written address qualifier of an atom or red operation. */
enum class AtomicAddressQualifier : uint8_t {
  Generic,
  Global,
  Shared,
  SharedCta,
  SharedCluster,
};
/** Semantic value of a PTX vector-arity modifier such as ``.v2``. */
enum class VectorArity : uint8_t { Invalid, V2, V4, V8 };
/** Closed written CTA-group size shared by tensor-copy and Tensor Memory syntax. */
enum class TcgenCtaGroup : uint8_t { One, Two };
/** Written block-scale selector. Absent retains omission provenance; later
 *  operational rules may derive an effective layout without rewriting source. */
enum class TcgenScaleVectorSize : uint8_t {
  Absent,
  Vec1X,
  Vec2X,
  Vec4X,
  Block16,
  Block32
};
/** Collector-buffer identity shared by activation and weight-stationary MMA. */
enum class TcgenCollectorBuffer : uint8_t { Unspecified, A, B0, B1, B2, B3 };
/** Written collector action, independent of its buffer. */
enum class TcgenCollectorOp : uint8_t {
  Unspecified,
  Fill,
  Use,
  LastUse,
  Discard
};
/** One source qualifier; the all-Unspecified pair preserves omission. */
struct TcgenCollectorControl {
  /** Written buffer, or Unspecified only when the qualifier is absent. */
  TcgenCollectorBuffer buffer = TcgenCollectorBuffer::Unspecified;
  /** Written action, or Unspecified only when the qualifier is absent. */
  TcgenCollectorOp operation = TcgenCollectorOp::Unspecified;

  /** Source presence derived from the canonical absent pair. */
  [[nodiscard]] constexpr bool is_present() const noexcept {
    return buffer != TcgenCollectorBuffer::Unspecified &&
           operation != TcgenCollectorOp::Unspecified;
  }
  /** Compare both typed components, including their omission sentinels. */
  constexpr bool operator==(const TcgenCollectorControl&) const = default;
};
/** Closed register-transfer shape, distinct from an MMA matrix shape. */
enum class TcgenDataMovementShape : uint8_t {
  S32x32b,
  S16x64b,
  S16x128b,
  S16x256b,
  S16x32bx2,
  S128x256b,
  S4x256b,
  S128x128b,
  S64x128b,
  S32x128b
};
/** Closed multicast topology written by a Tensor Memory copy. */
enum class TcgenCopyMulticast : uint8_t {
  None,
  WarpX2_02_13,
  WarpX2_01_23,
  WarpX4
};
/** Copy decompression selected by destination and source format tokens. */
enum class TcgenCopyFormat : uint8_t { None, B6x16P32, B4x16P64 };
/** Canonical legal copy shape and multicast pair. */
struct TcgenCopyShapePair {
  /** Written movement shape. */
  TcgenDataMovementShape shape;
  /** Written multicast topology or unicast omission. */
  TcgenCopyMulticast multicast;
};
/** Number of repeated transfer shapes selected by a written suffix. */
enum class TcgenRepeat : uint8_t { X1, X2, X4, X8, X16, X32, X64, X128 };
/** Reduction applied to each lane's loaded columns. */
enum class TcgenReductionOp : uint8_t { Min, Max };
/** Same-thread register-transfer completion class. */
enum class TcgenWaitClass : uint8_t { Load, Store };
/** Written commit address qualifier; access still uses the generic proxy. */
enum class TcgenCommitAddressSpelling : uint8_t { Generic, SharedCluster };
/** Ordering direction of an operand-free specialized TCGEN fence. */
enum class TcgenFenceDirection : uint8_t { BeforeThreadSync, AfterThreadSync };
/** Allocation-management action independent of written opcode modifiers. */
enum class TcgenAllocationAction : uint8_t {
  Alloc,
  Dealloc,
  RelinquishAllocPermit
};
/** Local permission effect; execution ordering remains a runtime obligation. */
enum class TcgenAllocationPermitEffect : uint8_t {
  RequiresPermit,
  ReleasesAllocation,
  RelinquishesPermit
};
/** Number of participating warps issuing an allocation-management operation. */
enum class TcgenIssueGranularity : uint8_t { OneWarp, WarpPair };
/** Map an instruction's CTA group to its participating-warp count. */
constexpr TcgenIssueGranularity tcgen_issue_granularity(
    TcgenCtaGroup group) noexcept {
  return group == TcgenCtaGroup::Two ? TcgenIssueGranularity::WarpPair
                                     : TcgenIssueGranularity::OneWarp;
}
/** Tensor-copy spelling of the closed CTA-group value; no TCGEN body rule follows. */
using TensorCtaGroup = TcgenCtaGroup;
/** Signal destination selected by a tensor-copy CTA group and multicast mode. */
enum class TensorCtaSignalRouting : uint8_t {
  Destination,
  DestinationOrPeer,
  MulticastDestinations,
  MulticastParityPeers
};
/** Owned written group plus effective mbarrier-routing interpretation. */
struct TensorCtaGroupRole {
  /** Absent for omitted spelling; present value owns its source locations. */
  std::optional<WithLocs<TensorCtaGroup>> spelled;
  /** Omitted spelling defaults to one only on applicable tensor loads. */
  TensorCtaGroup effective = TensorCtaGroup::One;
  /** Routing obligation; actual CTA ownership remains unknown at compile time. */
  TensorCtaSignalRouting routing = TensorCtaSignalRouting::Destination;
};
/** Dimension count encoded by a tiled tensor instruction. */
enum class TensorRank : uint8_t { One = 1, Two, Three, Four, Five };
/** Tensor transfer interpretation represented by this operand. */
/** Instruction-selected interpretation of a tensor coordinate vector. */
enum class TensorAccessMode : uint8_t {
  Tiled,
  Im2colNoOffs,
  Im2col,
  Im2colW,
  Im2colW128,
  TileGather4,
  TileScatter4
};
/** Source-order meaning of a gather/scatter coordinate position. */
enum class TensorGatherScatterCoordinateRole : uint8_t {
  Column,
  Row0,
  Row1,
  Row2,
  Row3
};
/** A scalar mask whose bits select destination CTA ranks for a cluster copy. */
enum class TensorCtaMaskRole : uint8_t { MulticastCluster };
/** Return the scalar lane count, or zero for the invalid sentinel. */
constexpr uint8_t vector_arity_count(VectorArity arity) noexcept {
  switch (arity) {
    case VectorArity::V2:
      return 2;
    case VectorArity::V4:
      return 4;
    case VectorArity::V8:
      return 8;
    case VectorArity::Invalid:
      return 0;
  }
  return 0;
}
/** Warp-level and warpgroup matrix family, independent of opcode spelling. */
enum class MatrixFamily : uint8_t {
  LDMATRIX,
  STMATRIX,
  MOVMATRIX,
  MMA,
  MMA_SPARSE,
  WMMA_LOAD,
  WMMA_STORE,
  WMMA_MMA,
  WGMMA,
  WGMMA_SPARSE
};
/** Logical matrix element, independent of register packing. */
enum class MatrixElementType : uint8_t {
  B1,
  B8,
  B8X16,
  B16,
  B4X16_P64,
  B6X16_P32,
  F16,
  BF16,
  TF32,
  F32,
  F64,
  S8,
  U8,
  S32,
  S4,
  U4,
  E4M3,
  E5M2,
  E3M2,
  E2M3,
  E2M1
};
/** Row or column placement of a logical matrix operand. */
enum class MatrixLayout : uint8_t { NONE, ROW, COL };
/** MMA numeric format, including block-scaled formats. */
enum class MatrixKind : uint8_t { CLASSIC, F8F6F4, MXF8F6F4, MXF4, MXF4NVF4 };
/** Single-bit multiply replacement before population count. */
enum class MatrixBitOperation : uint8_t { NONE, XOR, AND };
/** Sparse metadata ordering requirement. */
enum class MatrixSparseOrder : uint8_t { NONE, NATIVE, ORDERED };
/** Logical scale-factor type selected by block-scaled MMA. */
enum class MatrixScaleType : uint8_t { NONE, UE8M0, UE4M3 };
/** Written matrix address qualifier, independent of address provenance. */
enum class MatrixAddressQualifier : uint8_t {
  NONE,
  GLOBAL,
  SHARED,
  SHARED_CTA
};
/** Logical register-fragment role in a matrix instruction. */
enum class MatrixFragmentRole : uint8_t { D, A, B, C };
/** Logical M×N×K shape; raw movement instructions use K=0. */
struct MatrixShape {
  uint16_t m = 0;
  uint16_t n = 0;
  uint16_t k = 0;
  /** Compare logical dimensions. */
  bool operator==(const MatrixShape&) const = default;
};
/** One register fragment's semantic role, packing and lane count. */
struct MatrixFragmentShape {
  /** Stable generated operand name in static storage. */
  std::string_view operand_field_id{};
  MatrixFragmentRole role = MatrixFragmentRole::D;
  MatrixElementType element_type = MatrixElementType::B16;
  base::ScalarType register_type = base::ScalarType::Invalid;
  /** Number of register lanes in the corresponding owned operand. */
  uint8_t register_count = 0;
  /** Compare the exact register-fragment contract. */
  bool operator==(const MatrixFragmentShape&) const = default;
};
/** Generated immediate domains for one A/B block-scale selector tuple. */
struct MatrixScaleSelectorDescriptor {
  /** Static operand name locating the owned two-slot tuple. */
  std::string_view operand_field_id{};
  MatrixFragmentRole role = MatrixFragmentRole::A;
  /** Bit i permits immediate byte ID i. */
  uint8_t byte_mask = 0;
  /** Largest permitted immediate thread ID, inclusive. */
  uint8_t thread_max = 0;
  /** Compare the typed selector contract. */
  bool operator==(const MatrixScaleSelectorDescriptor&) const = default;
};
/** Source location of a warpgroup operand within shared/register forms. */
enum class WgmmaSourcePlacement : uint8_t { NONE, SHARED, REGISTER };
/** Sparse metadata carrier for a warpgroup matrix operation. */
enum class WgmmaSparseMetadataKind : uint8_t {
  NONE,
  TWO_OF_FOUR,
  ONE_OF_TWO_TF32
};
/** Marker for the fixed 128-thread warpgroup execution contract. */
struct WarpGroup128 {
  /** PTX warpgroup size; participation uniformity is a runtime obligation. */
  inline static constexpr uint16_t thread_count = 128;
  /** Compare the marker without inferring runtime scheduling. */
  bool operator==(const WarpGroup128&) const = default;
};
/** Immutable generated matrix topology, copied into resolved metadata. */
struct MatrixInstructionDescriptor {
  MatrixFamily family = MatrixFamily::MMA;
  MatrixShape shape{};
  MatrixLayout a_layout = MatrixLayout::NONE;
  MatrixLayout b_layout = MatrixLayout::NONE;
  MatrixLayout c_layout = MatrixLayout::NONE;
  MatrixLayout d_layout = MatrixLayout::NONE;
  MatrixKind kind = MatrixKind::CLASSIC;
  MatrixBitOperation bit_operation = MatrixBitOperation::NONE;
  MatrixSparseOrder sparse_order = MatrixSparseOrder::NONE;
  MatrixScaleType scale_type = MatrixScaleType::NONE;
  MatrixAddressQualifier address_qualifier = MatrixAddressQualifier::NONE;
  std::optional<MatrixElementType> source_packing;
  std::optional<MatrixElementType> destination_packing;
  bool transpose = false;
  uint8_t matrix_count = 0;
  uint8_t scale_vector_size = 0;
  /** Only the first fragment_count entries are live. */
  std::array<MatrixFragmentShape, 4> fragments{};
  uint8_t fragment_count = 0;
  /** Only the first scale_selector_count entries are live. */
  std::array<MatrixScaleSelectorDescriptor, 2> scale_selectors{};
  uint8_t scale_selector_count = 0;
  WgmmaSourcePlacement source_placement = WgmmaSourcePlacement::NONE;
  WgmmaSparseMetadataKind sparse_metadata_kind = WgmmaSparseMetadataKind::NONE;
  std::optional<WarpGroup128> warpgroup;
  /** Compare the complete instruction-local topology and controls. */
  bool operator==(const MatrixInstructionDescriptor&) const = default;
};
/** Function provenance retained for resolved memory addresses. */
enum class EnclosingFunctionKind : uint8_t { Unknown, Entry, Device };
/** Stack action independent of opcode spellings. */
enum class StackOperation : uint8_t { Save, Restore, Allocate };
/** Immutable static stack form semantics; alignment is a minimum in bytes. */
struct StackInstructionDescriptor {
  /** Source-selected save, restore or allocation action. */
  StackOperation operation;
  /** Unsigned operand-use width in bits, restricted to 32 or 64. */
  uint8_t width;
  /** Default minimum alignment in bytes when no explicit constant is present. */
  uint32_t default_alignment = 8;
  /** Compare exact static contracts. */
  bool operator==(const StackInstructionDescriptor&) const = default;
};
/** Parameter role independent of binding-layer enum types. */
enum class ParameterDirection : uint8_t { None, Input, Return, CallArgument };
/** PTX 9.3 subqualifier retained for a .param memory access. */
enum class ParameterAddressQualifier : uint8_t { Default, Entry, Function };

/** Source-visible CFT action of one exact resolved instruction form. */
enum class FabricOperation : uint8_t {
  TryGet,
  TryPut,
  TryRed,
  TryPullred,
  Submit,
  Wait,
};

/** Logical endpoint topology required at runtime by one fabric operation. */
enum class FabricEndpointKind : uint8_t { None, Unicast, Multicast };

/** Direction of a fabric operation's local CTA shared-memory data access. */
enum class FabricSharedAccess : uint8_t { None, Read, Write };

/**
 * Immutable instruction-local CFT obligations available to IR consumers.
 * Handle offsets are register values: callers must ensure 16-byte data
 * alignment and, for counted forms, an 8-byte counter at a 256-byte-aligned
 * nonoverlapping offset. These numeric and endpoint conditions are runtime
 * obligations; the descriptor does not claim they were statically proven.
 */
struct FabricInstructionDescriptor {
  FabricOperation operation;
  FabricEndpointKind endpoint;
  FabricSharedAccess shared_access;
  /** Full-operation barrier completion; read wait is only a partial fence. */
  base::AsyncCompletionKind completion;
  /** Whether destination effects also increment an external byte counter. */
  bool counted;
  /** Whether the associated barrier reports errors through report::fabric. */
  bool reports_fabric;
  /** Live barrier initialization requires layout v1 for the four try forms. */
  std::optional<base::MbarrierLayout> required_mbarrier_layout;
};

/** Coordinate topology supported by surface access instructions. */
enum class SurfaceGeometry : uint8_t {
  OneD,
  TwoD,
  ThreeD,
  ArrayOneD,
  ArrayTwoD
};
/** The unit selected by the x coordinate: raw bytes or formatted samples. */
enum class SurfaceAddressingMode : uint8_t { Byte, Sample };
/** Source-selected behavior when a surface coordinate is out of bounds. */
enum class SurfaceBoundaryMode : uint8_t { Trap, Clamp, Zero };
/** The closed surface reduction operation domain. */
enum class SurfaceReductionOperation : uint8_t { Add, Min, Max, And, Or };
/** Query identity; surface configuration and the returned value are runtime facts. */
enum class SurfaceQuery : uint8_t {
  Width,
  Height,
  Depth,
  ChannelDataType,
  ChannelOrder,
  ArraySize,
  MemoryLayout
};
/** Interpretation of one preserved surface coordinate lane. */
enum class SurfaceLaneRole : uint8_t { Spatial, ArrayLayer, Ignored };
/** Return the exact source tuple size for a surface geometry. */
inline constexpr size_t surface_coordinate_arity(
    SurfaceGeometry geometry) noexcept {
  switch (geometry) {
    case SurfaceGeometry::OneD:
      return 1;
    case SurfaceGeometry::TwoD:
    case SurfaceGeometry::ArrayOneD:
      return 2;
    case SurfaceGeometry::ThreeD:
    case SurfaceGeometry::ArrayTwoD:
      return 4;
  }
  return 0;
}
/** Classify each source lane without discarding ignored padding. */
inline constexpr SurfaceLaneRole surface_lane_role(SurfaceGeometry geometry,
                                                   size_t index) noexcept {
  if ((geometry == SurfaceGeometry::ArrayOneD ||
       geometry == SurfaceGeometry::ArrayTwoD) &&
      index == 0)
    return SurfaceLaneRole::ArrayLayer;
  if ((geometry == SurfaceGeometry::ThreeD ||
       geometry == SurfaceGeometry::ArrayTwoD) &&
      index == 3)
    return SurfaceLaneRole::Ignored;
  return SurfaceLaneRole::Spatial;
}
namespace checker {
struct AvailabilityDescriptor;
}
/** Immutable exact-form surface semantics for AST-free consumers. */
struct SurfaceInstructionDescriptor {
  /** Absent only for a surface property query. */
  std::optional<SurfaceGeometry> geometry;
  /** Absent for queries; this selects coordinate units, not access direction. */
  std::optional<SurfaceAddressingMode> addressing;
  /** Absent for queries; no runtime boundary proof is implied. */
  std::optional<SurfaceBoundaryMode> boundary;
  /** Present only on surface reduction forms. */
  std::optional<SurfaceReductionOperation> operation;
  /** Present only on surface query forms. */
  std::optional<SurfaceQuery> query;
  /** Number of source or destination data registers. */
  uint8_t vector_arity = 1;
  /** Static-lifetime canonical feature gate for an indirect resource carrier. */
  const checker::AvailabilityDescriptor* indirect_availability = nullptr;
};
/** Mutable type selection borrowed from a concrete surface form. */
struct SurfaceSelectedTypes {
  /** The access/reduction data type, or query result type. */
  std::optional<base::ScalarType> data_type;
};

/** Source-selected texturing mode; absence in a module defaults to Unified. */
enum class TextureMode : uint8_t { Unified, Independent };
/** Static geometric interpretation of texture coordinate lanes. */
enum class TextureGeometry : uint8_t {
  OneD,
  TwoD,
  ThreeD,
  ArrayOneD,
  ArrayTwoD,
  Cube,
  ArrayCube,
  TwoDMultisample,
  ArrayTwoDMultisample
};
/** Written mipmap control, retaining omission independently of explicit base. */
enum class TextureMipmapMode : uint8_t { Omitted, Base, Level, Gradient };
/** One statically meaningful role assigned to a texture coordinate lane. */
enum class TextureLaneRole : uint8_t { Spatial, ArrayLayer, Sample, Ignored };
/** Classify a written coordinate lane under PTX texture geometry rules. */
inline constexpr TextureLaneRole texture_lane_role(TextureGeometry geometry,
                                                   size_t index,
                                                   size_t lane_count) noexcept {
  if (lane_count == 4 &&
      (geometry == TextureGeometry::OneD || geometry == TextureGeometry::TwoD ||
       geometry == TextureGeometry::ArrayOneD) &&
      index >= (geometry == TextureGeometry::OneD ? 1u : 2u))
    return TextureLaneRole::Ignored;
  if ((geometry == TextureGeometry::ArrayOneD ||
       geometry == TextureGeometry::ArrayTwoD ||
       geometry == TextureGeometry::ArrayCube ||
       geometry == TextureGeometry::ArrayTwoDMultisample) &&
      index == 0)
    return TextureLaneRole::ArrayLayer;
  if ((geometry == TextureGeometry::TwoDMultisample && index == 0) ||
      (geometry == TextureGeometry::ArrayTwoDMultisample && index == 1))
    return TextureLaneRole::Sample;
  if (index == 3 && (geometry == TextureGeometry::ThreeD ||
                     geometry == TextureGeometry::ArrayTwoD ||
                     geometry == TextureGeometry::Cube ||
                     geometry == TextureGeometry::TwoDMultisample))
    return TextureLaneRole::Ignored;
  return TextureLaneRole::Spatial;
}
/** Component selected by a four-texel footprint read. */
enum class TextureComponent : uint8_t { Red, Green, Blue, Alpha };
/** Static query identity; the returned value remains a runtime resource fact. */
enum class TextureQuery : uint8_t {
  Width,
  Height,
  Depth,
  ChannelDataType,
  ChannelOrder,
  NormalizedCoords,
  ArraySize,
  NumMipmapLevels,
  NumSamples,
  ForceUnnormalizedCoords,
  FilterMode,
  AddressMode0,
  AddressMode1,
  AddressMode2
};
namespace checker {
struct AvailabilityDescriptor;
}
/** One generated texture-family form's immutable static contract. */
struct TextureInstructionDescriptor {
  /** Geometry is absent for txq and istypep. */
  std::optional<TextureGeometry> geometry;
  /** Omitted versus explicit base/level/gradient is source-visible. */
  TextureMipmapMode mipmap = TextureMipmapMode::Omitted;
  /** Present only on tld4 forms. */
  std::optional<TextureComponent> component;
  /** Present only on txq forms. */
  std::optional<TextureQuery> query;
  /** Queried resource kind for istypep, if applicable. */
  std::optional<base::OpaqueResourceKind> tested_kind;
  /** Expected result register count; 1 for query and predicate type tests. */
  uint8_t result_arity = 1;
  /** Optional controls admitted by this form; written presence lives in operands. */
  bool allows_residency = false;
  bool allows_offset = false;
  bool allows_compare = false;
  /** Whether this exact query form has a separate integer mip level. */
  bool query_level = false;
  /** Static-lifetime canonical availability for an indirect resource carrier. */
  const checker::AvailabilityDescriptor* indirect_availability = nullptr;
};

/** Mutable selected type modifiers borrowed from one concrete texture form. */
struct TextureSelectedTypes {
  /** Data result element type when a form writes a texture vector. */
  std::optional<base::ScalarType> result_type;
  /** Spatial coordinate type when a form samples a texture. */
  std::optional<base::ScalarType> coordinate_type;
};

namespace checker {
using base::AsyncProxyKind;
using base::BooleanOperator;
using base::CacheOperator;
using base::ComparisonOperator;
using base::EvictionPriority;
using base::MbarrierLayout;
using base::MbarrierPhaseType;
using base::MemoryConsistency;
using base::MemoryScope;
using base::PrefetchSize;
using base::ProxyKindPair;
using base::RoundingMode;
using base::ScalarType;
using base::TestProperty;

namespace detail {
template <typename Function>
concept OverloadFunctionObject =
    std::is_class_v<std::remove_cvref_t<Function>> &&
    requires { &std::remove_cvref_t<Function>::operator(); };
template <OverloadFunctionObject... Functions>
struct Overloaded : Functions... {
  using Functions::operator()...;
};
template <OverloadFunctionObject... Functions>
Overloaded(Functions...) -> Overloaded<Functions...>;
}  // namespace detail

enum class OperandShape : uint32_t {
  Register = 1 << 0,
  Predicate = 1 << 1,
  Immediate = 1 << 2,
  Address = 1 << 3,
  Symbol = 1 << 4,
  Vector = 1 << 5,
  BranchTarget = 1 << 6,
  SpecialRegister = 1 << 7,
  DirectCallTarget = 1 << 8,
  CallReturnParameter = 1 << 9,
  CallArguments = 1 << 10,
  IndirectCallee = 1 << 11,
  BranchTargetSet = 1 << 12,
  ShflDestination = 1 << 13,
  PredicatePair = 1 << 14,
  TensorOperand = 1 << 15,
  FabricHandle = 1 << 16,
  TextureAccess = 1 << 17,
  TextureQueryResource = 1 << 18,
  TextureResult = 1 << 19,
  SurfaceAccess = 1 << 20,
  SurfaceQueryResource = 1 << 21,
  StackToken = uint32_t{1} << 24,
  LocalAllocationResult = uint32_t{1} << 25,
  VideoOperand = uint32_t{1} << 28
};
constexpr OperandShape operator|(OperandShape lhs, OperandShape rhs) {
  using Underlying = std::underlying_type_t<OperandShape>;
  return static_cast<OperandShape>(static_cast<Underlying>(lhs) |
                                   static_cast<Underlying>(rhs));
}
enum class OperandRole : uint8_t {
  Destination,
  Source,
  Address,
  Predicate,
  BranchTarget,
  Barrier,
  ThreadCount
};
enum class OperandAccess : uint8_t { Read, Write, ReadWrite, Control };
enum class OperandTypeExpressionKind : uint8_t {
  None,
  FixedScalar,
  ModifierField
};
/** Integer conversion selected by a semantic operand use after source decode. */
enum class ImmediateConversionPolicy : uint8_t { Narrow, RequireTargetRange };
/** A PTX ISA version represented without syntax-AST ownership. */
struct PtxVersion {
  uint16_t major = 0;
  uint16_t minor = 0;
  constexpr auto operator<=>(const PtxVersion&) const = default;
};
/** Fixed DNF capacity shared by generated availability descriptors. */
inline constexpr size_t kMaxAvailabilityClauses = 6;
/** Maximum capabilities retained by one generated availability clause. */
inline constexpr size_t kMaxAvailabilityCapabilities = 4;
/** One AND-clause in a bounded generated target-availability expression. */
struct AvailabilityClause {
  PtxVersion minimum_ptx_version{};
  uint32_t minimum_sm_version = 0;
  bool has_exact_target = false;
  base::TargetArchitecture exact_target_architecture{};
  base::TargetFlavor exact_target_flavor = base::TargetFlavor::Generic;
  std::string_view required_family{};
  /** Borrowed capability names; only the first ``capability_count`` are live. */
  std::array<std::string_view, kMaxAvailabilityCapabilities> capabilities{};
  uint8_t capability_count = 0;
};
/** Target requirements attached to a variant, layout, modifier, or value. */
struct AvailabilityDescriptor {
  PtxVersion minimum_ptx_version{};
  uint32_t minimum_sm_version = 0;
  std::string_view required_family{};
  std::array<AvailabilityClause, kMaxAvailabilityClauses> any_of{};
  uint8_t any_of_count = 0;
};
struct TypeExpressionDescriptor {
  OperandTypeExpressionKind kind = OperandTypeExpressionKind::None;
  ScalarType fixed_scalar_type = ScalarType::Invalid;
  std::string_view modifier_field_id{};
};
struct AddressStateSpaceDescriptor {
  MemoryStateSpace state_space = MemoryStateSpace::Invalid;
  AvailabilityDescriptor availability;
};
struct ParameterAddressConstraint {
  ParameterDirection direction = ParameterDirection::None;
  AvailabilityDescriptor function_availability;
};
/** Address-space policy selected by an immutable generated operand binding. */
enum class AddressSymbolResolutionPolicy : uint8_t {
  /** Keep parameter address space equal to its declaration state space. */
  PreserveDeclarationSpace,
  /** Model `mov` taking a device formal parameter address as a local address. */
  MaterializeDeviceParameter,
};
/** Fields needed to derive one generated natural-address-alignment rule. */
struct AddressAlignmentConstraint {
  std::span<const std::string_view> address_field_ids;
  std::string_view type_field_id;
  std::string_view vector_field_id;
  std::string_view immediate_operand_field_id;
  uint64_t alignment = 0;
};
enum class VectorTypePolicy : uint8_t { Aggregate, Element };
/** Inclusive domain for a known integer source in each value-vector lane. */
struct SignedVectorImmediateRange {
  /** Inclusive lower bound on the evaluated signed source value. */
  int64_t minimum;
  /** Inclusive upper bound on the evaluated signed source value. */
  int64_t maximum;
};
/** Required base shape for one bracketed address operand. */
enum class AddressBasePolicy : uint8_t { Any, Register };
/** Owned address base shape projected into checker views. */
enum class AddressBaseKind : uint8_t { Unknown, Register, Immediate, Symbol };
/** Permitted signed source range for an address's optional immediate offset. */
enum class AddressOffsetDomain : uint8_t { Unrestricted, Signed32 };
enum class MbarrierStateTokenForm : uint8_t { Register, RegisterOrSink, Sink };
inline constexpr size_t kMaxRegisterVectorPayloadBits = 256;
/** Largest explicit register fragment; ordinary operand limits remain narrower. */
inline constexpr size_t kMaxOperandElements = 128;
/** Semantic constraints for one generated operand position. */
/** Canonical video slot obligations retained by generated checker metadata. */
struct VideoOperandDescriptor {
  VideoLanes lanes = VideoLanes::Scalar;
  VideoOperation operation = VideoOperation::Arithmetic;
  VideoOperandPosition position = VideoOperandPosition::Destination;
  VideoSelectorPolicy selector = VideoSelectorPolicy::None;
  VideoOperandTypeUse type_use = VideoOperandTypeUse::Unsigned;
  /** Field identity used only for ModifierField interpretation; empty for
   * Unsigned and BitCarrier interpretations. */
  std::string_view type_field_id;
  /** Generated Boolean flag identities, empty when the form has no control. */
  std::string_view sat_field_id;
  std::string_view po_field_id;
  bool allow_immediate = false;
  bool allow_negate = false;
};
struct OperandDescriptor {
  std::string_view target_field_id;
  TypeExpressionDescriptor type_expression;
  base::ScalarTypeSizePolicy register_width_policy =
      base::ScalarTypeSizePolicy::SameWidth;
  OperandRole role;
  OperandAccess access;
  OperandShape allowed_shapes;
  std::span<const uint8_t> allowed_vector_arities;
  std::string_view vector_arity_modifier_field_id{};
  VectorTypePolicy vector_type_policy = VectorTypePolicy::Aggregate;
  bool allow_vector_sink = false;
  /** Admit an owned named ordinary vector, never a generic source vector. */
  bool allow_named_vector = false;
  size_t vector_sink_payload_bits = 0;
  /** Optional declared lane-type domain, independent of instruction suffix. */
  std::span<const base::ScalarType> allowed_register_types;
  /** Enforce one integer-or-float family per vector; bit lanes are neutral. */
  bool require_uniform_register_family = false;
  /** Read-only lanes may be registers or typed immediate values. */
  bool source_value_vector = false;
  /** Absent leaves known immediate values unconstrained by a source range. */
  std::optional<SignedVectorImmediateRange> vector_signed_immediate_range;
  bool allow_destination_sink = false;
  bool allow_predicate_sink = false;
  MbarrierStateTokenForm mbarrier_state_token_form =
      MbarrierStateTokenForm::Register;
  AvailabilityDescriptor sink_availability;
  bool allow_function_symbol = false;
  /** Preserve a formal parameter's declared state space while resolving an address. */
  bool preserve_parameter_address_space = false;
  std::string_view type_tag{};
  uint8_t minimum_elements = 0;
  uint8_t maximum_elements = 0;
  OperandShape allowed_element_shapes{};
  /** Empty means no static state-space restriction. */
  std::span<const AddressStateSpaceDescriptor> allowed_address_state_spaces;
  std::string_view state_space_modifier_field_id{};
  AddressBasePolicy address_base_policy = AddressBasePolicy::Any;
  AddressOffsetDomain address_offset_domain = AddressOffsetDomain::Unrestricted;
  ParameterAddressConstraint parameter_constraint;
  /** Required owned tensor mode; absent for every non-tensor operand. */
  std::optional<TensorAccessMode> expected_tensor_mode;
  /** Fixed instruction tensor dimension, independent of coordinate count. */
  std::optional<TensorRank> expected_tensor_rank;
  /** Static geometry for a composite surface access operand. */
  std::optional<SurfaceGeometry> surface_geometry;
  /** Static geometry for a composite texture access operand. */
  std::optional<TextureGeometry> texture_geometry;
  /** Admit the tex compatibility spelling with four coordinate lanes. */
  bool texture_legacy_v4_coordinates = false;
  /** Merge adjacent source resource and coordinate operands for tex compatibility. */
  bool texture_unbracketed = false;
  /** Expected bound opaque declaration kind for a bracketed query resource. */
  std::optional<base::OpaqueResourceKind> texture_resource_kind;
  /** Sampler query binds texture in unified mode and sampler in independent mode. */
  bool texture_query_sampler_by_mode = false;
  /** Require the result's residency predicate in this operand layout. */
  bool texture_residency_required = false;
  /** Present only for the mask coupled to tensor cluster multicast. */
  std::optional<TensorCtaMaskRole> tensor_cta_mask_role;
  /** Independent conversion contract; type provenance does not select it. */
  ImmediateConversionPolicy immediate_conversion_policy =
      ImmediateConversionPolicy::Narrow;
  /** Present only for compound typed video operands. */
  std::optional<VideoOperandDescriptor> video;
};
struct FieldView {
  std::string_view field_id;
  std::optional<bool> bool_value;
  std::optional<CacheOperator> cache_operator;
  std::optional<EvictionPriority> eviction_priority;
  std::optional<PrefetchSize> prefetch_size;
  std::optional<ScalarType> scalar_type;
  std::optional<ComparisonOperator> comparison_operator;
  std::optional<TestProperty> test_property;
  std::optional<BooleanOperator> boolean_operator;
  std::optional<VectorArity> vector_arity;
  std::optional<MemoryStateSpace> memory_state_space;
  std::optional<MemoryConsistency> memory_consistency;
  std::optional<MemoryScope> memory_scope;
  std::optional<MbarrierPhaseType> mbarrier_phase_type;
  std::optional<MbarrierLayout> mbarrier_layout;
  std::optional<TcgenCtaGroup> tcgen_cta_group;
  std::optional<TcgenScaleVectorSize> tcgen_scale_vector_size;
  std::optional<TcgenCollectorControl> tcgen_collector;
  std::optional<TcgenDataMovementShape> tcgen_shape;
  std::optional<TcgenRepeat> tcgen_repeat;
  std::optional<TcgenReductionOp> tcgen_reduction_op;
  std::optional<TcgenWaitClass> tcgen_wait_class;
  std::optional<VideoScale> video_scale;
  std::optional<VideoShiftMode> video_shift_mode;
  std::optional<VideoSecondaryOp> video_secondary_op;
  std::optional<VideoType> video_type;
  std::optional<AsyncProxyKind> async_proxy_kind;
  std::optional<ProxyKindPair> proxy_kind_pair;
  std::span<const SourceRange> locations;
};
struct OperandTypeCompatibilityDescriptor {
  std::string_view target_field_id;
  base::SpecialRegisterKind special_register_kind =
      base::SpecialRegisterKind::Invalid;
  uint8_t instruction_width = 0;
  ScalarType effective_type = ScalarType::Invalid;
  AvailabilityDescriptor availability;
};
/** Non-owning semantic projection of one resolved operand field. */
struct OperandView {
  std::string_view field_id;
  OperandShape actual_shape;
  std::optional<ScalarType> immediate_type;
  std::optional<uint64_t> immediate_bits;
  /** Numerical negativity of the evaluated signed integer source. */
  std::optional<bool> immediate_is_negative;
  std::optional<ScalarType> register_type;
  /** Direct opaque mov source identity; its instruction type must be .u64. */
  std::optional<base::OpaqueResourceKind> opaque_resource_kind;
  /** Declaration identity; absent for declaration-free standalone operands. */
  std::optional<binding::SymbolId> register_symbol_id;
  /** Resolved register category, independent of an unknown declaration type. */
  std::optional<ResolvedRegisterClass> register_class;
  /** Borrowed scalar carrier, valid only for this operand-view traversal. */
  const ResolvedRegisterRef* register_ref = nullptr;
  /** Borrowed data/predicate carriers for paired scalar destinations. */
  std::array<const ResolvedRegisterRef*, 2> paired_register_refs{};
  /** Declared vector lane count; scalar TCGEN sources require absence. */
  std::optional<uint8_t> register_vector_width;
  /** A cp.async fourth operand is an explicit cache policy, not source size. */
  bool cp_async_cache_policy = false;
  bool is_sink = false;
  /** Whether a predicate-pair value retains at least one destination lane. */
  bool predicate_pair_has_destination = true;
  std::array<ScalarType, 2> predicate_pair_types{};
  /** Whether the data lane of a d|p destination remains present. */
  bool paired_destination_data_present = true;
  /** Whether the predicate lane of a d|p destination remains present. */
  bool paired_destination_predicate_present = true;
  /** Declared type of the present predicate lane of a d|p destination. */
  std::optional<ScalarType> paired_destination_predicate_type;
  /** A destination payload contains a complemented predicate. */
  bool destination_predicate_negated = false;
  std::optional<ScalarType> special_register_type;
  std::optional<base::SpecialRegisterId> special_register_id;
  std::optional<MemoryStateSpace> address_state_space;
  AddressBaseKind address_base_kind = AddressBaseKind::Unknown;
  /** True for absent or representable signed-32 address offsets. */
  bool address_offset_fits_signed32 = true;
  std::optional<uint64_t> address_alignment;
  bool address_unified = false;
  /** Known only for declaration-bound address bases. */
  std::optional<bool> address_declaration_is_unified;
  EnclosingFunctionKind enclosing_function_kind =
      EnclosingFunctionKind::Unknown;
  ParameterDirection parameter_direction = ParameterDirection::None;
  ParameterAddressQualifier parameter_qualifier =
      ParameterAddressQualifier::Default;
  std::array<ScalarType, kMaxOperandElements> vector_element_types{};
  std::array<OperandShape, kMaxOperandElements> vector_element_shapes{};
  /** Original integer source for each immediate vector lane, when present. */
  std::array<std::optional<uint64_t>, kMaxOperandElements>
      vector_immediate_source_bits{};
  /** Current owned integer payload for each immediate vector lane. */
  std::array<std::optional<uint64_t>, kMaxOperandElements>
      vector_immediate_bits{};
  /** Signed negativity accompanies vector_immediate_source_bits. */
  std::array<bool, kMaxOperandElements> vector_immediate_negative{};
  /** Borrowed lane references; null for sinks and non-register lanes. */
  std::array<const ResolvedRegisterRef*, kMaxOperandElements>
      vector_element_registers{};
  /** Borrowed complete register-vector source; valid only during checking. */
  const ResolvedRegisterVector* register_vector = nullptr;
  /** Borrowed typed values for source-vector lanes, null for registers. */
  std::array<const ResolvedImmediate*, kMaxOperandElements>
      vector_element_immediates{};
  /** Composite tensor coordinates with a statically negative immediate. */
  bool tensor_has_negative_immediate = false;
  /** Composite tensor rank encoded by the owned operand. */
  std::optional<TensorRank> tensor_rank;
  /** Borrowed owned payload for rank, provenance, and signedness checks. */
  const ResolvedTensorOperand* tensor_operand = nullptr;
  /** Borrowed owned video value; lifetime is the synchronous checker call. */
  const ResolvedVideoOperand* video_operand = nullptr;
  /** Original element count before fixed-size checker projection. */
  size_t vector_arity = 0;
  uint8_t vector_sink_count = 0;
  std::optional<AvailabilityDescriptor> value_availability;
  std::string_view value_name{};
  std::span<const SourceRange> locations;
  /** Evaluated 64-bit integer bits before the operand use narrows them. */
  std::optional<uint64_t> integer_source_bits;
};
/** Borrowed target properties used by checker availability validation. */
struct TargetInfo {
  PtxVersion ptx_version{};
  uint32_t sm_version = 0;
  std::span<const std::string_view> enabled_family_features{};
  std::optional<base::TargetIdentity> identity;
  std::span<const std::string_view> capabilities{};
};
/** Variant-local index of one modifier slot in generated descriptor order. */
struct ModifierSlotTag {
  uint16_t value = 0;
  bool operator==(const ModifierSlotTag&) const = default;
};
struct OperandLayoutDescriptor {
  std::string_view layout_name;
  AvailabilityDescriptor availability;
  /** Modifier slots this layout rejects; layout selection is shape-only. */
  std::span<const ModifierSlotTag> forbidden_modifiers;
};
enum class ModifierValueKind : uint8_t {
  Bool,
  ScalarType,
  RoundingMode,
  ComparisonOperator,
  TestProperty,
  BooleanOperator,
  CacheOperator,
  EvictionPriority,
  PrefetchSize,
  VectorArity,
  MemoryStateSpace,
  MemoryConsistency,
  MemoryScope,
  MbarrierPhaseType,
  MbarrierLayout,
  TcgenCtaGroup,
  TcgenScaleVectorSize,
  TcgenCollectorControl,
  TcgenDataMovementShape,
  TcgenRepeat,
  TcgenReductionOp,
  TcgenWaitClass,
  VideoScale,
  VideoShiftMode,
  VideoSecondaryOp,
  VideoType,
  AsyncProxyKind,
  ProxyKindPair
};
struct ModifierValueAvailabilityDescriptor {
  std::string_view kind_id;
  ModifierValueKind value_kind;
  bool bool_value = false;
  ScalarType scalar_type = ScalarType::Invalid;
  RoundingMode rounding_mode = RoundingMode::Invalid;
  ComparisonOperator comparison_operator = ComparisonOperator::Invalid;
  TestProperty test_property = TestProperty::Invalid;
  BooleanOperator boolean_operator = BooleanOperator::Invalid;
  CacheOperator cache_operator = CacheOperator::Unspecified;
  EvictionPriority eviction_priority = EvictionPriority::Invalid;
  PrefetchSize prefetch_size = PrefetchSize::None;
  VectorArity vector_arity = VectorArity::Invalid;
  MemoryStateSpace memory_state_space = MemoryStateSpace::Invalid;
  MemoryConsistency memory_consistency = MemoryConsistency::Omitted;
  MemoryScope memory_scope = MemoryScope::None;
  MbarrierPhaseType mbarrier_phase_type = MbarrierPhaseType::Primary;
  MbarrierLayout mbarrier_layout = MbarrierLayout::V0;
  TcgenCtaGroup tcgen_cta_group = TcgenCtaGroup::One;
  TcgenScaleVectorSize tcgen_scale_vector_size = TcgenScaleVectorSize::Absent;
  TcgenCollectorControl tcgen_collector;
  TcgenDataMovementShape tcgen_shape = TcgenDataMovementShape::S32x32b;
  TcgenRepeat tcgen_repeat = TcgenRepeat::X1;
  TcgenReductionOp tcgen_reduction_op = TcgenReductionOp::Min;
  TcgenWaitClass tcgen_wait_class = TcgenWaitClass::Load;
  VideoScale video_scale = VideoScale::None;
  VideoShiftMode video_shift_mode = VideoShiftMode::Clamp;
  VideoSecondaryOp video_secondary_op = VideoSecondaryOp::None;
  VideoType video_type = VideoType::S32;
  AsyncProxyKind async_proxy_kind = AsyncProxyKind::Async;
  ProxyKindPair proxy_kind_pair = ProxyKindPair::TensormapToGeneric;
  AvailabilityDescriptor availability;
};
/** One target-independent semantic modifier value admitted by a variant. */
struct ModifierValueDomainDescriptor {
  /** Borrowed generated modifier-kind text; storage outlives every check. */
  std::string_view kind_id;
  /** Selects the single meaningful typed payload member below. */
  ModifierValueKind value_kind;
  bool bool_value = false;
  ScalarType scalar_type = ScalarType::Invalid;
  RoundingMode rounding_mode = RoundingMode::Invalid;
  ComparisonOperator comparison_operator = ComparisonOperator::Invalid;
  TestProperty test_property = TestProperty::Invalid;
  BooleanOperator boolean_operator = BooleanOperator::Invalid;
  CacheOperator cache_operator = CacheOperator::Unspecified;
  EvictionPriority eviction_priority = EvictionPriority::Invalid;
  PrefetchSize prefetch_size = PrefetchSize::None;
  VectorArity vector_arity = VectorArity::Invalid;
  MemoryStateSpace memory_state_space = MemoryStateSpace::Invalid;
  MemoryConsistency memory_consistency = MemoryConsistency::Omitted;
  MemoryScope memory_scope = MemoryScope::None;
  MbarrierPhaseType mbarrier_phase_type = MbarrierPhaseType::Primary;
  MbarrierLayout mbarrier_layout = MbarrierLayout::V0;
  TcgenCtaGroup tcgen_cta_group = TcgenCtaGroup::One;
  TcgenScaleVectorSize tcgen_scale_vector_size = TcgenScaleVectorSize::Absent;
  TcgenCollectorControl tcgen_collector;
  TcgenDataMovementShape tcgen_shape = TcgenDataMovementShape::S32x32b;
  TcgenRepeat tcgen_repeat = TcgenRepeat::X1;
  TcgenReductionOp tcgen_reduction_op = TcgenReductionOp::Min;
  TcgenWaitClass tcgen_wait_class = TcgenWaitClass::Load;
  VideoScale video_scale = VideoScale::None;
  VideoShiftMode video_shift_mode = VideoShiftMode::Clamp;
  VideoSecondaryOp video_secondary_op = VideoSecondaryOp::None;
  VideoType video_type = VideoType::S32;
  AsyncProxyKind async_proxy_kind = AsyncProxyKind::Async;
  ProxyKindPair proxy_kind_pair = ProxyKindPair::TensormapToGeneric;
};
struct ModifierValueView {
  std::string_view kind_id;
  ModifierValueKind value_kind;
  bool bool_value = false;
  ScalarType scalar_type = ScalarType::Invalid;
  RoundingMode rounding_mode = RoundingMode::Invalid;
  ComparisonOperator comparison_operator = ComparisonOperator::Invalid;
  TestProperty test_property = TestProperty::Invalid;
  BooleanOperator boolean_operator = BooleanOperator::Invalid;
  CacheOperator cache_operator = CacheOperator::Unspecified;
  EvictionPriority eviction_priority = EvictionPriority::Invalid;
  PrefetchSize prefetch_size = PrefetchSize::None;
  VectorArity vector_arity = VectorArity::Invalid;
  MemoryStateSpace memory_state_space = MemoryStateSpace::Invalid;
  MemoryConsistency memory_consistency = MemoryConsistency::Omitted;
  MemoryScope memory_scope = MemoryScope::None;
  MbarrierPhaseType mbarrier_phase_type = MbarrierPhaseType::Primary;
  MbarrierLayout mbarrier_layout = MbarrierLayout::V0;
  TcgenCtaGroup tcgen_cta_group = TcgenCtaGroup::One;
  TcgenScaleVectorSize tcgen_scale_vector_size = TcgenScaleVectorSize::Absent;
  TcgenCollectorControl tcgen_collector;
  TcgenDataMovementShape tcgen_shape = TcgenDataMovementShape::S32x32b;
  TcgenRepeat tcgen_repeat = TcgenRepeat::X1;
  TcgenReductionOp tcgen_reduction_op = TcgenReductionOp::Min;
  TcgenWaitClass tcgen_wait_class = TcgenWaitClass::Load;
  VideoScale video_scale = VideoScale::None;
  VideoShiftMode video_shift_mode = VideoShiftMode::Clamp;
  VideoSecondaryOp video_secondary_op = VideoSecondaryOp::None;
  VideoType video_type = VideoType::S32;
  AsyncProxyKind async_proxy_kind = AsyncProxyKind::Async;
  ProxyKindPair proxy_kind_pair = ProxyKindPair::TensormapToGeneric;
  bool is_present = false;
  std::span<const SourceRange> locations;
  /**
   * Position of this slot in the variant's generated modifier order. Appended
   * so that existing positional aggregate initializers keep compiling.
   */
  ModifierSlotTag slot;
};
struct VariantDescriptor {
  std::string_view variant_name;
  AvailabilityDescriptor availability;
  std::span<const ModifierValueDomainDescriptor> modifier_value_domains;
  std::span<const ModifierValueAvailabilityDescriptor>
      modifier_value_availabilities;
  std::span<const OperandLayoutDescriptor> operand_layouts;
  std::span<const OperandTypeCompatibilityDescriptor>
      operand_type_compatibilities;
  std::string_view rule_id;
  /** Whether a bracket-address `.unified` suffix is legal for this variant. */
  bool permits_unified_address = false;
  /** Operation contract applied to known `.unified` declaration addresses. */
  enum class UnifiedAddressAccess : uint8_t { None, Read, Write };
  UnifiedAddressAccess unified_address_access = UnifiedAddressAccess::None;
  /** Written atomic suffix domain and its selected state-space/address slots. */
  struct AtomicAddressQualifierDescriptor {
    /** Resolved state-space modifier field selected by the variant. */
    std::string_view state_space_field_id;
    /** Resolved address operand used for provenance validation. */
    std::string_view address_operand_id;
    /** Exact written qualifier values admitted by this variant. */
    std::span<const AtomicAddressQualifier> allowed_values;
  } atomic_address_qualifier;
  /** One MMIO semantic alternative and its target requirement. */
  struct MmioSemanticDescriptor {
    MemoryConsistency semantics = MemoryConsistency::Omitted;
    AvailabilityDescriptor availability;
  };
  struct MemoryConsistencyDescriptor {
    std::string_view semantics_field_id;
    std::string_view scope_field_id;
    std::string_view mmio_field_id;
    std::string_view cache_field_id;
    std::string_view address_field_id;
    std::string_view type_field_id;
    std::string_view state_space_field_id;
    std::span<const MmioSemanticDescriptor> mmio_semantics;
  } memory_consistency;
  std::span<const AddressAlignmentConstraint> address_alignments;
  struct MemoryVectorDescriptor {
    std::string_view type_field_id;
    std::string_view vector_field_id;
    std::string_view address_field_id;
    std::string_view state_space_field_id;
    AvailabilityDescriptor availability;
    /** Require a PTX 8.8 256-bit vector rather than legacy vector forms. */
    bool require_modern = false;
  } memory_vector;
  struct ImmediateValueDescriptor {
    std::string_view operand_field_id;
    std::span<const uint64_t> allowed_values;
  } immediate_value;
  struct ImmediateMultipleOfDescriptor {
    std::string_view operand_field_id;
    uint64_t divisor = 0;
  } immediate_multiple_of;
  struct ImmediateRangeDescriptor {
    std::string_view operand_field_id;
    uint64_t minimum = 0;
    bool has_maximum = false;
    uint64_t maximum = ~uint64_t{0};
  };
  std::span<const ImmediateRangeDescriptor> immediate_ranges;
};
struct InstructionDescriptor {
  std::string_view opcode_name;
  std::span<const VariantDescriptor> variants;
};

}  // namespace checker

using base::AsyncProxyKind;
using base::BooleanOperator;
using base::CacheOperator;
using base::ComparisonOperator;
using base::EvictionPriority;
using base::MbarrierLayout;
using base::MbarrierPhaseType;
using base::MemoryConsistency;
using base::MemoryScope;
using base::PrefetchSize;
using base::ProxyKindPair;
using base::RoundingMode;
using base::ScalarType;
using base::TestProperty;
enum class ParameterDeclarationRole : uint8_t {
  EntryInput,
  DeviceInput,
  DeviceReturn,
  BodyLocal
};
/** Owned, validated `.param` declaration data with no allocation offsets. */
struct ResolvedParameterDeclaration {
  /** Identity and lexical scope in the owning module symbol table. */
  binding::SymbolId symbol_id;
  binding::ScopeId scope_id;
  ParameterDeclarationRole role{};
  ScalarType scalar_type{ScalarType::Invalid};
  uint64_t alignment{};
  bool explicit_alignment{};
  uint32_t vector_width{1};
  /** Outer-to-inner element counts; null denotes a supported unsized axis. */
  std::vector<std::optional<uint64_t>> array_extents;
  /** Total declared bytes, absent only for a supported unsized parameter. */
  std::optional<uint64_t> byte_extent;
  std::optional<call_argument_compatibility::PointerProperties> pointer;
};
/** Name-only opaque entry input with no implied allocation or byte layout. */
struct ResolvedOpaqueEntryParameter {
  /** Stable symbol identity in the owning module. */
  binding::SymbolId symbol_id;
  /** Lexical scope of the entry parameter declaration. */
  binding::ScopeId scope_id;
  /** Texture, sampler, or surface identity declared by this input. */
  base::OpaqueResourceKind kind{};
  /** Explicit source alignment in bytes; absent never implies natural alignment. */
  std::optional<uint64_t> explicit_alignment;
  /** Retained declaration array shape without assigning element bytes. */
  bool is_array{};
  /** Known array extent; absent for a source array with unspecified extent. */
  std::optional<uint64_t> array_extent;
  /** Complete source range of the header parameter. */
  SourceRange range;
};
enum class ResolvedRegisterClass : uint8_t { General, Predicate };
/** Distinguish source-written selectors from implicit named-vector lanes. */
enum class RegisterComponentOrigin : uint8_t {
  ExplicitSelector,
  NamedProjection
};

/** Exact selector text and location; never synthesized for implicit lanes. */
struct ResolvedRegisterSelector {
  /** Source spelling including its leading dot, such as .x or .r. */
  std::string spelling;
  /** Location of the written selector alone. */
  SourceRange range;
  /** Compare source provenance as well as spelling. */
  bool operator==(const ResolvedRegisterSelector&) const = default;
};

/** Owned scalar projection of a real ordinary vector-register declaration. */
struct ResolvedRegisterComponent {
  /** Canonical x/r=0, y/g=1, z/b=2, w/a=3 lane identity. */
  uint8_t lane{};
  /** Actual declaration width, independent of the effective scalar shape. */
  uint8_t declaration_width{};
  /** Explicit source selector or future implicit whole-vector projection. */
  RegisterComponentOrigin origin{RegisterComponentOrigin::ExplicitSelector};
  /** Written base, including any parameterized-register member suffix. */
  std::string base_spelling;
  /** Exact base identifier range; named projections reuse the whole range. */
  SourceRange base_range;
  /** Complete component range; named projections reuse the whole range. */
  SourceRange range;
  /** Present only when the source explicitly wrote a selector. */
  std::optional<ResolvedRegisterSelector> selector;
  /** Compare all owned projection metadata. */
  bool operator==(const ResolvedRegisterComponent&) const = default;
};

/** Register carrier with real declaration identity and optional scalar projection. */
struct ResolvedRegisterRef {
  std::string spelling;
  ResolvedRegisterClass register_class;
  std::optional<uint32_t> index;
  std::optional<binding::SymbolId> symbol_id;
  std::optional<uint32_t> parameterized_index;
  std::optional<ScalarType> declared_type;
  /** Effective whole-vector width; absent for scalars and selected components. */
  std::optional<uint8_t> vector_width;
  /** Component declaration width/provenance, without inventing a scalar symbol. */
  std::optional<ResolvedRegisterComponent> component;
  bool operator==(const ResolvedRegisterRef&) const = default;
};
/** Decode an ordinary selector without extending the hardware component domain. */
inline std::optional<uint8_t> ordinary_register_lane(
    std::string_view selector) {
  if (selector == ".x" || selector == ".r")
    return 0;
  if (selector == ".y" || selector == ".g")
    return 1;
  if (selector == ".z" || selector == ".b")
    return 2;
  if (selector == ".w" || selector == ".a")
    return 3;
  return std::nullopt;
}

/** Validate explicit scalar/brace provenance; reserved named projections fail. */
inline bool valid_register_component(const ResolvedRegisterRef& ref) {
  if (!ref.component)
    return true;
  const auto& part = *ref.component;
  const auto before = [](SourcePos left, SourcePos right) {
    return left.line < right.line ||
           (left.line == right.line && left.column <= right.column);
  };
  const auto nonempty = [&](SourceRange range) {
    return range.start.line > 0 && range.start.column > 0 &&
           before(range.start, range.end) && range.start != range.end;
  };
  if (!ref.symbol_id || !ref.declared_type || ref.vector_width ||
      ref.register_class != ResolvedRegisterClass::General ||
      (*ref.declared_type == ScalarType::Pred ||
       *ref.declared_type == ScalarType::Invalid) ||
      (part.declaration_width != 2 && part.declaration_width != 4) ||
      part.lane >= part.declaration_width || part.base_spelling.empty() ||
      !nonempty(part.base_range) || !nonempty(part.range) ||
      part.base_range.start.line != part.base_range.end.line ||
      part.base_range.end.column - part.base_range.start.column !=
          static_cast<int32_t>(part.base_spelling.size()) ||
      part.range.start != part.base_range.start)
    return false;
  if (part.origin != RegisterComponentOrigin::ExplicitSelector ||
      !part.selector)
    return false;
  const auto lane = ordinary_register_lane(part.selector->spelling);
  return lane == part.lane && nonempty(part.selector->range) &&
         part.selector->range.start.line == part.selector->range.end.line &&
         part.selector->range.end.column - part.selector->range.start.column ==
             static_cast<int32_t>(part.selector->spelling.size()) &&
         before(part.base_range.end, part.selector->range.start) &&
         part.range.end == part.selector->range.end &&
         ref.spelling == part.base_spelling + part.selector->spelling;
}

/** Compare physical register identity, treating ordinary selector aliases equally. */
inline bool same_register_storage(const ResolvedRegisterRef& left,
                                  const ResolvedRegisterRef& right) {
  if (left.symbol_id && right.symbol_id)
    return left.symbol_id == right.symbol_id &&
           left.parameterized_index == right.parameterized_index &&
           (left.component ? std::optional{left.component->lane}
                           : std::nullopt) ==
               (right.component ? std::optional{right.component->lane}
                                : std::nullopt);
  return left.spelling == right.spelling;
}

/** Runtime-opaque stack position with owned register and function provenance. */
struct ResolvedStackToken {
  /** Scalar integer/bit carrier; no runtime token origin is inferred. */
  ResolvedRegisterRef register_ref;
  /** Unknown when the caller supplies no owning function scope. */
  EnclosingFunctionKind enclosing_function_kind =
      EnclosingFunctionKind::Unknown;
  /** Stable owning function scope, absent only for fragments. */
  std::optional<binding::ScopeId> function_scope;
  /** Compare carrier and cached ownership. */
  bool operator==(const ResolvedStackToken&) const = default;
};
/** Local stack allocation result; the runtime address is not simulated. */
struct ResolvedLocalAllocationResult {
  /** Scalar destination preserving declared register type and binding. */
  ResolvedRegisterRef register_ref;
  /** Unknown when the caller supplies no owning function scope. */
  EnclosingFunctionKind enclosing_function_kind =
      EnclosingFunctionKind::Unknown;
  /** Stable owning function scope, absent only for fragments. */
  std::optional<binding::ScopeId> function_scope;
  /** Address-space role of the produced value, independent of address size. */
  base::DeclarationStateSpace address_state_space =
      base::DeclarationStateSpace::Local;
  /** Compare carrier, local role and cached ownership. */
  bool operator==(const ResolvedLocalAllocationResult&) const = default;
};
/** One byte selected from a scalar 32-bit source or merge destination. */
struct VideoByteSelector {
  /** Byte index within one 32-bit register, in 0..3. */
  uint8_t index{};
  /** Compare the selected byte identity. */
  bool operator==(const VideoByteSelector&) const = default;
};
/** One halfword selected from a scalar 32-bit source or merge destination. */
struct VideoHalfSelector {
  /** Halfword index within one 32-bit register, in 0..1. */
  uint8_t index{};
  /** Compare the selected halfword identity. */
  bool operator==(const VideoHalfSelector&) const = default;
};
/** Two source halfwords selected from the concatenated a/b register pair. */
struct VideoHalfSwizzle {
  /** Written most-significant-to-least-significant digits in 0..3; repeats allowed. */
  std::array<uint8_t, 2> indices{};
  /** Compare the written source selection order. */
  bool operator==(const VideoHalfSwizzle&) const = default;
};
/** Four source bytes selected from the concatenated a/b register pair. */
struct VideoByteSwizzle {
  /** Written most-significant-to-least-significant digits in 0..7; repeats allowed. */
  std::array<uint8_t, 4> indices{};
  /** Compare the written source selection order. */
  bool operator==(const VideoByteSwizzle&) const = default;
};
/** Nonempty two-lane destination write mask in canonical descending order. */
struct VideoHalfMask {
  /** Only the first count entries are live; indices are in 0..1. */
  std::array<uint8_t, 2> indices{};
  uint8_t count{};
  /** Compare the retained destination mask representation. */
  bool operator==(const VideoHalfMask&) const = default;
};
/** Nonempty four-lane destination write mask in canonical descending order. */
struct VideoByteMask {
  /** Only the first count entries are live; indices are in 0..3. */
  std::array<uint8_t, 4> indices{};
  uint8_t count{};
  /** Compare the retained destination mask representation. */
  bool operator==(const VideoByteMask&) const = default;
};
/** Typed scalar selection, cross-source swizzle, or SIMD destination mask. */
using VideoSelector =
    std::variant<VideoByteSelector, VideoHalfSelector, VideoHalfSwizzle,
                 VideoByteSwizzle, VideoHalfMask, VideoByteMask>;
/** Bound direct resource declaration, independent of generic address layout. */
struct ResolvedOpaqueSymbolRef {
  /** Source spelling retained for diagnostics and consumer display. */
  std::string spelling;
  /** Stable declaration identity in the owning resolved module. */
  binding::SymbolId symbol_id;
  /** Member of a parameterized opaque declaration, if source selected one. */
  std::optional<uint32_t> parameterized_index;
  /** Declaration scope, which must be module or current entry input. */
  binding::ScopeId scope_id;
  /** Opaque identity declared by the bound symbol. */
  base::OpaqueResourceKind kind{};
  /** True only for the owning entry's opaque input. */
  bool entry_input = false;
  /** Source location of this direct resource use. */
  SourceRange range;
  bool operator==(const ResolvedOpaqueSymbolRef&) const = default;
};

/** Direct typed resource or an indirect 64-bit scalar register handle. */
struct ResolvedOpaqueResourceRef {
  /** Indirect carrier does not prove its runtime pointee kind. */
  std::variant<ResolvedOpaqueSymbolRef, ResolvedRegisterRef> value;
  /** Expected resource use selected by the instruction descriptor. */
  base::OpaqueResourceKind expected_kind{};
  /** Exact source range for a direct name or indirect carrier register. */
  SourceRange source_range;
  bool operator==(const ResolvedOpaqueResourceRef&) const = default;
};
/** Borrowed opaque Table 43 source register selected by one Tensor Memory copy.
 * The pointer is valid only while its owning instruction payload lives.
 */
struct TcgenCopyDescriptorView {
  /** Scalar 64-bit source register, borrowed from the owning copy form. */
  const ResolvedRegisterRef* source;
};
/** Validate known carrier metadata before exposing an opaque descriptor role. */
inline std::optional<TcgenCopyDescriptorView> tcgen_copy_descriptor_view(
    const ResolvedRegisterRef& source) noexcept {
  if (source.register_class != ResolvedRegisterClass::General ||
      source.vector_width || (source.symbol_id && !source.declared_type))
    return std::nullopt;
  if (source.declared_type) {
    const auto kind = base::scalar_kind(*source.declared_type);
    if (base::scalar_size_of(*source.declared_type) != 8 ||
        (kind != base::ScalarKind::Bit && kind != base::ScalarKind::Signed &&
         kind != base::ScalarKind::Unsigned))
      return std::nullopt;
  }
  return TcgenCopyDescriptorView{&source};
}
/** Runtime-opaque shared-memory matrix descriptor carried by a bound register. */
struct ResolvedSharedMatrixDescriptor {
  /** Bound b64 register whose runtime value is the matrix descriptor. */
  ResolvedRegisterRef register_ref;
  /** Compare source register identity, not unknown runtime contents. */
  bool operator==(const ResolvedSharedMatrixDescriptor&) const = default;
};
struct ResolvedMbarrierStateToken {
  std::optional<ResolvedRegisterRef> register_ref;
  bool operator==(const ResolvedMbarrierStateToken&) const = default;
};
struct ResolvedRegisterOrSink {
  std::optional<ResolvedRegisterRef> register_ref;
  bool operator==(const ResolvedRegisterOrSink&) const = default;
};
/** A typed immediate retaining both use-width and integer-source values. */
struct ResolvedImmediate {
  /** Bits after conversion to the scalar type selected by this operand use. */
  uint64_t bits;
  /** Scalar type selected by this operand use. */
  ScalarType type;
  /** True only when the evaluated integer source is signed and negative. */
  bool is_negative = false;
  /** Evaluated 64-bit integer bits, absent for floating-point immediates. */
  std::optional<uint64_t> integer_source_bits;
  bool operator==(const ResolvedImmediate&) const = default;
};
/** Check owned value-vector immediate provenance and an optional signed domain. */
[[nodiscard]] inline bool valid_value_vector_immediate(
    const ResolvedImmediate& value,
    std::optional<checker::SignedVectorImmediateRange> range = std::nullopt) {
  const auto kind = base::scalar_kind(value.type);
  const auto bytes = base::scalar_size_of(value.type);
  if (bytes == 0 || bytes > 8)
    return false;
  const unsigned width = static_cast<unsigned>(bytes) * 8;
  const uint64_t mask = width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
  if ((value.bits & ~mask) != 0)
    return false;
  if (kind == base::ScalarKind::Float)
    return !range && !value.integer_source_bits && !value.is_negative;
  if (kind != base::ScalarKind::Bit && kind != base::ScalarKind::Signed &&
      kind != base::ScalarKind::Unsigned)
    return false;
  if (!value.integer_source_bits ||
      value.bits != (*value.integer_source_bits & mask))
    return false;
  const int64_t source = std::bit_cast<int64_t>(*value.integer_source_bits);
  if (value.is_negative)
    return source < 0 &&
           (!range || (source >= range->minimum && source <= range->maximum));
  if (!range)
    return true;
  return range->maximum >= 0 &&
         *value.integer_source_bits <= static_cast<uint64_t>(range->maximum) &&
         (range->minimum <= 0 ||
          *value.integer_source_bits >= static_cast<uint64_t>(range->minimum));
}
/** Whole ordinary vector declaration reference, without implicit lane selection. */
struct ResolvedVectorRegisterRef {
  ResolvedRegisterRef register_ref;
  /** Compare the real declaration identity and cached shape. */
  bool operator==(const ResolvedVectorRegisterRef&) const = default;
};
/** Written source form of a register-vector operand. */
enum class ResolvedVectorSourceKind : uint8_t { BraceList, NamedVector };
/** Owned source provenance; no borrowed syntax survives resolution. */
struct ResolvedVectorSource {
  ResolvedVectorSourceKind kind{ResolvedVectorSourceKind::BraceList};
  /** Present only for a named source, with its exact identifier location. */
  std::optional<WithLocs<ResolvedVectorRegisterRef>> whole_base;
  /** Complete written identifier or brace-list range. */
  SourceRange range;
  /** Compare source form and all retained provenance. */
  bool operator==(const ResolvedVectorSource&) const = default;
};
/** Register lanes with explicit brace or complete named-vector provenance. */
struct ResolvedRegisterVector {
  std::vector<std::optional<ResolvedRegisterRef>> elements;
  ResolvedVectorSource source;
  /** Compare lane payloads and source provenance. */
  bool operator==(const ResolvedRegisterVector&) const = default;
};

/** Validate vector provenance; only a complete named container admits projections.
 * Empty brace provenance remains compatible with legacy standalone payloads;
 * owned/source-aware callers require it and provide the owning instruction range.
 */
inline bool valid_register_vector_source(const ResolvedRegisterVector& vector,
                                         std::span<const SourceRange> locations,
                                         bool require_brace_range = false,
                                         SourceRange owner = {}) {
  const auto before = [](SourcePos left, SourcePos right) {
    return left.line < right.line ||
           (left.line == right.line && left.column <= right.column);
  };
  const auto valid = [&](SourceRange range) {
    return range.start.line > 0 && range.start.column > 0 &&
           range.end.line > 0 && range.end.column > 0 &&
           before(range.start, range.end) && range.start != range.end;
  };
  const auto inside = [&](SourceRange inner, SourceRange outer) {
    return valid(inner) && valid(outer) && before(outer.start, inner.start) &&
           before(inner.end, outer.end);
  };
  const auto& source = vector.source;
  if (source.kind == ResolvedVectorSourceKind::BraceList) {
    if (source.whole_base)
      return false;
    for (const auto& lane : vector.elements)
      if (lane && !valid_register_component(*lane))
        return false;
    if (source.range == SourceRange{})
      return !require_brace_range;
    return valid(source.range) &&
           (owner == SourceRange{} || inside(source.range, owner)) &&
           locations.size() == vector.elements.size() &&
           std::ranges::all_of(locations, [&](SourceRange range) {
             return inside(range, source.range);
           });
  }
  if (source.kind != ResolvedVectorSourceKind::NamedVector ||
      !source.whole_base || !valid(source.range) ||
      (owner != SourceRange{} && !inside(source.range, owner)) ||
      source.whole_base->locs.size() != 1 ||
      source.whole_base->locs.front() != source.range)
    return false;
  const auto& base = source.whole_base->value.register_ref;
  const size_t arity = vector.elements.size();
  if (!base.symbol_id || !base.declared_type || base.component ||
      base.register_class != ResolvedRegisterClass::General ||
      (arity != 2 && arity != 4) || base.vector_width != arity ||
      *base.declared_type == ScalarType::Pred ||
      *base.declared_type == ScalarType::Invalid ||
      base::scalar_size_of(*base.declared_type) == 0 ||
      arity * base::scalar_size_of(*base.declared_type) > 16 ||
      locations.size() != arity || base.spelling.empty() ||
      source.range.start.line != source.range.end.line ||
      source.range.end.column - source.range.start.column !=
          static_cast<int32_t>(base.spelling.size()))
    return false;
  for (size_t index = 0; index < arity; ++index) {
    if (!vector.elements[index] || locations[index] != source.range)
      return false;
    auto expected = base;
    expected.vector_width.reset();
    expected.component = ResolvedRegisterComponent{
        .lane = static_cast<uint8_t>(index),
        .declaration_width = static_cast<uint8_t>(arity),
        .origin = RegisterComponentOrigin::NamedProjection,
        .base_spelling = base.spelling,
        .base_range = source.range,
        .range = source.range};
    if (*vector.elements[index] != expected)
      return false;
  }
  return true;
}
/** Owned CUDA Fabric Transport handle with bound scalar register components. */
struct ResolvedFabricHandle {
  /** Unsigned 32-bit endpoint value in a 32-bit integer/bit register carrier. */
  WithLocs<ResolvedRegisterRef> endpoint;
  /** Unsigned 64-bit byte offset in a 64-bit integer/bit register carrier. */
  WithLocs<ResolvedRegisterRef> data_offset;
  /** Optional counted byte offset in a 64-bit integer/bit register carrier. */
  std::optional<WithLocs<ResolvedRegisterRef>> counter_offset;
  /** Bracket and comma locations retained for owned shape revalidation. */
  SourceRange left_bracket_range;
  std::vector<SourceRange> comma_ranges;
  SourceRange right_bracket_range;
};
struct ResolvedPredicate {
  ResolvedRegisterRef register_ref;
  bool negated{};
  bool operator==(const ResolvedPredicate&) const = default;
};
/** Texture data result with a separately retained optional residency predicate. */
struct ResolvedTextureResult {
  /** Two packed or four scalar result registers, as selected by the form. */
  ResolvedRegisterVector data;
  /** Chosen result element type at resolution, retained for AST-free checks. */
  ScalarType result_type = ScalarType::Invalid;
  /** Per-lane locations corresponding to data, independent of brace range. */
  std::vector<SourceRange> data_ranges;
  /** Written predicate destination; absence remains distinct from a false value. */
  std::optional<ResolvedPredicate> residency;
  /** Exact predicate register use range when residency is present. */
  SourceRange residency_range;
  /** Source location of the separator, empty if no residency was written. */
  SourceRange pipe_range;
  bool operator==(const ResolvedTextureResult&) const = default;
};
struct ResolvedBranchTarget {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  bool operator==(const ResolvedBranchTarget&) const = default;
};
struct ResolvedBranchTargetSet {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  bool operator==(const ResolvedBranchTargetSet&) const = default;
};
struct ResolvedSpecialRegisterRef {
  std::string spelling;
  base::SpecialRegisterId id;
  std::optional<base::VectorComponent> component;
  bool operator==(const ResolvedSpecialRegisterRef&) const = default;
};

/** Canonical truth value of an integer predicate-source constant. */
struct ResolvedPredicateConstant {
  bool value{};
  bool operator==(const ResolvedPredicateConstant&) const = default;
};

/** A predicate special-register source with its source-level complement. */
struct ResolvedPredicateSpecialRegister {
  ResolvedSpecialRegisterRef register_ref;
  bool negated{};
  bool operator==(const ResolvedPredicateSpecialRegister&) const = default;
};
using ResolvedPredicateSource =
    std::variant<ResolvedPredicate, ResolvedPredicateSpecialRegister,
                 ResolvedPredicateConstant>;
struct ResolvedVectorSpecialRegisterRef {
  std::string spelling;
  base::SpecialRegisterId id;
  bool operator==(const ResolvedVectorSpecialRegisterRef&) const = default;
};
struct ResolvedFunctionRef {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  bool is_entry{};
  std::optional<checker::AvailabilityDescriptor> address_availability;
  /** Contextual availability has no value-equality contract. */
  bool operator==(const ResolvedFunctionRef&) const = delete;
};
struct ResolvedIndirectMetadataRef {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  std::optional<binding::SymbolKind> declaration_kind;
  bool operator==(const ResolvedIndirectMetadataRef&) const = default;
};
using ResolvedIndirectCallee =
    std::variant<ResolvedRegisterRef, ResolvedIndirectMetadataRef>;
struct ResolvedCallParameterRef {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  std::optional<uint32_t> parameterized_index;
  /** Semantic declaration state space; the base alias needs no syntax AST. */
  std::optional<base::DeclarationStateSpace> state_space;
  std::optional<ScalarType> declared_type;
  bool operator==(const ResolvedCallParameterRef&) const = default;
};
/**
 * A call literal which is deferred only outside a successfully contextual module.
 *
 * Module resolution fills ``value`` from the formal signature. The spelling and
 * lexical kind remain only as provenance for standalone/deferred consumers.
 */
struct ResolvedCallLiteral {
  /** Original source spelling retained for an intentionally deferred literal. */
  std::string spelling;
  /** Base lexical category independent of complete syntax-AST ownership. */
  base::LiteralCategory kind{};
  /** Evaluated expression domain retained before a formal scalar type is known. */
  std::optional<declaration_semantics::NumericConstantValue> source_value;
  /** Formal-driven typed value present after successful module call checking. */
  std::optional<ResolvedImmediate> value;
  bool operator==(const ResolvedCallLiteral&) const = default;
};
using ResolvedCallArgument =
    std::variant<ResolvedCallParameterRef, ResolvedCallLiteral>;
struct ResolvedCallArguments {
  std::vector<WithLocs<ResolvedCallArgument>> values;
  bool operator==(const ResolvedCallArguments&) const = default;
};
/** An addressable data symbol, optionally bound to a module declaration. */
struct ResolvedSymbolRef {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  std::optional<uint32_t> parameterized_index;
  std::optional<binding::SymbolKind> declaration_kind;
  /** Declared space; may differ from the produced address space for parameters. */
  std::optional<base::DeclarationStateSpace> declaration_state_space;
  /** Effective address space after context-sensitive address materialization. */
  std::optional<base::DeclarationStateSpace> address_state_space;
  std::optional<ScalarType> declared_type;
  /** Guaranteed byte alignment for a bound declaration address. */
  std::optional<uint64_t> address_alignment;
  /** Target requirement contributed by this address value, if any. */
  std::optional<checker::AvailabilityDescriptor> address_availability;
  /** Declaration metadata needed for AST-free `.unified` validation. */
  std::optional<bool> declaration_is_unified;
  /** Owning function context retained for direct parameter-address validation. */
  EnclosingFunctionKind enclosing_function_kind =
      EnclosingFunctionKind::Unknown;
  /** Parameter-space qualifier selected by an instruction for this direct address. */
  ParameterAddressQualifier parameter_qualifier =
      ParameterAddressQualifier::Default;
  /** Contextual availability has no value-equality contract. */
  bool operator==(const ResolvedSymbolRef&) const = delete;
};
enum class ResolvedAddressOffsetOperator : uint8_t { Add, Subtract };
struct ResolvedAddressOffset {
  ResolvedAddressOffsetOperator operation = ResolvedAddressOffsetOperator::Add;
  ResolvedImmediate value;
  bool operator==(const ResolvedAddressOffset&) const = default;
};
/** Check the effective signed offset after combining operator and literal sign. */
[[nodiscard]] inline bool address_offset_fits_signed32(
    const ResolvedAddressOffset& offset) {
  if (offset.operation != ResolvedAddressOffsetOperator::Add &&
      offset.operation != ResolvedAddressOffsetOperator::Subtract)
    return false;
  if (offset.value.type != ScalarType::S64 ||
      (offset.value.integer_source_bits &&
       *offset.value.integer_source_bits != offset.value.bits))
    return false;
  const uint64_t source_bits = offset.value.bits;
  const uint64_t magnitude =
      offset.value.is_negative ? uint64_t{0} - source_bits : source_bits;
  const bool negative =
      (offset.operation == ResolvedAddressOffsetOperator::Subtract) !=
      offset.value.is_negative;
  return magnitude <= (negative ? uint64_t{1} << 31 : (uint64_t{1} << 31) - 1);
}
using ResolvedAddressBase =
    std::variant<ResolvedRegisterRef, ResolvedImmediate, ResolvedSymbolRef>;
/** Admission and byte-displacement domain selected by the final instruction form. */
enum class NamedArrayAddressPolicy : uint8_t { Reject, Memory, Mov };
/** Owned symbolic array subscript, independent of a byte-address addend. */
struct ResolvedNamedArrayIndex {
  /** Source integer bits/signedness or the exact bound scalar index carrier. */
  std::variant<declaration_semantics::IntegerConstantValue,
               WithLocs<ResolvedRegisterRef>>
      index;
  /** Register displacement in element units; absent for a constant-only index. */
  std::optional<declaration_semantics::IntegerConstantValue> displacement;
  /** Written operator retained independently of normalized byte displacement. */
  ResolvedAddressOffsetOperator operation = ResolvedAddressOffsetOperator::Add;
  /** Bytes per declaration scalar element, excluding vectors and inner extents. */
  uint64_t scalar_stride{};
  /** Checked constant contribution in bytes; dynamic register arithmetic is symbolic. */
  int64_t byte_displacement{};
  /** Exact base, subscript child, optional operator/term and bracket provenance. */
  SourceRange base_range;
  SourceRange index_range;
  SourceRange operator_range;
  SourceRange displacement_range;
  SourceRange left_bracket_range;
  SourceRange right_bracket_range;
  SourceRange range;
};
/** A PTX address expression with resolved base, offset, and function context. */
struct ResolvedAddress {
  ResolvedAddressBase base;
  std::optional<ResolvedAddressOffset> offset;
  /** Unknown for standalone resolution; retained for parameter semantics. */
  EnclosingFunctionKind enclosing_function_kind =
      EnclosingFunctionKind::Unknown;
  ParameterAddressQualifier parameter_qualifier =
      ParameterAddressQualifier::Default;
  /** True when source spelled the bracket-address `.unified` suffix. */
  bool unified = false;
  /** Suffix provenance for checker diagnostics. */
  SourceRange unified_range;
  /** An owned named subscript; mutually exclusive with the byte offset. */
  std::optional<ResolvedNamedArrayIndex> named_index;
  /** A symbol base can carry contextual availability without value equality. */
  bool operator==(const ResolvedAddress&) const = delete;
};
/** Compute a checked signed byte contribution without evaluating an index register. */
std::optional<int64_t> named_array_byte_displacement(
    const ResolvedNamedArrayIndex& index);
/** Revalidate the symbolic subscript and its source ranges for one consumer. */
bool valid_named_array_address(const ResolvedAddress& address,
                               NamedArrayAddressPolicy policy,
                               SourceRange range);
/** Return guaranteed alignment after byte offsets or scalar-stride indexing. */
std::optional<uint64_t> resolved_address_alignment(
    const ResolvedAddress& address);
struct ResolvedOperandLayoutTag {
  uint16_t value = 0;
  bool operator==(const ResolvedOperandLayoutTag&) const = default;
};
using RegOrImm = std::variant<ResolvedRegisterRef, ResolvedImmediate>;
/** Owned video register or constant operand, retaining written selection and minus. */
struct ResolvedVideoOperand {
  /** Owned source value and its identifier or literal location. */
  WithLocs<RegOrImm> value;
  /** Absence retains an omitted selector instead of inventing explicit syntax. */
  std::optional<WithLocs<VideoSelector>> selector;
  /** Written arithmetic minus, meaningful only for supported vmad sources. */
  bool negated{};
  /** Independent minus location; absent for a non-negated source. */
  std::optional<SourceRange> minus_range;
};

/** Read-only braced source lanes retaining register identities or typed values. */
struct ResolvedValueVector {
  /** Ordered lane payload; WithLocs owns corresponding source ranges. */
  std::vector<RegOrImm> elements;
  bool operator==(const ResolvedValueVector&) const = default;
};
/** One preserved surface coordinate, with its interpretation and source range. */
struct ResolvedSurfaceLane {
  /** Scalar register identity or evaluated typed immediate. */
  RegOrImm value;
  /** Spatial s32, array-layer u32, or ignored padding interpretation. */
  SurfaceLaneRole role{};
  /** Original lane location, retained independently of syntax lifetime. */
  SourceRange range;
  /** Compare the exact source payload. */
  bool operator==(const ResolvedSurfaceLane&) const = default;
};
/** Surface access with one direct declaration or indirect u64-compatible handle. */
struct ResolvedSurfaceAccess {
  /** Surface identity, never an ordinary memory address. */
  ResolvedOpaqueResourceRef surface;
  /** Ordered lanes including ignored padding. */
  std::vector<ResolvedSurfaceLane> coordinates;
  /** Whether source coordinates were enclosed in braces. */
  bool coordinates_packed = true;
  /** Access brackets are required and retained after syntax release. */
  bool bracketed = true;
  /** Delimiter provenance for source association and diagnostics. */
  SourceRange left_bracket_range;
  std::vector<SourceRange> comma_ranges;
  SourceRange right_bracket_range;
  /** Compare the exact source payload. */
  bool operator==(const ResolvedSurfaceAccess&) const = default;
};
/** Surface query resource and required bracket provenance. */
struct ResolvedSurfaceQueryResource {
  /** One direct surface declaration or indirect scalar carrier. */
  ResolvedOpaqueResourceRef resource;
  /** Simple square brackets must remain present after mutation. */
  bool bracketed = true;
  /** Delimiter locations remain valid without the syntax tree. */
  SourceRange left_bracket_range;
  SourceRange right_bracket_range;
  /** Compare the exact source payload. */
  bool operator==(const ResolvedSurfaceQueryResource&) const = default;
};

/** One owned coordinate lane with role and source location retained. */
struct ResolvedTextureLane {
  /** Register declaration type is retained even when role interpretation differs. */
  RegOrImm value;
  /** Array/sample/spatial/ignored meaning selected by geometry metadata. */
  TextureLaneRole role{};
  SourceRange range;
  bool operator==(const ResolvedTextureLane&) const = default;
};

/** Texture access operand with direct or indirect resource identities. */
struct ResolvedTextureAccess {
  /** Texture identity, required on every tex/tld4 access. */
  ResolvedOpaqueResourceRef texture;
  /** Explicit independent sampler; absence is preserved. */
  std::optional<ResolvedOpaqueResourceRef> sampler;
  /** Ordered coordinate lanes, including written ignored padding. */
  std::vector<ResolvedTextureLane> coordinates;
  /** Selected spatial coordinate type; array/sample lanes remain u32 roles. */
  ScalarType coordinate_type = ScalarType::Invalid;
  /** Square-bracket omission is retained for compatibility source forms. */
  bool bracketed = true;
  /** True when a coordinate was spelled as a brace pack. */
  bool coordinates_packed = true;
  /** Bracket/comma provenance, empty when delimiters were omitted. */
  SourceRange left_bracket_range;
  std::vector<SourceRange> comma_ranges;
  SourceRange right_bracket_range;
  bool operator==(const ResolvedTextureAccess&) const = default;
};

/** One query resource reference with source bracket presence retained. */
struct ResolvedTextureQueryResource {
  ResolvedOpaqueResourceRef resource;
  bool bracketed = true;
  SourceRange left_bracket_range;
  SourceRange right_bracket_range;
  bool operator==(const ResolvedTextureQueryResource&) const = default;
};
/** Owned 32-bit Tensor Memory address source, without allocation provenance. */
struct TensorMemoryAddress {
  /** Register or converted immediate; locations live in its WithLocs owner. */
  RegOrImm value;
  /** True only when the source spelled required simple brackets. */
  bool bracketed = false;
  /** Compare the exact source payload after syntax release. */
  bool operator==(const TensorMemoryAddress&) const = default;
};
/** Evaluated integer source for a split Tensor Memory transfer. */
enum class TcgenIntegerSourceKind : uint8_t { Signed, Unsigned };
/** Owned split offset without a fabricated use conversion. */
struct TcgenHalfSplitOffset {
  /** Two's-complement evaluated integer source bits. */
  uint64_t source_bits = 0;
  /** Whether the source literal is signed or unsigned. */
  TcgenIntegerSourceKind source_kind = TcgenIntegerSourceKind::Signed;
  /** Compare the exact source payload. */
  bool operator==(const TcgenHalfSplitOffset&) const = default;
};
/** Owned block-scale selector pair in byte-ID then thread-ID order. */
struct ResolvedMatrixScaleSelector {
  RegOrImm byte_id;
  RegOrImm thread_id;
  /** Compare both selector values after syntax release. */
  bool operator==(const ResolvedMatrixScaleSelector&) const = default;
};
struct ResolvedTensorCoordinate {
  std::vector<RegOrImm> elements;
  bool operator==(const ResolvedTensorCoordinate&) const = default;
};
/** Owned unsigned-16 instruction-use information for an im2col read. */
struct ResolvedTensorIm2colInfo {
  /** W/H/D offsets or W halo/offset; values retain original literal provenance. */
  std::vector<RegOrImm> elements;
  /** Full brace-pack range, independent of individual element ranges. */
  SourceRange pack_range;
  bool operator==(const ResolvedTensorIm2colInfo&) const = default;
};
/** Semantic role of one im2col information element. */
enum class TensorIm2colInfoRole : uint8_t {
  OffsetW,
  OffsetH,
  OffsetD,
  Halo,
  Offset
};
/** Owned descriptor pointer with its storage identity and address metadata. */
struct ResolvedTensorMapRef {
  ResolvedAddress address;
  SourceRange range;
};
/** One owned composite descriptor and coordinate operand. */
struct ResolvedTensorOperand {
  ResolvedTensorMapRef tensor_map;
  ResolvedTensorCoordinate coordinates;
  TensorRank rank = TensorRank::One;
  TensorAccessMode mode = TensorAccessMode::Tiled;
  /** Coordinate element ranges, independent of the enclosing operand range. */
  std::vector<SourceRange> coordinate_ranges;
};

/** Return a role only when the owned mode, rank, and info arity agree. */
std::optional<TensorIm2colInfoRole> tensor_im2col_info_role(
    const ResolvedTensorOperand& tensor, const ResolvedTensorIm2colInfo& info,
    size_t index);

/** Return a role only for a rank-two, five-coordinate gather/scatter tensor. */
std::optional<TensorGatherScatterCoordinateRole>
tensor_gather_scatter_coordinate_role(const ResolvedTensorOperand& tensor,
                                      size_t index) noexcept;

/** Encoded field identity of a tiled tensor-map replacement. */
enum class TensorMapReplaceField : uint8_t {
  GlobalAddress,
  Rank,
  BoxDim,
  GlobalDim,
  GlobalStride,
  ElementStride,
  Elemtype,
  InterleaveLayout,
  SwizzleMode,
  SwizzleAtomicity,
  FillMode,
};

/** Table 33 element encoding; code 15 has direction-dependent interpretation. */
enum class TensorMapElementType : uint8_t {
  U8,
  U16,
  U32,
  S32,
  U64,
  S64,
  F16,
  F32,
  F32Ftz,
  F64,
  BF16,
  TF32,
  TF32Ftz,
  B4x16,
  B4x16P64,
  B6x16P32OrB6p2x16,
};
/** Table 33 interleave-layout encoding. */
enum class TensorMapInterleaveLayout : uint8_t { None, Bytes16, Bytes32 };
/** Table 33 swizzle-mode encoding. */
enum class TensorMapSwizzleMode : uint8_t {
  None,
  Bytes32,
  Bytes64,
  Bytes128,
  Bytes96,
};
/** Table 33 swizzle-atomicity encoding. */
enum class TensorMapSwizzleAtomicity : uint8_t {
  Bytes16,
  Bytes32,
  Bytes32Flip8,
  Bytes64,
};
/** Table 33 out-of-bounds fill encoding. */
enum class TensorMapFillMode : uint8_t { Zero, OobNan };

/** One exact field/code association shared by public projections and checks. */
struct TensorMapEncodedCode {
  /** Encoded descriptor field, independent of descriptor contents. */
  TensorMapReplaceField field;
  /** Original integer source code, before any operand-width conversion. */
  uint8_t code;
  /** Field-specific interpretation of that code. */
  std::variant<TensorMapElementType, TensorMapInterleaveLayout,
               TensorMapSwizzleMode, TensorMapSwizzleAtomicity,
               TensorMapFillMode>
      value;
};

/** Closed PTX 9.3 Table 33 code set; one row is one valid field/code pair. */
inline constexpr std::array<TensorMapEncodedCode, 30> tensor_map_encoded_codes{{
    {TensorMapReplaceField::Elemtype, 0, TensorMapElementType::U8},
    {TensorMapReplaceField::Elemtype, 1, TensorMapElementType::U16},
    {TensorMapReplaceField::Elemtype, 2, TensorMapElementType::U32},
    {TensorMapReplaceField::Elemtype, 3, TensorMapElementType::S32},
    {TensorMapReplaceField::Elemtype, 4, TensorMapElementType::U64},
    {TensorMapReplaceField::Elemtype, 5, TensorMapElementType::S64},
    {TensorMapReplaceField::Elemtype, 6, TensorMapElementType::F16},
    {TensorMapReplaceField::Elemtype, 7, TensorMapElementType::F32},
    {TensorMapReplaceField::Elemtype, 8, TensorMapElementType::F32Ftz},
    {TensorMapReplaceField::Elemtype, 9, TensorMapElementType::F64},
    {TensorMapReplaceField::Elemtype, 10, TensorMapElementType::BF16},
    {TensorMapReplaceField::Elemtype, 11, TensorMapElementType::TF32},
    {TensorMapReplaceField::Elemtype, 12, TensorMapElementType::TF32Ftz},
    {TensorMapReplaceField::Elemtype, 13, TensorMapElementType::B4x16},
    {TensorMapReplaceField::Elemtype, 14, TensorMapElementType::B4x16P64},
    {TensorMapReplaceField::Elemtype, 15,
     TensorMapElementType::B6x16P32OrB6p2x16},
    {TensorMapReplaceField::InterleaveLayout, 0,
     TensorMapInterleaveLayout::None},
    {TensorMapReplaceField::InterleaveLayout, 1,
     TensorMapInterleaveLayout::Bytes16},
    {TensorMapReplaceField::InterleaveLayout, 2,
     TensorMapInterleaveLayout::Bytes32},
    {TensorMapReplaceField::SwizzleMode, 0, TensorMapSwizzleMode::None},
    {TensorMapReplaceField::SwizzleMode, 1, TensorMapSwizzleMode::Bytes32},
    {TensorMapReplaceField::SwizzleMode, 2, TensorMapSwizzleMode::Bytes64},
    {TensorMapReplaceField::SwizzleMode, 3, TensorMapSwizzleMode::Bytes128},
    {TensorMapReplaceField::SwizzleMode, 4, TensorMapSwizzleMode::Bytes96},
    {TensorMapReplaceField::SwizzleAtomicity, 0,
     TensorMapSwizzleAtomicity::Bytes16},
    {TensorMapReplaceField::SwizzleAtomicity, 1,
     TensorMapSwizzleAtomicity::Bytes32},
    {TensorMapReplaceField::SwizzleAtomicity, 2,
     TensorMapSwizzleAtomicity::Bytes32Flip8},
    {TensorMapReplaceField::SwizzleAtomicity, 3,
     TensorMapSwizzleAtomicity::Bytes64},
    {TensorMapReplaceField::FillMode, 0, TensorMapFillMode::Zero},
    {TensorMapReplaceField::FillMode, 1, TensorMapFillMode::OobNan},
}};

/** Decode an exact source code only when the owned .b32 value is consistent. */
constexpr std::optional<TensorMapEncodedCode> tensor_map_encoded_code(
    TensorMapReplaceField field, const ResolvedImmediate& immediate) noexcept {
  if (immediate.type != ScalarType::B32 || !immediate.integer_source_bits ||
      immediate.is_negative ||
      immediate.bits != (*immediate.integer_source_bits & uint64_t{0xffffffff}))
    return std::nullopt;
  for (const auto& entry : tensor_map_encoded_codes)
    if (entry.field == field && entry.code == *immediate.integer_source_bits)
      return entry;
  return std::nullopt;
}

/** Project one field3 source code into its closed, field-specific enum. */
template <typename Value>
  requires(std::same_as<Value, TensorMapElementType> ||
           std::same_as<Value, TensorMapInterleaveLayout> ||
           std::same_as<Value, TensorMapSwizzleMode> ||
           std::same_as<Value, TensorMapSwizzleAtomicity> ||
           std::same_as<Value, TensorMapFillMode>)
constexpr std::optional<Value> project_tensor_map_encoded_value(
    TensorMapReplaceField field, const ResolvedImmediate& immediate) noexcept {
  const auto decoded = tensor_map_encoded_code(field, immediate);
  if (!decoded)
    return std::nullopt;
  if (const auto* value = std::get_if<Value>(&decoded->value))
    return *value;
  return std::nullopt;
}
struct ResolvedShflSyncDestination {
  std::optional<WithLoc<ResolvedRegisterRef>> data;
  std::optional<WithLoc<ResolvedPredicate>> predicate;
  bool operator==(const ResolvedShflSyncDestination&) const = default;
};
struct ResolvedPredicatePair {
  ResolvedPredicate first;
  ResolvedPredicate second;
  bool operator==(const ResolvedPredicatePair&) const = default;
};
/** A predicate destination that may intentionally discard its result. */
struct ResolvedPredicateOrSink {
  /** Present only when source did not spell the PTX discard sink `_`. */
  std::optional<ResolvedPredicate> predicate;
  bool operator==(const ResolvedPredicateOrSink&) const = default;
};
/** A two-lane predicate destination that may discard exactly one lane. */
struct ResolvedPredicatePairOrSink {
  /** First comparison result, absent only for the PTX discard sink `_`. */
  std::optional<ResolvedPredicate> first;
  /** Complement comparison result, absent only for the PTX discard sink `_`. */
  std::optional<ResolvedPredicate> second;
  bool operator==(const ResolvedPredicatePairOrSink&) const = default;
};
using ResolvedMovSource =
    std::variant<ResolvedRegisterRef, ResolvedImmediate,
                 ResolvedSpecialRegisterRef, ResolvedFunctionRef,
                 ResolvedSymbolRef, ResolvedAddress, ResolvedOpaqueSymbolRef>;
/** Cache-policy register distinguished from a source-size register. */
struct ResolvedCpAsyncCachePolicy {
  /** Bound 64-bit register that carries the L2 eviction policy. */
  ResolvedRegisterRef register_ref;
  bool operator==(const ResolvedCpAsyncCachePolicy&) const = default;
};
/** Fourth non-bulk copy operand: byte count, ignore predicate, or L2 policy. */
using ResolvedCpAsyncSourceControl =
    std::variant<ResolvedRegisterRef, ResolvedImmediate, ResolvedPredicate,
                 ResolvedCpAsyncCachePolicy>;
}  // namespace ptx_frontend::resolved_ir
