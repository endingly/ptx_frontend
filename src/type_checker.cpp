#include "type_checker.hpp"
#include <array>
#include <magic_enum/magic_enum.hpp>
#include <type_traits>
#include <vector>
#include "ptx_ir/base.hpp"

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

bool TypeChecker::check_operand(const ParsedOp& op,
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

bool TypeChecker::check_operand(const ParsedOp& op, ScalarType expected) {
  // extract register name (if any) and look up in symbol table
  std::optional<Ident> reg_name;

  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, RegOrImmediate<Ident>>) {
          if (std::holds_alternative<Ident>(v.value))
            reg_name = std::get<Ident>(v.value);
          // immediate: skip type check
        } else if constexpr (std::is_same_v<T, ParsedOp::RegOffset>) {
          reg_name = v.base;
        } else if constexpr (std::is_same_v<T, ParsedOp::VecMemberIdx>) {
          reg_name = v.base;
        }
        // ImmediateValue / VecPack: skip
      },
      op.value);

  if (!reg_name)
    return true;  // immediate — skip

  auto it = sym_.find(std::string(*reg_name));
  if (it == sym_.end()) {
    error("undefined register: " + std::string(*reg_name));
    return false;
  }

  // if expected type is .b16, .b32, or .b64, allow matching .f16, .f32, or .f64 respectively
  if (expected == ScalarType::B16 or expected == ScalarType::B32 or
      expected == ScalarType::B64 or expected == ScalarType::B8 or
      expected == ScalarType::B128) {

    static constexpr auto all_enums = magic_enum::enum_values<ScalarType>();

    std::vector<ScalarType> matching_types;
    auto target_width = scalar_size_of(expected);
    for (auto&& item : all_enums) {
      if (scalar_size_of(item) == target_width)
        matching_types.push_back(item);
    }

    if (!utils::contains(matching_types, it->second.type))
      return false;
  } else {
    if (it->second.type != expected) {
      error("type mismatch on " + std::string(*reg_name) + ": expected " +
            to_string(expected) + ", got " + to_string(it->second.type));
      return false;
    }
  }

  return true;
}

bool TypeChecker::check_dst_src2(const InstrAdd<ParsedOp>& i, ScalarType t) {
  bool ok = true;
  ok &= check_operand(i.dst, t);
  ok &= check_operand(i.src1, t);
  ok &= check_operand(i.src2, t);
  return ok;
}

};  // namespace ptx_frontend