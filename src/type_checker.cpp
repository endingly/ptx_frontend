#include "type_checker.hpp"
#include <array>
#include <type_traits>
#include <vector>

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

void TypeChecker::check_operand(const ParsedOp& op, ScalarType expected) {
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
    return;  // immediate — skip

  auto it = sym_.find(std::string(*reg_name));
  if (it == sym_.end()) {
    error("undefined register: " + std::string(*reg_name));
    return;
  }
  if (it->second.type != expected)
    error("type mismatch on " + std::string(*reg_name) + ": expected " +
          to_string(expected) + ", got " + to_string(it->second.type));
}

void TypeChecker::check_dst_src2(const InstrAdd<ParsedOp>& i, ScalarType t) {
  check_operand(i.dst, t);
  check_operand(i.src1, t);
  check_operand(i.src2, t);
}

};  // namespace ptx_frontend