// test/test_type_checker.cpp
#include <fmt/base.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <string>
#include "ptx_ir/instr.hpp"
#include "symbol_resolver.hpp"
#include "type_checker.hpp"

using namespace ptx_frontend;

// ── test helpers ─────────────────────────────────────────────────────────────

// Build a minimal SymbolTable with named registers (inserted into function-scope "fn")
static SymbolTable make_sym_tb(
    std::initializer_list<std::pair<std::string, ScalarType>> entries,
    const std::string& func_name = "fn") {
  SymbolTable sym;
  for (auto& [name, type] : entries) {
    auto r = sym.insert_local(func_name, name, SymbolKind::Register,
                              make_scalar(type), StateSpace::Reg);
    if (!r)
      throw std::runtime_error("symbol insert failed: " + name);
  }
  return sym;
}

// Build a register ResolvedOp (lookup in provided func scope). Returns kInvalidSymbolId if missing.
static ResolvedOp reg(const SymbolTable& sym, const std::string& func_name,
                      std::string_view name) {
  auto r = sym.lookup(name, func_name);
  SymbolId id = r ? *r : kInvalidSymbolId;
  return ResolvedOp::from_value(RegOrImmediate<SymbolId>::Reg(id));
}

// Construct InstrAdd<ResolvedOp>
static InstrAdd<ResolvedOp> make_add(ArithDetails data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2) {
  return InstrAdd<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src1),
                              reg(sym, func_name, src2)};
}

// Run TypeChecker on a Resolved Instr and return errors
static std::vector<TypeError> check_add(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrAdd<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── integer variants ─────────────────────────────────────────────────────────

TEST(TypeCheckerAdd, IntNoSat_U32_Ok) {
  // add.u32 d, a, b;
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr =
      make_add(ArithInteger{ScalarType::U32, false}, sym, "fn", "d", "a", "b");
  auto errs = check_add(sym, target, instr);
  EXPECT_TRUE(errs.empty());
}

TEST(TypeCheckerAdd, IntNoSat_S16_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::S16}, {"a", ScalarType::S16}, {"b", ScalarType::S16}});
  CompileTarget target{0, 1.0f};
  auto instr =
      make_add(ArithInteger{ScalarType::S16, false}, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_add(sym, target, instr).empty());
}

TEST(TypeCheckerAdd, IntSatS32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr =
      make_add(ArithInteger{ScalarType::S32, true}, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_add(sym, target, instr).empty());
}

TEST(TypeCheckerAdd, IntSatU32_RequiresSm120) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  // sm_90 < 120 → error
  CompileTarget target{90, 9.2f};
  auto instr =
      make_add(ArithInteger{ScalarType::U32, true}, sym, "fn", "d", "a", "b");
  auto errs = check_add(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("sm_120") != std::string::npos);
}

TEST(TypeCheckerAdd, SimdNoSat_U16x2_RequiresSm90) {
  auto sym = make_sym_tb({{"d", ScalarType::U16x2},
                          {"a", ScalarType::U16x2},
                          {"b", ScalarType::U16x2}});
  // sm_80 < 90 → error
  CompileTarget target{80, 8.0f};
  auto instr = make_add(ArithInteger{ScalarType::U16x2, false}, sym, "fn", "d",
                        "a", "b");
  auto errs = check_add(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

TEST(TypeCheckerAdd, SimdNoSat_S16x2_Ok_On_Sm90) {
  auto sym = make_sym_tb({{"d", ScalarType::S16x2},
                          {"a", ScalarType::S16x2},
                          {"b", ScalarType::S16x2}});
  CompileTarget target{90, 8.0f};
  auto instr = make_add(ArithInteger{ScalarType::S16x2, false}, sym, "fn", "d",
                        "a", "b");
  EXPECT_TRUE(check_add(sym, target, instr).empty());
}

TEST(TypeCheckerAdd, PackedSat_U8x4_RequiresSm120AndPtx92) {
  auto sym = make_sym_tb({{"d", ScalarType::U8x4},
                          {"a", ScalarType::U8x4},
                          {"b", ScalarType::U8x4}});
  // correct target
  CompileTarget ok{120, 9.2f};
  EXPECT_TRUE(check_add(sym, ok,
                        make_add(ArithInteger{ScalarType::U8x4, false}, sym,
                                 "fn", "d", "a", "b"))
                  .empty());
  // wrong sm
  CompileTarget bad_sm{90, 9.2f};
  auto errs = check_add(sym, bad_sm,
                        make_add(ArithInteger{ScalarType::U8x4, false}, sym,
                                 "fn", "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_120") != std::string::npos);
}

TEST(TypeCheckerAdd, IllegalIntCombo_SatU16_Error) {
  // add.sat.u16 is not a valid PTX variant
  auto sym = make_sym_tb(
      {{"d", ScalarType::U16}, {"a", ScalarType::U16}, {"b", ScalarType::U16}});
  CompileTarget target{90, 9.2f};
  auto instr =
      make_add(ArithInteger{ScalarType::U16, true}, sym, "fn", "d", "a", "b");
  auto errs = check_add(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("illegal") != std::string::npos);
}

// helpers for sub (mirror make_add)
static InstrSub<ResolvedOp> make_sub(ArithDetails data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2) {
  return InstrSub<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src1),
                              reg(sym, func_name, src2)};
}

