#pragma once
#include <fmt/core.h>
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

struct OperandSlotDescriptor {
  std::string_view field_id;          // "dst", "src1", "src2", "barrier_id"
  OperandSyntaxShape allowed_shapes;  // AST stage allow syntax shape
  OperandPresence presence;

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

struct Add {
  enum class VariantType {
    IntegerNoSat,
    SatS32,
    SimdNoSatSm90,
    PackedOptionalSatSm120,
    SatSm120,
  };

  struct IntegerNoSat {
    // no saturation for this variant
    WithLocs<ScalarType> type;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_sat_s32
  struct SatS32 {
    // saturation is always true for this variant
    // type is always S32 for this variant
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_simd_no_sat_sm90
  struct SimdNoSatSm90 {
    // no saturation for this variant
    WithLocs<ScalarType> type;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_packed_optional_sat_sm120
  struct PackedOptionalSatSm120 {
    WithLocs<ScalarType> type;
    WithLocs<bool> saturate;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_sat_sm120
  struct SatSm120 {
    // saturation is always true for this variant
    WithLocs<ScalarType> type;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  using Variant = std::variant<IntegerNoSat, SatS32, SimdNoSatSm90,
                               PackedOptionalSatSm120, SatSm120>;
  Variant variant;

  bool check();
  static const check_end::InstructionDescriptor& get_inst_descriptor() noexcept;
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

template <PtxOperator T>
std::optional<std::string> get_kind_id(std::string input_str) {
  struct ModifierKindEntry {
    std::string_view spelling;
    std::string_view kind_id;
  };
  auto count_modifier_kind_entries =
      [](const check_end::InstructionDescriptor& instruction) constexpr
      -> size_t {
    std::size_t count = 0;
    for (const auto& variant : instruction.variants) {
      for (const auto& modifier : variant.modifiers) {
        for (const std::string_view spelling : modifier.allowed_values) {
          bool already_seen = false;
          bool reached_current_entry = false;
          for (const auto& prior_variant : instruction.variants) {
            for (const auto& prior_modifier : prior_variant.modifiers) {
              for (const std::string_view prior_spelling :
                   prior_modifier.allowed_values) {
                if (&prior_modifier == &modifier &&
                    &prior_spelling == &spelling) {
                  reached_current_entry = true;
                  break;
                }
                if (prior_spelling == spelling) {
                  if (prior_modifier.kind_id != modifier.kind_id) {
                    throw "one modifier spelling maps to multiple kind IDs";
                  }
                  already_seen = true;
                }
              }
              if (reached_current_entry)
                break;
            }
            if (reached_current_entry)
              break;
          }
          if (!already_seen)
            ++count;
        }
      }
    }
    return count;
  };
  const check_end::InstructionDescriptor& inst_desc = T::get_inst_descriptor();
  constexpr size_t count = count_modifier_kind_entries(inst_desc);
  auto build_modifier_kind_entries =
      [&count](const check_end::InstructionDescriptor& instruction) constexpr
      -> std::array<ModifierKindEntry, count> {
    std::array<ModifierKindEntry, count> result{};
    std::size_t result_size = 0;
    for (const auto& variant : instruction.variants) {
      for (const auto& modifier : variant.modifiers) {
        for (const std::string_view spelling : modifier.allowed_values) {
          bool already_seen = false;
          for (std::size_t index = 0; index < result_size; ++index) {
            const auto& existing = result[index];
            if (existing.spelling != spelling)
              continue;
            if (existing.kind_id != modifier.kind_id) {
              throw "one modifier spelling maps to multiple kind IDs";
            }
            already_seen = true;
            break;
          }
          if (already_seen)
            continue;
          if (result_size == count) {
            throw "modifier-kind entry count does not match output capacity";
          }
          result[result_size++] = ModifierKindEntry{
              .spelling = spelling,
              .kind_id = modifier.kind_id,
          };
        }
      }
    }
    if (result_size != count) {
      throw "modifier-kind entry count does not match generated table";
    }
    return result;
  };
  constexpr std::array<ModifierKindEntry, count> find_map_r =
      build_modifier_kind_entries(inst_desc);

  for (const auto& entry : find_map_r) {
    if (entry.spelling == input_str)
      return std::string(entry.kind_id);
  }
  return std::nullopt;
}

using ActualModifierTable =
    std::unordered_map<std::string, const syntax_ast::AstModifier*>;

template <PtxOperator T>
std::expected<ActualModifierTable, ResolveDiagnostic> collect_actual_modifiers(
    const syntax_ast::AstInstruction& ast) {
  ActualModifierTable result;

  for (const auto& modifier : ast.modifiers) {
    const auto kind_id = get_kind_id<T>(modifier.syntax.text);
    if (!kind_id) {
      return std::unexpected(ResolveDiagnostic{
          .range = modifier.syntax.range,
          .message =
              fmt::format("Unknown modifier '{}'.", modifier.syntax.text),
      });
    }

    const auto [_, inserted] = result.emplace(*kind_id, &modifier);
    if (!inserted) {
      return std::unexpected(ResolveDiagnostic{
          .range = modifier.syntax.range,
          .message = fmt::format("Duplicate '{}' modifier.", *kind_id),
      });
    }
  }

  return result;
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
};  // namespace ptx_frontend::resolved_ir