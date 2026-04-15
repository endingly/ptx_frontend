// ============================================================
// AUTO-GENERATED -- do NOT edit by hand.
// Re-generate with:  python3 scripts/gen_type_checker_code.py
// ============================================================

#include "type_checker_gen.hpp"
#include <array>
#include <optional>
#include <string>
#include <variant>

namespace ptx_frontend {

namespace {
template <typename T, std::size_t N>
bool is_one_of(const T& val, const std::array<T, N>& arr) {
  for (const auto& x : arr) if (val == x) return true;
  return false;
}
}  // namespace

void TypeCheckerGen::check_module(const ResolvedModule& /*mod*/) {
  // Dispatch happens via ptx_visit_dispatch.hpp visitor pattern.
}

// ── check_op_scalar_type ───────────────────────────────────────────────────
// Verify that a resolved register operand has the expected scalar type.
// Extracts a SymbolId from the operand, looks it up in the symbol table,
// and compares the declared type.  Immediate and address operands are
// silently ignored (they carry no type information in the symbol table).
void TypeCheckerGen::check_op_scalar_type(
    const ResolvedOp& op, ScalarType expected, const char* ctx) {
  if (!resolved_symbols_) return;

  // Walk the ParsedOperand variant to find a SymbolId.
  std::optional<SymbolId> sym_id;
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, RegOrImmediate<SymbolId>>) {
          if (std::holds_alternative<SymbolId>(v.value))
            sym_id = std::get<SymbolId>(v.value);
        } else if constexpr (std::is_same_v<T, ResolvedOp::RegOffset>) {
          sym_id = v.base;
        } else if constexpr (std::is_same_v<T, ResolvedOp::VecMemberIdx>) {
          sym_id = v.base;
        }
        // ImmediateValue, VecPack, Pred: no symbol to look up
      },
      op.value);

  if (!sym_id.has_value() || *sym_id == kInvalidSymbolId) return;

  const auto& entry = resolved_symbols_->entry(*sym_id);
  // Only scalar types carry a single ScalarType tag.
  if (!std::holds_alternative<ScalarTypeT>(entry.type)) return;
  ScalarType actual = std::get<ScalarTypeT>(entry.type).type;
  if (actual != expected) {
    error(std::string(ctx) + ": operand type mismatch: expected "
          + to_string(expected)
          + " got " + to_string(actual));
  }
}

// PTX ISA §9.7.6.1
void TypeCheckerGen::check_setp(const InstrSetp<ResolvedOp>& instr) {
  // variant 0: setp with optional bool combinator
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "setp with optional bool combinator")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::B16, ScalarType::B32, ScalarType::B64, ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64, ScalarType::F32, ScalarType::F64, ScalarType::F16, ScalarType::F16x2, ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(instr.data.type_, _allowed); }()))) { error("setp with optional bool combinator: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("setp: no matching variant found");
  }
}

// PTX ISA §9.7.6.2
void TypeCheckerGen::check_set(const InstrSet<ResolvedOp>& instr) {
  // variant 0: set with optional bool combinator
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "set with optional bool combinator")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("set: no matching variant found");
  }
}

// PTX ISA §9.7.10.1
void TypeCheckerGen::check_bra(const InstrBra<ResolvedOp>& instr) {
  // variant 0: bra (uni)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "bra (uni)")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("bra: no matching variant found");
  }
}

// PTX ISA §9.7.10.3
void TypeCheckerGen::check_call(const InstrCall<ResolvedOp>& instr) {
  // variant 0: call (uni) with optional return list
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "call (uni) with optional return list")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("call: no matching variant found");
  }
}

// PTX ISA §9.7.10.4
void TypeCheckerGen::check_ret(const InstrRet& instr) {
  // variant 0: ret (uni)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "ret (uni)")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("ret: no matching variant found");
  }
}

// PTX ISA §9.7.10.6
void TypeCheckerGen::check_trap(const InstrTrap& instr) {
  // variant 0: trap
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "trap")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("trap: no matching variant found");
  }
}

// PTX ISA §9.7.10.7
void TypeCheckerGen::check_nanosleep(const InstrNanosleep<ResolvedOp>& instr) {
  // variant 0: nanosleep.u32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "nanosleep.u32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("nanosleep: no matching variant found");
  }
}

// PTX ISA §9.7.9.3
void TypeCheckerGen::check_mov(const InstrMov<ResolvedOp>& instr) {
  // variant 0: mov scalar/pred \x2014 dst and src are both scalar
  auto check_v0 = [&]() -> bool {
    return true;
  };
  // variant 1: mov vector pack \x2014 dst is scalar reg, src is { a, b [, c, d] } The number of elements is inferred from sizeof(type) / sizeof(element). e.g. mov.b32 r, {a,b} means 2\xd7u16\x2192b32\x0a
  auto check_v1 = [&]() -> bool {
    return true;
  };
  // variant 2: mov vector unpack \x2014 dst is { d, e [, f, g] }, src is scalar reg e.g. mov.b64 {lo, hi}, %x\x0a
  auto check_v2 = [&]() -> bool {
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2())) {
    error("mov: no matching variant found");
  }
}

// PTX ISA §9.7.9.8
void TypeCheckerGen::check_ld(const InstrLd<ResolvedOp>& instr) {
  // variant 0: ld weak/default
  auto check_v0 = [&]() -> bool {
    return true;
  };
  // variant 1: ld volatile
  auto check_v1 = [&]() -> bool {
    return true;
  };
  // variant 2: ld relaxed/acquire
  auto check_v2 = [&]() -> bool {
    return true;
  };
  // variant 3: ld.global.nc (non-coherent)
  auto check_v3 = [&]() -> bool {
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3())) {
    error("ld: no matching variant found");
  }
}

// PTX ISA §9.7.9.11
void TypeCheckerGen::check_st(const InstrSt<ResolvedOp>& instr) {
  // variant 0: st weak/default
  auto check_v0 = [&]() -> bool {
    return true;
  };
  // variant 1: st volatile
  auto check_v1 = [&]() -> bool {
    return true;
  };
  // variant 2: st relaxed/release
  auto check_v2 = [&]() -> bool {
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2())) {
    error("st: no matching variant found");
  }
}

// PTX ISA §9.7.9.15
void TypeCheckerGen::check_prefetch(const InstrPrefetch<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("prefetch: no matching variant found");
  }
}

// PTX ISA §9.7.9.15
void TypeCheckerGen::check_prefetchu(const InstrPrefetch<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("prefetchu: no matching variant found");
  }
}

// PTX ISA §9.7.9.21
void TypeCheckerGen::check_cvt(const InstrCvt<ResolvedOp>& instr) {
  // variant 0: cvt integer/float rounding
  auto check_v0 = [&]() -> bool {
    return true;
  };
  // variant 1: cvt f8x2 pack (frnd2)
  auto check_v1 = [&]() -> bool {
    return true;
  };
  // variant 2: cvt f8x2 from f32 (rn.satfinite)
  auto check_v2 = [&]() -> bool {
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2())) {
    error("cvt: no matching variant found");
  }
}

// PTX ISA §9.7.9.22
void TypeCheckerGen::check_cvt_pack(const InstrCvtPack<ResolvedOp>& instr) {
  // variant 0: cvt.pack.sat.etype.ntype (8-bit)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(7.0f, "cvt.pack.sat.etype.ntype (8-bit)")) return false;
    if (!require_sm(72u, "cvt.pack.sat.etype.ntype (8-bit)")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U8, ScalarType::S8}); return is_one_of(instr.data.to, _allowed); }()))) { error("cvt.pack.sat.etype.ntype (8-bit): modifier to check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::S32, ScalarType::U32}); return is_one_of(instr.data.from, _allowed); }()))) { error("cvt.pack.sat.etype.ntype (8-bit): modifier from check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("cvt.pack: no matching variant found");
  }
}

// PTX ISA §9.7.9.25
void TypeCheckerGen::check_cp_async(const InstrCpAsync<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("cp.async: no matching variant found");
  }
}

// PTX ISA §9.7.9.25.3.1
void TypeCheckerGen::check_cp_async_commit_group(const InstrCpAsyncCommitGroup& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("cp.async.commit_group: no matching variant found");
  }
}

// PTX ISA §9.7.9.25.3.2
void TypeCheckerGen::check_cp_async_wait_group(const InstrCpAsyncWaitGroup<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("cp.async.wait_group: no matching variant found");
  }
}

// PTX ISA §9.7.9.25.3.3
void TypeCheckerGen::check_cp_async_wait_all(const InstrCpAsyncWaitAll& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("cp.async.wait_all: no matching variant found");
  }
}

// PTX ISA §9.7.9.18
void TypeCheckerGen::check_createpolicy_fractional(const InstrCreatePolicyFractional<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("createpolicy.fractional: no matching variant found");
  }
}

// PTX ISA §9.7.8.3 (read-only data cache load, deprecated since PTX 6.5)
void TypeCheckerGen::check_ldu(const InstrLdu<ResolvedOp>& instr) {
  // variant 0: ldu.global.type dst, [src]
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(2.0f, "ldu.global.type dst, [src]")) return false;
    if (!require_sm(20u, "ldu.global.type dst, [src]")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("ldu: no matching variant found");
  }
}

