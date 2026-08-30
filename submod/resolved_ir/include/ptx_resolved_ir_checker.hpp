#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/base/ptx_special_register.hpp>
#include <ptx_frontend/base/ptx_target.hpp>
#include <ptx_frontend/common/source_loc.hpp>

namespace ptx_frontend::resolved_ir {

/** State-space identity used by resolved modifiers and effective addresses. */
enum class MemoryStateSpace : uint8_t {
  Invalid,
  Generic,
  Global,
  Shared,
  Local,
  Parameter,
  Constant,
};

/** Semantic value of a PTX vector-arity modifier such as ``.v2``. */
enum class VectorArity : uint8_t {
  Invalid,
  V2,
  V4,
  V8,
};

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

/** Enclosing function provenance retained only for resolved memory addresses. */
enum class EnclosingFunctionKind : uint8_t {
  Unknown,
  Entry,
  Device,
};

/** Parameter role independent of binding-layer enum types. */
enum class ParameterDirection : uint8_t {
  None,
  Input,
  Return,
  CallArgument,
};

/** PTX 9.3 subqualifier retained for a .param memory access. */
enum class ParameterAddressQualifier : uint8_t {
  Default,
  Entry,
  Function,
};

namespace checker {
using base::ScalarType;
using base::RoundingMode;
using base::ComparisonOperator;
using base::BooleanOperator;
using base::CacheOperator;
using base::EvictionPriority;
using base::MemoryConsistency;
using base::MemoryScope;
using base::MbarrierLayout;
using base::MbarrierPhaseType;
namespace detail {

/** Combine variant-specific lambdas into the visitor accepted by std::visit. */
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
  ThreadCount,
};

enum class OperandAccess : uint8_t {
  Read,
  Write,
  ReadWrite,
  Control,
};

/** A compile-time-normalized scalar-type source for one operand. */
enum class OperandTypeExpressionKind : uint8_t {
  None,
  FixedScalar,
  ModifierField,
};

/** A PTX ISA version used by both checker descriptors and compilation targets. */
struct PtxVersion {
  uint16_t major = 0;
  uint16_t minor = 0;

