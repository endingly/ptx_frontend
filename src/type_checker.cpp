#include "type_checker.hpp"
#include <array>
#include <type_traits>
#include <vector>

namespace ptx_frontend {

template <typename T, std::size_t N>
bool is_one_of(T& target_item,
               const std::array<std::remove_const_t<T>, N>& arr) {
  for (const auto& item : arr) {
    if (target_item == item) {
      return true;
    }
  }
  return false;
}

// ── operand-kind predicates ─────────────────────────────────────────────────
// These are used by the generated type-checker code to verify that each
// instruction argument has the correct form (register, immediate, or either).

/// Returns true when `op` is a register reference (including vector-member
/// access like %r0.x).  Immediates, register-offset addresses, and vector
/// packs all return false.
template <OperandLike Op>
static bool operand_is_reg(const Op& op) {
  using Id = typename Op::id_type;
  using RegOffset = typename Op::RegOffset;
  using VecMemberIdx = typename Op::VecMemberIdx;
  using VecPack = typename Op::VecPack;
  return std::visit(
      [](const auto& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, VecMemberIdx>)
          return true;  // %r0.x  — register component
        else if constexpr (std::is_same_v<T, RegOrImmediate<Id>>)
          return std::holds_alternative<Id>(v.value);  // only the Reg branch
        else
          return false;  // RegOffset / ImmediateValue / VecPack
      },
      op.value);
}

/// Returns true when `op` is an immediate value (integer or float literal).
template <OperandLike Op>
static bool operand_is_imm(const Op& op) {
  using Id = typename Op::id_type;
  return std::visit(
      [](const auto& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ImmediateValue>)
          return true;
        else if constexpr (std::is_same_v<T, RegOrImmediate<Id>>)
          return std::holds_alternative<ImmediateValue>(v.value);
        else
          return false;
      },
      op.value);
}

/// Returns true when `op` is a register or an immediate value.
/// Register-offset addresses ([%r+4]) and vector packs ({%r0, %r1}) are
/// rejected because they are not valid arithmetic source operands.
template <OperandLike Op>
static bool operand_is_reg_or_imm(const Op& op) {
  using VecPack = typename Op::VecPack;
  using RegOffset = typename Op::RegOffset;
  return std::visit(
      [](const auto& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        // Register-offset (e.g. [%r0+4]) is a memory-address form, not a
        // scalar register or immediate — reject it.
        if constexpr (std::is_same_v<T, RegOffset>)
          return false;
        // Vector pack ({%r0, %r1, ...}) is not a scalar operand.
        else if constexpr (std::is_same_v<T, VecPack>)
          return false;
        else
          return true;  // VecMemberIdx, RegOrImmediate, ImmediateValue
      },
      op.value);
}

// ── TypeChecker methods ─────────────────────────────────────────────────────

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

#include "type_checker.src.gen"

};  // namespace ptx_frontend