// PTX ISA §9.7.2.1
void TypeCheckerGen::check_fma(const InstrFma<ResolvedOp>& instr) {
  // variant 0: fma.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "fma.f32")) return false;
    if (!((instr.data.type_ == ScalarType::F32))) { error("fma.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "fma.f32: dst");
    check_op_scalar_type(instr.src1, instr.data.type_, "fma.f32: src1");
    check_op_scalar_type(instr.src2, instr.data.type_, "fma.f32: src2");
    check_op_scalar_type(instr.src3, instr.data.type_, "fma.f32: src3");
    return true;
  };
  // variant 1: fma.f64
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "fma.f64")) return false;
    if (!require_sm(13u, "fma.f64")) return false;
    if (!((instr.data.type_ == ScalarType::F64))) { error("fma.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "fma.f64: dst");
    check_op_scalar_type(instr.src1, instr.data.type_, "fma.f64: src1");
    check_op_scalar_type(instr.src2, instr.data.type_, "fma.f64: src2");
    check_op_scalar_type(instr.src3, instr.data.type_, "fma.f64: src3");
    return true;
  };
  // variant 2: fma half-precision (f16/f16x2)
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(6.0f, "fma half-precision (f16/f16x2)")) return false;
    if (!require_sm(53u, "fma half-precision (f16/f16x2)")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2}); return is_one_of(instr.data.type_, _allowed); }()))) { error("fma half-precision (f16/f16x2): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "fma half-precision (f16/f16x2): dst");
    check_op_scalar_type(instr.src1, instr.data.type_, "fma half-precision (f16/f16x2): src1");
    check_op_scalar_type(instr.src2, instr.data.type_, "fma half-precision (f16/f16x2): src2");
    check_op_scalar_type(instr.src3, instr.data.type_, "fma half-precision (f16/f16x2): src3");
    return true;
  };
  // variant 3: fma bf16/bf16x2
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(7.0f, "fma bf16/bf16x2")) return false;
    if (!require_sm(80u, "fma bf16/bf16x2")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(instr.data.type_, _allowed); }()))) { error("fma bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "fma bf16/bf16x2: dst");
    check_op_scalar_type(instr.src1, instr.data.type_, "fma bf16/bf16x2: src1");
    check_op_scalar_type(instr.src2, instr.data.type_, "fma bf16/bf16x2: src2");
    check_op_scalar_type(instr.src3, instr.data.type_, "fma bf16/bf16x2: src3");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3())) {
    error("fma: no matching variant found");
  }
}

// PTX ISA §9.7.2.2
void TypeCheckerGen::check_rcp(const InstrRcp<ResolvedOp>& instr) {
  // variant 0: rcp.approx.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "rcp.approx.f32")) return false;
    if (!((instr.data.type_ == ScalarType::F32))) { error("rcp.approx.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "rcp.approx.f32: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "rcp.approx.f32: src");
    return true;
  };
  // variant 1: rcp.approx.f64
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "rcp.approx.f64")) return false;
    if (!((instr.data.type_ == ScalarType::F64))) { error("rcp.approx.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "rcp.approx.f64: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "rcp.approx.f64: src");
    return true;
  };
  // variant 2: rcp.rnd.f32
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "rcp.rnd.f32")) return false;
    if (!((instr.data.type_ == ScalarType::F32))) { error("rcp.rnd.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "rcp.rnd.f32: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "rcp.rnd.f32: src");
    return true;
  };
  // variant 3: rcp.rnd.f64
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(1.0f, "rcp.rnd.f64")) return false;
    if (!((instr.data.type_ == ScalarType::F64))) { error("rcp.rnd.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "rcp.rnd.f64: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "rcp.rnd.f64: src");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3())) {
    error("rcp: no matching variant found");
  }
}

// PTX ISA §9.7.2.3
void TypeCheckerGen::check_sqrt(const InstrSqrt<ResolvedOp>& instr) {
  // variant 0: sqrt.approx.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "sqrt.approx.f32")) return false;
    if (!((instr.data.type_ == ScalarType::F32))) { error("sqrt.approx.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "sqrt.approx.f32: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "sqrt.approx.f32: src");
    return true;
  };
  // variant 1: sqrt.rnd.f32
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "sqrt.rnd.f32")) return false;
    if (!((instr.data.type_ == ScalarType::F32))) { error("sqrt.rnd.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "sqrt.rnd.f32: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "sqrt.rnd.f32: src");
    return true;
  };
  // variant 2: sqrt.rnd.f64
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "sqrt.rnd.f64")) return false;
    if (!require_sm(13u, "sqrt.rnd.f64")) return false;
    if (!((instr.data.type_ == ScalarType::F64))) { error("sqrt.rnd.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "sqrt.rnd.f64: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "sqrt.rnd.f64: src");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2())) {
    error("sqrt: no matching variant found");
  }
}

// PTX ISA §9.7.2.4
void TypeCheckerGen::check_rsqrt(const InstrRsqrt<ResolvedOp>& instr) {
  // variant 0: rsqrt.approx.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "rsqrt.approx.f32")) return false;
    if (!((instr.data.type_ == ScalarType::F32))) { error("rsqrt.approx.f32: modifier type_ check failed"); return false; }
    return true;
  };
  // variant 1: rsqrt.approx.f64
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "rsqrt.approx.f64")) return false;
    if (!((instr.data.type_ == ScalarType::F64))) { error("rsqrt.approx.f64: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0() || check_v1())) {
    error("rsqrt: no matching variant found");
  }
}

// PTX ISA §9.7.2.5
void TypeCheckerGen::check_sin(const InstrSin<ResolvedOp>& instr) {
  // variant 0: sin.approx.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "sin.approx.f32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("sin: no matching variant found");
  }
}

// PTX ISA §9.7.2.6
void TypeCheckerGen::check_cos(const InstrCos<ResolvedOp>& instr) {
  // variant 0: cos.approx.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "cos.approx.f32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("cos: no matching variant found");
  }
}

// PTX ISA §9.7.2.7
void TypeCheckerGen::check_lg2(const InstrLg2<ResolvedOp>& instr) {
  // variant 0: lg2.approx.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "lg2.approx.f32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("lg2: no matching variant found");
  }
}

// PTX ISA §9.7.2.8
void TypeCheckerGen::check_ex2(const InstrEx2<ResolvedOp>& instr) {
  // variant 0: ex2.approx.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "ex2.approx.f32")) return false;
    if (!((instr.data.type_ == ScalarType::F32))) { error("ex2.approx.f32: modifier type_ check failed"); return false; }
    return true;
  };
  // variant 1: ex2.approx f16/f16x2/bf16/bf16x2
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(6.0f, "ex2.approx f16/f16x2/bf16/bf16x2")) return false;
    if (!require_sm(53u, "ex2.approx f16/f16x2/bf16/bf16x2")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2, ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(instr.data.type_, _allowed); }()))) { error("ex2.approx f16/f16x2/bf16/bf16x2: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0() || check_v1())) {
    error("ex2: no matching variant found");
  }
}

// PTX ISA §9.7.2.9
void TypeCheckerGen::check_tanh(const InstrTanh<ResolvedOp>& instr) {
  // variant 0: tanh.approx.f32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(7.5f, "tanh.approx.f32")) return false;
    if (!require_sm(80u, "tanh.approx.f32")) return false;
    if (!((instr.data == ScalarType::F32))) { error("tanh.approx.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "tanh.approx.f32: dst");
    check_op_scalar_type(instr.src, instr.data, "tanh.approx.f32: src");
    return true;
  };
  // variant 1: tanh.approx f16/f16x2/bf16/bf16x2
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(7.5f, "tanh.approx f16/f16x2/bf16/bf16x2")) return false;
    if (!require_sm(80u, "tanh.approx f16/f16x2/bf16/bf16x2")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2, ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(instr.data, _allowed); }()))) { error("tanh.approx f16/f16x2/bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "tanh.approx f16/f16x2/bf16/bf16x2: dst");
    check_op_scalar_type(instr.src, instr.data, "tanh.approx f16/f16x2/bf16/bf16x2: src");
    return true;
  };
  if (!(check_v0() || check_v1())) {
    error("tanh: no matching variant found");
  }
}

// PTX ISA §9.7.2.10
void TypeCheckerGen::check_copysign(const InstrCopysign<ResolvedOp>& instr) {
  // variant 0: copysign.f32/f64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(7.0f, "copysign.f32/f64")) return false;
    if (!require_sm(80u, "copysign.f32/f64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F32, ScalarType::F64}); return is_one_of(instr.data, _allowed); }()))) { error("copysign.f32/f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "copysign.f32/f64: dst");
    check_op_scalar_type(instr.src1, instr.data, "copysign.f32/f64: src1");
    check_op_scalar_type(instr.src2, instr.data, "copysign.f32/f64: src2");
    return true;
  };
  if (!(check_v0())) {
    error("copysign: no matching variant found");
  }
}

