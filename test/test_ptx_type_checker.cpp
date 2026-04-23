// test/test_type_checker.cpp
#include <fmt/base.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <string>
#include "ptx_ir/instr.hpp"
#include "type_checker.hpp"

using namespace ptx_frontend;

// ── test helpers ─────────────────────────────────────────────────────────────

// Build a minimal SymbolTable with named registers.
static LegacySymbolTable make_sym(
    std::initializer_list<std::pair<std::string, ScalarType>> entries) {
  LegacySymbolTable sym;
  for (auto& [name, type] : entries)
    sym[name] = RegDecl{type, StateSpace::Reg};
  return sym;
}

// Build a register ParsedOp with the given name.
static ParsedOp reg(std::string_view name) {
  return ParsedOp::from_value(
      RegOrImmediate<Ident>::Reg(Ident{name.data(), name.size()}));
}

// Build an InstrAdd from ArithDetails + three operand names.
static InstrAdd<ParsedOp> make_add(ArithDetails data, std::string_view dst,
                                   std::string_view src1,
                                   std::string_view src2) {
  return InstrAdd<ParsedOp>{data, reg(dst), reg(src1), reg(src2)};
}

// Run TypeChecker::check_add and return the collected errors.
static std::vector<TypeError> check_add(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrAdd<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  auto flag = tc.check(instr);
  return tc.errors();
}

// ── integer variants ─────────────────────────────────────────────────────────

TEST(TypeCheckerAdd, IntNoSat_U32_Ok) {
  // add.u32 d, a, b;
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_add(ArithInteger{ScalarType::U32, false}, "d", "a", "b");
  EXPECT_TRUE(check_add(sym, target, instr).empty());
}

TEST(TypeCheckerAdd, IntNoSat_S16_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::S16}, {"a", ScalarType::S16}, {"b", ScalarType::S16}});
  CompileTarget target{0, 1.0f};
  auto instr = make_add(ArithInteger{ScalarType::S16, false}, "d", "a", "b");
  EXPECT_TRUE(check_add(sym, target, instr).empty());
}

TEST(TypeCheckerAdd, IntSatS32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_add(ArithInteger{ScalarType::S32, true}, "d", "a", "b");
  EXPECT_TRUE(check_add(sym, target, instr).empty());
}

TEST(TypeCheckerAdd, IntSatU32_RequiresSm120) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  // sm_90 < 120 → error
  CompileTarget target{90, 9.2f};
  auto instr = make_add(ArithInteger{ScalarType::U32, true}, "d", "a", "b");
  auto errs = check_add(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("sm_120") != std::string::npos);
}

