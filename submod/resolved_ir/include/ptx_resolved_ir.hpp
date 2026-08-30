#pragma once
#include <fmt/core.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <ptx_frontend/base/base.hpp>
#include <source_location>
#include <span>
#include <type_traits>
#include <unordered_set>
#include <vector>
#include <ptx_frontend/base/ptx_special_register.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/common/source_loc.hpp>
#include <ptx_frontend/common/utils.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::declaration_semantics {
struct FunctionParameterContract;
}

namespace ptx_frontend::resolved_ir {
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
using base::AsyncProxyKind;
using base::ProxyKindPair;
namespace check_end {

using OperandShape = checker::OperandShape;
using OperandRole = checker::OperandRole;
using OperandAccess = checker::OperandAccess;
using OperandTypeExpressionKind = checker::OperandTypeExpressionKind;
using TypeExpressionDescriptor = checker::TypeExpressionDescriptor;

enum class OperandSyntaxShape : uint16_t {
  Identifier = 1 << 0,
  Immediate = 1 << 1,
  Address = 1 << 2,
  VectorPack = 1 << 3,
  VectorMember = 1 << 4,
  Predicate = 1 << 5,
  Group = 1 << 6,  // for op call syntax (...)
  CallTarget = 1 << 7,
  CallTargetSet = 1 << 8,
  BranchTarget = 1 << 9,
  BranchTargetSet = 1 << 10,
  RegisterPredicatePair = 1 << 11,
};

constexpr OperandSyntaxShape operator|(OperandSyntaxShape lhs,
                                       OperandSyntaxShape rhs) {
  using Underlying = std::underlying_type_t<OperandSyntaxShape>;
  return static_cast<OperandSyntaxShape>(static_cast<Underlying>(lhs) |
                                         static_cast<Underlying>(rhs));
}

OperandSyntaxShape get_operand_syntax_shape(
    const syntax_ast::AstOperand& operand);

enum class OperandPresence : uint8_t {
  Required,
  Optional,
};

enum class ResolvedValueKind : uint8_t {
  Bool,
  ScalarType,
  RoundingMode,
  ComparisonOperator,
  BooleanOperator,
  CacheOperator,
  EvictionPriority,
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
  PredicateSource,
  Immediate,
  RegOrImm,
  ShflDestination,
  PredicatePair,
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
  MbarrierStateToken,
};

struct SyntaxOperandSlotDescriptor {
  OperandSyntaxShape allowed_shapes;
  OperandPresence presence;
  /** Descriptor/domain identity only; syntax has no token tag to compare. */
  std::string_view type_tag{};
  /** Inclusive brace-pack bounds; zero means this is not a variable pack. */
  uint8_t minimum_elements = 0;
  uint8_t maximum_elements = 0;
  /** Syntax shapes accepted for each element of a modern brace pack. */
  OperandSyntaxShape allowed_element_shapes{};
};

enum class OperandLayoutKind : uint8_t {
  Flat,  // normal layout
  Call,  // fixed direct-call group layouts
  IndirectCall,  // fixed indirect-call group and metadata layouts
};

struct SyntaxOperandLayoutDescriptor {
  std::string_view layout_id;
  OperandLayoutKind kind;
  std::span<const SyntaxOperandSlotDescriptor> slots;
};

enum class PresenceRequirement {
  Absent,    // YAML: absent
  Optional,  // YAML: optional
  Required,  // YAML: required / fixed
};

struct SyntaxModifierDescriptor {
  std::span<const std::string_view> allowed_values;
  PresenceRequirement presence;
  std::string_view kind_id;