// PTX ISA §9.7.1.1
void TypeCheckerGen::check_add(const InstrAdd<ResolvedOp>& instr) {
  // variant 0: add integer no-sat (universal)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "add integer no-sat (universal)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U64, ScalarType::S16, ScalarType::S64, ScalarType::U32, ScalarType::S32}); return is_one_of(d.type_, _allowed); }()))) { error("add integer no-sat (universal): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add integer no-sat (universal): dst");
    check_op_scalar_type(instr.src1, d.type_, "add integer no-sat (universal): src1");
    check_op_scalar_type(instr.src2, d.type_, "add integer no-sat (universal): src2");
    return true;
  };
  // variant 1: add.sat.s32 (universal)
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "add.sat.s32 (universal)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!((d.saturate == true))) { error("add.sat.s32 (universal): modifier saturate check failed"); return false; }
    if (!((d.type_ == ScalarType::S32))) { error("add.sat.s32 (universal): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add.sat.s32 (universal): dst");
    check_op_scalar_type(instr.src1, d.type_, "add.sat.s32 (universal): src1");
    check_op_scalar_type(instr.src2, d.type_, "add.sat.s32 (universal): src2");
    return true;
  };
  // variant 2: add SIMD no-sat (sm_90+)
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(8.0f, "add SIMD no-sat (sm_90+)")) return false;
    if (!require_sm(90u, "add SIMD no-sat (sm_90+)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16x2, ScalarType::S16x2}); return is_one_of(d.type_, _allowed); }()))) { error("add SIMD no-sat (sm_90+): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add SIMD no-sat (sm_90+): dst");
    check_op_scalar_type(instr.src1, d.type_, "add SIMD no-sat (sm_90+): src1");
    check_op_scalar_type(instr.src2, d.type_, "add SIMD no-sat (sm_90+): src2");
    return true;
  };
  // variant 3: add packed optional-sat (sm_120f family)
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(9.2f, "add packed optional-sat (sm_120f family)")) return false;
    if (!require_sm(120u, "add packed optional-sat (sm_120f family)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U8x4, ScalarType::S8x4}); return is_one_of(d.type_, _allowed); }()))) { error("add packed optional-sat (sm_120f family): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add packed optional-sat (sm_120f family): dst");
    check_op_scalar_type(instr.src1, d.type_, "add packed optional-sat (sm_120f family): src1");
    check_op_scalar_type(instr.src2, d.type_, "add packed optional-sat (sm_120f family): src2");
    return true;
  };
  // variant 4: add SIMD sat / add.sat.u32 (sm_120f family)
  auto check_v4 = [&]() -> bool {
    if (!require_ptx(9.2f, "add SIMD sat / add.sat.u32 (sm_120f family)")) return false;
    if (!require_sm(120u, "add SIMD sat / add.sat.u32 (sm_120f family)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!((d.saturate == true))) { error("add SIMD sat / add.sat.u32 (sm_120f family): modifier saturate check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16x2, ScalarType::S16x2, ScalarType::U32}); return is_one_of(d.type_, _allowed); }()))) { error("add SIMD sat / add.sat.u32 (sm_120f family): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add SIMD sat / add.sat.u32 (sm_120f family): dst");
    check_op_scalar_type(instr.src1, d.type_, "add SIMD sat / add.sat.u32 (sm_120f family): src1");
    check_op_scalar_type(instr.src2, d.type_, "add SIMD sat / add.sat.u32 (sm_120f family): src2");
    return true;
  };
  // variant 5: add.f32
  auto check_v5 = [&]() -> bool {
    if (!require_ptx(1.0f, "add.f32")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32))) { error("add.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add.f32: dst");
    check_op_scalar_type(instr.src1, d.type_, "add.f32: src1");
    check_op_scalar_type(instr.src2, d.type_, "add.f32: src2");
    return true;
  };
  // variant 6: add.f64
  auto check_v6 = [&]() -> bool {
    if (!require_ptx(1.0f, "add.f64")) return false;
    if (!require_sm(13u, "add.f64")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F64))) { error("add.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add.f64: dst");
    check_op_scalar_type(instr.src1, d.type_, "add.f64: src1");
    check_op_scalar_type(instr.src2, d.type_, "add.f64: src2");
    return true;
  };
  // variant 7: add.f32x2
  auto check_v7 = [&]() -> bool {
    if (!require_ptx(8.6f, "add.f32x2")) return false;
    if (!require_sm(100u, "add.f32x2")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32x2))) { error("add.f32x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add.f32x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "add.f32x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "add.f32x2: src2");
    return true;
  };
  // variant 8: add half-precision (f16, f16x2)
  auto check_v8 = [&]() -> bool {
    if (!require_ptx(6.0f, "add half-precision (f16, f16x2)")) return false;
    if (!require_sm(53u, "add half-precision (f16, f16x2)")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2}); return is_one_of(d.type_, _allowed); }()))) { error("add half-precision (f16, f16x2): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add half-precision (f16, f16x2): dst");
    check_op_scalar_type(instr.src1, d.type_, "add half-precision (f16, f16x2): src1");
    check_op_scalar_type(instr.src2, d.type_, "add half-precision (f16, f16x2): src2");
    return true;
  };
  // variant 9: add bf16/bf16x2
  auto check_v9 = [&]() -> bool {
    if (!require_ptx(7.0f, "add bf16/bf16x2")) return false;
    if (!require_sm(80u, "add bf16/bf16x2")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(d.type_, _allowed); }()))) { error("add bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "add bf16/bf16x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "add bf16/bf16x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "add bf16/bf16x2: src2");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3() || check_v4() || check_v5() || check_v6() || check_v7() || check_v8() || check_v9())) {
    error("add: no matching variant found");
  }
}

// PTX ISA §9.7.1.2
void TypeCheckerGen::check_sub(const InstrSub<ResolvedOp>& instr) {
  // variant 0: sub integer no-sat (universal)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "sub integer no-sat (universal)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S64, ScalarType::S32}); return is_one_of(d.type_, _allowed); }()))) { error("sub integer no-sat (universal): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub integer no-sat (universal): dst");
    check_op_scalar_type(instr.src1, d.type_, "sub integer no-sat (universal): src1");
    check_op_scalar_type(instr.src2, d.type_, "sub integer no-sat (universal): src2");
    return true;
  };
  // variant 1: sub.sat.s32 (universal)
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "sub.sat.s32 (universal)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!((d.saturate == true))) { error("sub.sat.s32 (universal): modifier saturate check failed"); return false; }
    if (!((d.type_ == ScalarType::S32))) { error("sub.sat.s32 (universal): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub.sat.s32 (universal): dst");
    check_op_scalar_type(instr.src1, d.type_, "sub.sat.s32 (universal): src1");
    check_op_scalar_type(instr.src2, d.type_, "sub.sat.s32 (universal): src2");
    return true;
  };
  // variant 2: sub SIMD no-sat (sm_90+)
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(8.0f, "sub SIMD no-sat (sm_90+)")) return false;
    if (!require_sm(90u, "sub SIMD no-sat (sm_90+)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16x2, ScalarType::S16x2}); return is_one_of(d.type_, _allowed); }()))) { error("sub SIMD no-sat (sm_90+): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub SIMD no-sat (sm_90+): dst");
    check_op_scalar_type(instr.src1, d.type_, "sub SIMD no-sat (sm_90+): src1");
    check_op_scalar_type(instr.src2, d.type_, "sub SIMD no-sat (sm_90+): src2");
    return true;
  };
  // variant 3: sub packed optional-sat (sm_120f family)
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(9.2f, "sub packed optional-sat (sm_120f family)")) return false;
    if (!require_sm(120u, "sub packed optional-sat (sm_120f family)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U8x4, ScalarType::S8x4}); return is_one_of(d.type_, _allowed); }()))) { error("sub packed optional-sat (sm_120f family): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub packed optional-sat (sm_120f family): dst");
    check_op_scalar_type(instr.src1, d.type_, "sub packed optional-sat (sm_120f family): src1");
    check_op_scalar_type(instr.src2, d.type_, "sub packed optional-sat (sm_120f family): src2");
    return true;
  };
  // variant 4: sub SIMD sat / sub.sat.u32 (sm_120f family)
  auto check_v4 = [&]() -> bool {
    if (!require_ptx(9.2f, "sub SIMD sat / sub.sat.u32 (sm_120f family)")) return false;
    if (!require_sm(120u, "sub SIMD sat / sub.sat.u32 (sm_120f family)")) return false;
    if (!std::holds_alternative<ArithInteger>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithInteger>(instr.data);
    if (!((d.saturate == true))) { error("sub SIMD sat / sub.sat.u32 (sm_120f family): modifier saturate check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16x2, ScalarType::S16x2, ScalarType::U32}); return is_one_of(d.type_, _allowed); }()))) { error("sub SIMD sat / sub.sat.u32 (sm_120f family): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub SIMD sat / sub.sat.u32 (sm_120f family): dst");
    check_op_scalar_type(instr.src1, d.type_, "sub SIMD sat / sub.sat.u32 (sm_120f family): src1");
    check_op_scalar_type(instr.src2, d.type_, "sub SIMD sat / sub.sat.u32 (sm_120f family): src2");
    return true;
  };
  // variant 5: sub.f32
  auto check_v5 = [&]() -> bool {
    if (!require_ptx(1.0f, "sub.f32")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32))) { error("sub.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub.f32: dst");
    check_op_scalar_type(instr.src1, d.type_, "sub.f32: src1");
    check_op_scalar_type(instr.src2, d.type_, "sub.f32: src2");
    return true;
  };
  // variant 6: sub.f32x2
  auto check_v6 = [&]() -> bool {
    if (!require_ptx(8.6f, "sub.f32x2")) return false;
    if (!require_sm(100u, "sub.f32x2")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32x2))) { error("sub.f32x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub.f32x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "sub.f32x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "sub.f32x2: src2");
    return true;
  };
  // variant 7: sub.f64
  auto check_v7 = [&]() -> bool {
    if (!require_ptx(1.0f, "sub.f64")) return false;
    if (!require_sm(13u, "sub.f64")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F64))) { error("sub.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub.f64: dst");
    check_op_scalar_type(instr.src1, d.type_, "sub.f64: src1");
    check_op_scalar_type(instr.src2, d.type_, "sub.f64: src2");
    return true;
  };
  // variant 8: sub half-precision (f16, f16x2)
  auto check_v8 = [&]() -> bool {
    if (!require_ptx(6.0f, "sub half-precision (f16, f16x2)")) return false;
    if (!require_sm(53u, "sub half-precision (f16, f16x2)")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2}); return is_one_of(d.type_, _allowed); }()))) { error("sub half-precision (f16, f16x2): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub half-precision (f16, f16x2): dst");
    check_op_scalar_type(instr.src1, d.type_, "sub half-precision (f16, f16x2): src1");
    check_op_scalar_type(instr.src2, d.type_, "sub half-precision (f16, f16x2): src2");
    return true;
  };
  // variant 9: sub bf16/bf16x2
  auto check_v9 = [&]() -> bool {
    if (!require_ptx(7.0f, "sub bf16/bf16x2")) return false;
    if (!require_sm(80u, "sub bf16/bf16x2")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(d.type_, _allowed); }()))) { error("sub bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "sub bf16/bf16x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "sub bf16/bf16x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "sub bf16/bf16x2: src2");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3() || check_v4() || check_v5() || check_v6() || check_v7() || check_v8() || check_v9())) {
    error("sub: no matching variant found");
  }
}

