#include "type_checker.hpp"
#include <array>
#include <magic_enum/magic_enum.hpp>
#include <type_traits>
#include <variant>
#include <vector>
#include "ptx_ir/base.hpp"
#include "symbol_resolver.hpp"

namespace ptx_frontend {

void TypeChecker::error(std::string msg) {
  errors_.push_back(TypeError{std::move(msg)});
}

bool TypeChecker::require_sm(uint32_t min_v, std::string_view ctx) {
  if (target_.sm < min_v) {
    error(std::string(ctx) + " requires sm_" + std::to_string(min_v) +
          " or higher (target is sm_" + std::to_string(target_.sm) + ")");
    return false;
  }
  return true;
}

bool TypeChecker::require_ptx(float min_v, std::string_view ctx) {
  if (target_.ptx_version < min_v) {
    error(std::string(ctx) + " requires PTX " + std::to_string(min_v) +
          " or higher (target is PTX " + std::to_string(target_.ptx_version) +
          ")");
    return false;
  }
  return true;
}

bool TypeChecker::require_sm(uint32_t min_v) {
  return target_.sm >= min_v;
}

bool TypeChecker::require_ptx(float min_v) {
  return target_.ptx_version >= min_v;
}

bool TypeChecker::check_operand(const ResolvedOp& op,
                                const std::vector<ScalarType>& expected) {
  auto clear = [&](int32_t pop_num) {
    // pop n-1 errors, keep the last one which is the most relevant
    int32_t to_pop = pop_num;
    while (to_pop-- > 0 && !errors_.empty())
      errors_.pop_back();
  };

  for (int32_t i = 0; i < expected.size(); i++) {
    auto flag = check_operand(op, expected[i]);
    if (flag) {
      clear(i);  // pop errors from previous attempts
      return flag;
    }
  }
  clear(expected.size() - 1);  // pop all but the last error
  return false;
}

bool TypeChecker::check_operand(const ResolvedOp& op, ScalarType expected) {
  using Id = ResolvedOp::id_type;
  std::optional<Id> reg_name;
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, RegOrImmediate<Id>>) {
          if (std::holds_alternative<Id>(v.value))
            reg_name = std::get<Id>(v.value);
        } else if constexpr (std::is_same_v<T, ResolvedOp::RegOffset>) {
          reg_name = v.base;
        } else if constexpr (std::is_same_v<T, ResolvedOp::VecMemberIdx>) {
          reg_name = v.base;
        }
      },
      op.value);

  if (!reg_name)
    return true;  // immediate or vec-pack -> skip

  Type declared_type;
  std::string name_for_err;

  // check if the register is defined and get its type
  SymbolId id = *reg_name;
  if (id == kInvalidSymbolId) {
    error("undefined register id");
    return false;
  }
  const SymbolEntry& ent = sym_.entry(id);
  declared_type = ent.type;
  name_for_err = ent.name;

  auto actual_scalar = std::visit(
      [&](const auto& t) -> std::optional<ScalarType> {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, ScalarTypeT> or
                      std::is_same_v<T, VectorTypeT> or
                      std::is_same_v<T, ArrayTypeT>) {
          return t.type;
        }
        return std::nullopt;
      },
      declared_type);
  if (!actual_scalar) {
    error("type mismatch on " + name_for_err + ": expected " +
          to_string(expected) + ", got non-scalar type");
    return false;
  }

  // bit-kind: match by element bit-width (B32 matches F32 etc.)
  if (expected == ScalarType::B8 || expected == ScalarType::B16 ||
      expected == ScalarType::B32 || expected == ScalarType::B64 ||
      expected == ScalarType::B128) {

    auto target_width = scalar_size_of(expected);
    static constexpr auto all_enums = magic_enum::enum_values<ScalarType>();
    std::vector<ScalarType> matching;
    for (auto e : all_enums)
      if (scalar_size_of(e) == target_width)
        matching.push_back(e);

    if (!utils::contains(matching, *actual_scalar)) {
      error("type mismatch on " + name_for_err + ": expected " +
            to_string(expected) + ", got " + to_string(*actual_scalar));
      return false;
    }
    return true;
  }

  if (*actual_scalar != expected) {
    error("type mismatch on " + name_for_err + ": expected " +
          to_string(expected) + ", got " + to_string(*actual_scalar));
    return false;
  }
  return true;
}

};  // namespace ptx_frontend