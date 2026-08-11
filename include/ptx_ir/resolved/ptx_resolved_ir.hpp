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
#include "ptx_ir/syntax/ptx_syntax_ast.hpp"

namespace ptx_frontend::resolved_ir {

namespace check_end {

enum class OperandShape : uint16_t {
  Register = 1 << 0,
  Immediate = 1 << 1,
  Address = 1 << 2,
  Symbol = 1 << 3,
  Vector = 1 << 4,
};

template <typename T>
  requires(std::is_scoped_enum_v<T>)
constexpr T operator|(T lhs, T rhs) {
  using under_type = std::underlying_type_t<T>;
  return static_cast<T>(static_cast<under_type>(lhs) |
                        static_cast<under_type>(rhs));
}

template <typename T>
  requires(std::is_scoped_enum_v<T>)
constexpr bool valid(T enum_value) {
  using under_type = std::underlying_type_t<T>;
  constexpr int32_t bit_num = sizeof(under_type) * 8;
  bool flag = false;
  constexpr under_type full_r = 0;
  for (auto item : magic_enum::enum_values<T>()) {
    full_r |= item;
  }
  under_type enum_value_under = static_cast<under_type>(enum_value);
  if ((enum_value_under & full_r) == enum_value_under) {
    return true;
  } else {
    return false;
  }
}

enum class OperandRole : uint8_t {
  Destination,
  Source,
  Address,
  Predicate,
  BranchTarget,
};

enum class OperandAccess : uint8_t {
  Read,
  Write,
  ReadWrite,
};

enum class OperandSyntaxShape : uint8_t {
  Identifier = 1 << 0,
  Immediate = 1 << 1,
  Address = 1 << 2,
  VectorPack = 1 << 3,
  VectorMember = 1 << 4,
  Group = 1 << 5,  // for op call syntax (...)
};

OperandSyntaxShape get_operand_syntax_shape(
    const syntax_ast::AstOperand& operand);

enum class OperandPresence : uint8_t {
  Required,
  Optional,
};

enum class ResolvedValueKind : uint8_t {
  Bool,
  ScalarType,
  Register,
  Immediate,
  RegOrImm,
};

struct OperandSlotDescriptor {
  std::string_view field_id;          // "dst", "src1", "src2", "barrier_id"
  OperandSyntaxShape allowed_shapes;  // AST stage allow syntax shape
  OperandPresence presence;
  ResolvedValueKind value_kind;
  std::string_view type_expr;  // for example "$type"

  // OperandRole role;
  // OperandAccess access;
  // OperandShape allowed_resolved_shapes;
  // StateSpace allowed_state_spaces;
};

enum class OperandLayoutKind : uint8_t {
  Flat,  // normal layout
};

struct OperandLayoutDescriptor {
  OperandLayoutKind layout_id;
  std::span<const OperandSlotDescriptor> slots;
};

enum class PresenceRequirement {
  Absent,    // YAML: absent
  Optional,  // YAML: optional
  Required,  // YAML: required / fixed
};

struct ModifierDescriptor {
  std::span<const std::string_view> allowed_values;
  PresenceRequirement presence;
  std::string_view kind_id;
  ResolvedValueKind value_kind;

  bool check(std::string modifier_str) const;
};

struct VariantDescriptor {
  std::string_view variant_name;
  std::span<const ModifierDescriptor> modifiers;
  std::span<const OperandLayoutDescriptor> operand_layouts;

  int32_t get_required_modifier_num() const;
};

struct InstructionDescriptor {
  std::string_view Opcode_name;
  std::span<const VariantDescriptor> variants;
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

struct ResolvedRegisterId {
  uint32_t value;
  bool operator==(const ResolvedRegisterId&) const = default;
};

struct ResolvedImmediate {
  uint64_t bits;
  ScalarType type;
  bool operator==(const ResolvedImmediate&) const = default;
};

using RegOrImm = std::variant<ResolvedRegisterId, ResolvedImmediate>;

using ResolvedFieldValue =
    std::variant<WithLocs<bool>, WithLocs<ScalarType>,
                 WithLocs<ResolvedRegisterId>, WithLocs<ResolvedImmediate>,
                 WithLocs<RegOrImm>>;
using ResolvedFieldMap = std::unordered_map<std::string, ResolvedFieldValue>;

struct ResolvedInstructionFields {
  std::string_view variant_name;
  ResolvedFieldMap modifiers;
  ResolvedFieldMap operands;
};

template <typename T>
concept PtxOperator = requires(T object) {
  typename T::VariantType;
  requires std::is_scoped_enum_v<typename T::VariantType>;
  { object.check() } -> std::same_as<bool>;
  {
    T::get_inst_descriptor()
  } -> std::same_as<const check_end::InstructionDescriptor&>;
};

using ActualModifierTable =
    std::unordered_map<std::string, const syntax_ast::AstModifier*>;

/**
 * Collect source modifiers by descriptor kind ID.
 *
 * This is the sole implementation of spelling-to-kind mapping and duplicate
 * detection.  Type-specific callers should use the template adapter below.
 */
std::expected<ActualModifierTable, ResolveDiagnostic> collect_actual_modifiers(
    const syntax_ast::AstInstruction& ast,
    const check_end::InstructionDescriptor& instruction);

template <PtxOperator T>
std::expected<ActualModifierTable, ResolveDiagnostic> collect_actual_modifiers(
    const syntax_ast::AstInstruction& ast) {
  return collect_actual_modifiers(ast, T::get_inst_descriptor());
}

bool matches_variant(const check_end::VariantDescriptor& variant,
                     const ActualModifierTable& actual_modifiers);
bool matches_modifier_slot(const check_end::ModifierDescriptor& descriptor,
                           const ActualModifierTable& actual_modifiers);

template <PtxOperator T>
std::expected<typename T::VariantType, ResolveDiagnostic> selectVariant(
    const syntax_ast::AstInstruction& ast) {
  const auto actual_modifiers = collect_actual_modifiers<T>(ast);
  if (!actual_modifiers)
    return std::unexpected(actual_modifiers.error());

  const auto inst_desc = T::get_inst_descriptor();

  std::optional<size_t> selected_index;
  for (size_t index = 0; index < inst_desc.variants.size(); ++index) {
    if (!matches_variant(inst_desc.variants[index], *actual_modifiers))
      continue;

    if (selected_index) {
      return std::unexpected(ResolveDiagnostic{
          .range = ast.range,
          .message = fmt::format(
              "Ambiguous modifier combination for instruction '{}'.",
              ast.opcode.syntax.text),
      });
    }

    selected_index = index;
  }

  if (!selected_index) {
    return std::unexpected(ResolveDiagnostic{
        .range = ast.range,
        .message = fmt::format(
            "No variant of instruction '{}' accepts this modifier combination.",
            ast.opcode.syntax.text),
    });
  }

  const auto& variant_name = inst_desc.variants[*selected_index].variant_name;

  const auto variant =
      magic_enum::enum_cast<typename T::VariantType>(variant_name);
  if (!variant) {
    throw ResolveException(fmt::format(
        "Descriptor variant '{}.{}' has no matching VariantType enumerator.",
        utils::type_name<T>(), variant_name));
  }
  return *variant;
}

/** Resolve one syntax instruction into its opcode-specific resolved IR. */
template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast);

std::expected<ResolvedInstructionFields, ResolveDiagnostic> resolve_fields(
    const syntax_ast::AstInstruction& ast,
    const check_end::InstructionDescriptor& instruction,
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