  bool check(std::string modifier_str) const;
};

struct SyntaxVariantDescriptor {
  std::string_view variant_name;
  std::span<const SyntaxModifierDescriptor> modifiers;
  std::span<const SyntaxOperandLayoutDescriptor> operand_layouts;

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
  MemoryConsistency,
  MemoryScope,
  MemoryStateSpace,
  MbarrierPhaseType,
  MbarrierLayout,
  AsyncProxyKind,
  ProxyKindPair,
};

struct ResolvedModifierDefaultDescriptor {
  ResolvedModifierDefaultKind kind = ResolvedModifierDefaultKind::None;
  bool bool_value = false;
  ScalarType scalar_type = ScalarType::Invalid;
  RoundingMode rounding_mode = RoundingMode::Invalid;
  CacheOperator cache_operator = CacheOperator::Unspecified;
  MemoryStateSpace memory_state_space = MemoryStateSpace::Invalid;
  MbarrierPhaseType mbarrier_phase_type = MbarrierPhaseType::Primary;
  MbarrierLayout mbarrier_layout = MbarrierLayout::V0;
  AsyncProxyKind async_proxy_kind = AsyncProxyKind::Async;
  ProxyKindPair proxy_kind_pair = ProxyKindPair::TensormapToGeneric;
  MemoryConsistency memory_consistency = MemoryConsistency::Omitted;
  MemoryScope memory_scope = MemoryScope::None;
};

struct ResolvedModifierBindingDescriptor {
  std::string_view source_kind_id;
  std::string_view target_field_id;
  ResolvedModifierDefaultDescriptor default_value;
};

using ResolvedOperandBindingDescriptor = checker::OperandDescriptor;

struct ResolvedOperandLayoutDescriptor {
  std::string_view layout_id;
  std::span<const ResolvedFieldDescriptor> fields;
  std::span<const ResolvedOperandBindingDescriptor> bindings;
};

struct ResolvedVariantDescriptor {
  std::string_view variant_name;
  // Modifier fields are shared by all layouts. Operand fields belong to the
  // selected ResolvedOperandLayoutDescriptor because layouts may give one
  // semantic role different resolved representations.
  std::span<const ResolvedFieldDescriptor> fields;
  std::span<const ResolvedModifierBindingDescriptor> modifier_bindings;
  std::span<const ResolvedOperandLayoutDescriptor> operand_layouts;
};

struct ResolvedInstructionDescriptor {
  std::string_view opcode_name;
  std::span<const ResolvedVariantDescriptor> variants;
};

};  // namespace check_end

struct ResolveDiagnostic {
  SourceRange range;
  std::string message;
};

class ResolveException : public std::runtime_error {
 public:
  explicit ResolveException(
      std::string message,
      std::source_location where = std::source_location::current())
      : std::runtime_error(std::move(message)), where_(where) {}

  [[nodiscard]] const std::source_location& where() const noexcept {
    return where_;
  }