// PTX ISA §9.7.1.3
void TypeCheckerGen::check_mul(const InstrMul<ResolvedOp>& instr) {
  // variant 0: mul integer hi/lo
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mul integer hi/lo")) return false;
    if (!std::holds_alternative<MulInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MulInt>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<MulIntControl>({MulIntControl::High, MulIntControl::Low}); return is_one_of(d.control, _allowed); }()))) { error("mul integer hi/lo: modifier control check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(d.type_, _allowed); }()))) { error("mul integer hi/lo: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mul integer hi/lo: dst");
    check_op_scalar_type(instr.src1, d.type_, "mul integer hi/lo: src1");
    check_op_scalar_type(instr.src2, d.type_, "mul integer hi/lo: src2");
    return true;
  };
  // variant 1: mul.wide
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "mul.wide")) return false;
    if (!std::holds_alternative<MulInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MulInt>(instr.data);
    if (!((d.control == MulIntControl::Wide))) { error("mul.wide: modifier control check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::S16, ScalarType::S32}); return is_one_of(d.type_, _allowed); }()))) { error("mul.wide: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mul.wide: dst");
    check_op_scalar_type(instr.src1, d.type_, "mul.wide: src1");
    check_op_scalar_type(instr.src2, d.type_, "mul.wide: src2");
    return true;
  };
  // variant 2: mul.f32
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "mul.f32")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32))) { error("mul.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mul.f32: dst");
    check_op_scalar_type(instr.src1, d.type_, "mul.f32: src1");
    check_op_scalar_type(instr.src2, d.type_, "mul.f32: src2");
    return true;
  };
  // variant 3: mul.f32x2
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(8.6f, "mul.f32x2")) return false;
    if (!require_sm(100u, "mul.f32x2")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32x2))) { error("mul.f32x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mul.f32x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "mul.f32x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "mul.f32x2: src2");
    return true;
  };
  // variant 4: mul.f64
  auto check_v4 = [&]() -> bool {
    if (!require_ptx(1.0f, "mul.f64")) return false;
    if (!require_sm(13u, "mul.f64")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F64))) { error("mul.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mul.f64: dst");
    check_op_scalar_type(instr.src1, d.type_, "mul.f64: src1");
    check_op_scalar_type(instr.src2, d.type_, "mul.f64: src2");
    return true;
  };
  // variant 5: mul half-precision (f16, f16x2)
  auto check_v5 = [&]() -> bool {
    if (!require_ptx(6.0f, "mul half-precision (f16, f16x2)")) return false;
    if (!require_sm(53u, "mul half-precision (f16, f16x2)")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2}); return is_one_of(d.type_, _allowed); }()))) { error("mul half-precision (f16, f16x2): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mul half-precision (f16, f16x2): dst");
    check_op_scalar_type(instr.src1, d.type_, "mul half-precision (f16, f16x2): src1");
    check_op_scalar_type(instr.src2, d.type_, "mul half-precision (f16, f16x2): src2");
    return true;
  };
  // variant 6: mul bf16/bf16x2
  auto check_v6 = [&]() -> bool {
    if (!require_ptx(7.0f, "mul bf16/bf16x2")) return false;
    if (!require_sm(80u, "mul bf16/bf16x2")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(d.type_, _allowed); }()))) { error("mul bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mul bf16/bf16x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "mul bf16/bf16x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "mul bf16/bf16x2: src2");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3() || check_v4() || check_v5() || check_v6())) {
    error("mul: no matching variant found");
  }
}

// PTX ISA §9.7.1.4
void TypeCheckerGen::check_mad(const InstrMad<ResolvedOp>& instr) {
  // variant 0: mad integer hi/lo
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mad integer hi/lo")) return false;
    if (!std::holds_alternative<MadInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MadInt>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<MulIntControl>({MulIntControl::High, MulIntControl::Low}); return is_one_of(d.control, _allowed); }()))) { error("mad integer hi/lo: modifier control check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(d.type_, _allowed); }()))) { error("mad integer hi/lo: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mad integer hi/lo: dst");
    check_op_scalar_type(instr.src1, d.type_, "mad integer hi/lo: src1");
    check_op_scalar_type(instr.src2, d.type_, "mad integer hi/lo: src2");
    check_op_scalar_type(instr.src3, d.type_, "mad integer hi/lo: src3");
    return true;
  };
  // variant 1: mad.wide
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "mad.wide")) return false;
    if (!std::holds_alternative<MadInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MadInt>(instr.data);
    if (!((d.control == MulIntControl::Wide))) { error("mad.wide: modifier control check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::S16, ScalarType::S32}); return is_one_of(d.type_, _allowed); }()))) { error("mad.wide: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mad.wide: dst");
    check_op_scalar_type(instr.src1, d.type_, "mad.wide: src1");
    check_op_scalar_type(instr.src2, d.type_, "mad.wide: src2");
    check_op_scalar_type(instr.src3, d.type_, "mad.wide: src3");
    return true;
  };
  // variant 2: mad.hi.sat.s32
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "mad.hi.sat.s32")) return false;
    if (!std::holds_alternative<MadInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MadInt>(instr.data);
    if (!((d.control == MulIntControl::High))) { error("mad.hi.sat.s32: modifier control check failed"); return false; }
    if (!((d.saturate == true))) { error("mad.hi.sat.s32: modifier saturate check failed"); return false; }
    if (!((d.type_ == ScalarType::S32))) { error("mad.hi.sat.s32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mad.hi.sat.s32: dst");
    check_op_scalar_type(instr.src1, d.type_, "mad.hi.sat.s32: src1");
    check_op_scalar_type(instr.src2, d.type_, "mad.hi.sat.s32: src2");
    check_op_scalar_type(instr.src3, d.type_, "mad.hi.sat.s32: src3");
    return true;
  };
  // variant 3: mad.f32
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(1.0f, "mad.f32")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32))) { error("mad.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mad.f32: dst");
    check_op_scalar_type(instr.src1, d.type_, "mad.f32: src1");
    check_op_scalar_type(instr.src2, d.type_, "mad.f32: src2");
    check_op_scalar_type(instr.src3, d.type_, "mad.f32: src3");
    return true;
  };
  // variant 4: mad.f64
  auto check_v4 = [&]() -> bool {
    if (!require_ptx(1.0f, "mad.f64")) return false;
    if (!require_sm(13u, "mad.f64")) return false;
    if (!std::holds_alternative<ArithFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<ArithFloat>(instr.data);
    if (!((d.type_ == ScalarType::F64))) { error("mad.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "mad.f64: dst");
    check_op_scalar_type(instr.src1, d.type_, "mad.f64: src1");
    check_op_scalar_type(instr.src2, d.type_, "mad.f64: src2");
    check_op_scalar_type(instr.src3, d.type_, "mad.f64: src3");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3() || check_v4())) {
    error("mad: no matching variant found");
  }
}

// PTX ISA §9.7.1.5
void TypeCheckerGen::check_mul24(const InstrMul24<ResolvedOp>& instr) {
  // variant 0: mul24 hi/lo
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mul24 hi/lo")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<Mul24Control>({Mul24Control::Hi, Mul24Control::Lo}); return is_one_of(instr.data.control, _allowed); }()))) { error("mul24 hi/lo: modifier control check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::S32}); return is_one_of(instr.data.type_, _allowed); }()))) { error("mul24 hi/lo: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("mul24: no matching variant found");
  }
}

// PTX ISA §9.7.1.7
void TypeCheckerGen::check_sad(const InstrSad<ResolvedOp>& instr) {
  // variant 0: sad integer
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "sad integer")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(instr.data, _allowed); }()))) { error("sad integer: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "sad integer: dst");
    check_op_scalar_type(instr.src1, instr.data, "sad integer: src1");
    check_op_scalar_type(instr.src2, instr.data, "sad integer: src2");
    check_op_scalar_type(instr.src3, instr.data, "sad integer: src3");
    return true;
  };
  if (!(check_v0())) {
    error("sad: no matching variant found");
  }
}