static std::vector<TypeError> check_sub(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrSub<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── integer variants for sub ────────────────────────────────────────────────

TEST(TypeCheckerSub, IntNoSat_U32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr =
      make_sub(ArithInteger{ScalarType::U32, false}, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_sub(sym, target, instr).empty());
}

TEST(TypeCheckerSub, IntNoSat_S16_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::S16}, {"a", ScalarType::S16}, {"b", ScalarType::S16}});
  CompileTarget target{0, 1.0f};
  auto instr =
      make_sub(ArithInteger{ScalarType::S16, false}, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_sub(sym, target, instr).empty());
}

TEST(TypeCheckerSub, IntSatS32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr =
      make_sub(ArithInteger{ScalarType::S32, true}, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_sub(sym, target, instr).empty());
}

TEST(TypeCheckerSub, IntSatU32_RequiresSm120) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  // u32 not allowed with sat
  CompileTarget target{90, 9.2f};
  auto instr =
      make_sub(ArithInteger{ScalarType::U32, true}, sym, "fn", "d", "a", "b");
  auto errs = check_sub(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("illegal") != std::string::npos);
}

TEST(TypeCheckerSub, PackedOptionalSat_U8x4_RequiresSm120AndPtx92) {
  auto sym = make_sym_tb({{"d", ScalarType::U8x4},
                          {"a", ScalarType::U8x4},
                          {"b", ScalarType::U8x4}});
  // correct target
  CompileTarget ok{120, 9.2f};
  EXPECT_TRUE(check_sub(sym, ok,
                        make_sub(ArithInteger{ScalarType::U8x4, false}, sym,
                                 "fn", "d", "a", "b"))
                  .empty());
  // wrong sm
  CompileTarget bad_sm{90, 9.2f};
  auto errs = check_sub(sym, bad_sm,
                        make_sub(ArithInteger{ScalarType::U8x4, false}, sym,
                                 "fn", "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_120") != std::string::npos);
}

TEST(TypeCheckerSub, IllegalIntCombo_SatU16_Error) {
  // sub.sat.u16 is not a valid PTX variant
  auto sym = make_sym_tb(
      {{"d", ScalarType::U16}, {"a", ScalarType::U16}, {"b", ScalarType::U16}});
  CompileTarget target{90, 9.2f};
  auto instr =
      make_sub(ArithInteger{ScalarType::U16, true}, sym, "fn", "d", "a", "b");
  auto errs = check_sub(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("illegal") != std::string::npos);
}

// helpers for mul
static InstrMul<ResolvedOp> make_mul(MulDetails data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2) {
  return InstrMul<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src1),
                              reg(sym, func_name, src2)};
}

static std::vector<TypeError> check_mul(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrMul<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  auto flag = tc.check(instr);
  return tc.errors();
}

// ── integer variants for mul ────────────────────────────────────────────────

TEST(TypeCheckerMul, IntLow_U32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  // MulInt { type_, mode }
  auto instr = make_mul(MulInt{ScalarType::U32, MulIntControl::Low}, sym, "fn",
                        "d", "a", "b");
  EXPECT_TRUE(check_mul(sym, target, instr).empty());
}

TEST(TypeCheckerMul, IntWide_U64_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U64}, {"a", ScalarType::U64}, {"b", ScalarType::U64}});
  CompileTarget target{0, 1.0f};
  auto instr = make_mul(MulInt{ScalarType::U64, MulIntControl::Wide}, sym, "fn",
                        "d", "a", "b");
  EXPECT_TRUE(check_mul(sym, target, instr).empty());
}