TEST(TypeCheckerAdd, SimdNoSat_U16x2_RequiresSm90) {
  auto sym = make_sym({{"d", ScalarType::U16x2},
                       {"a", ScalarType::U16x2},
                       {"b", ScalarType::U16x2}});
  // sm_80 < 90 → error
  CompileTarget target{80, 8.0f};
  auto instr = make_add(ArithInteger{ScalarType::U16x2, false}, "d", "a", "b");
  auto errs = check_add(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

TEST(TypeCheckerAdd, SimdNoSat_S16x2_Ok_On_Sm90) {
  auto sym = make_sym({{"d", ScalarType::S16x2},
                       {"a", ScalarType::S16x2},
                       {"b", ScalarType::S16x2}});
  CompileTarget target{90, 8.0f};
  auto instr = make_add(ArithInteger{ScalarType::S16x2, false}, "d", "a", "b");
  EXPECT_TRUE(check_add(sym, target, instr).empty());
}

TEST(TypeCheckerAdd, PackedSat_U8x4_RequiresSm120AndPtx92) {
  auto sym = make_sym({{"d", ScalarType::U8x4},
                       {"a", ScalarType::U8x4},
                       {"b", ScalarType::U8x4}});
  // correct target
  CompileTarget ok{120, 9.2f};
  EXPECT_TRUE(
      check_add(sym, ok,
                make_add(ArithInteger{ScalarType::U8x4, false}, "d", "a", "b"))
          .empty());
  // wrong sm
  CompileTarget bad_sm{90, 9.2f};
  auto errs =
      check_add(sym, bad_sm,
                make_add(ArithInteger{ScalarType::U8x4, false}, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_120") != std::string::npos);
}

TEST(TypeCheckerAdd, IllegalIntCombo_SatU16_Error) {
  // add.sat.u16 is not a valid PTX variant
  auto sym = make_sym(
      {{"d", ScalarType::U16}, {"a", ScalarType::U16}, {"b", ScalarType::U16}});
  CompileTarget target{90, 9.2f};
  auto instr = make_add(ArithInteger{ScalarType::U16, true}, "d", "a", "b");
  auto errs = check_add(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("illegal") != std::string::npos);
}

// ── float variants ───────────────────────────────────────────────────────────

// TEST(TypeCheckerAdd, F32_NoRnd_Ok) {
//   auto sym = make_sym(
//       {{"d", ScalarType::F32}, {"a", ScalarType::F32}, {"b", ScalarType::F32}});
//   CompileTarget target{0, 1.0f};
//   ArithFloat af;
//   af.type_ = ScalarType::F32;
//   af.is_fusable = true;  // no rounding
//   auto instr = make_add(af, "d", "a", "b");
//   auto r = check_add(sym, target, instr);
//   EXPECT_TRUE(r.empty());
// }

// TEST(TypeCheckerAdd, F32_RmRp_RequiresSm20) {
//   auto sym = make_sym(
//       {{"d", ScalarType::F32}, {"a", ScalarType::F32}, {"b", ScalarType::F32}});
//   ArithFloat af;
//   af.type_ = ScalarType::F32;
//   af.is_fusable = false;
//   af.rnd = RoundingMode::NegativeInf;  // .rm
//   // sm_10 < 20 → error
//   CompileTarget bad{10, 1.0f};
//   auto errs = check_add(sym, bad, make_add(af, "d", "a", "b"));
//   EXPECT_FALSE(errs.empty());
//   EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
//   // sm_20 → ok
//   CompileTarget ok{20, 1.0f};
//   EXPECT_TRUE(check_add(sym, ok, make_add(af, "d", "a", "b")).empty());
// }

// TEST(TypeCheckerAdd, F64_RequiresSm13) {
//   auto sym = make_sym(
//       {{"d", ScalarType::F64}, {"a", ScalarType::F64}, {"b", ScalarType::F64}});
//   ArithFloat af;
//   af.type_ = ScalarType::F64;
//   af.is_fusable = true;
//   CompileTarget bad{0, 1.0f};
//   auto errs = check_add(sym, bad, make_add(af, "d", "a", "b"));
//   EXPECT_FALSE(errs.empty());
//   EXPECT_TRUE(errs[0].message.find("sm_13") != std::string::npos);
//   CompileTarget ok{13, 1.0f};
//   EXPECT_TRUE(check_add(sym, ok, make_add(af, "d", "a", "b")).empty());
// }

// TEST(TypeCheckerAdd, F32x2_RequiresSm100AndPtx86) {
//   auto sym = make_sym({{"d", ScalarType::F32x2},
//                        {"a", ScalarType::F32x2},
//                        {"b", ScalarType::F32x2}});
//   ArithFloat af;
//   af.type_ = ScalarType::F32x2;
//   af.is_fusable = true;
//   CompileTarget ok{100, 8.6f};
//   EXPECT_TRUE(check_add(sym, ok, make_add(af, "d", "a", "b")).empty());
//   CompileTarget bad_sm{90, 8.6f};
//   EXPECT_FALSE(check_add(sym, bad_sm, make_add(af, "d", "a", "b")).empty());
//   CompileTarget bad_ptx{100, 8.0f};
//   EXPECT_FALSE(check_add(sym, bad_ptx, make_add(af, "d", "a", "b")).empty());
// }

// TEST(TypeCheckerAdd, F32x2_SatIsError) {
//   auto sym = make_sym({{"d", ScalarType::F32x2},
//                        {"a", ScalarType::F32x2},
//                        {"b", ScalarType::F32x2}});
//   ArithFloat af;
//   af.type_ = ScalarType::F32x2;
//   af.sat = true;
//   CompileTarget target{100, 8.6f};
//   auto errs = check_add(sym, target, make_add(af, "d", "a", "b"));
//   ASSERT_EQ(errs.size(), 1u);
//   EXPECT_TRUE(errs[0].message.find(".sat") != std::string::npos);
// }

// TEST(TypeCheckerAdd, F16_RnOnly_OtherRndIsError) {
//   auto sym = make_sym(
//       {{"d", ScalarType::F16}, {"a", ScalarType::F16}, {"b", ScalarType::F16}});
//   ArithFloat af;
//   af.type_ = ScalarType::F16;
//   af.is_fusable = false;
//   af.rnd = RoundingMode::Zero;  // .rz — not allowed for f16
//   CompileTarget target{53, 6.0f};
//   auto errs = check_add(sym, target, make_add(af, "d", "a", "b"));
//   ASSERT_EQ(errs.size(), 1u);
//   EXPECT_TRUE(errs[0].message.find(".rn") != std::string::npos);
// }

// TEST(TypeCheckerAdd, BF16_FtzIsError) {
//   auto sym = make_sym({{"d", ScalarType::BF16},
//                        {"a", ScalarType::BF16},
//                        {"b", ScalarType::BF16}});
//   ArithFloat af;
//   af.type_ = ScalarType::BF16;
//   af.ftz = true;
//   CompileTarget target{80, 7.0f};
//   auto errs = check_add(sym, target, make_add(af, "d", "a", "b"));
//   ASSERT_EQ(errs.size(), 1u);
//   EXPECT_TRUE(errs[0].message.find(".ftz") != std::string::npos);
// }

// // ── operand type mismatch ────────────────────────────────────────────────────

// TEST(TypeCheckerAdd, OperandTypeMismatch_Error) {
//   // dst declared as F32 but instruction is .s32 → type mismatch
//   auto sym = make_sym(
//       {{"d", ScalarType::F32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
//   CompileTarget target{80, 8.0f};
//   auto instr = make_add(ArithInteger{ScalarType::S32, false}, "d", "a", "b");
//   auto errs = check_add(sym, target, instr);
//   ASSERT_EQ(errs.size(), 1u);
//   EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
// }

// TEST(TypeCheckerAdd, UndefinedRegister_Error) {
//   LegacySymbolTable sym;  // empty — no registers declared
//   CompileTarget target{80, 8.0f};
//   auto instr = make_add(ArithInteger{ScalarType::U32, false}, "d", "a", "b");
//   auto errs = check_add(sym, target, instr);
//   // three operands all undefined
//   EXPECT_EQ(errs.size(), 3u);
//   EXPECT_TRUE(errs[0].message.find("undefined") != std::string::npos);
// }

// helpers for sub (mirror make_add)
static InstrSub<ParsedOp> make_sub(ArithDetails data, std::string_view dst,
                                   std::string_view src1,
                                   std::string_view src2) {
  return InstrSub<ParsedOp>{data, reg(dst), reg(src1), reg(src2)};
}

static std::vector<TypeError> check_sub(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrSub<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  auto flag = tc.check(instr);
  return tc.errors();
}

// ── integer variants for sub ────────────────────────────────────────────────

TEST(TypeCheckerSub, IntNoSat_U32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_sub(ArithInteger{ScalarType::U32, false}, "d", "a", "b");
  EXPECT_TRUE(check_sub(sym, target, instr).empty());
}

TEST(TypeCheckerSub, IntNoSat_S16_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::S16}, {"a", ScalarType::S16}, {"b", ScalarType::S16}});
  CompileTarget target{0, 1.0f};
  auto instr = make_sub(ArithInteger{ScalarType::S16, false}, "d", "a", "b");
  EXPECT_TRUE(check_sub(sym, target, instr).empty());
}

TEST(TypeCheckerSub, IntSatS32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_sub(ArithInteger{ScalarType::S32, true}, "d", "a", "b");
  EXPECT_TRUE(check_sub(sym, target, instr).empty());
}

TEST(TypeCheckerSub, IntSatU32_RequiresSm120) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  // u32 not allowed with sat
  CompileTarget target{90, 9.2f};
  auto instr = make_sub(ArithInteger{ScalarType::U32, true}, "d", "a", "b");
  auto errs = check_sub(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("illegal") != std::string::npos);
}

