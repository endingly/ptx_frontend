#pragma once
#include <fmt/core.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <ptx_ir/base.hpp>
#include <source_location>
#include <span>
#include <type_traits>
#include <unordered_set>
#include "ptx_ir/ptx_resolved_ir_checker.hpp"
#include "ptx_ir/source_loc.hpp"
#include "ptx_ir/syntax/ptx_syntax_ast.hpp"
#include "utils.hpp"

namespace ptx_frontend::resolved_ir {

namespace check_end {

using OperandShape = checker::OperandShape;
using OperandRole = checker::OperandRole;
using OperandAccess = checker::OperandAccess;
using OperandTypeExpressionKind = checker::OperandTypeExpressionKind;
using TypeExpressionDescriptor = checker::TypeExpressionDescriptor;

enum class OperandSyntaxShape : uint8_t {
  Identifier = 1 << 0,
  Immediate = 1 << 1,
  Address = 1 << 2,
  VectorPack = 1 << 3,
  VectorMember = 1 << 4,
  Predicate = 1 << 5,
  Group = 1 << 6,  // for op call syntax (...)
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
  Register,
  Predicate,
  Immediate,
  RegOrImm,
};

struct SyntaxOperandSlotDescriptor {
  OperandSyntaxShape allowed_shapes;
  OperandPresence presence;
};

enum class OperandLayoutKind : uint8_t {
  Flat,  // normal layout
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
};

struct ResolvedModifierDefaultDescriptor {
  ResolvedModifierDefaultKind kind = ResolvedModifierDefaultKind::None;
  bool bool_value = false;
  ScalarType scalar_type = ScalarType::Invalid;
  RoundingMode rounding_mode = RoundingMode::Invalid;
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

/**
 * Register identity before declaration binding is available.
 *
 * `spelling` is owned so the resolved IR does not depend on the Syntax AST
 * lifetime. `index` is retained for the currently supported numbered-register
 * syntax, but it is not the register's identity by itself.
 */
struct ResolvedRegisterRef {
  std::string spelling;
  ResolvedRegisterClass register_class;
  uint32_t index;
  bool operator==(const ResolvedRegisterRef&) const = default;
};

struct ResolvedImmediate {
  uint64_t bits;
  ScalarType type;
  bool operator==(const ResolvedImmediate&) const = default;
};

struct ResolvedPredicate {
  ResolvedRegisterRef register_ref;
  bool negated{};
  bool operator==(const ResolvedPredicate&) const = default;
};

/** The selected operand-layout index within the resolved instruction variant. */
struct ResolvedOperandLayoutTag {
  uint16_t value = 0;
  bool operator==(const ResolvedOperandLayoutTag&) const = default;
};

using RegOrImm = std::variant<ResolvedRegisterRef, ResolvedImmediate>;

using ResolvedFieldValue =
    std::variant<WithLocs<bool>, WithLocs<ScalarType>, WithLocs<RoundingMode>,
                 WithLocs<ResolvedRegisterRef>, WithLocs<ResolvedImmediate>,
                 WithLocs<RegOrImm>, WithLocs<ResolvedPredicate>>;
using ResolvedFieldMap = std::unordered_map<std::string, ResolvedFieldValue>;

struct ResolvedInstructionFields {
  std::string_view variant_name;
  ResolvedOperandLayoutTag operand_layout;
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

/** Resolve one syntax instruction into its opcode-specific resolved IR. */
template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast);

/** Resolve one lexer-classified immediate literal for a selected scalar type. */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type);

std::expected<ResolvedInstructionFields, ResolveDiagnostic> resolve_fields(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& syntax_instruction,
    const check_end::ResolvedInstructionDescriptor& resolved_instruction,
    std::string_view variant_name);

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