// PTX ISA §9.7.1.8
void TypeCheckerGen::check_div(const InstrDiv<ResolvedOp>& instr) {
  // variant 0: div integer
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "div integer")) return false;
    if (!std::holds_alternative<DivInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<DivInt>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(d.type_, _allowed); }()))) { error("div integer: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "div integer: dst");
    check_op_scalar_type(instr.src1, d.type_, "div integer: src1");
    check_op_scalar_type(instr.src2, d.type_, "div integer: src2");
    return true;
  };
  // variant 1: div.approx.f32
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "div.approx.f32")) return false;
    if (!std::holds_alternative<DivFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<DivFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32))) { error("div.approx.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "div.approx.f32: dst");
    check_op_scalar_type(instr.src1, d.type_, "div.approx.f32: src1");
    check_op_scalar_type(instr.src2, d.type_, "div.approx.f32: src2");
    return true;
  };
  // variant 2: div.full.f32
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "div.full.f32")) return false;
    if (!std::holds_alternative<DivFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<DivFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32))) { error("div.full.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "div.full.f32: dst");
    check_op_scalar_type(instr.src1, d.type_, "div.full.f32: src1");
    check_op_scalar_type(instr.src2, d.type_, "div.full.f32: src2");
    return true;
  };
  // variant 3: div.rnd.f32
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(1.0f, "div.rnd.f32")) return false;
    if (!std::holds_alternative<DivFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<DivFloat>(instr.data);
    if (!((d.type_ == ScalarType::F32))) { error("div.rnd.f32: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "div.rnd.f32: dst");
    check_op_scalar_type(instr.src1, d.type_, "div.rnd.f32: src1");
    check_op_scalar_type(instr.src2, d.type_, "div.rnd.f32: src2");
    return true;
  };
  // variant 4: div.rnd.f64
  auto check_v4 = [&]() -> bool {
    if (!require_ptx(1.0f, "div.rnd.f64")) return false;
    if (!require_sm(13u, "div.rnd.f64")) return false;
    if (!std::holds_alternative<DivFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<DivFloat>(instr.data);
    if (!((d.type_ == ScalarType::F64))) { error("div.rnd.f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "div.rnd.f64: dst");
    check_op_scalar_type(instr.src1, d.type_, "div.rnd.f64: src1");
    check_op_scalar_type(instr.src2, d.type_, "div.rnd.f64: src2");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3() || check_v4())) {
    error("div: no matching variant found");
  }
}

// PTX ISA §9.7.1.9
void TypeCheckerGen::check_rem(const InstrRem<ResolvedOp>& instr) {
  // variant 0: rem integer
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "rem integer")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(instr.data, _allowed); }()))) { error("rem integer: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "rem integer: dst");
    check_op_scalar_type(instr.src1, instr.data, "rem integer: src1");
    check_op_scalar_type(instr.src2, instr.data, "rem integer: src2");
    return true;
  };
  if (!(check_v0())) {
    error("rem: no matching variant found");
  }
}

// PTX ISA §9.7.1.10
void TypeCheckerGen::check_abs(const InstrAbs<ResolvedOp>& instr) {
  // variant 0: abs integer
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "abs integer")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("abs integer: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "abs integer: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "abs integer: src");
    return true;
  };
  // variant 1: abs.f32/f64
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "abs.f32/f64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F32, ScalarType::F64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("abs.f32/f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "abs.f32/f64: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "abs.f32/f64: src");
    return true;
  };
  // variant 2: abs.f16/f16x2
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(6.0f, "abs.f16/f16x2")) return false;
    if (!require_sm(53u, "abs.f16/f16x2")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2}); return is_one_of(instr.data.type_, _allowed); }()))) { error("abs.f16/f16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "abs.f16/f16x2: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "abs.f16/f16x2: src");
    return true;
  };
  // variant 3: abs.bf16/bf16x2
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(7.0f, "abs.bf16/bf16x2")) return false;
    if (!require_sm(80u, "abs.bf16/bf16x2")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(instr.data.type_, _allowed); }()))) { error("abs.bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "abs.bf16/bf16x2: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "abs.bf16/bf16x2: src");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3())) {
    error("abs: no matching variant found");
  }
}

// PTX ISA §9.7.1.11
void TypeCheckerGen::check_neg(const InstrNeg<ResolvedOp>& instr) {
  // variant 0: neg integer
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "neg integer")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("neg integer: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "neg integer: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "neg integer: src");
    return true;
  };
  // variant 1: neg.f32/f64
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "neg.f32/f64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F32, ScalarType::F64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("neg.f32/f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "neg.f32/f64: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "neg.f32/f64: src");
    return true;
  };
  // variant 2: neg.f16/f16x2
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(6.0f, "neg.f16/f16x2")) return false;
    if (!require_sm(53u, "neg.f16/f16x2")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2}); return is_one_of(instr.data.type_, _allowed); }()))) { error("neg.f16/f16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "neg.f16/f16x2: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "neg.f16/f16x2: src");
    return true;
  };
  // variant 3: neg.bf16/bf16x2
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(7.0f, "neg.bf16/bf16x2")) return false;
    if (!require_sm(80u, "neg.bf16/bf16x2")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(instr.data.type_, _allowed); }()))) { error("neg.bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "neg.bf16/bf16x2: dst");
    check_op_scalar_type(instr.src, instr.data.type_, "neg.bf16/bf16x2: src");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3())) {
    error("neg: no matching variant found");
  }
}

// PTX ISA §9.7.1.12
void TypeCheckerGen::check_min(const InstrMin<ResolvedOp>& instr) {
  // variant 0: min integer (universal)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "min integer (universal)")) return false;
    if (!std::holds_alternative<MinMaxInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxInt>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(d.type_, _allowed); }()))) { error("min integer (universal): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "min integer (universal): dst");
    check_op_scalar_type(instr.src1, d.type_, "min integer (universal): src1");
    check_op_scalar_type(instr.src2, d.type_, "min integer (universal): src2");
    return true;
  };
  // variant 1: min SIMD integer (sm_90+)
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(8.0f, "min SIMD integer (sm_90+)")) return false;
    if (!require_sm(90u, "min SIMD integer (sm_90+)")) return false;
    if (!std::holds_alternative<MinMaxInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxInt>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16x2, ScalarType::S16x2}); return is_one_of(d.type_, _allowed); }()))) { error("min SIMD integer (sm_90+): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "min SIMD integer (sm_90+): dst");
    check_op_scalar_type(instr.src1, d.type_, "min SIMD integer (sm_90+): src1");
    check_op_scalar_type(instr.src2, d.type_, "min SIMD integer (sm_90+): src2");
    return true;
  };
  // variant 2: min.f32/f64
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "min.f32/f64")) return false;
    if (!std::holds_alternative<MinMaxFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F32, ScalarType::F64}); return is_one_of(d.type_, _allowed); }()))) { error("min.f32/f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "min.f32/f64: dst");
    check_op_scalar_type(instr.src1, d.type_, "min.f32/f64: src1");
    check_op_scalar_type(instr.src2, d.type_, "min.f32/f64: src2");
    return true;
  };
  // variant 3: min.f16/f16x2
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(6.0f, "min.f16/f16x2")) return false;
    if (!require_sm(53u, "min.f16/f16x2")) return false;
    if (!std::holds_alternative<MinMaxFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2}); return is_one_of(d.type_, _allowed); }()))) { error("min.f16/f16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "min.f16/f16x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "min.f16/f16x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "min.f16/f16x2: src2");
    return true;
  };
  // variant 4: min.bf16/bf16x2
  auto check_v4 = [&]() -> bool {
    if (!require_ptx(7.0f, "min.bf16/bf16x2")) return false;
    if (!require_sm(80u, "min.bf16/bf16x2")) return false;
    if (!std::holds_alternative<MinMaxFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(d.type_, _allowed); }()))) { error("min.bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "min.bf16/bf16x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "min.bf16/bf16x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "min.bf16/bf16x2: src2");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3() || check_v4())) {
    error("min: no matching variant found");
  }
}

// PTX ISA §9.7.1.13
void TypeCheckerGen::check_max(const InstrMax<ResolvedOp>& instr) {
  // variant 0: max integer (universal)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "max integer (universal)")) return false;
    if (!std::holds_alternative<MinMaxInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxInt>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(d.type_, _allowed); }()))) { error("max integer (universal): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "max integer (universal): dst");
    check_op_scalar_type(instr.src1, d.type_, "max integer (universal): src1");
    check_op_scalar_type(instr.src2, d.type_, "max integer (universal): src2");
    return true;
  };
  // variant 1: max SIMD integer (sm_90+)
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(8.0f, "max SIMD integer (sm_90+)")) return false;
    if (!require_sm(90u, "max SIMD integer (sm_90+)")) return false;
    if (!std::holds_alternative<MinMaxInt>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxInt>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U16x2, ScalarType::S16x2}); return is_one_of(d.type_, _allowed); }()))) { error("max SIMD integer (sm_90+): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "max SIMD integer (sm_90+): dst");
    check_op_scalar_type(instr.src1, d.type_, "max SIMD integer (sm_90+): src1");
    check_op_scalar_type(instr.src2, d.type_, "max SIMD integer (sm_90+): src2");
    return true;
  };
  // variant 2: max.f32/f64
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "max.f32/f64")) return false;
    if (!std::holds_alternative<MinMaxFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F32, ScalarType::F64}); return is_one_of(d.type_, _allowed); }()))) { error("max.f32/f64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "max.f32/f64: dst");
    check_op_scalar_type(instr.src1, d.type_, "max.f32/f64: src1");
    check_op_scalar_type(instr.src2, d.type_, "max.f32/f64: src2");
    return true;
  };
  // variant 3: max.f16/f16x2
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(6.0f, "max.f16/f16x2")) return false;
    if (!require_sm(53u, "max.f16/f16x2")) return false;
    if (!std::holds_alternative<MinMaxFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::F16, ScalarType::F16x2}); return is_one_of(d.type_, _allowed); }()))) { error("max.f16/f16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "max.f16/f16x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "max.f16/f16x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "max.f16/f16x2: src2");
    return true;
  };
  // variant 4: max.bf16/bf16x2
  auto check_v4 = [&]() -> bool {
    if (!require_ptx(7.0f, "max.bf16/bf16x2")) return false;
    if (!require_sm(80u, "max.bf16/bf16x2")) return false;
    if (!std::holds_alternative<MinMaxFloat>(instr.data)) return false;
    [[maybe_unused]] auto& d = std::get<MinMaxFloat>(instr.data);
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::BF16, ScalarType::BF16x2}); return is_one_of(d.type_, _allowed); }()))) { error("max.bf16/bf16x2: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, d.type_, "max.bf16/bf16x2: dst");
    check_op_scalar_type(instr.src1, d.type_, "max.bf16/bf16x2: src1");
    check_op_scalar_type(instr.src2, d.type_, "max.bf16/bf16x2: src2");
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3() || check_v4())) {
    error("max: no matching variant found");
  }
}

