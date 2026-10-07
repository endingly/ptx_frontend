#pragma once

#include <array>
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

namespace ptx_frontend::resolved_ir {

struct ResolvedRegisterRef;
struct ResolvedTensorOperand;
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
/** Parameter role independent of binding-layer enum types. */
enum class ParameterDirection : uint8_t { None, Input, Return, CallArgument };
/** PTX 9.3 subqualifier retained for a .param memory access. */
enum class ParameterAddressQualifier : uint8_t { Default, Entry, Function };

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

enum class OperandShape : uint16_t {
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
  TensorOperand = 1 << 15
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
  size_t vector_sink_payload_bits = 0;
  /** Optional declared lane-type domain, independent of instruction suffix. */
  std::span<const base::ScalarType> allowed_register_types;
  /** Enforce one integer-or-float family per vector; bit lanes are neutral. */
  bool require_uniform_register_family = false;
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
  /** Present only for the mask coupled to tensor cluster multicast. */
  std::optional<TensorCtaMaskRole> tensor_cta_mask_role;
  /** Independent conversion contract; type provenance does not select it. */
  ImmediateConversionPolicy immediate_conversion_policy =
      ImmediateConversionPolicy::Narrow;
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
  std::optional<TcgenDataMovementShape> tcgen_shape;
  std::optional<TcgenRepeat> tcgen_repeat;
  std::optional<TcgenReductionOp> tcgen_reduction_op;
  std::optional<TcgenWaitClass> tcgen_wait_class;
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
  /** Declaration identity; absent for declaration-free standalone operands. */
  std::optional<binding::SymbolId> register_symbol_id;
  /** Resolved register category, independent of an unknown declaration type. */
  std::optional<ResolvedRegisterClass> register_class;
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
  /** Composite tensor coordinates with a statically negative immediate. */
  bool tensor_has_negative_immediate = false;
  /** Composite tensor rank encoded by the owned operand. */
  std::optional<TensorRank> tensor_rank;
  /** Borrowed owned payload for rank, provenance, and signedness checks. */
  const ResolvedTensorOperand* tensor_operand = nullptr;
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
  TcgenDataMovementShape,
  TcgenRepeat,
  TcgenReductionOp,
  TcgenWaitClass,
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
  TcgenDataMovementShape tcgen_shape = TcgenDataMovementShape::S32x32b;
  TcgenRepeat tcgen_repeat = TcgenRepeat::X1;
  TcgenReductionOp tcgen_reduction_op = TcgenReductionOp::Min;
  TcgenWaitClass tcgen_wait_class = TcgenWaitClass::Load;
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
  TcgenDataMovementShape tcgen_shape = TcgenDataMovementShape::S32x32b;
  TcgenRepeat tcgen_repeat = TcgenRepeat::X1;
  TcgenReductionOp tcgen_reduction_op = TcgenReductionOp::Min;
  TcgenWaitClass tcgen_wait_class = TcgenWaitClass::Load;
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
  TcgenDataMovementShape tcgen_shape = TcgenDataMovementShape::S32x32b;
  TcgenRepeat tcgen_repeat = TcgenRepeat::X1;
  TcgenReductionOp tcgen_reduction_op = TcgenReductionOp::Min;
  TcgenWaitClass tcgen_wait_class = TcgenWaitClass::Load;
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
enum class ResolvedRegisterClass : uint8_t { General, Predicate };
struct ResolvedRegisterRef {
  std::string spelling;
  ResolvedRegisterClass register_class;
  std::optional<uint32_t> index;
  std::optional<binding::SymbolId> symbol_id;
  std::optional<uint32_t> parameterized_index;
  std::optional<ScalarType> declared_type;
  std::optional<uint8_t> vector_width;
  bool operator==(const ResolvedRegisterRef&) const = default;
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
struct ResolvedRegisterVector {
  std::vector<std::optional<ResolvedRegisterRef>> elements;
  bool operator==(const ResolvedRegisterVector&) const = default;
};
struct ResolvedPredicate {
  ResolvedRegisterRef register_ref;
  bool negated{};
  bool operator==(const ResolvedPredicate&) const = default;
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
struct ResolvedVectorRegisterRef {
  ResolvedRegisterRef register_ref;
  bool operator==(const ResolvedVectorRegisterRef&) const = default;
};
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
  /** A symbol base can carry contextual availability without value equality. */
  bool operator==(const ResolvedAddress&) const = delete;
};
struct ResolvedOperandLayoutTag {
  uint16_t value = 0;
  bool operator==(const ResolvedOperandLayoutTag&) const = default;
};
using RegOrImm = std::variant<ResolvedRegisterRef, ResolvedImmediate>;
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
                 ResolvedSymbolRef, ResolvedAddress>;
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