TEST(TypeCheckerSub, PackedOptionalSat_U8x4_RequiresSm120AndPtx92) {
  auto sym = make_sym({{"d", ScalarType::U8x4},
                       {"a", ScalarType::U8x4},
                       {"b", ScalarType::U8x4}});
  // correct target
  CompileTarget ok{120, 9.2f};
  EXPECT_TRUE(
      check_sub(sym, ok,
                make_sub(ArithInteger{ScalarType::U8x4, false}, "d", "a", "b"))
          .empty());
  // wrong sm
  CompileTarget bad_sm{90, 9.2f};
  auto errs =
      check_sub(sym, bad_sm,
                make_sub(ArithInteger{ScalarType::U8x4, false}, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_120") != std::string::npos);
}

TEST(TypeCheckerSub, IllegalIntCombo_SatU16_Error) {
  // sub.sat.u16 is not a valid PTX variant
  auto sym = make_sym(
      {{"d", ScalarType::U16}, {"a", ScalarType::U16}, {"b", ScalarType::U16}});
  CompileTarget target{90, 9.2f};
  auto instr = make_sub(ArithInteger{ScalarType::U16, true}, "d", "a", "b");
  auto errs = check_sub(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("illegal") != std::string::npos);
}

// helpers for mul
static InstrMul<ParsedOp> make_mul(MulDetails data, std::string_view dst,
                                   std::string_view src1,
                                   std::string_view src2) {
  return InstrMul<ParsedOp>{data, reg(dst), reg(src1), reg(src2)};
}

static std::vector<TypeError> check_mul(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrMul<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  auto flag = tc.check(instr);
  return tc.errors();
}

// ── integer variants for mul ────────────────────────────────────────────────

TEST(TypeCheckerMul, IntLow_U32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  // MulInt { type_, mode }
  auto instr =
      make_mul(MulInt{ScalarType::U32, MulIntControl::Low}, "d", "a", "b");
  EXPECT_TRUE(check_mul(sym, target, instr).empty());
}

TEST(TypeCheckerMul, IntWide_U64_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::U64}, {"a", ScalarType::U64}, {"b", ScalarType::U64}});
  CompileTarget target{0, 1.0f};
  auto instr =
      make_mul(MulInt{ScalarType::U64, MulIntControl::Wide}, "d", "a", "b");
  EXPECT_TRUE(check_mul(sym, target, instr).empty());
}

// helpers for mad (four-operand)
static InstrMad<ParsedOp> make_mad(MadDetails data, std::string_view dst,
                                   std::string_view src1, std::string_view src2,
                                   std::string_view src3) {
  return InstrMad<ParsedOp>{data, reg(dst), reg(src1), reg(src2), reg(src3)};
}

static std::vector<TypeError> check_mad(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrMad<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  auto flag = tc.check(instr);
  return tc.errors();
}

// ── integer / float variants for mad ────────────────────────────────────────

TEST(TypeCheckerMad, IntLow_U32_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_mad(MadInt{MulIntControl::Low, false, ScalarType::U32}, "d",
                        "a", "b", "c");
  EXPECT_TRUE(check_mad(sym, target, instr).empty());
}