// PTX ISA §9.7.1.14
void TypeCheckerGen::check_clz(const InstrClz<ResolvedOp>& instr) {
  // variant 0: clz b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(2.0f, "clz b32/b64")) return false;
    if (!require_sm(20u, "clz b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("clz b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.src, instr.data, "clz b32/b64: src");
    return true;
  };
  if (!(check_v0())) {
    error("clz: no matching variant found");
  }
}

// PTX ISA §9.7.1.15
void TypeCheckerGen::check_brev(const InstrBrev<ResolvedOp>& instr) {
  // variant 0: brev b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(2.0f, "brev b32/b64")) return false;
    if (!require_sm(20u, "brev b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("brev b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "brev b32/b64: dst");
    check_op_scalar_type(instr.src, instr.data, "brev b32/b64: src");
    return true;
  };
  if (!(check_v0())) {
    error("brev: no matching variant found");
  }
}

// PTX ISA §9.7.1.16
void TypeCheckerGen::check_popc(const InstrPopc<ResolvedOp>& instr) {
  // variant 0: popc b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(2.0f, "popc b32/b64")) return false;
    if (!require_sm(20u, "popc b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("popc b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.src, instr.data, "popc b32/b64: src");
    return true;
  };
  if (!(check_v0())) {
    error("popc: no matching variant found");
  }
}

// PTX ISA §9.7.1.17
void TypeCheckerGen::check_bfe(const InstrBfe<ResolvedOp>& instr) {
  // variant 0: bfe u32/u64/s32/s64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(2.0f, "bfe u32/u64/s32/s64")) return false;
    if (!require_sm(20u, "bfe u32/u64/s32/s64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::U64, ScalarType::S32, ScalarType::S64}); return is_one_of(instr.data, _allowed); }()))) { error("bfe u32/u64/s32/s64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "bfe u32/u64/s32/s64: dst");
    check_op_scalar_type(instr.src1, instr.data, "bfe u32/u64/s32/s64: src1");
    check_op_scalar_type(instr.src2, instr.data, "bfe u32/u64/s32/s64: src2");
    check_op_scalar_type(instr.src3, instr.data, "bfe u32/u64/s32/s64: src3");
    return true;
  };
  if (!(check_v0())) {
    error("bfe: no matching variant found");
  }
}

// PTX ISA §9.7.1.18
void TypeCheckerGen::check_bfi(const InstrBfi<ResolvedOp>& instr) {
  // variant 0: bfi b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(2.0f, "bfi b32/b64")) return false;
    if (!require_sm(20u, "bfi b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("bfi b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "bfi b32/b64: dst");
    check_op_scalar_type(instr.src1, instr.data, "bfi b32/b64: src1");
    check_op_scalar_type(instr.src2, instr.data, "bfi b32/b64: src2");
    check_op_scalar_type(instr.src3, instr.data, "bfi b32/b64: src3");
    check_op_scalar_type(instr.src4, instr.data, "bfi b32/b64: src4");
    return true;
  };
  if (!(check_v0())) {
    error("bfi: no matching variant found");
  }
}

// PTX ISA §9.7.1.19
void TypeCheckerGen::check_bmsk(const InstrBmsk<ResolvedOp>& instr) {
  // variant 0: bmsk.clamp/wrap.b32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(7.0f, "bmsk.clamp/wrap.b32")) return false;
    if (!require_sm(80u, "bmsk.clamp/wrap.b32")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<BmskMode>({BmskMode::Clamp, BmskMode::Wrap}); return is_one_of(instr.data, _allowed); }()))) { error("bmsk.clamp/wrap.b32: modifier mode check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("bmsk: no matching variant found");
  }
}

// PTX ISA §9.7.1.20
void TypeCheckerGen::check_dp4a(const InstrDp4a<ResolvedOp>& instr) {
  // variant 0: dp4a u32/s32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(6.0f, "dp4a u32/s32")) return false;
    if (!require_sm(61u, "dp4a u32/s32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("dp4a: no matching variant found");
  }
}

// PTX ISA §9.7.1.21
void TypeCheckerGen::check_dp2a(const InstrDp2a<ResolvedOp>& instr) {
  // variant 0: dp2a lo/hi u32/s32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(7.0f, "dp2a lo/hi u32/s32")) return false;
    if (!require_sm(72u, "dp2a lo/hi u32/s32")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<Dp2aControl>({Dp2aControl::Low, Dp2aControl::High}); return is_one_of(instr.data.control, _allowed); }()))) { error("dp2a lo/hi u32/s32: modifier control check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("dp2a: no matching variant found");
  }
}

// PTX ISA §9.7.1.22
void TypeCheckerGen::check_add_cc(const InstrAddExtended<ResolvedOp>& instr) {
  // variant 0: add.cc u32/s32/u64/s64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "add.cc u32/s32/u64/s64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::S32, ScalarType::U64, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("add.cc u32/s32/u64/s64: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("add.cc: no matching variant found");
  }
}

// PTX ISA §9.7.1.23
void TypeCheckerGen::check_addc(const InstrAddExtended<ResolvedOp>& instr) {
  // variant 0: addc with optional .cc
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "addc with optional .cc")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::S32, ScalarType::U64, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("addc with optional .cc: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("addc: no matching variant found");
  }
}

// PTX ISA §9.7.1.24
void TypeCheckerGen::check_sub_cc(const InstrSubExtended<ResolvedOp>& instr) {
  // variant 0: sub.cc u32/s32/u64/s64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "sub.cc u32/s32/u64/s64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::S32, ScalarType::U64, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("sub.cc u32/s32/u64/s64: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("sub.cc: no matching variant found");
  }
}

// PTX ISA §9.7.1.25
void TypeCheckerGen::check_subc(const InstrSubExtended<ResolvedOp>& instr) {
  // variant 0: subc with optional .cc
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "subc with optional .cc")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::S32, ScalarType::U64, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("subc with optional .cc: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("subc: no matching variant found");
  }
}

// PTX ISA §9.7.1.26
void TypeCheckerGen::check_mad_cc(const InstrMadExtended<ResolvedOp>& instr) {
  // variant 0: mad.cc lo/hi u32/s32/u64/s64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mad.cc lo/hi u32/s32/u64/s64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<MulIntControl>({MulIntControl::Low, MulIntControl::High}); return is_one_of(instr.data.control, _allowed); }()))) { error("mad.cc lo/hi u32/s32/u64/s64: modifier control check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::S32, ScalarType::U64, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("mad.cc lo/hi u32/s32/u64/s64: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("mad.cc: no matching variant found");
  }
}

// PTX ISA §9.7.1.27
void TypeCheckerGen::check_madc(const InstrMadExtended<ResolvedOp>& instr) {
  // variant 0: madc lo/hi with optional .cc
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "madc lo/hi with optional .cc")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<MulIntControl>({MulIntControl::Low, MulIntControl::High}); return is_one_of(instr.data.control, _allowed); }()))) { error("madc lo/hi with optional .cc: modifier control check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::S32, ScalarType::U64, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("madc lo/hi with optional .cc: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("madc: no matching variant found");
  }
}

// PTX ISA §9.7.5.1
void TypeCheckerGen::check_and(const InstrAnd<ResolvedOp>& instr) {
  // variant 0: and pred/b16/b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "and pred/b16/b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::Pred, ScalarType::B16, ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("and pred/b16/b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "and pred/b16/b32/b64: dst");
    check_op_scalar_type(instr.src1, instr.data, "and pred/b16/b32/b64: src1");
    check_op_scalar_type(instr.src2, instr.data, "and pred/b16/b32/b64: src2");
    return true;
  };
  if (!(check_v0())) {
    error("and: no matching variant found");
  }
}

// PTX ISA §9.7.5.2
void TypeCheckerGen::check_or(const InstrOr<ResolvedOp>& instr) {
  // variant 0: or pred/b16/b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "or pred/b16/b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::Pred, ScalarType::B16, ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("or pred/b16/b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "or pred/b16/b32/b64: dst");
    check_op_scalar_type(instr.src1, instr.data, "or pred/b16/b32/b64: src1");
    check_op_scalar_type(instr.src2, instr.data, "or pred/b16/b32/b64: src2");
    return true;
  };
  if (!(check_v0())) {
    error("or: no matching variant found");
  }
}