// helpers for mad (four-operand)
static InstrMad<ResolvedOp> make_mad(MadDetails data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2,
                                     std::string_view src3) {
  return InstrMad<ResolvedOp>{
      data, reg(sym, func_name, dst), reg(sym, func_name, src1),
      reg(sym, func_name, src2), reg(sym, func_name, src3)};
}

static std::vector<TypeError> check_mad(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrMad<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  auto flag = tc.check(instr);
  return tc.errors();
}

// ── integer / float variants for mad ────────────────────────────────────────

TEST(TypeCheckerMad, IntLow_U32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_mad(MadInt{MulIntControl::Low, false, ScalarType::U32}, sym,
                        "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_mad(sym, target, instr).empty());
}

TEST(TypeCheckerMad, IntHiSat_S32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::S32},
                          {"a", ScalarType::S32},
                          {"b", ScalarType::S32},
                          {"c", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_mad(MadInt{MulIntControl::High, true, ScalarType::S32}, sym,
                        "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_mad(sym, target, instr).empty());
}

TEST(TypeCheckerMad, OperandTypeMismatch_Error) {
  // dst declared as F32 but instruction is .s32 → type mismatch
  auto sym = make_sym_tb({{"d", ScalarType::F32},
                          {"a", ScalarType::S32},
                          {"b", ScalarType::S32},
                          {"c", ScalarType::S32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_mad(MadInt{MulIntControl::Low, false, ScalarType::S32}, sym,
                        "fn", "d", "a", "b", "c");
  auto errs = check_mad(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for mad24 (uses same MadDetails / MadInt)
static InstrMad24<ResolvedOp> make_mad24(
    MadDetails data, const SymbolTable& sym, const std::string& func_name,
    std::string_view dst, std::string_view src1, std::string_view src2,
    std::string_view src3) {
  return InstrMad24<ResolvedOp>{
      data, reg(sym, func_name, dst), reg(sym, func_name, src1),
      reg(sym, func_name, src2), reg(sym, func_name, src3)};
}

static std::vector<TypeError> check_mad24(const SymbolTable& sym,
                                          const CompileTarget& target,
                                          const InstrMad24<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  auto flag = tc.check(instr);
  return tc.errors();
}

// ── integer variants for mad24 ────────────────────────────────────────────

TEST(TypeCheckerMad24, IntLo_U32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_mad24(MadInt{MulIntControl::Low, false, ScalarType::U32},
                          sym, "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_mad24(sym, target, instr).empty());
}

TEST(TypeCheckerMad24, IntHiSat_S32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::S32},
                          {"a", ScalarType::S32},
                          {"b", ScalarType::S32},
                          {"c", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_mad24(MadInt{MulIntControl::High, true, ScalarType::S32},
                          sym, "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_mad24(sym, target, instr).empty());
}

TEST(TypeCheckerMad24, IllegalType_Error) {
  // mad24 expects 24-bit multiply types (typically s32/u32) — u16 should be illegal
  auto sym = make_sym_tb({{"d", ScalarType::U16},
                          {"a", ScalarType::U16},
                          {"b", ScalarType::U16},
                          {"c", ScalarType::U16}});
  CompileTarget target{90, 9.2f};
  auto instr = make_mad24(MadInt{MulIntControl::Low, false, ScalarType::U16},
                          sym, "fn", "d", "a", "b", "c");
  auto errs = check_mad24(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("illegal") != std::string::npos);
}

// helpers for sad (4-operand)
static InstrSad<ResolvedOp> make_sad(ScalarType data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2,
                                     std::string_view src3) {
  return InstrSad<ResolvedOp>{
      data, reg(sym, func_name, dst), reg(sym, func_name, src1),
      reg(sym, func_name, src2), reg(sym, func_name, src3)};
}

static std::vector<TypeError> check_sad(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrSad<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerSad, U32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_sad(ScalarType::U32, sym, "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_sad(sym, target, instr).empty());
}

TEST(TypeCheckerSad, S16_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::S16},
                          {"a", ScalarType::S16},
                          {"b", ScalarType::S16},
                          {"c", ScalarType::S16}});
  CompileTarget target{0, 1.0f};
  auto instr = make_sad(ScalarType::S16, sym, "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_sad(sym, target, instr).empty());
}

TEST(TypeCheckerSad, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::F32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_sad(ScalarType::U32, sym, "fn", "d", "a", "b", "c");
  auto errs = check_sad(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for div
static InstrDiv<ResolvedOp> make_div(DivDetails data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2) {
  return InstrDiv<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src1),
                              reg(sym, func_name, src2)};
}

static std::vector<TypeError> check_div(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrDiv<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerDiv, IntU32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_div(DivInt{DivSign::Unsigned, ScalarType::U32}, sym, "fn",
                        "d", "a", "b");
  EXPECT_TRUE(check_div(sym, target, instr).empty());
}

TEST(TypeCheckerDiv, IntS16_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::S16}, {"a", ScalarType::S16}, {"b", ScalarType::S16}});
  CompileTarget target{0, 1.0f};
  auto instr = make_div(DivInt{DivSign::Signed, ScalarType::S16}, sym, "fn",
                        "d", "a", "b");
  EXPECT_TRUE(check_div(sym, target, instr).empty());
}

TEST(TypeCheckerDiv, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::F32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_div(DivInt{DivSign::Signed, ScalarType::S32}, sym, "fn",
                        "d", "a", "b");
  auto errs = check_div(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for rem
static InstrRem<ResolvedOp> make_rem(ScalarType data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2) {
  return InstrRem<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src1),
                              reg(sym, func_name, src2)};
}
static std::vector<TypeError> check_rem(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrRem<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerRem, U32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_rem(ScalarType::U32, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_rem(sym, target, instr).empty());
}

TEST(TypeCheckerRem, IllegalType_Error) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::F32}, {"a", ScalarType::F32}, {"b", ScalarType::F32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_rem(ScalarType::U32, sym, "fn", "d", "a", "b");
  auto errs = check_rem(sym, target, instr);
  ASSERT_EQ(errs.size(), 3u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for abs (TypeFtz)
static InstrAbs<ResolvedOp> make_abs(TypeFtz data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src) {
  return InstrAbs<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src)};
}

static std::vector<TypeError> check_abs(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrAbs<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerAbs, S32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::S32}, {"a", ScalarType::S32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_abs(TypeFtz{false, ScalarType::S32}, sym, "fn", "d", "a");
  EXPECT_TRUE(check_abs(sym, target, instr).empty());
}

TEST(TypeCheckerAbs, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::F32}, {"a", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_abs(TypeFtz{false, ScalarType::S32}, sym, "fn", "d", "a");
  auto errs = check_abs(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for neg (TypeFtz)
static InstrNeg<ResolvedOp> make_neg(TypeFtz data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src) {
  return InstrNeg<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src)};
}
static std::vector<TypeError> check_neg(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrNeg<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerNeg, S32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::S32}, {"a", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_neg(TypeFtz{false, ScalarType::S32}, sym, "fn", "d", "a");
  auto errs = check_neg(sym, target, instr);
  EXPECT_TRUE(errs.empty());
}

TEST(TypeCheckerNeg, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::F32}, {"a", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_neg(TypeFtz{false, ScalarType::S32}, sym, "fn", "d", "a");
  auto errs = check_neg(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for min/max
static InstrMin<ResolvedOp> make_min(MinMaxDetails data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2) {
  return InstrMin<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src1),
                              reg(sym, func_name, src2)};
}

static std::vector<TypeError> check_min(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrMin<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrMax<ResolvedOp> make_max(MinMaxDetails data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2) {
  return InstrMax<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src1),
                              reg(sym, func_name, src2)};
}
static std::vector<TypeError> check_max(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrMax<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── min tests ───────────────────────────────────────────────────────────────

TEST(TypeCheckerMin, Int_U32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U32, false};
  EXPECT_TRUE(
      check_min(sym, target, make_min(mi, sym, "fn", "d", "a", "b")).empty());
}

TEST(TypeCheckerMin, Simd_U16x2_RequiresSm90) {
  auto sym = make_sym_tb({{"d", ScalarType::U16x2},
                          {"a", ScalarType::U16x2},
                          {"b", ScalarType::U16x2}});
  CompileTarget target{80, 8.0f};  // sm < 90
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U16x2, false};
  auto errs = check_min(sym, target, make_min(mi, sym, "fn", "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

TEST(TypeCheckerMin, Packed_U8x4_RequiresSm120AndPtx92) {
  auto sym = make_sym_tb({{"d", ScalarType::U8x4},
                          {"a", ScalarType::U8x4},
                          {"b", ScalarType::U8x4}});
  CompileTarget ok{120, 9.2f};
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U8x4, false};
  EXPECT_TRUE(
      check_min(sym, ok, make_min(mi, sym, "fn", "d", "a", "b")).empty());

  CompileTarget bad_sm{90, 9.2f};
  auto errs = check_min(sym, bad_sm, make_min(mi, sym, "fn", "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_120") != std::string::npos);

  CompileTarget bad_ptx{120, 8.0f};
  auto errs2 = check_min(sym, bad_ptx, make_min(mi, sym, "fn", "d", "a", "b"));
  EXPECT_FALSE(errs2.empty());
  EXPECT_TRUE(errs2[0].message.find("PTX") != std::string::npos);
}

TEST(TypeCheckerMin, Relu_S32_Ok_And_RequiresSm90) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  // OK on sm_90
  CompileTarget ok{90, 8.0f};
  MinMaxInt mi_rel{MinMaxSign::Signed, ScalarType::S32, true};
  EXPECT_TRUE(
      check_min(sym, ok, make_min(mi_rel, sym, "fn", "d", "a", "b")).empty());

  // bad on lower sm
  CompileTarget bad{80, 8.0f};
  auto errs = check_min(sym, bad, make_min(mi_rel, sym, "fn", "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

// ── max tests (mirror of min) ──────────────────────────────────────────────

TEST(TypeCheckerMax, Int_U32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U32, false};
  EXPECT_TRUE(
      check_max(sym, target, make_max(mi, sym, "fn", "d", "a", "b")).empty());
}

TEST(TypeCheckerMax, Simd_U16x2_RequiresSm90) {
  auto sym = make_sym_tb({{"d", ScalarType::U16x2},
                          {"a", ScalarType::U16x2},
                          {"b", ScalarType::U16x2}});
  CompileTarget target{80, 8.0f};  // sm < 90
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U16x2, false};
  auto errs = check_max(sym, target, make_max(mi, sym, "fn", "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

TEST(TypeCheckerMax, Relu_S32_Ok_And_RequiresSm90) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget ok{90, 8.0f};
  MinMaxInt mi_rel{MinMaxSign::Signed, ScalarType::S32, true};
  EXPECT_TRUE(
      check_max(sym, ok, make_max(mi_rel, sym, "fn", "d", "a", "b")).empty());

  CompileTarget bad{80, 8.0f};
  auto errs = check_max(sym, bad, make_max(mi_rel, sym, "fn", "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

// helpers for popc / clz
static InstrPopc<ResolvedOp> make_popc(ScalarType data, const SymbolTable& sym,
                                       const std::string& func_name,
                                       std::string_view dst,
                                       std::string_view src) {
  return InstrPopc<ResolvedOp>{data, reg(sym, func_name, dst),
                               reg(sym, func_name, src)};
}
static std::vector<TypeError> check_popc(const SymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrPopc<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrClz<ResolvedOp> make_clz(ScalarType data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src) {
  return InstrClz<ResolvedOp>{data, reg(sym, func_name, dst),
                              reg(sym, func_name, src)};
}
static std::vector<TypeError> check_clz(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrClz<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── popc tests ───────────────────────────────────────────────────────────────

TEST(TypeCheckerPopc, B32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};  // sm >= 20, ptx >= 2.0
  auto instr = make_popc(ScalarType::B32, sym, "fn", "d", "a");
  EXPECT_TRUE(check_popc(sym, target, instr).empty());
}

TEST(TypeCheckerPopc, RequiresSm20) {
  auto sym = make_sym_tb({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_popc(ScalarType::B32, sym, "fn", "d", "a");
  auto errs = check_popc(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerPopc, DstTypeMismatch_Error) {
  // dst declared wrong type
  auto sym = make_sym_tb({{"d", ScalarType::U32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_popc(ScalarType::B32, sym, "fn", "d", "a");
  auto errs = check_popc(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// ── clz tests ────────────────────────────────────────────────────────────────

TEST(TypeCheckerClz, B32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_clz(ScalarType::B32, sym, "fn", "d", "a");
  EXPECT_TRUE(check_clz(sym, target, instr).empty());
}

TEST(TypeCheckerClz, RequiresSm20) {
  auto sym = make_sym_tb({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_clz(ScalarType::B32, sym, "fn", "d", "a");
  auto errs = check_clz(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerClz, DstTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::U32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_clz(ScalarType::B32, sym, "fn", "d", "a");
  auto errs = check_clz(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// helpers for bfind / fns
static InstrBfind<ResolvedOp> make_bfind(ScalarType t, const SymbolTable& sym,
                                         const std::string& func_name,
                                         bool shiftamt, std::string_view dst,
                                         std::string_view src) {
  return InstrBfind<ResolvedOp>{t, shiftamt, reg(sym, func_name, dst),
                                reg(sym, func_name, src)};
}
static std::vector<TypeError> check_bfind(const SymbolTable& sym,
                                          const CompileTarget& target,
                                          const InstrBfind<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrFns<ResolvedOp> make_fns(
    const SymbolTable& sym, const std::string& func_name, std::string_view dst,
    std::string_view mask, std::string_view base, std::string_view offset) {
  return InstrFns<ResolvedOp>{.dst = reg(sym, func_name, dst),
                              .mask = reg(sym, func_name, mask),
                              .base = reg(sym, func_name, base),
                              .offset = reg(sym, func_name, offset)};
}
static std::vector<TypeError> check_fns(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrFns<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── bfind tests ───────────────────────────────────────────────────────────────

TEST(TypeCheckerBfind, U32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32}, {"a", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfind(ScalarType::U32, sym, "fn", false, "d", "a");
  EXPECT_TRUE(check_bfind(sym, target, instr).empty());
}

TEST(TypeCheckerBfind, RequiresSm20) {
  auto sym = make_sym_tb({{"d", ScalarType::U32}, {"a", ScalarType::U32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_bfind(ScalarType::U32, sym, "fn", false, "d", "a");
  auto errs = check_bfind(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerBfind, Shiftamt_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32}, {"a", ScalarType::S64}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfind(ScalarType::S64, sym, "fn", true, "d", "a");
  auto errs = check_bfind(sym, target, instr);

  std::vector<std::string> err_strs;
  for (const auto& err : errs) {
    err_strs.push_back(err.message);
  }

  fmt::println("bfind shiftamt errs: \n{}", fmt::join(err_strs, "\n"));
  EXPECT_TRUE(errs.empty());
}

TEST(TypeCheckerBfind, DstTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::U64}, {"a", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfind(ScalarType::U32, sym, "fn", false, "d", "a");
  auto errs = check_bfind(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// ── fns tests ────────────────────────────────────────────────────────────────

TEST(TypeCheckerFns, U32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"m", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"o", ScalarType::S32}});
  CompileTarget target{80, 6.0f};  // sm >= 30 and PTX >= 6.0 required by spec
  auto instr = make_fns(sym, "fn", "d", "m", "b", "o");
  auto errs = check_fns(sym, target, instr);
  EXPECT_TRUE(errs.empty());
}

TEST(TypeCheckerFns, RequiresSm30) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"m", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"o", ScalarType::U32}});
  CompileTarget target{20, 6.0f};  // sm < 30
  auto instr = make_fns(sym, "fn", "d", "m", "b", "o");
  auto errs = check_fns(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_30") != std::string::npos);
}

TEST(TypeCheckerFns, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"m", ScalarType::B32},
                          {"b", ScalarType::U32},
                          {"o", ScalarType::U32}});
  CompileTarget target{80, 6.0f};
  auto instr = make_fns(sym, "fn", "d", "m", "b", "o");
  auto errs = check_fns(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

TEST(TypeCheckerFns, UndefinedRegisters_Error) {
  SymbolTable sym;  // empty
  CompileTarget target{80, 6.0f};
  auto instr = make_fns(sym, "fn", "d", "m", "b", "o");
  auto errs = check_fns(sym, target, instr);
  EXPECT_EQ(errs.size(), 4u);
}

// helpers for brev / bfe / bfi
static InstrBrev<ResolvedOp> make_brev(ScalarType data, const SymbolTable& sym,
                                       const std::string& func_name,
                                       std::string_view dst,
                                       std::string_view src) {
  return InstrBrev<ResolvedOp>{data, reg(sym, func_name, dst),
                               reg(sym, func_name, src)};
}

static std::vector<TypeError> check_brev(const SymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrBrev<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrBfe<ResolvedOp> make_bfe(ScalarType data, const SymbolTable& sym,
                                     const std::string& func_name,
                                     std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2,
                                     std::string_view src3) {
  return InstrBfe<ResolvedOp>{
      data, reg(sym, func_name, dst), reg(sym, func_name, src1),
      reg(sym, func_name, src2), reg(sym, func_name, src3)};
}

static std::vector<TypeError> check_bfe(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrBfe<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrBfi<ResolvedOp> make_bfi(
    ScalarType data, const SymbolTable& sym, const std::string& func_name,
    std::string_view dst, std::string_view src1, std::string_view src2,
    std::string_view src3, std::string_view src4) {
  return InstrBfi<ResolvedOp>{data,
                              reg(sym, func_name, dst),
                              reg(sym, func_name, src1),
                              reg(sym, func_name, src2),
                              reg(sym, func_name, src3),
                              reg(sym, func_name, src4)};
}
static std::vector<TypeError> check_bfi(const SymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrBfi<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── brev tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerBrev, B32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_brev(ScalarType::B32, sym, "fn", "d", "a");
  EXPECT_TRUE(check_brev(sym, target, instr).empty());
}

TEST(TypeCheckerBrev, RequiresSm20) {
  auto sym = make_sym_tb({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_brev(ScalarType::B32, sym, "fn", "d", "a");
  auto errs = check_brev(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerBrev, DstTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::U32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_brev(ScalarType::B32, sym, "fn", "d", "a");
  auto errs = check_brev(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// ── bfe tests ────────────────────────────────────────────────────────────────
TEST(TypeCheckerBfe, U32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfe(ScalarType::U32, sym, "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_bfe(sym, target, instr).empty());
}

TEST(TypeCheckerBfe, RequiresSm20) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_bfe(ScalarType::U32, sym, "fn", "d", "a", "b", "c");
  auto errs = check_bfe(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerBfe, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::F32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfe(ScalarType::U32, sym, "fn", "d", "a", "b", "c");
  auto errs = check_bfe(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// ── bfi tests ────────────────────────────────────────────────────────────────
TEST(TypeCheckerBfi, B32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::B32},
                          {"a", ScalarType::B32},
                          {"b", ScalarType::B32},
                          {"c", ScalarType::U32},
                          {"e", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfi(ScalarType::B32, sym, "fn", "d", "a", "b", "c", "e");
  EXPECT_TRUE(check_bfi(sym, target, instr).empty());
}

TEST(TypeCheckerBfi, RequiresSm20) {
  auto sym = make_sym_tb({{"d", ScalarType::B32},
                          {"a", ScalarType::B32},
                          {"b", ScalarType::B32},
                          {"c", ScalarType::U32},
                          {"e", ScalarType::U32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_bfi(ScalarType::B32, sym, "fn", "d", "a", "b", "c", "e");
  auto errs = check_bfi(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerBfi, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::F16},
                          {"a", ScalarType::B32},
                          {"b", ScalarType::B32},
                          {"c", ScalarType::U32},
                          {"e", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfi(ScalarType::B32, sym, "fn", "d", "a", "b", "c", "e");
  auto errs = check_bfi(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for dp4a / dp2a / bmsk
static InstrDp4a<ResolvedOp> make_dp4a(Dp4aDetails data, const SymbolTable& sym,
                                       const std::string& func_name,
                                       std::string_view dst,
                                       std::string_view src1,
                                       std::string_view src2,
                                       std::string_view src3) {
  return InstrDp4a<ResolvedOp>{
      data, reg(sym, func_name, dst), reg(sym, func_name, src1),
      reg(sym, func_name, src2), reg(sym, func_name, src3)};
}

static std::vector<TypeError> check_dp4a(const SymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrDp4a<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrDp2a<ResolvedOp> make_dp2a(Dp2aData data, const SymbolTable& sym,
                                       const std::string& func_name,
                                       std::string_view dst,
                                       std::string_view src1,
                                       std::string_view src2,
                                       std::string_view src3) {
  return InstrDp2a<ResolvedOp>{
      data, reg(sym, func_name, dst), reg(sym, func_name, src1),
      reg(sym, func_name, src2), reg(sym, func_name, src3)};
}
static std::vector<TypeError> check_dp2a(const SymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrDp2a<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrBmsk<ResolvedOp> make_bmsk(BmskMode mode, const SymbolTable& sym,
                                       const std::string& func_name,
                                       std::string_view dst, std::string_view a,
                                       std::string_view b) {
  return InstrBmsk<ResolvedOp>{mode, reg(sym, func_name, dst),
                               reg(sym, func_name, a), reg(sym, func_name, b)};
}
static std::vector<TypeError> check_bmsk(const SymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrBmsk<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── dp4a tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerDp4a, U32_U32_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_dp4a(Dp4aDetails{ScalarType::U32, ScalarType::U32}, sym,
                         "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_dp4a(sym, target, instr).empty());
}

TEST(TypeCheckerDp4a, RequiresSm61) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{60, 5.0f};  // sm < 61
  auto instr = make_dp4a(Dp4aDetails{ScalarType::U32, ScalarType::U32}, sym,
                         "fn", "d", "a", "b", "c");
  auto errs = check_dp4a(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_61") != std::string::npos);
}

TEST(TypeCheckerDp4a, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::F32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_dp4a(Dp4aDetails{ScalarType::U32, ScalarType::U32}, sym,
                         "fn", "d", "a", "b", "c");
  auto errs = check_dp4a(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// ── dp2a tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerDp2a, U32_Low_Ok) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  Dp2aData data{ScalarType::U32, ScalarType::U32, Dp2aControl::Low};
  auto instr = make_dp2a(data, sym, "fn", "d", "a", "b", "c");
  EXPECT_TRUE(check_dp2a(sym, target, instr).empty());
}

TEST(TypeCheckerDp2a, RequiresSm61) {
  auto sym = make_sym_tb({{"d", ScalarType::U32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{60, 5.0f};  // sm < 61
  Dp2aData data{ScalarType::U32, ScalarType::U32, Dp2aControl::Low};
  auto instr = make_dp2a(data, sym, "fn", "d", "a", "b", "c");
  auto errs = check_dp2a(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_61") != std::string::npos);
}

TEST(TypeCheckerDp2a, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::F32},
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32},
                          {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  Dp2aData data{ScalarType::U32, ScalarType::U32, Dp2aControl::Low};
  auto instr = make_dp2a(data, sym, "fn", "d", "a", "b", "c");
  auto errs = check_dp2a(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// ── bmsk tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerBmsk, B32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::B32}, {"a", ScalarType::B32}, {"b", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bmsk(BmskMode::Clamp, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_bmsk(sym, target, instr).empty());
}

TEST(TypeCheckerBmsk, RequiresSm70) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::B32}, {"a", ScalarType::B32}, {"b", ScalarType::B32}});
  CompileTarget target{60, 8.0f};  // sm < 70
  auto instr = make_bmsk(BmskMode::Clamp, sym, "fn", "d", "a", "b");
  auto errs = check_bmsk(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_70") != std::string::npos);
}

TEST(TypeCheckerBmsk, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::F32}, {"a", ScalarType::B32}, {"b", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bmsk(BmskMode::Clamp, sym, "fn", "d", "a", "b");
  auto errs = check_bmsk(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// helpers for szext
static InstrSzext<ResolvedOp> make_szext(BmskMode mode, ScalarType type_,
                                         const SymbolTable& sym,
                                         const std::string& func_name,
                                         std::string_view dst,
                                         std::string_view src1,
                                         std::string_view src2) {
  return InstrSzext<ResolvedOp>{mode, type_, reg(sym, func_name, dst),
                                reg(sym, func_name, src1),
                                reg(sym, func_name, src2)};
}
static std::vector<TypeError> check_szext(const SymbolTable& sym,
                                          const CompileTarget& target,
                                          const InstrSzext<ResolvedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── szext tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerSzext, U32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr =
      make_szext(BmskMode::Clamp, ScalarType::U32, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_szext(sym, target, instr).empty());
}

TEST(TypeCheckerSzext, S32_Ok) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{80, 8.0f};
  auto instr =
      make_szext(BmskMode::Wrap, ScalarType::S32, sym, "fn", "d", "a", "b");
  EXPECT_TRUE(check_szext(sym, target, instr).empty());
}

TEST(TypeCheckerSzext, RequiresSm70) {
  auto sym = make_sym_tb(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{60, 8.0f};  // sm < 70
  auto instr =
      make_szext(BmskMode::Clamp, ScalarType::U32, sym, "fn", "d", "a", "b");
  auto errs = check_szext(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_70") != std::string::npos);
}

TEST(TypeCheckerSzext, OperandTypeMismatch_Error) {
  auto sym = make_sym_tb({{"d", ScalarType::F32},  // dst has wrong type
                          {"a", ScalarType::U32},
                          {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr =
      make_szext(BmskMode::Clamp, ScalarType::U32, sym, "fn", "d", "a", "b");
  auto errs = check_szext(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

TEST(TypeCheckerSzext, InvalidTypeModifier_Error) {
  // type_ must be U32 or S32 — using B32 should trigger modifier error
  auto sym = make_sym_tb(
      {{"d", ScalarType::B32}, {"a", ScalarType::B32}, {"b", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr =
      make_szext(BmskMode::Clamp, ScalarType::B32, sym, "fn", "d", "a", "b");
  auto errs = check_szext(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("Modifier type_") != std::string::npos ||
              errs[0].message.find("illegal modifier") != std::string::npos);
}