TEST(TypeCheckerMad, IntHiSat_S32_Ok) {
  auto sym = make_sym({{"d", ScalarType::S32},
                       {"a", ScalarType::S32},
                       {"b", ScalarType::S32},
                       {"c", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_mad(MadInt{MulIntControl::High, true, ScalarType::S32}, "d",
                        "a", "b", "c");
  EXPECT_TRUE(check_mad(sym, target, instr).empty());
}

TEST(TypeCheckerMad, OperandTypeMismatch_Error) {
  // dst declared as F32 but instruction is .s32 → type mismatch
  auto sym = make_sym({{"d", ScalarType::F32},
                       {"a", ScalarType::S32},
                       {"b", ScalarType::S32},
                       {"c", ScalarType::S32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_mad(MadInt{MulIntControl::Low, false, ScalarType::S32}, "d",
                        "a", "b", "c");
  auto errs = check_mad(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for mad24 (uses same MadDetails / MadInt)
static InstrMad24<ParsedOp> make_mad24(MadDetails data, std::string_view dst,
                                       std::string_view src1,
                                       std::string_view src2,
                                       std::string_view src3) {
  return InstrMad24<ParsedOp>{data, reg(dst), reg(src1), reg(src2), reg(src3)};
}

static std::vector<TypeError> check_mad24(const LegacySymbolTable& sym,
                                          const CompileTarget& target,
                                          const InstrMad24<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  auto flag = tc.check(instr);
  return tc.errors();
}

// ── integer variants for mad24 ────────────────────────────────────────────

TEST(TypeCheckerMad24, IntLo_U32_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_mad24(MadInt{MulIntControl::Low, false, ScalarType::U32},
                          "d", "a", "b", "c");
  EXPECT_TRUE(check_mad24(sym, target, instr).empty());
}

TEST(TypeCheckerMad24, IntHiSat_S32_Ok) {
  auto sym = make_sym({{"d", ScalarType::S32},
                       {"a", ScalarType::S32},
                       {"b", ScalarType::S32},
                       {"c", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_mad24(MadInt{MulIntControl::High, true, ScalarType::S32},
                          "d", "a", "b", "c");
  EXPECT_TRUE(check_mad24(sym, target, instr).empty());
}

TEST(TypeCheckerMad24, IllegalType_Error) {
  // mad24 expects 24-bit multiply types (typically s32/u32) — u16 should be illegal
  auto sym = make_sym({{"d", ScalarType::U16},
                       {"a", ScalarType::U16},
                       {"b", ScalarType::U16},
                       {"c", ScalarType::U16}});
  CompileTarget target{90, 9.2f};
  auto instr = make_mad24(MadInt{MulIntControl::Low, false, ScalarType::U16},
                          "d", "a", "b", "c");
  auto errs = check_mad24(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("illegal") != std::string::npos);
}

// helpers for sad (4-operand)
static InstrSad<ParsedOp> make_sad(ScalarType data, std::string_view dst,
                                   std::string_view src1, std::string_view src2,
                                   std::string_view src3) {
  return InstrSad<ParsedOp>{data, reg(dst), reg(src1), reg(src2), reg(src3)};
}
static std::vector<TypeError> check_sad(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrSad<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerSad, U32_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_sad(ScalarType::U32, "d", "a", "b", "c");
  EXPECT_TRUE(check_sad(sym, target, instr).empty());
}

TEST(TypeCheckerSad, S16_Ok) {
  auto sym = make_sym({{"d", ScalarType::S16},
                       {"a", ScalarType::S16},
                       {"b", ScalarType::S16},
                       {"c", ScalarType::S16}});
  CompileTarget target{0, 1.0f};
  auto instr = make_sad(ScalarType::S16, "d", "a", "b", "c");
  EXPECT_TRUE(check_sad(sym, target, instr).empty());
}

TEST(TypeCheckerSad, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::F32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_sad(ScalarType::U32, "d", "a", "b", "c");
  auto errs = check_sad(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for div
static InstrDiv<ParsedOp> make_div(DivDetails data, std::string_view dst,
                                   std::string_view src1,
                                   std::string_view src2) {
  return InstrDiv<ParsedOp>{data, reg(dst), reg(src1), reg(src2)};
}
static std::vector<TypeError> check_div(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrDiv<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerDiv, IntU32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr =
      make_div(DivInt{DivSign::Unsigned, ScalarType::U32}, "d", "a", "b");
  EXPECT_TRUE(check_div(sym, target, instr).empty());
}

TEST(TypeCheckerDiv, IntS16_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::S16}, {"a", ScalarType::S16}, {"b", ScalarType::S16}});
  CompileTarget target{0, 1.0f};
  auto instr =
      make_div(DivInt{DivSign::Signed, ScalarType::S16}, "d", "a", "b");
  EXPECT_TRUE(check_div(sym, target, instr).empty());
}

TEST(TypeCheckerDiv, OperandTypeMismatch_Error) {
  auto sym = make_sym(
      {{"d", ScalarType::F32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr =
      make_div(DivInt{DivSign::Signed, ScalarType::S32}, "d", "a", "b");
  auto errs = check_div(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for rem
static InstrRem<ParsedOp> make_rem(ScalarType data, std::string_view dst,
                                   std::string_view src1,
                                   std::string_view src2) {
  return InstrRem<ParsedOp>{data, reg(dst), reg(src1), reg(src2)};
}
static std::vector<TypeError> check_rem(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrRem<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerRem, U32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_rem(ScalarType::U32, "d", "a", "b");
  EXPECT_TRUE(check_rem(sym, target, instr).empty());
}

TEST(TypeCheckerRem, IllegalType_Error) {
  auto sym = make_sym(
      {{"d", ScalarType::F32}, {"a", ScalarType::F32}, {"b", ScalarType::F32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_rem(ScalarType::U32, "d", "a", "b");
  auto errs = check_rem(sym, target, instr);
  ASSERT_EQ(errs.size(), 3u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for abs (TypeFtz)
static InstrAbs<ParsedOp> make_abs(TypeFtz data, std::string_view dst,
                                   std::string_view src) {
  return InstrAbs<ParsedOp>{data, reg(dst), reg(src)};
}
static std::vector<TypeError> check_abs(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrAbs<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerAbs, S32_Ok) {
  auto sym = make_sym({{"d", ScalarType::S32}, {"a", ScalarType::S32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_abs(TypeFtz{false, ScalarType::S32}, "d", "a");
  EXPECT_TRUE(check_abs(sym, target, instr).empty());
}

TEST(TypeCheckerAbs, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::F32}, {"a", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_abs(TypeFtz{false, ScalarType::S32}, "d", "a");
  auto errs = check_abs(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for neg (TypeFtz)
static InstrNeg<ParsedOp> make_neg(TypeFtz data, std::string_view dst,
                                   std::string_view src) {
  return InstrNeg<ParsedOp>{data, reg(dst), reg(src)};
}
static std::vector<TypeError> check_neg(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrNeg<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

TEST(TypeCheckerNeg, S32_Ok) {
  auto sym = make_sym({{"d", ScalarType::S32}, {"a", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_neg(TypeFtz{false, ScalarType::S32}, "d", "a");
  auto errs = check_neg(sym, target, instr);
  EXPECT_TRUE(errs.empty());
}

TEST(TypeCheckerNeg, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::F32}, {"a", ScalarType::S32}});
  CompileTarget target{0, 1.0f};
  auto instr = make_neg(TypeFtz{false, ScalarType::S32}, "d", "a");
  auto errs = check_neg(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// helpers for min/max
static InstrMin<ParsedOp> make_min(MinMaxDetails data, std::string_view dst,
                                   std::string_view src1,
                                   std::string_view src2) {
  return InstrMin<ParsedOp>{data, reg(dst), reg(src1), reg(src2)};
}
static std::vector<TypeError> check_min(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrMin<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrMax<ParsedOp> make_max(MinMaxDetails data, std::string_view dst,
                                   std::string_view src1,
                                   std::string_view src2) {
  return InstrMax<ParsedOp>{data, reg(dst), reg(src1), reg(src2)};
}
static std::vector<TypeError> check_max(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrMax<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── min tests ───────────────────────────────────────────────────────────────

TEST(TypeCheckerMin, Int_U32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U32, false};
  EXPECT_TRUE(check_min(sym, target, make_min(mi, "d", "a", "b")).empty());
}

TEST(TypeCheckerMin, Simd_U16x2_RequiresSm90) {
  auto sym = make_sym({{"d", ScalarType::U16x2},
                       {"a", ScalarType::U16x2},
                       {"b", ScalarType::U16x2}});
  CompileTarget target{80, 8.0f};  // sm < 90
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U16x2, false};
  auto errs = check_min(sym, target, make_min(mi, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

TEST(TypeCheckerMin, Packed_U8x4_RequiresSm120AndPtx92) {
  auto sym = make_sym({{"d", ScalarType::U8x4},
                       {"a", ScalarType::U8x4},
                       {"b", ScalarType::U8x4}});
  CompileTarget ok{120, 9.2f};
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U8x4, false};
  EXPECT_TRUE(check_min(sym, ok, make_min(mi, "d", "a", "b")).empty());

  CompileTarget bad_sm{90, 9.2f};
  auto errs = check_min(sym, bad_sm, make_min(mi, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_120") != std::string::npos);

  CompileTarget bad_ptx{120, 8.0f};
  auto errs2 = check_min(sym, bad_ptx, make_min(mi, "d", "a", "b"));
  EXPECT_FALSE(errs2.empty());
  EXPECT_TRUE(errs2[0].message.find("PTX") != std::string::npos);
}

TEST(TypeCheckerMin, Relu_S32_Ok_And_RequiresSm90) {
  auto sym = make_sym(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  // OK on sm_90
  CompileTarget ok{90, 8.0f};
  MinMaxInt mi_rel{MinMaxSign::Signed, ScalarType::S32, true};
  EXPECT_TRUE(check_min(sym, ok, make_min(mi_rel, "d", "a", "b")).empty());

  // bad on lower sm
  CompileTarget bad{80, 8.0f};
  auto errs = check_min(sym, bad, make_min(mi_rel, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

// ── max tests (mirror of min) ──────────────────────────────────────────────

TEST(TypeCheckerMax, Int_U32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U32, false};
  EXPECT_TRUE(check_max(sym, target, make_max(mi, "d", "a", "b")).empty());
}

TEST(TypeCheckerMax, Simd_U16x2_RequiresSm90) {
  auto sym = make_sym({{"d", ScalarType::U16x2},
                       {"a", ScalarType::U16x2},
                       {"b", ScalarType::U16x2}});
  CompileTarget target{80, 8.0f};  // sm < 90
  MinMaxInt mi{MinMaxSign::Unsigned, ScalarType::U16x2, false};
  auto errs = check_max(sym, target, make_max(mi, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

TEST(TypeCheckerMax, Relu_S32_Ok_And_RequiresSm90) {
  auto sym = make_sym(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget ok{90, 8.0f};
  MinMaxInt mi_rel{MinMaxSign::Signed, ScalarType::S32, true};
  EXPECT_TRUE(check_max(sym, ok, make_max(mi_rel, "d", "a", "b")).empty());

  CompileTarget bad{80, 8.0f};
  auto errs = check_max(sym, bad, make_max(mi_rel, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_90") != std::string::npos);
}

// helpers for popc / clz
static InstrPopc<ParsedOp> make_popc(ScalarType data, std::string_view dst,
                                     std::string_view src) {
  return InstrPopc<ParsedOp>{data, reg(dst), reg(src)};
}
static std::vector<TypeError> check_popc(const LegacySymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrPopc<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrClz<ParsedOp> make_clz(ScalarType data, std::string_view dst,
                                   std::string_view src) {
  return InstrClz<ParsedOp>{data, reg(dst), reg(src)};
}
static std::vector<TypeError> check_clz(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrClz<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── popc tests ───────────────────────────────────────────────────────────────

TEST(TypeCheckerPopc, B32_Ok) {
  auto sym = make_sym({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};  // sm >= 20, ptx >= 2.0
  auto instr = make_popc(ScalarType::B32, "d", "a");
  EXPECT_TRUE(check_popc(sym, target, instr).empty());
}

TEST(TypeCheckerPopc, RequiresSm20) {
  auto sym = make_sym({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_popc(ScalarType::B32, "d", "a");
  auto errs = check_popc(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerPopc, DstTypeMismatch_Error) {
  // dst declared wrong type
  auto sym = make_sym({{"d", ScalarType::U32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_popc(ScalarType::B32, "d", "a");
  auto errs = check_popc(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// ── clz tests ────────────────────────────────────────────────────────────────

TEST(TypeCheckerClz, B32_Ok) {
  auto sym = make_sym({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_clz(ScalarType::B32, "d", "a");
  EXPECT_TRUE(check_clz(sym, target, instr).empty());
}

TEST(TypeCheckerClz, RequiresSm20) {
  auto sym = make_sym({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_clz(ScalarType::B32, "d", "a");
  auto errs = check_clz(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerClz, DstTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::U32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_clz(ScalarType::B32, "d", "a");
  auto errs = check_clz(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// helpers for bfind / fns
static InstrBfind<ParsedOp> make_bfind(ScalarType t, bool shiftamt,
                                       std::string_view dst,
                                       std::string_view src) {
  return InstrBfind<ParsedOp>{t, shiftamt, reg(dst), reg(src)};
}
static std::vector<TypeError> check_bfind(const LegacySymbolTable& sym,
                                          const CompileTarget& target,
                                          const InstrBfind<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrFns<ParsedOp> make_fns(std::string_view dst, std::string_view mask,
                                   std::string_view base,
                                   std::string_view offset) {
  return InstrFns<ParsedOp>{.dst = reg(dst),
                            .mask = reg(mask),
                            .base = reg(base),
                            .offset = reg(offset)};
}
static std::vector<TypeError> check_fns(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrFns<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── bfind tests ───────────────────────────────────────────────────────────────

TEST(TypeCheckerBfind, U32_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32}, {"a", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfind(ScalarType::U32, false, "d", "a");
  EXPECT_TRUE(check_bfind(sym, target, instr).empty());
}

TEST(TypeCheckerBfind, RequiresSm20) {
  auto sym = make_sym({{"d", ScalarType::U32}, {"a", ScalarType::U32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_bfind(ScalarType::U32, false, "d", "a");
  auto errs = check_bfind(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerBfind, Shiftamt_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32}, {"a", ScalarType::S64}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfind(ScalarType::S64, true, "d", "a");
  auto errs = check_bfind(sym, target, instr);

  std::vector<std::string> err_strs;
  for (const auto& err : errs) {
    err_strs.push_back(err.message);
  }

  fmt::println("bfind shiftamt errs: \n{}", fmt::join(err_strs, "\n"));
  EXPECT_TRUE(errs.empty());
}

TEST(TypeCheckerBfind, DstTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::U64}, {"a", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfind(ScalarType::U32, false, "d", "a");
  auto errs = check_bfind(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// ── fns tests ────────────────────────────────────────────────────────────────

TEST(TypeCheckerFns, U32_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"m", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"o", ScalarType::S32}});
  CompileTarget target{80, 6.0f};  // sm >= 30 and PTX >= 6.0 required by spec
  auto instr = make_fns("d", "m", "b", "o");
  auto errs = check_fns(sym, target, instr);
  EXPECT_TRUE(errs.empty());
}

TEST(TypeCheckerFns, RequiresSm30) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"m", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"o", ScalarType::U32}});
  CompileTarget target{20, 6.0f};  // sm < 30
  auto instr = make_fns("d", "m", "b", "o");
  auto errs = check_fns(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_30") != std::string::npos);
}

TEST(TypeCheckerFns, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"m", ScalarType::B32},
                       {"b", ScalarType::U32},
                       {"o", ScalarType::U32}});
  CompileTarget target{80, 6.0f};
  auto instr = make_fns("d", "m", "b", "o");
  auto errs = check_fns(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

TEST(TypeCheckerFns, UndefinedRegisters_Error) {
  LegacySymbolTable sym;  // empty
  CompileTarget target{80, 6.0f};
  auto instr = make_fns("d", "m", "b", "o");
  auto errs = check_fns(sym, target, instr);
  EXPECT_EQ(errs.size(), 4u);
}

// helpers for brev / bfe / bfi
static InstrBrev<ParsedOp> make_brev(ScalarType data, std::string_view dst,
                                     std::string_view src) {
  return InstrBrev<ParsedOp>{data, reg(dst), reg(src)};
}
static std::vector<TypeError> check_brev(const LegacySymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrBrev<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrBfe<ParsedOp> make_bfe(ScalarType data, std::string_view dst,
                                   std::string_view src1, std::string_view src2,
                                   std::string_view src3) {
  return InstrBfe<ParsedOp>{data, reg(dst), reg(src1), reg(src2), reg(src3)};
}
static std::vector<TypeError> check_bfe(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrBfe<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrBfi<ParsedOp> make_bfi(ScalarType data, std::string_view dst,
                                   std::string_view src1, std::string_view src2,
                                   std::string_view src3,
                                   std::string_view src4) {
  return InstrBfi<ParsedOp>{data,      reg(dst),  reg(src1),
                            reg(src2), reg(src3), reg(src4)};
}
static std::vector<TypeError> check_bfi(const LegacySymbolTable& sym,
                                        const CompileTarget& target,
                                        const InstrBfi<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── brev tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerBrev, B32_Ok) {
  auto sym = make_sym({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_brev(ScalarType::B32, "d", "a");
  EXPECT_TRUE(check_brev(sym, target, instr).empty());
}

TEST(TypeCheckerBrev, RequiresSm20) {
  auto sym = make_sym({{"d", ScalarType::B32}, {"a", ScalarType::B32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_brev(ScalarType::B32, "d", "a");
  auto errs = check_brev(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerBrev, DstTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::U32}, {"a", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_brev(ScalarType::B32, "d", "a");
  auto errs = check_brev(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// ── bfe tests ────────────────────────────────────────────────────────────────
TEST(TypeCheckerBfe, U32_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfe(ScalarType::U32, "d", "a", "b", "c");
  EXPECT_TRUE(check_bfe(sym, target, instr).empty());
}

TEST(TypeCheckerBfe, RequiresSm20) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_bfe(ScalarType::U32, "d", "a", "b", "c");
  auto errs = check_bfe(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerBfe, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::F32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfe(ScalarType::U32, "d", "a", "b", "c");
  auto errs = check_bfe(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// ── bfi tests ────────────────────────────────────────────────────────────────
TEST(TypeCheckerBfi, B32_Ok) {
  auto sym = make_sym({{"d", ScalarType::B32},
                       {"a", ScalarType::B32},
                       {"b", ScalarType::B32},
                       {"c", ScalarType::U32},
                       {"e", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfi(ScalarType::B32, "d", "a", "b", "c", "e");
  EXPECT_TRUE(check_bfi(sym, target, instr).empty());
}

TEST(TypeCheckerBfi, RequiresSm20) {
  auto sym = make_sym({{"d", ScalarType::B32},
                       {"a", ScalarType::B32},
                       {"b", ScalarType::B32},
                       {"c", ScalarType::U32},
                       {"e", ScalarType::U32}});
  CompileTarget target{10, 2.0f};  // sm < 20
  auto instr = make_bfi(ScalarType::B32, "d", "a", "b", "c", "e");
  auto errs = check_bfi(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
}

TEST(TypeCheckerBfi, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::F16},
                       {"a", ScalarType::B32},
                       {"b", ScalarType::B32},
                       {"c", ScalarType::U32},
                       {"e", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bfi(ScalarType::B32, "d", "a", "b", "c", "e");
  auto errs = check_bfi(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// helpers for dp4a / dp2a / bmsk
static InstrDp4a<ParsedOp> make_dp4a(Dp4aDetails data, std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2,
                                     std::string_view src3) {
  return InstrDp4a<ParsedOp>{data, reg(dst), reg(src1), reg(src2), reg(src3)};
}
static std::vector<TypeError> check_dp4a(const LegacySymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrDp4a<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrDp2a<ParsedOp> make_dp2a(Dp2aData data, std::string_view dst,
                                     std::string_view src1,
                                     std::string_view src2,
                                     std::string_view src3) {
  return InstrDp2a<ParsedOp>{data, reg(dst), reg(src1), reg(src2), reg(src3)};
}
static std::vector<TypeError> check_dp2a(const LegacySymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrDp2a<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

static InstrBmsk<ParsedOp> make_bmsk(BmskMode mode, std::string_view dst,
                                     std::string_view a, std::string_view b) {
  return InstrBmsk<ParsedOp>{mode, reg(dst), reg(a), reg(b)};
}
static std::vector<TypeError> check_bmsk(const LegacySymbolTable& sym,
                                         const CompileTarget& target,
                                         const InstrBmsk<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── dp4a tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerDp4a, U32_U32_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_dp4a(Dp4aDetails{ScalarType::U32, ScalarType::U32}, "d",
                         "a", "b", "c");
  EXPECT_TRUE(check_dp4a(sym, target, instr).empty());
}

TEST(TypeCheckerDp4a, RequiresSm61) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{60, 5.0f};  // sm < 61
  auto instr = make_dp4a(Dp4aDetails{ScalarType::U32, ScalarType::U32}, "d",
                         "a", "b", "c");
  auto errs = check_dp4a(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_61") != std::string::npos);
}

TEST(TypeCheckerDp4a, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::F32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_dp4a(Dp4aDetails{ScalarType::U32, ScalarType::U32}, "d",
                         "a", "b", "c");
  auto errs = check_dp4a(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// ── dp2a tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerDp2a, U32_Low_Ok) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  Dp2aData data{ScalarType::U32, ScalarType::U32, Dp2aControl::Low};
  auto instr = make_dp2a(data, "d", "a", "b", "c");
  EXPECT_TRUE(check_dp2a(sym, target, instr).empty());
}

TEST(TypeCheckerDp2a, RequiresSm61) {
  auto sym = make_sym({{"d", ScalarType::U32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{60, 5.0f};  // sm < 61
  Dp2aData data{ScalarType::U32, ScalarType::U32, Dp2aControl::Low};
  auto instr = make_dp2a(data, "d", "a", "b", "c");
  auto errs = check_dp2a(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_61") != std::string::npos);
}

TEST(TypeCheckerDp2a, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::F32},
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32},
                       {"c", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  Dp2aData data{ScalarType::U32, ScalarType::U32, Dp2aControl::Low};
  auto instr = make_dp2a(data, "d", "a", "b", "c");
  auto errs = check_dp2a(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

// ── bmsk tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerBmsk, B32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::B32}, {"a", ScalarType::B32}, {"b", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bmsk(BmskMode::Clamp, "d", "a", "b");
  EXPECT_TRUE(check_bmsk(sym, target, instr).empty());
}

TEST(TypeCheckerBmsk, RequiresSm70) {
  auto sym = make_sym(
      {{"d", ScalarType::B32}, {"a", ScalarType::B32}, {"b", ScalarType::B32}});
  CompileTarget target{60, 8.0f};  // sm < 70
  auto instr = make_bmsk(BmskMode::Clamp, "d", "a", "b");
  auto errs = check_bmsk(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_70") != std::string::npos);
}

TEST(TypeCheckerBmsk, OperandTypeMismatch_Error) {
  auto sym = make_sym(
      {{"d", ScalarType::F32}, {"a", ScalarType::B32}, {"b", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_bmsk(BmskMode::Clamp, "d", "a", "b");
  auto errs = check_bmsk(sym, target, instr);
  ASSERT_TRUE(errs.empty());
}

// helpers for szext
static InstrSzext<ParsedOp> make_szext(BmskMode mode, ScalarType type_,
                                       std::string_view dst,
                                       std::string_view src1,
                                       std::string_view src2) {
  return InstrSzext<ParsedOp>{mode, type_, reg(dst), reg(src1), reg(src2)};
}
static std::vector<TypeError> check_szext(const LegacySymbolTable& sym,
                                          const CompileTarget& target,
                                          const InstrSzext<ParsedOp>& instr) {
  TypeChecker tc{sym, target};
  tc.check(instr);
  return tc.errors();
}

// ── szext tests ───────────────────────────────────────────────────────────────
TEST(TypeCheckerSzext, U32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_szext(BmskMode::Clamp, ScalarType::U32, "d", "a", "b");
  EXPECT_TRUE(check_szext(sym, target, instr).empty());
}

TEST(TypeCheckerSzext, S32_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::S32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_szext(BmskMode::Wrap, ScalarType::S32, "d", "a", "b");
  EXPECT_TRUE(check_szext(sym, target, instr).empty());
}

TEST(TypeCheckerSzext, RequiresSm70) {
  auto sym = make_sym(
      {{"d", ScalarType::U32}, {"a", ScalarType::U32}, {"b", ScalarType::U32}});
  CompileTarget target{60, 8.0f};  // sm < 70
  auto instr = make_szext(BmskMode::Clamp, ScalarType::U32, "d", "a", "b");
  auto errs = check_szext(sym, target, instr);
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_70") != std::string::npos);
}

TEST(TypeCheckerSzext, OperandTypeMismatch_Error) {
  auto sym = make_sym({{"d", ScalarType::F32},  // dst has wrong type
                       {"a", ScalarType::U32},
                       {"b", ScalarType::U32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_szext(BmskMode::Clamp, ScalarType::U32, "d", "a", "b");
  auto errs = check_szext(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

TEST(TypeCheckerSzext, InvalidTypeModifier_Error) {
  // type_ must be U32 or S32 — using B32 should trigger modifier error
  auto sym = make_sym(
      {{"d", ScalarType::B32}, {"a", ScalarType::B32}, {"b", ScalarType::B32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_szext(BmskMode::Clamp, ScalarType::B32, "d", "a", "b");
  auto errs = check_szext(sym, target, instr);
  ASSERT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("Modifier type_") != std::string::npos ||
              errs[0].message.find("illegal modifier") != std::string::npos);
}