// PTX ISA §9.7.5.3
void TypeCheckerGen::check_xor(const InstrXor<ResolvedOp>& instr) {
  // variant 0: xor pred/b16/b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "xor pred/b16/b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::Pred, ScalarType::B16, ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("xor pred/b16/b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "xor pred/b16/b32/b64: dst");
    check_op_scalar_type(instr.src1, instr.data, "xor pred/b16/b32/b64: src1");
    check_op_scalar_type(instr.src2, instr.data, "xor pred/b16/b32/b64: src2");
    return true;
  };
  if (!(check_v0())) {
    error("xor: no matching variant found");
  }
}

// PTX ISA §9.7.5.4
void TypeCheckerGen::check_not(const InstrNot<ResolvedOp>& instr) {
  // variant 0: not pred/b16/b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "not pred/b16/b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::Pred, ScalarType::B16, ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("not pred/b16/b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "not pred/b16/b32/b64: dst");
    check_op_scalar_type(instr.src, instr.data, "not pred/b16/b32/b64: src");
    return true;
  };
  if (!(check_v0())) {
    error("not: no matching variant found");
  }
}

// PTX ISA §9.7.5.7
void TypeCheckerGen::check_shl(const InstrShl<ResolvedOp>& instr) {
  // variant 0: shl b16/b32/b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "shl b16/b32/b64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::B16, ScalarType::B32, ScalarType::B64}); return is_one_of(instr.data, _allowed); }()))) { error("shl b16/b32/b64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data, "shl b16/b32/b64: dst");
    check_op_scalar_type(instr.src1, instr.data, "shl b16/b32/b64: src1");
    return true;
  };
  if (!(check_v0())) {
    error("shl: no matching variant found");
  }
}

// PTX ISA §9.7.5.8
void TypeCheckerGen::check_shr(const InstrShr<ResolvedOp>& instr) {
  // variant 0: shr b/u/s 16/32/64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "shr b/u/s 16/32/64")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::B16, ScalarType::B32, ScalarType::B64, ScalarType::U16, ScalarType::U32, ScalarType::U64, ScalarType::S16, ScalarType::S32, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("shr b/u/s 16/32/64: modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.dst, instr.data.type_, "shr b/u/s 16/32/64: dst");
    check_op_scalar_type(instr.src1, instr.data.type_, "shr b/u/s 16/32/64: src1");
    return true;
  };
  if (!(check_v0())) {
    error("shr: no matching variant found");
  }
}

// PTX ISA §9.7.5.9
void TypeCheckerGen::check_shf(const InstrShf<ResolvedOp>& instr) {
  // variant 0: shf.l/r.clamp/wrap.b32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(3.0f, "shf.l/r.clamp/wrap.b32")) return false;
    if (!require_sm(32u, "shf.l/r.clamp/wrap.b32")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ShiftDirection>({ShiftDirection::Left, ShiftDirection::Right}); return is_one_of(instr.data.direction, _allowed); }()))) { error("shf.l/r.clamp/wrap.b32: modifier direction check failed"); return false; }
    if (!(([&]{ static constexpr auto _allowed = std::to_array<FunnelShiftMode>({FunnelShiftMode::Clamp, FunnelShiftMode::Wrap}); return is_one_of(instr.data.mode, _allowed); }()))) { error("shf.l/r.clamp/wrap.b32: modifier mode check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("shf: no matching variant found");
  }
}

// PTX ISA §9.7.5.11
void TypeCheckerGen::check_bfind(const InstrBfind<ResolvedOp>& instr) {
  // variant 0: bfind.u32/u64 (unsigned, find highest set bit)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(2.0f, "bfind.u32/u64 (unsigned, find highest set bit)")) return false;
    if (!require_sm(20u, "bfind.u32/u64 (unsigned, find highest set bit)")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::U64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("bfind.u32/u64 (unsigned, find highest set bit): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.src, instr.data.type_, "bfind.u32/u64 (unsigned, find highest set bit): src");
    return true;
  };
  // variant 1: bfind.s32/s64 (signed, find highest non-sign bit)
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(2.0f, "bfind.s32/s64 (signed, find highest non-sign bit)")) return false;
    if (!require_sm(20u, "bfind.s32/s64 (signed, find highest non-sign bit)")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::S32, ScalarType::S64}); return is_one_of(instr.data.type_, _allowed); }()))) { error("bfind.s32/s64 (signed, find highest non-sign bit): modifier type_ check failed"); return false; }
    check_op_scalar_type(instr.src, instr.data.type_, "bfind.s32/s64 (signed, find highest non-sign bit): src");
    return true;
  };
  if (!(check_v0() || check_v1())) {
    error("bfind: no matching variant found");
  }
}

// PTX ISA §9.7.13.4
void TypeCheckerGen::check_ldmatrix(const InstrLdMatrix<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("ldmatrix: no matching variant found");
  }
}

// PTX ISA §9.7.13.5
void TypeCheckerGen::check_stmatrix(const InstrStMatrix<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("stmatrix: no matching variant found");
  }
}

// PTX ISA §9.7.13.2
void TypeCheckerGen::check_mma_sync(const InstrMma<ResolvedOp>& instr) {
  // variant 0: mma.sync.aligned (standard)
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("mma.sync: no matching variant found");
  }
}

// PTX ISA §9.7.11.16
void TypeCheckerGen::check_cp_async_bulk_tensor(const InstrCpAsyncBulkTensor<ResolvedOp>& instr) {
  // variant 0: cp.async.bulk.tensor shared\x2190global (load)
  auto check_v0 = [&]() -> bool {
    return true;
  };
  // variant 1: cp.async.bulk.tensor global\x2190shared (store)
  auto check_v1 = [&]() -> bool {
    return true;
  };
  if (!(check_v0() || check_v1())) {
    error("cp.async.bulk.tensor: no matching variant found");
  }
}

// PTX ISA §9.7.15.2
void TypeCheckerGen::check_tcgen05_ld(const InstrTcgen05Ld<ResolvedOp>& instr) {
  // variant 0: tcgen05.ld.sync.aligned
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("tcgen05.ld: no matching variant found");
  }
}

// PTX ISA §9.7.15.3
void TypeCheckerGen::check_tcgen05_st(const InstrTcgen05St<ResolvedOp>& instr) {
  // variant 0: tcgen05.st.sync.aligned
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("tcgen05.st: no matching variant found");
  }
}

// PTX ISA §9.7.15.6
void TypeCheckerGen::check_tcgen05_fence(const InstrTcgen05Fence<ResolvedOp>& instr) {
  // variant 0: tcgen05.fence before/after
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("tcgen05.fence: no matching variant found");
  }
}

// PTX ISA §9.7.15.8
void TypeCheckerGen::check_tcgen05_wait(const InstrTcgen05Wait<ResolvedOp>& instr) {
  // variant 0: tcgen05.wait_group
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("tcgen05.wait: no matching variant found");
  }
}

// PTX ISA §9.7.15.9
void TypeCheckerGen::check_tcgen05_alloc(const InstrTcgen05Alloc<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("tcgen05.alloc: no matching variant found");
  }
}

// PTX ISA §9.7.15.9
void TypeCheckerGen::check_tcgen05_dealloc(const InstrTcgen05Dealloc<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("tcgen05.dealloc: no matching variant found");
  }
}

// PTX ISA §9.7.15.9
void TypeCheckerGen::check_tcgen05_relinquish_alloc_permit(const InstrTcgen05Relinquish<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("tcgen05.relinquish_alloc_permit: no matching variant found");
  }
}

// PTX ISA §9.7.15.10
void TypeCheckerGen::check_tcgen05_shift(const InstrTcgen05Shift<ResolvedOp>& instr) {
  // variant 0: 
  auto check_v0 = [&]() -> bool {
    return true;
  };
  if (!(check_v0())) {
    error("tcgen05.shift: no matching variant found");
  }
}

// PTX ISA §9.7.12.1
void TypeCheckerGen::check_brkpt(const InstrBrkpt& instr) {
  // variant 0: brkpt
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "brkpt")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("brkpt: no matching variant found");
  }
}

// PTX ISA §9.7.12.3
void TypeCheckerGen::check_griddepcontrol_launch_dependents(const InstrGridDepControl& instr) {
  // variant 0: griddepcontrol.launch_dependents
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "griddepcontrol.launch_dependents")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("griddepcontrol.launch_dependents: no matching variant found");
  }
}

// PTX ISA §9.7.12.3
void TypeCheckerGen::check_griddepcontrol_wait(const InstrGridDepControl& instr) {
  // variant 0: griddepcontrol.wait
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "griddepcontrol.wait")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("griddepcontrol.wait: no matching variant found");
  }
}

// PTX ISA §9.7.12.4
void TypeCheckerGen::check_setmaxnreg(const InstrSetMaxNReg<ResolvedOp>& instr) {
  // variant 0: setmaxnreg inc/dec u32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "setmaxnreg inc/dec u32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("setmaxnreg: no matching variant found");
  }
}

// PTX ISA §9.7.11.1
void TypeCheckerGen::check_bar_red(const InstrBarRed<ResolvedOp>& instr) {
  // variant 0: bar.red popc/and/or
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "bar.red popc/and/or")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("bar.red: no matching variant found");
  }
}

// PTX ISA §9.7.11.2
void TypeCheckerGen::check_barrier_sync(const InstrClusterBarrier<ResolvedOp>& instr) {
  // variant 0: barrier.sync with optional thread count
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "barrier.sync with optional thread count")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("barrier.sync: no matching variant found");
  }
}

