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

void TypeChecker::error(std::string msg) {
  errors_.push_back(TypeError{std::move(msg)});
}

void TypeChecker::require_sm(uint32_t min_v, std::string_view ctx) {
  if (target_.sm < min_v)
    error(std::string(ctx) + " requires sm_" + std::to_string(min_v) +
          " or higher (target is sm_" + std::to_string(target_.sm) + ")");
}

void TypeChecker::require_ptx(float min_v, std::string_view ctx) {
  if (target_.ptx_version < min_v)
    error(std::string(ctx) + " requires PTX " + std::to_string(min_v) +
          " or higher (target is PTX " + std::to_string(target_.ptx_version) +
          ")");
}

void TypeChecker::check_add(const InstrAdd<ParsedOp>& i) {
  std::visit(
      [&](auto&& d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, ArithInteger>)
          check_add_integer(i);
        else if constexpr (std::is_same_v<T, ArithFloat>)
          check_add_float(i);
      },
      i.data);
}

void TypeChecker::check_add_integer(const InstrAdd<ParsedOp>& i) {
  using ST = ScalarType;
  auto d = std::get<ArithInteger>(i.data);
  const auto t = d.type_;
  const bool sat = d.saturate;

  // ── variant: integer no-sat, universal (ptx 1.0, sm 0) ─────────────────
  static constexpr std::array universal_nosat = {ST::U16, ST::U64, ST::S16,
                                                 ST::S64, ST::U32, ST::S32};
  if (!sat && is_one_of(t, universal_nosat)) {
    check_dst_src2(i, t);
    return;
  }
  // ── variant: add.sat.s32, universal (ptx 1.0, sm 0) ────────────────────
  if (sat && t == ST::S32) {
    check_dst_src2(i, ST::S32);
    return;
  }
  // ── variant: SIMD no-sat (ptx 8.0, sm 90+) ─────────────────────────────
  if (!sat && (t == ST::U16x2 || t == ST::S16x2)) {
    require_sm(90, "add" + to_string(t));
    require_ptx(8.0, "add" + to_string(t));
    check_dst_src2(i, t);
    return;
  }
  // ── variant: packed optional-sat (ptx 9.2, sm 120+) ────────────────────
  if (t == ST::U8x4 || t == ST::S8x4) {
    require_sm(120, "add" + to_string(t));
    require_ptx(9.2, "add" + to_string(t));
    check_dst_src2(i, t);
    return;
  }
  // ── variant: SIMD sat + u32 sat (ptx 9.2, sm 120+) ─────────────────────
  if (sat && (t == ST::U16x2 || t == ST::S16x2 || t == ST::U32)) {
    require_sm(120, "add.sat" + to_string(t));
    require_ptx(9.2, "add.sat" + to_string(t));
    check_dst_src2(i, t);
    return;
  }

  error("add: illegal integer combination type=" + to_string(t) +
        " sat=" + (sat ? "true" : "false"));
}

void TypeChecker::check_add_float(const InstrAdd<ParsedOp>& i) {
  using ST = ScalarType;
  using RM = RoundingMode;
  const auto d = std::get<ArithFloat>(i.data);
  const auto t = d.type_;
  const bool has_rnd = !d.is_fusable;  // is_fusable == rnd absent

  // value-level min_sm helper: .rm / .rp carry extra SM requirement
  auto check_rm_rp_sm = [&](int required_sm, std::string_view ctx) {
    if (has_rnd &&
        (d.rounding == RM::NegativeInf || d.rounding == RM::PositiveInf))
      require_sm(required_sm, std::string(ctx) + " with .rm/.rp");
  };

  // only-.rn helper
  auto require_rn_only = [&](std::string_view ctx) {
    if (has_rnd && d.rounding != RM::NearestEven)
      error(std::string(ctx) + " only supports .rn rounding");
  };

  switch (t) {
    // ── add.f32 (ptx 1.0, sm 0; .rm/.rp → sm 20) ───────────────────────────
    case ST::F32:
      check_rm_rp_sm(20, "add.f32");
      check_dst_src2(i, ST::F32);
      break;

    // ── add.f64 (ptx 1.0, sm 13; .rm/.rp inherit sm 13) ────────────────────
    case ST::F64:
      require_sm(13, "add.f64");
      check_dst_src2(i, ST::F64);
      break;

    // ── add.f32x2 (ptx 8.6, sm 100; no sat) ────────────────────────────────
    case ST::F32x2:
      require_sm(100, "add.f32x2");
      require_ptx(8.6, "add.f32x2");
      if (d.saturate)
        error("add.f32x2 does not support .sat");
      check_dst_src2(i, ST::F32x2);
      break;

    // ── add.f16 / .f16x2 (ptx 6.0, sm 53; .rn only) ────────────────────────
    case ST::F16:
    case ST::F16x2:
      require_sm(53, "add" + to_string(t));
      require_ptx(6.0, "add" + to_string(t));
      require_rn_only("add" + to_string(t));
      check_dst_src2(i, t);
      break;

    // ── add.bf16 / .bf16x2 (ptx 7.0, sm 80; .rn only; no sat/ftz) ──────────
    case ST::BF16:
    case ST::BF16x2:
      require_sm(80, "add" + to_string(t));
      require_ptx(7.0, "add" + to_string(t));
      require_rn_only("add" + to_string(t));
      if (d.saturate)
        error("add" + to_string(t) + " does not support .sat");
      if (d.flush_to_zero.has_value())
        error("add" + to_string(t) + " does not support .ftz");
      check_dst_src2(i, t);
      break;

    default:
      error("add: unsupported float type: " + to_string(t));
  }
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