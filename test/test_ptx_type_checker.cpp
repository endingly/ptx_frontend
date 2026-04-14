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
  tc.check_add(instr);
  return tc.errors();
}

// ── integer variants ─────────────────────────────────────────────────────────

TEST(TypeCheckerAdd, IntNoSat_U32_Ok) {
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

TEST(TypeCheckerAdd, F32_NoRnd_Ok) {
  auto sym = make_sym(
      {{"d", ScalarType::F32}, {"a", ScalarType::F32}, {"b", ScalarType::F32}});
  CompileTarget target{0, 1.0f};
  ArithFloat af;
  af.type_ = ScalarType::F32;
  af.is_fusable = true;  // no rounding
  auto instr = make_add(af, "d", "a", "b");
  EXPECT_TRUE(check_add(sym, target, instr).empty());
}

TEST(TypeCheckerAdd, F32_RmRp_RequiresSm20) {
  auto sym = make_sym(
      {{"d", ScalarType::F32}, {"a", ScalarType::F32}, {"b", ScalarType::F32}});
  ArithFloat af;
  af.type_ = ScalarType::F32;
  af.is_fusable = false;
  af.rounding = RoundingMode::NegativeInf;  // .rm
  // sm_10 < 20 → error
  CompileTarget bad{10, 1.0f};
  auto errs = check_add(sym, bad, make_add(af, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_20") != std::string::npos);
  // sm_20 → ok
  CompileTarget ok{20, 1.0f};
  EXPECT_TRUE(check_add(sym, ok, make_add(af, "d", "a", "b")).empty());
}

TEST(TypeCheckerAdd, F64_RequiresSm13) {
  auto sym = make_sym(
      {{"d", ScalarType::F64}, {"a", ScalarType::F64}, {"b", ScalarType::F64}});
  ArithFloat af;
  af.type_ = ScalarType::F64;
  af.is_fusable = true;
  CompileTarget bad{0, 1.0f};
  auto errs = check_add(sym, bad, make_add(af, "d", "a", "b"));
  EXPECT_FALSE(errs.empty());
  EXPECT_TRUE(errs[0].message.find("sm_13") != std::string::npos);
  CompileTarget ok{13, 1.0f};
  EXPECT_TRUE(check_add(sym, ok, make_add(af, "d", "a", "b")).empty());
}

TEST(TypeCheckerAdd, F32x2_RequiresSm100AndPtx86) {
  auto sym = make_sym({{"d", ScalarType::F32x2},
                       {"a", ScalarType::F32x2},
                       {"b", ScalarType::F32x2}});
  ArithFloat af;
  af.type_ = ScalarType::F32x2;
  af.is_fusable = true;
  CompileTarget ok{100, 8.6f};
  EXPECT_TRUE(check_add(sym, ok, make_add(af, "d", "a", "b")).empty());
  CompileTarget bad_sm{90, 8.6f};
  EXPECT_FALSE(check_add(sym, bad_sm, make_add(af, "d", "a", "b")).empty());
  CompileTarget bad_ptx{100, 8.0f};
  EXPECT_FALSE(check_add(sym, bad_ptx, make_add(af, "d", "a", "b")).empty());
}

TEST(TypeCheckerAdd, F32x2_SatIsError) {
  auto sym = make_sym({{"d", ScalarType::F32x2},
                       {"a", ScalarType::F32x2},
                       {"b", ScalarType::F32x2}});
  ArithFloat af;
  af.type_ = ScalarType::F32x2;
  af.saturate = true;
  CompileTarget target{100, 8.6f};
  auto errs = check_add(sym, target, make_add(af, "d", "a", "b"));
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find(".sat") != std::string::npos);
}

TEST(TypeCheckerAdd, F16_RnOnly_OtherRndIsError) {
  auto sym = make_sym(
      {{"d", ScalarType::F16}, {"a", ScalarType::F16}, {"b", ScalarType::F16}});
  ArithFloat af;
  af.type_ = ScalarType::F16;
  af.is_fusable = false;
  af.rounding = RoundingMode::Zero;  // .rz — not allowed for f16
  CompileTarget target{53, 6.0f};
  auto errs = check_add(sym, target, make_add(af, "d", "a", "b"));
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find(".rn") != std::string::npos);
}

TEST(TypeCheckerAdd, BF16_FtzIsError) {
  auto sym = make_sym({{"d", ScalarType::BF16},
                       {"a", ScalarType::BF16},
                       {"b", ScalarType::BF16}});
  ArithFloat af;
  af.type_ = ScalarType::BF16;
  af.flush_to_zero = true;
  CompileTarget target{80, 7.0f};
  auto errs = check_add(sym, target, make_add(af, "d", "a", "b"));
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find(".ftz") != std::string::npos);
}

// ── operand type mismatch ────────────────────────────────────────────────────

TEST(TypeCheckerAdd, OperandTypeMismatch_Error) {
  // dst declared as F32 but instruction is .s32 → type mismatch
  auto sym = make_sym(
      {{"d", ScalarType::F32}, {"a", ScalarType::S32}, {"b", ScalarType::S32}});
  CompileTarget target{80, 8.0f};
  auto instr = make_add(ArithInteger{ScalarType::S32, false}, "d", "a", "b");
  auto errs = check_add(sym, target, instr);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_TRUE(errs[0].message.find("type mismatch") != std::string::npos);
}

TEST(TypeCheckerAdd, UndefinedRegister_Error) {
  LegacySymbolTable sym;  // empty — no registers declared
  CompileTarget target{80, 8.0f};
  auto instr = make_add(ArithInteger{ScalarType::U32, false}, "d", "a", "b");
  auto errs = check_add(sym, target, instr);
  // three operands all undefined
  EXPECT_EQ(errs.size(), 3u);
  EXPECT_TRUE(errs[0].message.find("undefined") != std::string::npos);
}