  constexpr auto operator<=>(const PtxVersion&) const = default;
};

inline constexpr size_t kMaxAvailabilityClauses = 4;
inline constexpr size_t kMaxAvailabilityCapabilities = 4;

/** One AND-clause in a bounded generated availability expression. */
struct AvailabilityClause {
  PtxVersion minimum_ptx_version{};
  uint32_t minimum_sm_version = 0;
  bool has_exact_target = false;
  base::TargetArchitecture exact_target_architecture{};
  base::TargetFlavor exact_target_flavor = base::TargetFlavor::Generic;
  std::string_view required_family{};
  // ponytail: fixed DNF capacity; raise schema and this bound together if needed.
  std::array<std::string_view, kMaxAvailabilityCapabilities> capabilities{};
  uint8_t capability_count = 0;
};

/** Target requirements attached to a variant, layout, modifier, or value. */
struct AvailabilityDescriptor {
  PtxVersion minimum_ptx_version{};
  uint32_t minimum_sm_version = 0;
  // YAML `family`: minimum family-specific feature target.
  std::string_view required_family{};
  std::array<AvailabilityClause, kMaxAvailabilityClauses> any_of{};
  uint8_t any_of_count = 0;
};

/**
 * Type information generated from YAML rather than parsed at C++ runtime.
 *
 * ``modifier_field_id`` names the resolved modifier field for
 * ``modifier(name)`` expressions.  It is otherwise empty.
 */
struct TypeExpressionDescriptor {
  OperandTypeExpressionKind kind = OperandTypeExpressionKind::None;
  ScalarType fixed_scalar_type = ScalarType::Invalid;
  std::string_view modifier_field_id{};
};

/** One statically accepted effective address space and its target requirement. */
struct AddressStateSpaceDescriptor {
  MemoryStateSpace state_space = MemoryStateSpace::Invalid;
  AvailabilityDescriptor availability;
};

/** Direction and function-specific target requirement for explicit .param. */
struct ParameterAddressConstraint {
  ParameterDirection direction = ParameterDirection::None;
  AvailabilityDescriptor function_availability;
};

/** Fields needed to derive one generated address-alignment requirement. */
struct AddressAlignmentConstraint {
  std::span<const std::string_view> address_field_ids;
  std::string_view type_field_id;
  std::string_view vector_field_id;
  std::string_view immediate_operand_field_id;
  uint64_t alignment = 0;
};

/** How a vector operand derives each element type from the instruction type. */
enum class VectorTypePolicy : uint8_t {
  Aggregate,
  Element,
};

/** Maximum resolved register-vector payload width supported by this frontend. */
inline constexpr size_t kMaxRegisterVectorPayloadBits = 256;
/** Maximum element count accepted by schema-defined modern brace packs. */
inline constexpr size_t kMaxOperandElements = 64;

/** Semantic constraints for one operand position in a resolved layout. */
struct OperandDescriptor {
  std::string_view target_field_id;
  TypeExpressionDescriptor type_expression;
  base::ScalarTypeSizePolicy register_width_policy =
      base::ScalarTypeSizePolicy::SameWidth;
  OperandRole role;
  OperandAccess access;
  OperandShape allowed_shapes;
  std::span<const uint8_t> allowed_vector_arities;
  /** Resolved modifier field supplying the required vector element count. */
  std::string_view vector_arity_modifier_field_id{};
  VectorTypePolicy vector_type_policy = VectorTypePolicy::Aggregate;
  bool allow_vector_sink = false;
  bool allow_destination_sink = false;
  bool allow_function_symbol = false;
  /** Stable descriptor/token domain tag; empty for ordinary operands. */
  std::string_view type_tag{};
  /** Inclusive brace-pack bounds; zero means this is not a variable pack. */
  uint8_t minimum_elements = 0;
  uint8_t maximum_elements = 0;
  /** Source shapes allowed within a modern brace-pack primitive. */
  OperandShape allowed_element_shapes{};
  /** Static effective-address allowlist; empty means no static restriction. */
  std::span<const AddressStateSpaceDescriptor> allowed_address_state_spaces;
  /** Resolved modifier field supplying an explicit address-space constraint. */
  std::string_view state_space_modifier_field_id{};
  ParameterAddressConstraint parameter_constraint;
};

/** A non-owning semantic view of one resolved modifier field. */
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
  std::span<const SourceRange> locations;
};

/** A generated instruction-context override for one special-register value. */
struct OperandTypeCompatibilityDescriptor {
  std::string_view target_field_id;
  base::SpecialRegisterKind special_register_kind =
      base::SpecialRegisterKind::Invalid;
  uint8_t instruction_width = 0;
  ScalarType effective_type = ScalarType::Invalid;
  AvailabilityDescriptor availability;
};

/** A non-owning semantic view of one resolved operand field. */
struct OperandView {
  std::string_view field_id;
  OperandShape actual_shape;
  std::optional<ScalarType> immediate_type;
  std::optional<uint64_t> immediate_bits;
  std::optional<bool> immediate_is_negative;
  std::optional<ScalarType> register_type;
  std::array<ScalarType, 2> predicate_pair_types{};
  std::optional<ScalarType> special_register_type;
  std::optional<base::SpecialRegisterId> special_register_id;
  /** Effective address space; unknown for register/immediate/standalone bases. */
  std::optional<MemoryStateSpace> address_state_space;
  /** Guaranteed byte alignment; zero denotes an absolute address of zero. */
  std::optional<uint64_t> address_alignment;
  /** Function provenance is independent of whether the base binds a symbol. */
  EnclosingFunctionKind enclosing_function_kind = EnclosingFunctionKind::Unknown;
  /** None unless the base binds a formal or call-argument parameter. */
  ParameterDirection parameter_direction = ParameterDirection::None;
  ParameterAddressQualifier parameter_qualifier =
      ParameterAddressQualifier::Default;
  std::array<ScalarType, kMaxOperandElements> vector_element_types{};
  /** Per-element source shape for variable modern brace packs. */
  std::array<OperandShape, kMaxOperandElements> vector_element_shapes{};
  uint8_t vector_arity = 0;
  uint8_t vector_sink_count = 0;
  std::optional<AvailabilityDescriptor> value_availability;
  std::string_view value_name{};
  std::span<const SourceRange> locations;
};

/**
 * The target properties relevant to instruction availability checks.
 *
 * ``enabled_family_features`` is borrowed from the target profile's enabled
 * family feature levels: the caller owns the underlying strings for the
 * duration of the check. Keeping the ABI view-only also lets one target
 * advertise more than one enabled feature level.
 */
struct TargetInfo {
  PtxVersion ptx_version{};
  uint32_t sm_version = 0;
  std::span<const std::string_view> enabled_family_features{};
  std::optional<base::TargetIdentity> identity;
  std::span<const std::string_view> capabilities{};
};

/** Additional target requirements for one selected operand layout. */
struct OperandLayoutDescriptor {
  std::string_view layout_name;
  AvailabilityDescriptor availability;
};

/** Runtime representation used to compare modifier availability entries. */
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
};

