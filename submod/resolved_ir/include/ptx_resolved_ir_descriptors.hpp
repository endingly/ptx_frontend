#pragma once

#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>

namespace ptx_frontend::resolved_ir::check_end {
using OperandShape = checker::OperandShape;
using OperandRole = checker::OperandRole;
using OperandAccess = checker::OperandAccess;
using OperandTypeExpressionKind = checker::OperandTypeExpressionKind;
using ImmediateConversionPolicy = checker::ImmediateConversionPolicy;
using TypeExpressionDescriptor = checker::TypeExpressionDescriptor;
enum class OperandSyntaxShape : uint16_t {
  Identifier = 1 << 0,
  Immediate = 1 << 1,
  Address = 1 << 2,
  VectorPack = 1 << 3,
  VectorMember = 1 << 4,
  Predicate = 1 << 5,
  Group = 1 << 6,
  CallTarget = 1 << 7,
  CallTargetSet = 1 << 8,
  BranchTarget = 1 << 9,
  BranchTargetSet = 1 << 10,
  RegisterPredicatePair = 1 << 11,
  NegatedImmediate = 1 << 12
};
constexpr OperandSyntaxShape operator|(OperandSyntaxShape lhs,
                                       OperandSyntaxShape rhs) {
  using Underlying = std::underlying_type_t<OperandSyntaxShape>;
  return static_cast<OperandSyntaxShape>(static_cast<Underlying>(lhs) |
                                         static_cast<Underlying>(rhs));
}
enum class OperandPresence : uint8_t { Required, Optional };
enum class ResolvedValueKind : uint8_t {
  Bool,
  ScalarType,
  RoundingMode,
  ComparisonOperator,
  BooleanOperator,
  CacheOperator,
  EvictionPriority,
  PrefetchSize,
  MemoryConsistency,
  MemoryScope,
  VectorArity,
  MemoryStateSpace,
  MbarrierPhaseType,
  MbarrierLayout,
  AsyncProxyKind,
  ProxyKindPair,
  Register,
  Predicate,
  PredicateOrSink,
  PredicateSource,
  Immediate,
  RegOrImm,
  RegisterOrSink,
  ShflDestination,
  PredicatePair,
  PredicatePairOrSink,
  MovSource,
  VectorRegister,
  VectorSpecialRegister,
  BranchTarget,
  SpecialRegister,
  Symbol,
  Address,
  RegisterVector,
  TensorCoordinate,
  DirectCallTarget,
  IndirectCallee,
  BranchTargetSet,
  CallReturnParameter,
  CallArguments,
  MbarrierStateToken
};
/** Read-only source-syntax contract for one generated operand slot. */
struct SyntaxOperandSlotDescriptor {
  OperandSyntaxShape allowed_shapes;
  OperandPresence presence;
  /** Descriptor-domain identity; source has no comparable token tag. */
  std::string_view type_tag{};
  uint8_t minimum_elements = 0;
  uint8_t maximum_elements = 0;
  OperandSyntaxShape allowed_element_shapes{};
};
enum class OperandLayoutKind : uint8_t { Flat, Call, IndirectCall };
struct SyntaxOperandLayoutDescriptor {
  std::string_view layout_id;
  OperandLayoutKind kind;
  std::span<const SyntaxOperandSlotDescriptor> slots;
};
enum class PresenceRequirement { Absent, Optional, Required };
/** Read-only accepted values and presence rule for one modifier slot. */
struct SyntaxModifierDescriptor {
  std::span<const std::string_view> allowed_values;
  PresenceRequirement presence;
  std::string_view kind_id;
  bool check(std::string modifier_str) const;
};
struct SyntaxModifierOrderDescriptor {
  std::span<const SyntaxModifierDescriptor> modifiers;
};
/** Complete syntax form accepted for a generated instruction variant. */
struct SyntaxVariantDescriptor {
  std::string_view variant_name;
  std::span<const SyntaxModifierDescriptor> modifiers;
  std::span<const SyntaxOperandLayoutDescriptor> operand_layouts;
  std::span<const SyntaxModifierOrderDescriptor> modifier_order_aliases{};
  int32_t get_required_modifier_num() const;
};
struct SyntaxInstructionDescriptor {
  std::string_view Opcode_name;
  std::span<const SyntaxVariantDescriptor> variants;
};
struct ResolvedFieldDescriptor {
  std::string_view field_id;
  ResolvedValueKind value_kind;
};
enum class ResolvedModifierDefaultKind : uint8_t {
  None,
  Bool,
  ScalarType,
  RoundingMode,
  CacheOperator,
  EvictionPriority,
  PrefetchSize,
  MemoryConsistency,
  MemoryScope,
  MemoryStateSpace,
  MbarrierPhaseType,
  MbarrierLayout,
  AsyncProxyKind,
  ProxyKindPair
};
/** Typed generated default supplied when a modifier slot is omitted. */
struct ResolvedModifierDefaultDescriptor {
  ResolvedModifierDefaultKind kind = ResolvedModifierDefaultKind::None;
  bool bool_value = false;
  base::ScalarType scalar_type = base::ScalarType::Invalid;
  base::RoundingMode rounding_mode = base::RoundingMode::Invalid;
  base::CacheOperator cache_operator = base::CacheOperator::Unspecified;
  /** Typed omission or selected value for an eviction-priority slot. */
  base::EvictionPriority eviction_priority = base::EvictionPriority::Invalid;
  base::PrefetchSize prefetch_size = base::PrefetchSize::None;
  MemoryStateSpace memory_state_space = MemoryStateSpace::Invalid;
  base::MbarrierPhaseType mbarrier_phase_type =
      base::MbarrierPhaseType::Primary;
  base::MbarrierLayout mbarrier_layout = base::MbarrierLayout::V0;
  base::AsyncProxyKind async_proxy_kind = base::AsyncProxyKind::Async;
  base::ProxyKindPair proxy_kind_pair = base::ProxyKindPair::TensormapToGeneric;
  base::MemoryConsistency memory_consistency = base::MemoryConsistency::Omitted;
  base::MemoryScope memory_scope = base::MemoryScope::None;
};
struct ResolvedModifierBindingDescriptor {
  std::string_view source_kind_id;
  std::string_view target_field_id;
  ResolvedModifierDefaultDescriptor default_value;
};
using ResolvedOperandBindingDescriptor = checker::OperandDescriptor;
/** Read-only resolved-field and binding contract for one operand layout. */
struct ResolvedOperandLayoutDescriptor {
  std::string_view layout_id;
  std::span<const ResolvedFieldDescriptor> fields;
  std::span<const ResolvedOperandBindingDescriptor> bindings;
};
struct ResolvedVariantDescriptor {
  std::string_view variant_name;
  /** Variant-local implicit state effect; None for ordinary arithmetic. */
  ConditionCodeEffect condition_code_effect = ConditionCodeEffect::None;
  std::span<const ResolvedFieldDescriptor> fields;
  std::span<const ResolvedModifierBindingDescriptor> modifier_bindings;
  std::span<const ResolvedOperandLayoutDescriptor> operand_layouts;
};
struct ResolvedInstructionDescriptor {
  std::string_view opcode_name;
  std::span<const ResolvedVariantDescriptor> variants;
};
}  // namespace ptx_frontend::resolved_ir::check_end