 private:
  std::source_location where_;
};

enum class ResolvedRegisterClass : uint8_t {
  General,
  Predicate,
};

/** A register reference, optionally bound to its declaration in a module. */
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

/** Opaque mbarrier state; a null register reference is the ``_`` sink. */
struct ResolvedMbarrierStateToken {
  std::optional<ResolvedRegisterRef> register_ref;
  bool operator==(const ResolvedMbarrierStateToken&) const = default;
};

struct ResolvedImmediate {
  uint64_t bits;
  ScalarType type;
  bool is_negative = false;
  bool operator==(const ResolvedImmediate&) const = default;
};

/** A register vector; an empty element is the write-only ``_`` sink. */
struct ResolvedRegisterVector {
  std::vector<std::optional<ResolvedRegisterRef>> elements;
  bool operator==(const ResolvedRegisterVector&) const = default;
};

struct ResolvedPredicate {
  ResolvedRegisterRef register_ref;
  bool negated{};
  bool operator==(const ResolvedPredicate&) const = default;
};

/** A direct branch label, optionally bound to its declaration in a module. */
struct ResolvedBranchTarget {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  bool operator==(const ResolvedBranchTarget&) const = default;
};

/** A function-local .branchtargets declaration reference. */
struct ResolvedBranchTargetSet {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  bool operator==(const ResolvedBranchTargetSet&) const = default;
};

/** A predefined read-only PTX special register with target-independent identity. */
struct ResolvedSpecialRegisterRef {
  std::string spelling;
  base::SpecialRegisterId id;
  std::optional<base::VectorComponent> component;
  bool operator==(const ResolvedSpecialRegisterRef&) const = default;
};

/** The sole predicate-source union: a predicate register or scalar .sreg. */
using ResolvedPredicateSource =
    std::variant<ResolvedPredicate, ResolvedSpecialRegisterRef>;

/** A declared vector register accepted only by a vector instruction layout. */
struct ResolvedVectorRegisterRef {
  ResolvedRegisterRef register_ref;
  bool operator==(const ResolvedVectorRegisterRef&) const = default;
};

/** A vector special-register base accepted only by a vector instruction layout. */
struct ResolvedVectorSpecialRegisterRef {
  std::string spelling;
  base::SpecialRegisterId id;
  bool operator==(const ResolvedVectorSpecialRegisterRef&) const = default;
};

/** A function reference, bound to a device or kernel function declaration. */
struct ResolvedFunctionRef {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  bool is_entry{};
  std::optional<checker::AvailabilityDescriptor> address_availability;
  bool operator==(const ResolvedFunctionRef&) const = default;
};

/** A function-local .callprototype or .calltargets declaration reference. */
struct ResolvedIndirectMetadataRef {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  std::optional<binding::SymbolKind> declaration_kind;
  bool operator==(const ResolvedIndirectMetadataRef&) const = default;
};

/** One component of an indirect call: its .reg target or metadata label. */
using ResolvedIndirectCallee =
    std::variant<ResolvedRegisterRef, ResolvedIndirectMetadataRef>;

/** A .reg or .param symbol passed through a direct call boundary. */
struct ResolvedCallParameterRef {
  std::string spelling;
  std::optional<binding::SymbolId> symbol_id;
  std::optional<uint32_t> parameterized_index;
  std::optional<syntax_ast::AstStateSpace> state_space;
  std::optional<ScalarType> declared_type;
  bool operator==(const ResolvedCallParameterRef&) const = default;
};

/** A call immediate retained until a callee signature supplies its type. */
struct ResolvedCallLiteral {
  std::string spelling;
  syntax_ast::AstImmediateKind kind{};
  bool operator==(const ResolvedCallLiteral&) const = default;
};

using ResolvedCallArgument =
    std::variant<ResolvedCallParameterRef, ResolvedCallLiteral>;

/** The variadic input group of a direct call, with element source ranges. */
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
  std::optional<syntax_ast::AstStateSpace> declaration_state_space;
  /**
   * State space of the address produced for this symbol.
   *
   * This normally equals ``declaration_state_space``.  Formal parameters are
   * the important exception: a direct parameter address and a kernel
   * parameter used by ``mov`` retain ``.param``, while ``mov`` address-taking
   * materializes a device-function parameter in ``.local`` space.
   */
  std::optional<syntax_ast::AstStateSpace> address_state_space;
  std::optional<ScalarType> declared_type;
  /** Guaranteed byte alignment for a bound declaration address. */
  std::optional<uint64_t> address_alignment;
  /** Target requirement contributed by this address value, if any. */
  std::optional<checker::AvailabilityDescriptor> address_availability;
  bool operator==(const ResolvedSymbolRef&) const = default;
};

enum class ResolvedAddressOffsetOperator : uint8_t {
  Add,
  Subtract,
};

struct ResolvedAddressOffset {
  ResolvedAddressOffsetOperator operation = ResolvedAddressOffsetOperator::Add;
  ResolvedImmediate value;
  bool operator==(const ResolvedAddressOffset&) const = default;
};

using ResolvedAddressBase =
    std::variant<ResolvedRegisterRef, ResolvedImmediate, ResolvedSymbolRef>;

/** A PTX address expression with a resolved base and optional byte offset. */
struct ResolvedAddress {
  ResolvedAddressBase base;
  std::optional<ResolvedAddressOffset> offset;
  /** Instruction context captured by module resolution; standalone is unknown. */
  EnclosingFunctionKind enclosing_function_kind = EnclosingFunctionKind::Unknown;
  /** PTX 9.3 .param subqualifier selected by this memory instruction. */
  ParameterAddressQualifier parameter_qualifier =
      ParameterAddressQualifier::Default;
  bool operator==(const ResolvedAddress&) const = default;
};

/** The selected operand-layout index within the resolved instruction variant. */
struct ResolvedOperandLayoutTag {
  uint16_t value = 0;
  bool operator==(const ResolvedOperandLayoutTag&) const = default;
};

