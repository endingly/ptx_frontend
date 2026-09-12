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
/** Semantic value of a PTX vector-arity modifier such as ``.v2``. */
enum class VectorArity : uint8_t { Invalid, V2, V4, V8 };
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
using base::ProxyKindPair;
using base::RoundingMode;
using base::ScalarType;

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
  PredicatePair = 1 << 14
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
inline constexpr size_t kMaxAvailabilityClauses = 5;
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
/** Fields needed to derive one generated natural-address-alignment rule. */
struct AddressAlignmentConstraint {
  std::span<const std::string_view> address_field_ids;
  std::string_view type_field_id;
  std::string_view vector_field_id;
  std::string_view immediate_operand_field_id;
  uint64_t alignment = 0;
};
enum class VectorTypePolicy : uint8_t { Aggregate, Element };
enum class MbarrierStateTokenForm : uint8_t { Register, RegisterOrSink, Sink };
inline constexpr size_t kMaxRegisterVectorPayloadBits = 256;
inline constexpr size_t kMaxOperandElements = 64;
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
  bool allow_destination_sink = false;
  bool allow_predicate_sink = false;
  MbarrierStateTokenForm mbarrier_state_token_form =
      MbarrierStateTokenForm::Register;
  AvailabilityDescriptor sink_availability;
  bool allow_function_symbol = false;
  std::string_view type_tag{};
  uint8_t minimum_elements = 0;
  uint8_t maximum_elements = 0;
  OperandShape allowed_element_shapes{};
  /** Empty means no static state-space restriction. */
  std::span<const AddressStateSpaceDescriptor> allowed_address_state_spaces;
  std::string_view state_space_modifier_field_id{};
  ParameterAddressConstraint parameter_constraint;
  /** Independent conversion contract; type provenance does not select it. */
  ImmediateConversionPolicy immediate_conversion_policy =
      ImmediateConversionPolicy::Narrow;
};
struct FieldView {
  std::string_view field_id;
  std::optional<bool> bool_value;
  std::optional<CacheOperator> cache_operator;
  std::optional<EvictionPriority> eviction_priority;
  std::optional<ScalarType> scalar_type;
  std::optional<ComparisonOperator> comparison_operator;
  std::optional<BooleanOperator> boolean_operator;
  std::optional<VectorArity> vector_arity;
  std::optional<MemoryStateSpace> memory_state_space;
  std::optional<MemoryConsistency> memory_consistency;
  std::optional<MemoryScope> memory_scope;
  std::optional<MbarrierPhaseType> mbarrier_phase_type;
  std::optional<MbarrierLayout> mbarrier_layout;
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
  bool is_sink = false;
  std::array<ScalarType, 2> predicate_pair_types{};
  std::optional<ScalarType> special_register_type;
  std::optional<base::SpecialRegisterId> special_register_id;
  std::optional<MemoryStateSpace> address_state_space;
  std::optional<uint64_t> address_alignment;
  EnclosingFunctionKind enclosing_function_kind =
      EnclosingFunctionKind::Unknown;
  ParameterDirection parameter_direction = ParameterDirection::None;
  ParameterAddressQualifier parameter_qualifier =
      ParameterAddressQualifier::Default;
  std::array<ScalarType, kMaxOperandElements> vector_element_types{};
  std::array<OperandShape, kMaxOperandElements> vector_element_shapes{};
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
struct OperandLayoutDescriptor {
  std::string_view layout_name;
  AvailabilityDescriptor availability;
};
enum class ModifierValueKind : uint8_t {
  Bool,
  ScalarType,
  RoundingMode,
  ComparisonOperator,
  BooleanOperator,
  CacheOperator,
  EvictionPriority,
  VectorArity,
  MemoryStateSpace,
  MemoryConsistency,
  MemoryScope,
  MbarrierPhaseType,
  MbarrierLayout,
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
  BooleanOperator boolean_operator = BooleanOperator::Invalid;
  CacheOperator cache_operator = CacheOperator::Unspecified;
  EvictionPriority eviction_priority = EvictionPriority::Invalid;
  VectorArity vector_arity = VectorArity::Invalid;
  MemoryStateSpace memory_state_space = MemoryStateSpace::Invalid;
  MemoryConsistency memory_consistency = MemoryConsistency::Omitted;
  MemoryScope memory_scope = MemoryScope::None;
  MbarrierPhaseType mbarrier_phase_type = MbarrierPhaseType::Primary;
  MbarrierLayout mbarrier_layout = MbarrierLayout::V0;
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
  BooleanOperator boolean_operator = BooleanOperator::Invalid;
  CacheOperator cache_operator = CacheOperator::Unspecified;
  EvictionPriority eviction_priority = EvictionPriority::Invalid;
  VectorArity vector_arity = VectorArity::Invalid;
  MemoryStateSpace memory_state_space = MemoryStateSpace::Invalid;
  MemoryConsistency memory_consistency = MemoryConsistency::Omitted;
  MemoryScope memory_scope = MemoryScope::None;
  MbarrierPhaseType mbarrier_phase_type = MbarrierPhaseType::Primary;
  MbarrierLayout mbarrier_layout = MbarrierLayout::V0;
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
  BooleanOperator boolean_operator = BooleanOperator::Invalid;
  CacheOperator cache_operator = CacheOperator::Unspecified;
  EvictionPriority eviction_priority = EvictionPriority::Invalid;
  VectorArity vector_arity = VectorArity::Invalid;
  MemoryStateSpace memory_state_space = MemoryStateSpace::Invalid;
  MemoryConsistency memory_consistency = MemoryConsistency::Omitted;
  MemoryScope memory_scope = MemoryScope::None;
  MbarrierPhaseType mbarrier_phase_type = MbarrierPhaseType::Primary;
  MbarrierLayout mbarrier_layout = MbarrierLayout::V0;
  AsyncProxyKind async_proxy_kind = AsyncProxyKind::Async;
  ProxyKindPair proxy_kind_pair = ProxyKindPair::TensormapToGeneric;
  bool is_present = false;
  std::span<const SourceRange> locations;
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
  struct MemoryConsistencyDescriptor {
    std::string_view semantics_field_id;
    std::string_view scope_field_id;
    std::string_view mmio_field_id;
    std::string_view cache_field_id;
    std::string_view address_field_id;
    std::string_view state_space_field_id;
  } memory_consistency;
  std::span<const AddressAlignmentConstraint> address_alignments;
  struct MemoryVectorDescriptor {
    std::string_view type_field_id;
    std::string_view vector_field_id;
    std::string_view address_field_id;
    std::string_view state_space_field_id;
    AvailabilityDescriptor availability;
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
using base::ProxyKindPair;
using base::RoundingMode;
using base::ScalarType;
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
using ResolvedPredicateSource =
    std::variant<ResolvedPredicate, ResolvedSpecialRegisterRef>;
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
  bool operator==(const ResolvedFunctionRef&) const = default;
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
  bool operator==(const ResolvedSymbolRef&) const = default;
};
enum class ResolvedAddressOffsetOperator : uint8_t { Add, Subtract };
struct ResolvedAddressOffset {
  ResolvedAddressOffsetOperator operation = ResolvedAddressOffsetOperator::Add;
  ResolvedImmediate value;
  bool operator==(const ResolvedAddressOffset&) const = default;
};
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
  bool operator==(const ResolvedAddress&) const = default;
};
struct ResolvedOperandLayoutTag {
  uint16_t value = 0;
  bool operator==(const ResolvedOperandLayoutTag&) const = default;
};
using RegOrImm = std::variant<ResolvedRegisterRef, ResolvedImmediate>;
struct ResolvedTensorCoordinate {
  std::vector<RegOrImm> elements;
  bool operator==(const ResolvedTensorCoordinate&) const = default;
};
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
using ResolvedMovSource =
    std::variant<ResolvedRegisterRef, ResolvedImmediate,
                 ResolvedSpecialRegisterRef, ResolvedFunctionRef,
                 ResolvedSymbolRef, ResolvedAddress>;
}  // namespace ptx_frontend::resolved_ir