/** Target requirement attached to one legal semantic modifier value. */
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
  AvailabilityDescriptor availability;
};

/** Selected dynamic modifier value exposed by a generated checker wrapper. */
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
  bool is_present = false;
  std::span<const SourceRange> locations;
};

/**
 * Checker metadata for one resolved variant.
 *
 * ``rule_id`` is a stable YAML rule key (for example
 * ``integer_arith.add``).  The common checker does not interpret it: a
 * generated, typed wrapper will dispatch it after the target-independent
 * checks have succeeded.
 */
struct VariantDescriptor {
  std::string_view variant_name;
  AvailabilityDescriptor availability;
  std::span<const ModifierValueAvailabilityDescriptor>
      modifier_value_availabilities;
  std::span<const OperandLayoutDescriptor> operand_layouts;
  std::span<const OperandTypeCompatibilityDescriptor>
      operand_type_compatibilities;
  std::string_view rule_id;
  /** Empty field IDs mean this variant has no memory-consistency rule. */
  struct MemoryConsistencyDescriptor {
    std::string_view semantics_field_id;
    std::string_view scope_field_id;
    std::string_view mmio_field_id;
    std::string_view cache_field_id;
    std::string_view address_field_id;
    std::string_view state_space_field_id;
  } memory_consistency;
  /** Empty address fields mean this variant has no static alignment rule. */
  AddressAlignmentConstraint address_alignment;
  /** Empty field IDs mean this variant has no modern memory-vector rule. */
  struct MemoryVectorDescriptor {
    std::string_view type_field_id;
    std::string_view vector_field_id;
    std::string_view address_field_id;
    std::string_view state_space_field_id;
    AvailabilityDescriptor availability;
  } memory_vector;
  /** Empty field ID means this variant has no immediate allowlist. */
  struct ImmediateValueDescriptor {
    std::string_view operand_field_id;
    std::span<const uint64_t> allowed_values;
  } immediate_value;
  /** Empty field ID means this variant has no immediate divisor constraint. */
  struct ImmediateMultipleOfDescriptor {
    std::string_view operand_field_id;
    uint64_t divisor = 0;
  } immediate_multiple_of;
  /** Empty span means this variant has no immediate range constraints. */
  struct ImmediateRangeDescriptor {
    std::string_view operand_field_id;
    uint64_t minimum = 0;
    bool has_maximum = false;
    uint64_t maximum = ~uint64_t{0};
  };
  std::span<const ImmediateRangeDescriptor> immediate_ranges;
};

/** Checker metadata for all variants of one resolved instruction. */
struct InstructionDescriptor {
  std::string_view opcode_name;
  std::span<const VariantDescriptor> variants;
};

enum class CheckDiagnosticKind : uint8_t {
  UnknownTarget,
  UnsupportedPtxVersion,
  UnsupportedSmVersion,
  UnsupportedTargetFamily,
  UnsupportedAvailability,
  MissingVariantDescriptor,
  MissingOperand,
  UnexpectedOperand,
  UnsupportedOperandShape,
  InvalidOperandLayoutTag,
  OperandLayoutPayloadMismatch,
  MissingTypeField,
  MissingStateSpaceField,
  MissingVectorArityField,
  OperandTypeMismatch,
  AddressStateSpaceMismatch,
  AddressAlignmentMismatch,
  ParameterDirectionMismatch,
  ParameterQualifierMismatch,
  InvalidVectorOperand,
  MemoryConsistencyViolation,
  ImmediateValueMismatch,
  RuleViolation,
};

/** A checker diagnostic, always anchored to a resolved-IR source range. */
struct CheckDiagnostic {
  CheckDiagnosticKind kind;
  SourceRange range;
  std::string message;
};

using CheckDiagnostics = std::vector<CheckDiagnostic>;
using CheckResult = std::expected<void, CheckDiagnostics>;