using RegOrImm = std::variant<ResolvedRegisterRef, ResolvedImmediate>;

/** A brace-pack coordinate with register or immediate components. */
struct ResolvedTensorCoordinate {
  std::vector<RegOrImm> elements;
  bool operator==(const ResolvedTensorCoordinate&) const = default;
};

struct ResolvedShflSyncDestination {
  std::optional<ResolvedRegisterRef> data;
  ResolvedPredicate predicate;
  bool operator==(const ResolvedShflSyncDestination&) const = default;
};

struct ResolvedPredicatePair {
  ResolvedPredicate first;
  ResolvedPredicate second;
  bool operator==(const ResolvedPredicatePair&) const = default;
};

/** A scalar ``mov`` source after identifier classification and binding. */
using ResolvedMovSource =
    std::variant<ResolvedRegisterRef, ResolvedImmediate,
                 ResolvedSpecialRegisterRef, ResolvedFunctionRef,
                 ResolvedSymbolRef, ResolvedAddress>;

using ResolvedFieldValue =
    std::variant<WithLocs<bool>, WithLocs<ScalarType>, WithLocs<RoundingMode>,
                 WithLocs<ComparisonOperator>,
                 WithLocs<BooleanOperator>,
                 WithLocs<CacheOperator>, WithLocs<EvictionPriority>,
                 WithLocs<MemoryConsistency>,
                 WithLocs<MemoryScope>, WithLocs<VectorArity>,
                 WithLocs<MemoryStateSpace>,
                 WithLocs<MbarrierPhaseType>, WithLocs<MbarrierLayout>,
                 WithLocs<AsyncProxyKind>, WithLocs<ProxyKindPair>,
                 WithLocs<ResolvedRegisterRef>,
                 WithLocs<ResolvedMbarrierStateToken>,
                 WithLocs<ResolvedImmediate>,
                 WithLocs<RegOrImm>, WithLocs<ResolvedShflSyncDestination>,
                 WithLocs<ResolvedPredicatePair>,
                 WithLocs<ResolvedMovSource>,
                 WithLocs<ResolvedPredicate>, WithLocs<ResolvedBranchTarget>,
                 WithLocs<ResolvedBranchTargetSet>,
                 WithLocs<ResolvedSpecialRegisterRef>,
                 WithLocs<ResolvedPredicateSource>,
                 WithLocs<ResolvedVectorRegisterRef>,
                 WithLocs<ResolvedVectorSpecialRegisterRef>,
                 WithLocs<ResolvedSymbolRef>, WithLocs<ResolvedAddress>,
                 WithLocs<ResolvedRegisterVector>,
                 WithLocs<ResolvedTensorCoordinate>,
                 WithLocs<ResolvedFunctionRef>,
                 WithLocs<ResolvedIndirectCallee>,
                 WithLocs<ResolvedCallParameterRef>,
                 WithLocs<ResolvedCallArguments>>;
using ResolvedFieldMap = std::unordered_map<std::string, ResolvedFieldValue>;

struct ResolvedInstructionFields {
  std::string_view variant_name;
  ResolvedOperandLayoutTag operand_layout;
  std::optional<WithLocs<ResolvedPredicate>> execution_predicate;
  ResolvedFieldMap modifiers;
  ResolvedFieldMap operands;
};

template <typename T>
concept PtxOperator = requires(T object) {
  typename T::VariantType;
  requires std::is_scoped_enum_v<typename T::VariantType>;
  {
    T::get_syntax_descriptor()
  } -> std::same_as<const check_end::SyntaxInstructionDescriptor&>;
  {
    T::get_resolved_descriptor()
  } -> std::same_as<const check_end::ResolvedInstructionDescriptor&>;
};

using ActualModifierTable =
    std::unordered_map<std::string, const syntax_ast::AstModifier*>;

/** Declaration context used while resolving an instruction inside a module. */
struct ResolveContext {
  const binding::SymbolTable& symbols;
  /** Lexical scope for ordinary variables and parameters. */
  binding::ScopeId scope;
  /** Owning function scope for labels and control-flow metadata. */
  std::optional<binding::ScopeId> function_scope;
  /** Whether ``scope`` belongs to a kernel entry rather than a device func. */
  bool function_is_entry{};
};