// PTX ISA §9.7.11.2
void TypeCheckerGen::check_barrier_sync_aligned(const InstrClusterBarrier<ResolvedOp>& instr) {
  // variant 0: barrier.sync.aligned
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "barrier.sync.aligned")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("barrier.sync.aligned: no matching variant found");
  }
}

// PTX ISA §9.7.11.2
void TypeCheckerGen::check_barrier_arrive(const InstrClusterBarrier<ResolvedOp>& instr) {
  // variant 0: barrier.arrive
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "barrier.arrive")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("barrier.arrive: no matching variant found");
  }
}

// PTX ISA §9.7.11.2
void TypeCheckerGen::check_barrier_arrive_aligned(const InstrClusterBarrier<ResolvedOp>& instr) {
  // variant 0: barrier.arrive.aligned
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "barrier.arrive.aligned")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("barrier.arrive.aligned: no matching variant found");
  }
}

// PTX ISA §9.7.11.3
void TypeCheckerGen::check_barrier_cluster_arrive(const InstrClusterBarrier<ResolvedOp>& instr) {
  // variant 0: barrier.cluster.arrive with optional aligned
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "barrier.cluster.arrive with optional aligned")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("barrier.cluster.arrive: no matching variant found");
  }
}

// PTX ISA §9.7.11.3
void TypeCheckerGen::check_barrier_cluster_wait(const InstrClusterBarrier<ResolvedOp>& instr) {
  // variant 0: barrier.cluster.wait with optional aligned
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "barrier.cluster.wait with optional aligned")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("barrier.cluster.wait: no matching variant found");
  }
}

// PTX ISA §9.7.11.4
void TypeCheckerGen::check_membar(const InstrMembar& instr) {
  // variant 0: membar level
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "membar level")) return false;
    return true;
  };
  // variant 1: membar.proxy.proxykind
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "membar.proxy.proxykind")) return false;
    return true;
  };
  if (!(check_v0() || check_v1())) {
    error("membar: no matching variant found");
  }
}

// PTX ISA §9.7.11.5
void TypeCheckerGen::check_fence(const InstrFence<ResolvedOp>& instr) {
  // variant 0: fence acquire/release
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "fence acquire/release")) return false;
    return true;
  };
  // variant 1: fence.sem.sync_restrict::shared::{cta,cluster}.scope
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "fence.sem.sync_restrict::shared::{cta,cluster}.scope")) return false;
    return true;
  };
  // variant 2: fence.op_restrict.sem.scope
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "fence.op_restrict.sem.scope")) return false;
    return true;
  };
  // variant 3: fence.proxy
  auto check_v3 = [&]() -> bool {
    if (!require_ptx(1.0f, "fence.proxy")) return false;
    return true;
  };
  // variant 4: fence.proxy.tensormap::generic.sem.scope \x2014 uni-directional
  auto check_v4 = [&]() -> bool {
    if (!require_ptx(1.0f, "fence.proxy.tensormap::generic.sem.scope \x2014 uni-directional")) return false;
    return true;
  };
  // variant 5: fence.proxy.async::generic.sem.sync_restrict...
  auto check_v5 = [&]() -> bool {
    if (!require_ptx(1.0f, "fence.proxy.async::generic.sem.sync_restrict...")) return false;
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2() || check_v3() || check_v4() || check_v5())) {
    error("fence: no matching variant found");
  }
}

// PTX ISA §9.7.11.8
void TypeCheckerGen::check_vote(const InstrVote<ResolvedOp>& instr) {
  // variant 0: vote all/any/uni/ballot
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "vote all/any/uni/ballot")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("vote: no matching variant found");
  }
}

// PTX ISA §9.7.11.8
void TypeCheckerGen::check_vote_sync(const InstrVote<ResolvedOp>& instr) {
  // variant 0: vote.sync all/any/uni/ballot
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "vote.sync all/any/uni/ballot")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("vote.sync: no matching variant found");
  }
}

// PTX ISA §9.7.11.10
void TypeCheckerGen::check_activemask(const InstrActivemask<ResolvedOp>& instr) {
  // variant 0: activemask.b32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "activemask.b32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("activemask: no matching variant found");
  }
}

// PTX ISA §9.7.11.11
void TypeCheckerGen::check_redux_sync(const InstrReduxSync<ResolvedOp>& instr) {
  // variant 0: redux.sync op.type
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "redux.sync op.type")) return false;
    if (!(([&]{ static constexpr auto _allowed = std::to_array<ScalarType>({ScalarType::U32, ScalarType::S32, ScalarType::B32}); return is_one_of(instr.data.type_, _allowed); }()))) { error("redux.sync op.type: modifier type_ check failed"); return false; }
    return true;
  };
  if (!(check_v0())) {
    error("redux.sync: no matching variant found");
  }
}

// PTX ISA §9.7.11.12
void TypeCheckerGen::check_elect_sync(const InstrElectSync<ResolvedOp>& instr) {
  // variant 0: elect.sync.b32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "elect.sync.b32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("elect.sync: no matching variant found");
  }
}

// PTX ISA §9.7.11.13
void TypeCheckerGen::check_cp_async_bulk(const InstrCpAsyncBulk<ResolvedOp>& instr) {
  // variant 0: cp.async.bulk shared\x2190global
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "cp.async.bulk shared\x2190global")) return false;
    return true;
  };
  // variant 1: cp.async.bulk global\x2190shared (write-back)
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "cp.async.bulk global\x2190shared (write-back)")) return false;
    return true;
  };
  // variant 2: cp.async.bulk shared\x2190shared (cluster)
  auto check_v2 = [&]() -> bool {
    if (!require_ptx(1.0f, "cp.async.bulk shared\x2190shared (cluster)")) return false;
    return true;
  };
  if (!(check_v0() || check_v1() || check_v2())) {
    error("cp.async.bulk: no matching variant found");
  }
}

// PTX ISA §9.7.11.14
void TypeCheckerGen::check_cp_async_bulk_commit_group(const InstrCpAsyncBulkCommitGroup& instr) {
  // variant 0: cp.async.bulk.commit_group
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "cp.async.bulk.commit_group")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("cp.async.bulk.commit_group: no matching variant found");
  }
}

// PTX ISA §9.7.11.15
void TypeCheckerGen::check_cp_async_bulk_wait_group(const InstrCpAsyncBulkWaitGroup<ResolvedOp>& instr) {
  // variant 0: cp.async.bulk.wait_group with optional read
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "cp.async.bulk.wait_group with optional read")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("cp.async.bulk.wait_group: no matching variant found");
  }
}

// PTX ISA §9.7.11.17
void TypeCheckerGen::check_mbarrier_init(const InstrMbarrierInit<ResolvedOp>& instr) {
  // variant 0: mbarrier.init.b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mbarrier.init.b64")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("mbarrier.init: no matching variant found");
  }
}

// PTX ISA §9.7.11.17
void TypeCheckerGen::check_mbarrier_inval(const InstrMbarrierInval<ResolvedOp>& instr) {
  // variant 0: mbarrier.inval.b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mbarrier.inval.b64")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("mbarrier.inval: no matching variant found");
  }
}

// PTX ISA §9.7.11.18
void TypeCheckerGen::check_mbarrier_arrive(const InstrMbarrierArrive<ResolvedOp>& instr) {
  // variant 0: mbarrier.arrive (returns token)
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mbarrier.arrive (returns token)")) return false;
    return true;
  };
  // variant 1: mbarrier.arrive.noComplete
  auto check_v1 = [&]() -> bool {
    if (!require_ptx(1.0f, "mbarrier.arrive.noComplete")) return false;
    if (!((instr.data.no_complete == true))) { error("mbarrier.arrive.noComplete: modifier no_complete check failed"); return false; }
    return true;
  };
  if (!(check_v0() || check_v1())) {
    error("mbarrier.arrive: no matching variant found");
  }
}

// PTX ISA §9.7.11.18
void TypeCheckerGen::check_mbarrier_arrive_expect_tx(const InstrMbarrierExpectTx<ResolvedOp>& instr) {
  // variant 0: mbarrier.arrive.expect_tx.b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mbarrier.arrive.expect_tx.b64")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("mbarrier.arrive.expect_tx: no matching variant found");
  }
}

// PTX ISA §9.7.11.19
void TypeCheckerGen::check_mbarrier_test_wait(const InstrMbarrierTestWait<ResolvedOp>& instr) {
  // variant 0: mbarrier.test_wait.b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mbarrier.test_wait.b64")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("mbarrier.test_wait: no matching variant found");
  }
}

// PTX ISA §9.7.11.19
void TypeCheckerGen::check_mbarrier_try_wait(const InstrMbarrierTryWait<ResolvedOp>& instr) {
  // variant 0: mbarrier.try_wait.b64
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(1.0f, "mbarrier.try_wait.b64")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("mbarrier.try_wait: no matching variant found");
  }
}

// PTX ISA §9.7.9.1
void TypeCheckerGen::check_shfl_sync(const InstrShflSync<ResolvedOp>& instr) {
  // variant 0: shfl.sync mode.b32
  auto check_v0 = [&]() -> bool {
    if (!require_ptx(6.0f, "shfl.sync mode.b32")) return false;
    if (!require_sm(53u, "shfl.sync mode.b32")) return false;
    return true;
  };
  if (!(check_v0())) {
    error("shfl.sync: no matching variant found");
  }
}


}  // namespace ptx_frontend