namespace detail {

/**
 * Verify the actual call contract of one generated variant-check lambda.
 *
 * ``Overloaded`` cannot state this constraint itself because it does not know
 * the variant alternatives before ``std::visit`` is instantiated.
 */
template <typename Function, typename Variant>
concept VariantCheckFunction =
    std::invocable<Function&, const Variant&> &&
    std::same_as<std::invoke_result_t<Function&, const Variant&>, CheckResult>;

}  // namespace detail

/** Context supplied by the caller for checking one resolved instruction. */
struct Context {
  TargetInfo target;
  SourceRange instruction_range;
};

/** Return whether one generated availability requirement accepts ``target``. */
bool is_available(const AvailabilityDescriptor& availability,
                  const TargetInfo& target) noexcept;

/** Find generated checker metadata by the resolved C++ variant name. */
const VariantDescriptor* find_variant_descriptor(
    const InstructionDescriptor& instruction,
    std::string_view variant_name) noexcept;

/** Run common target-availability checks for one already selected variant. */
CheckResult check_availability(const VariantDescriptor& variant,
                               const Context& context);

/**
 * Find the selected variant descriptor and run all common checker logic.
 *
 * Rule-specific semantic checks intentionally remain outside this function;
 * generated typed wrappers dispatch ``VariantDescriptor::rule_id`` after this
 * common stage.
 */
CheckResult check_common(const InstructionDescriptor& instruction,
                         std::string_view variant_name, const Context& context);

/**
 * Check operand constraints independent of a particular opcode.
 *
 * The generated typed wrapper supplies views of the selected resolved layout.
 * This verifies operand-field identity, allowed resolved shape, and immediate
 * or declaration-bound register types constrained by the generated
 * ``TypeExpressionDescriptor``.
 */
CheckResult check_operands(
    std::span<const OperandDescriptor> descriptors,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    std::span<const OperandTypeCompatibilityDescriptor> type_compatibilities,
    const Context& context);

/** Verify that a resolved instruction preserved a valid selected layout tag. */
CheckResult check_operand_layout_tag(std::string_view variant_name,
                                     uint16_t selected_layout,
                                     size_t layout_count,
                                     const Context& context);

/**
 * Check target requirements contributed by the selected operand layout.
 *
 * A layout without YAML ``availability`` has an empty requirement; the
 * containing variant's availability is still checked by ``check_common``.
 */
CheckResult check_operand_layout_availability(const VariantDescriptor& variant,
                                              uint16_t selected_layout,
                                              const Context& context);

/** Check availability constraints for the actual dynamic modifier values. */
CheckResult check_modifier_value_availability(
    std::span<const ModifierValueAvailabilityDescriptor> descriptors,
    std::span<const ModifierValueView> actual_values, const Context& context);

/** Check generated ld/st memory-order and address-space cross constraints. */
CheckResult check_memory_consistency(
    const VariantDescriptor::MemoryConsistencyDescriptor& descriptor,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    const Context& context);

/** Check a natural-alignment rule when an address is statically known. */
CheckResult check_address_alignment(
    const AddressAlignmentConstraint& descriptor,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    const Context& context);

/** Check generated PTX 8.8 256-bit ld/st vector cross constraints. */
CheckResult check_memory_vector(
    const VariantDescriptor::MemoryVectorDescriptor& descriptor,
    std::span<const FieldView> fields, std::span<const OperandView> operands,
    const Context& context);

/** Check an immediate operand against an exact generated integer allowlist. */
CheckResult check_immediate_value(
    const VariantDescriptor::ImmediateValueDescriptor& descriptor,
    std::span<const OperandView> operands, const Context& context);

/** Check an immediate operand is divisible by a generated positive divisor. */
CheckResult check_immediate_multiple_of(
    const VariantDescriptor::ImmediateMultipleOfDescriptor& descriptor,
    std::span<const OperandView> operands, const Context& context);

/** Check an immediate operand against inclusive generated integer bounds. */
CheckResult check_immediate_range(
    const VariantDescriptor::ImmediateRangeDescriptor& descriptor,
    std::span<const OperandView> operands, const Context& context);

/**
 * Check one generated resolved instruction.
 *
 * Explicit specializations are generated per opcode once its rule wrapper is
 * available.  Declaring this template here gives all clients one stable
 * checker entry point without coupling this foundational ABI to generated
 * instruction types.
 */
template <typename T>
CheckResult check(const T& instruction, const Context& context);

}  // namespace checker
}  // namespace ptx_frontend::resolved_ir