/** Collect source modifiers by the slot IDs of one selected variant. */
std::expected<ActualModifierTable, ResolveDiagnostic> collect_actual_modifiers(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxVariantDescriptor& variant);

/** Select the unique variant whose local modifier slots accept the AST. */
std::expected<std::string_view, ResolveDiagnostic> select_variant_name(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& instruction);

template <PtxOperator T>
std::expected<typename T::VariantType, ResolveDiagnostic> selectVariant(
    const syntax_ast::AstInstruction& ast) {
  const auto& inst_desc = T::get_syntax_descriptor();
  const auto variant_name = select_variant_name(ast, inst_desc);
  if (!variant_name)
    return std::unexpected(variant_name.error());

  const auto variant =
      magic_enum::enum_cast<typename T::VariantType>(*variant_name);
  if (!variant) {
    throw ResolveException(fmt::format(
        "Descriptor variant '{}.{}' has no matching VariantType enumerator.",
        utils::type_name<T>(), *variant_name));
  }
  return *variant;
}

/** Implementation entry specialized by each generated opcode. */
template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast, const ResolveContext* context);

/** Resolve one standalone instruction without declaration binding. */
template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast) {
  return resolve<T>(ast, nullptr);
}

/** Resolve one instruction against an explicit module binding context. */
template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast, const ResolveContext& context) {
  return resolve<T>(ast, &context);
}

/** Resolve one lexer-classified immediate literal for a selected scalar type. */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type);

/** Type a retained call literal by one formal parameter contract. */
std::expected<WithLocs<ResolvedImmediate>, ResolveDiagnostic>
resolve_call_literal(const ResolvedCallLiteral& literal, SourceRange range,
                     const declaration_semantics::FunctionParameterContract&
                         formal);

std::expected<ResolvedInstructionFields, ResolveDiagnostic> resolve_fields(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& syntax_instruction,
    const check_end::ResolvedInstructionDescriptor& resolved_instruction,
    std::string_view variant_name, const ResolveContext* context = nullptr);

/** Translate catalogued special-register metadata into checker availability. */
checker::AvailabilityDescriptor special_register_availability(
    const base::Info& info);

/**
   * @brief Get a resolved modifier field from the resolved instruction fields.
   * 
   * @tparam T target field type, must be one of the types in resolved_ir::<IR>'s members
   * @param fields 
   * @param kind_id 
   * @return const WithLocs<T>& 
   */
template <typename T>
const WithLocs<T>& resolved_modifier(const ResolvedInstructionFields& fields,
                                     std::string_view kind_id) {
  const auto it = fields.modifiers.find(std::string(kind_id));
  if (it == fields.modifiers.end()) {
    throw ResolveException(
        fmt::format("Resolved modifier field '{}' is unavailable.", kind_id));
  }
  if (const auto* value = std::get_if<WithLocs<T>>(&it->second))
    return *value;
  throw ResolveException(fmt::format(
      "Resolved modifier field '{}' has an unexpected value type.", kind_id));
}

/**
 * @brief Get a resolved operand field from the resolved instruction fields.
 * 
 * @tparam T target field type, must be one of the types in resolved_ir::<IR>'s members
 * @param fields 
 * @param field_id 
 * @return const WithLocs<T>& 
 */
template <typename T>
const WithLocs<T>& resolved_operand(const ResolvedInstructionFields& fields,
                                    std::string_view field_id) {
  const auto it = fields.operands.find(std::string(field_id));
  if (it == fields.operands.end()) {
    throw ResolveException(
        fmt::format("Resolved operand field '{}' is unavailable.", field_id));
  }
  if (const auto* value = std::get_if<WithLocs<T>>(&it->second))
    return *value;
  throw ResolveException(fmt::format(
      "Resolved operand field '{}' has an unexpected value type.", field_id));
}

};  // namespace ptx_frontend::resolved_ir

// Generated instruction definitions.  The generated header also includes this
// foundational header so it may safely be included directly by clients.
#include "resolved_ir.gen.hpp"
