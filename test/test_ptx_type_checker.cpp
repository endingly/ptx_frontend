// test/test_type_checker.cpp
#include <gtest/gtest.h>
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