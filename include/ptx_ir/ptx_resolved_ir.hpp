#pragma once
#include <fmt/core.h>
#include <bit>
#include <bitset>
#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <cstdint>
#include <expected>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <ptx_ir/base.hpp>
#include <source_location>
#include <type_traits>
#include <unordered_set>
#include "ptx_ir/ptx_syntax_ast.hpp"

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
T operator|(T lhs, T rhs) {
  using under_type = std::underlying_type<T>;
  return static_cast<T>(static_cast<under_type>(lhs) |
                        static_cast<under_type>(rhs));
}

template <typename T>
  requires(std::is_scoped_enum_v<T>)
bool valid(T enum_value) {
  using under_type = std::underlying_type<T>;
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
  Vector = 1 << 3,
  Group = 1 << 4,  // for op call syntax (...)
};

enum class OperandPresence : uint8_t {
  Required,
  Optional,
};

struct OperandSlotDescriptor {
  std::string field_id;               // "dst", "src1", "src2", "barrier_id"
  OperandSyntaxShape allowed_shapes;  // AST 阶段允许的语法形状
  OperandPresence presence;
};

enum class OperandLayoutKind : uint8_t {
  Flat,  // normal layout
};

struct OperandLayoutDescriptor {

  OperandLayoutKind layout_id;
  std::vector<OperandSlotDescriptor> slots;
};

struct OperandDescriptor {
  std::string_view name;  // "dst", "src1", "src2"
  OperandRole role;
  OperandAccess access;
  OperandShape allowed_shapes;
  StateSpace allowed_state_spaces;
};

enum class PresenceRequirement {
  Absent,    // YAML: absent
  Optional,  // YAML: optional
  Required,  // YAML: required / fixed
};

struct ModifierDescriptor {
  std::unordered_set<std::string> allowed_values;
  PresenceRequirement presence;
  std::string kind_id;

  bool check(std::string modifier_str) const;
};

struct VariantDescriptor {
  std::string variant_name;
  std::vector<ModifierDescriptor> modifiers;
  std::vector<OperandLayoutDescriptor> operand_layouts;

  int32_t get_required_modifier_num() const;
};

struct InstructionDescriptor {
  std::string Opcode_name;
  std::vector<VariantDescriptor> variants;
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
    enum class Type {
      U16,
      U32,
      U64,
      S16,
      S32,
      S64,
    };
    // no saturation for this variant
    WithLocs<Type> type;
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
    enum class Type {
      U16x2,
      S16x2,
    };
    // no saturation for this variant
    WithLocs<Type> type;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_packed_optional_sat_sm120
  struct PackedOptionalSatSm120 {
    enum class Type {
      U8x4,
      S8x4,
    };

    WithLocs<Type> type;
    WithLocs<bool> saturate;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_sat_sm120
  struct SatSm120 {
    enum class Type {
      U16x2,
      S16x2,
      U32,
    };
    // saturation is always true for this variant
    WithLocs<Type> type;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  using Variant = std::variant<IntegerNoSat, SatS32, SimdNoSatSm90,
                               PackedOptionalSatSm120, SatSm120>;
  Variant variant;

  static std::expected<Add, ResolveDiagnostic> resolve(
      const syntax_ast::AstInstruction& ast);
  bool check();
  static check_end::InstructionDescriptor get_inst_descriptor();
};

template <typename T>
concept PtxOperator = requires(T object) {
  typename T::VariantType;
  requires std::is_scoped_enum_v<typename T::VariantType>;
  { object.check() } -> std::same_as<bool>;
  {
    T::get_inst_descriptor()
  } -> std::same_as<check_end::InstructionDescriptor>;
};

using str_map =
    std::unordered_map<std::string, std::unordered_set<std::string>>;

template <PtxOperator T>
std::optional<std::string> get_kind_id(std::string input_str) {
  str_map m_result;
  auto inst_desc = T::get_inst_descriptor();
  for (const auto& item : inst_desc.variants) {
    for (const auto& item_m : item.modifiers) {
      if (!m_result.contains(item_m.kind_id)) {
        m_result[item_m.kind_id] = std::unordered_set<std::string>();
        m_result[item_m.kind_id].insert_range(item_m.allowed_values);
      } else {
        for (const auto& item_c : item_m.allowed_values) {
          if (!m_result[item_m.kind_id].contains(item_c)) {
            m_result[item_m.kind_id].insert(item_c);
          }
        }
      }
    }
  }

  for (const auto& item : m_result) {
    if (item.second.contains(input_str))
      return item.first;
    else
      continue;
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

bool matches_modifier_slot(const check_end::ModifierDescriptor& descriptor,
                           const ActualModifierTable& actual_modifiers);

bool matches_variant(const check_end::VariantDescriptor& variant,
                     const ActualModifierTable& actual_modifiers);

};  // namespace ptx_frontend::resolved_ir