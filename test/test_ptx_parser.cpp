// import std;
import ptx_parser;
import ptx_ir;
// import gtest;
#include <gtest/gtest.h>

using namespace ptx_frontend;

// ── Test helpers ────────────────────────────────────────────────────────────

// Wrap a body snippet in a minimal .func so the parser can process it.
static std::string make_func(std::string_view body) {
  return std::string(R"(.version 8.0
.target sm_80
.address_size 64
.func test() {
)") + std::string(body) +
         "\n}\n";
}

// Parse a minimal function and return its instruction list (filtering out
// variable declarations and labels).
static std::vector<Instruction<ParsedOp>> parse_instrs(std::string_view body) {
  auto result = parse_module(make_func(body));
  if (!result.has_value() || result->directives.empty())
    return {};
  auto& method =
      std::get<Directive<ParsedOp>::MethodD>(result->directives[0].inner);
  std::vector<Instruction<ParsedOp>> out;
  for (auto& stmt : *method.func.body)
    if (auto* s = std::get_if<Statement<ParsedOp>::InstructionS>(&stmt.inner))
      out.push_back(s->instr);
  return out;
}

// Shorthand: get the Nth instruction, cast to InstrXxx.
template <typename T>
static const T& as(const std::vector<Instruction<ParsedOp>>& v, size_t n) {
  return std::get<T>(v[n]);
}

// Unwrap Reg operand → ident string.
static std::string_view reg(const ParsedOp& op) {
  return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
}

// Unwrap Imm operand → uint64.
static uint64_t imm(const ParsedOp& op) {
  auto& ri =
      std::get<ImmediateValue>(std::get<RegOrImmediate<Ident>>(op.value).value);
  return std::get<uint64_t>(ri.value);
}

static constexpr std::string_view MINIMAL_PTX = R"(
.version 8.0
.target sm_80
.address_size 64

.visible .entry kernel(.param .b64 param0)
{
    .reg .b64 %rd<2>;
    .reg .b32 %r<1>;

    ld.param.b64  %rd0, [param0];
    mov.b32       %r0, 42;
    ret;
}
)";

TEST(Parser, MinimalModule) {
  auto result = parse_module(MINIMAL_PTX);
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->ptx_version.first, 8);
  EXPECT_EQ(result->sm_version, 80);
  EXPECT_EQ(result->directives.size(), 1u);
}

TEST(Parser, MinimalModuleInstructions) {
  auto result = parse_module(MINIMAL_PTX);
  ASSERT_TRUE(result.has_value());

  // first directive is .entry kernel
  auto& dir = result->directives[0];
  auto& method = std::get<Directive<ParsedOp>::MethodD>(dir.inner);

  EXPECT_EQ(method.func.func_directive.name, "kernel");
  ASSERT_TRUE(method.func.body.has_value());

  auto& stmts = *method.func.body;
  // filter InstructionS
  std::vector<const Instruction<ParsedOp>*> instrs;
  for (auto& stmt : stmts) {
    if (auto* s = std::get_if<Statement<ParsedOp>::InstructionS>(&stmt.inner))
      instrs.push_back(&s->instr);
  }
  // ld / mov / ret → 3 instructions
  EXPECT_EQ(instrs.size(), 3u);

  // first ld
  EXPECT_TRUE(std::holds_alternative<InstrLd<ParsedOp>>(*instrs[0]));
  // second mov
  EXPECT_TRUE(std::holds_alternative<InstrMov<ParsedOp>>(*instrs[1]));
}

TEST(Parser, MovScalarImm) {
  auto instrs = parse_instrs("mov.b32 %r0, 42;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrMov<ParsedOp>>(instrs, 0);
  EXPECT_EQ(std::get<ScalarTypeT>(i.data.typ).type, ScalarType::B32);
}

TEST(Parser, MovScalarReg) {
  auto instrs = parse_instrs("mov.f64 %fd0, %fd1;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<InstrMov<ParsedOp>>(instrs[0]));
}

TEST(Parser, MovPack) {
  // scalar dst ← {a,b}
  auto instrs = parse_instrs("mov.b64 %rd0, {%r0, %r1};");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrMov<ParsedOp>>(instrs, 0);
  // src must be VecPack
  EXPECT_TRUE(
      std::holds_alternative<ParsedOperand<Ident>::VecPack>(i.src.value));
}

TEST(Parser, MovUnpack) {
  // {d,e} dst ← scalar src
  auto instrs = parse_instrs("mov.b64 {%r0, %r1}, %rd0;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrMov<ParsedOp>>(instrs, 0);
  EXPECT_TRUE(
      std::holds_alternative<ParsedOperand<Ident>::VecPack>(i.dst.value));
}

TEST(Parser, LdParam) {
  auto instrs = parse_instrs("ld.param.b64 %rd0, [param0];");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrLd<ParsedOp>>(instrs, 0);
  EXPECT_EQ(i.data.state_space, StateSpace::Param);
  EXPECT_EQ(i.data.qualifier.kind, LdStQualifier::Weak);
  EXPECT_EQ(std::get<ScalarTypeT>(i.data.typ).type, ScalarType::B64);
}

TEST(Parser, LdGlobal) {
  auto instrs = parse_instrs("ld.global.b32 %r0, [%rd0];");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrLd<ParsedOp>>(instrs, 0);
  EXPECT_EQ(i.data.state_space, StateSpace::Global);
}

TEST(Parser, LdVolatile) {
  auto instrs = parse_instrs("ld.volatile.global.b32 %r0, [%rd0];");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrLd<ParsedOp>>(instrs, 0);
  EXPECT_EQ(i.data.qualifier.kind, LdStQualifier::Volatile);
  EXPECT_EQ(i.data.state_space, StateSpace::Global);
}

TEST(Parser, LdRelaxed) {
  auto instrs = parse_instrs("ld.relaxed.gpu.global.b32 %r0, [%rd0];");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrLd<ParsedOp>>(instrs, 0);
  EXPECT_EQ(i.data.qualifier.kind, LdStQualifier::Relaxed);
  EXPECT_EQ(i.data.qualifier.scope, MemScope::Gpu);
}

TEST(Parser, StGlobal) {
  auto instrs = parse_instrs("st.global.b32 [%rd0], %r0;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrSt<ParsedOp>>(instrs, 0);
  EXPECT_EQ(i.data.state_space, StateSpace::Global);
}

TEST(Parser, StVolatile) {
  auto instrs = parse_instrs("st.volatile.shared.b32 [%rd0], %r0;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrSt<ParsedOp>>(instrs, 0);
  EXPECT_EQ(i.data.qualifier.kind, LdStQualifier::Volatile);
  EXPECT_EQ(i.data.state_space, StateSpace::Shared);
}

TEST(Parser, AddIntS32) {
  auto instrs = parse_instrs("add.s32 %r2, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& i = as<InstrAdd<ParsedOp>>(instrs, 0);
  auto& ai = std::get<ArithInteger>(i.data);
  EXPECT_EQ(ai.type_, ScalarType::S32);
  EXPECT_FALSE(ai.saturate);
}

TEST(Parser, AddIntSat) {
  auto instrs = parse_instrs("add.sat.s32 %r2, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& ai = std::get<ArithInteger>(as<InstrAdd<ParsedOp>>(instrs, 0).data);
  EXPECT_TRUE(ai.saturate);
}

TEST(Parser, SubS32) {
  auto instrs = parse_instrs("sub.s32 %r2, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<InstrSub<ParsedOp>>(instrs[0]));
}

TEST(Parser, MulLoS32) {
  auto instrs = parse_instrs("mul.lo.s32 %r2, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& mi = std::get<MulInt>(as<InstrMul<ParsedOp>>(instrs, 0).data);
  EXPECT_EQ(mi.type_, ScalarType::S32);
  EXPECT_EQ(mi.control, MulIntControl::Low);
}

TEST(Parser, MulWideS32) {
  auto instrs = parse_instrs("mul.wide.s32 %rd0, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& mi = std::get<MulInt>(as<InstrMul<ParsedOp>>(instrs, 0).data);
  EXPECT_EQ(mi.control, MulIntControl::Wide);
}

TEST(Parser, MulHiU32) {
  auto instrs = parse_instrs("mul.hi.u32 %r2, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& mi = std::get<MulInt>(as<InstrMul<ParsedOp>>(instrs, 0).data);
  EXPECT_EQ(mi.control, MulIntControl::High);
  EXPECT_EQ(mi.type_, ScalarType::U32);
}

TEST(Parser, MadLoS32) {
  auto instrs = parse_instrs("mad.lo.s32 %r3, %r0, %r1, %r2;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& mi = std::get<MadInt>(as<InstrMad<ParsedOp>>(instrs, 0).data);
  EXPECT_EQ(mi.control, MulIntControl::Low);
  EXPECT_FALSE(mi.saturate);
}

TEST(Parser, AddF32) {
  auto instrs = parse_instrs("add.rn.f32 %f2, %f0, %f1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& af = std::get<ArithFloat>(as<InstrAdd<ParsedOp>>(instrs, 0).data);
  EXPECT_EQ(af.type_, ScalarType::F32);
  EXPECT_EQ(af.rounding, RoundingMode::NearestEven);
}

TEST(Parser, MulF32Approx) {
  auto instrs = parse_instrs("mul.rn.f32 %f2, %f0, %f1;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<ArithFloat>(
      as<InstrMul<ParsedOp>>(instrs, 0).data));
}

TEST(Parser, FmaRnF32) {
  auto instrs = parse_instrs("fma.rn.f32 %f3, %f0, %f1, %f2;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& af = as<InstrFma<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(af.type_, ScalarType::F32);
  EXPECT_EQ(af.rounding, RoundingMode::NearestEven);
}

TEST(Parser, FmaRzFtzF32) {
  auto instrs = parse_instrs("fma.rz.ftz.f32 %f3, %f0, %f1, %f2;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& af = as<InstrFma<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(af.rounding, RoundingMode::Zero);
  EXPECT_EQ(af.flush_to_zero, std::optional<bool>{true});
}

TEST(Parser, CvtS32ToF32) {
  auto instrs = parse_instrs("cvt.rn.f32.s32 %f0, %r0;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& cd = as<InstrCvt<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(cd.from, ScalarType::S32);
  EXPECT_EQ(cd.to, ScalarType::F32);
}

TEST(Parser, CvtF32ToU32Trunc) {
  auto instrs = parse_instrs("cvt.rzi.u32.f32 %r0, %f0;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& cd = as<InstrCvt<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(cd.from, ScalarType::F32);
  EXPECT_EQ(cd.to, ScalarType::U32);
}

TEST(Parser, CvtaToGlobal) {
  auto instrs = parse_instrs("cvta.to.global.u64 %rd1, %rd0;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& cd = as<InstrCvta<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(cd.direction, CvtaDirection::GenericToExplicit);
  EXPECT_EQ(cd.state_space, StateSpace::Global);
}

TEST(Parser, CvtaFromGlobal) {
  auto instrs = parse_instrs("cvta.global.u64 %rd1, %rd0;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& cd = as<InstrCvta<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(cd.direction, CvtaDirection::ExplicitToGeneric);
}

TEST(Parser, AndB32) {
  auto instrs = parse_instrs("and.b32 %r2, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_EQ(as<InstrAnd<ParsedOp>>(instrs, 0).data, ScalarType::B32);
}

TEST(Parser, OrB32) {
  auto instrs = parse_instrs("or.b32 %r2, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<InstrOr<ParsedOp>>(instrs[0]));
}

TEST(Parser, Shl) {
  auto instrs = parse_instrs("shl.b32 %r1, %r0, 2;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_EQ(as<InstrShl<ParsedOp>>(instrs, 0).data, ScalarType::B32);
}

TEST(Parser, ShrU32) {
  auto instrs = parse_instrs("shr.u32 %r1, %r0, 1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& d = as<InstrShr<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(d.type_, ScalarType::U32);
  EXPECT_EQ(d.kind, RightShiftKind::Logical);
}

TEST(Parser, ShrS32Arithmetic) {
  auto instrs = parse_instrs("shr.s32 %r1, %r0, 1;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_EQ(as<InstrShr<ParsedOp>>(instrs, 0).data.kind,
            RightShiftKind::Arithmetic);
}

TEST(Parser, SetpLtS32) {
  auto instrs = parse_instrs("setp.lt.s32 %p0, %r0, %r1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& d = as<InstrSetp<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(d.type_, ScalarType::S32);
  EXPECT_EQ(std::get<SetpCompareInt>(d.cmp_op), SetpCompareInt::SignedLess);
  EXPECT_FALSE(d.flush_to_zero.has_value());
}

TEST(Parser, SetpEqF32Ftz) {
  auto instrs = parse_instrs("setp.eq.ftz.f32 %p0, %f0, %f1;");
  ASSERT_EQ(instrs.size(), 1u);
  auto& d = as<InstrSetp<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(d.type_, ScalarType::F32);
  EXPECT_EQ(std::get<SetpCompareFloat>(d.cmp_op), SetpCompareFloat::Eq);
  EXPECT_EQ(d.flush_to_zero, std::optional<bool>{true});
}

TEST(Parser, SetpBoolAndS32) {
  // setp.lt.and.s32 p0|p1, r0, r1, p2  → InstrSetpBool
  auto instrs = parse_instrs("setp.lt.and.s32 %p0|%p1, %r0, %r1, %p2;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<InstrSetpBool<ParsedOp>>(instrs[0]));
  auto& d = as<InstrSetpBool<ParsedOp>>(instrs, 0).data;
  EXPECT_EQ(d.bool_op, SetpBoolPostOp::And);
}

TEST(Parser, SelpS32) {
  auto instrs = parse_instrs("selp.s32 %r0, %r1, %r2, %p0;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_EQ(as<InstrSelp<ParsedOp>>(instrs, 0).data, ScalarType::S32);
}

TEST(Parser, Bra) {
  auto instrs = parse_instrs("bra target;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_EQ(as<InstrBra<Ident>>(instrs, 0).src, "target");
}

TEST(Parser, Ret) {
  auto instrs = parse_instrs("ret;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<InstrRet>(instrs[0]));
}

TEST(Parser, Trap) {
  auto instrs = parse_instrs("trap;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<InstrTrap>(instrs[0]));
}

TEST(Parser, BarSync) {
  auto instrs = parse_instrs("bar.sync 0;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_FALSE(as<InstrBar<ParsedOp>>(instrs, 0).data.aligned);
}

TEST(Parser, BarSyncAligned) {
  auto instrs = parse_instrs("bar.aligned.sync 0;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_TRUE(as<InstrBar<ParsedOp>>(instrs, 0).data.aligned);
}

TEST(Parser, MembarGl) {
  auto instrs = parse_instrs("membar.gl;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_EQ(as<InstrMembar>(instrs, 0).data, MemScope::Gpu);
}

TEST(Parser, MembarSys) {
  auto instrs = parse_instrs("membar.sys;");
  ASSERT_EQ(instrs.size(), 1u);
  EXPECT_EQ(as<InstrMembar>(instrs, 0).data, MemScope::Sys);
}

TEST(Parser, PredicateGuard) {
  auto result = parse_module(make_func("@%p0 add.s32 %r2, %r0, %r1;"));
  ASSERT_TRUE(result.has_value());
  auto& stmts =
      *std::get<Directive<ParsedOp>::MethodD>(result->directives[0].inner)
           .func.body;
  auto* s = std::get_if<Statement<ParsedOp>::InstructionS>(&stmts[0].inner);
  ASSERT_NE(s, nullptr);
  ASSERT_TRUE(s->pred.has_value());
  EXPECT_FALSE(s->pred->negate);
  EXPECT_EQ(s->pred->label, "%p0");
}

TEST(Parser, NegatedPredicateGuard) {
  auto result = parse_module(make_func("@!%p0 ret;"));
  ASSERT_TRUE(result.has_value());
  auto& stmts =
      *std::get<Directive<ParsedOp>::MethodD>(result->directives[0].inner)
           .func.body;
  auto* s = std::get_if<Statement<ParsedOp>::InstructionS>(&stmts[0].inner);
  ASSERT_NE(s, nullptr);
  EXPECT_TRUE(s->pred->negate);
}

TEST(Parser, LabelAndBra) {
  auto result = parse_module(make_func(R"(
    bra loop;
loop:
    ret;
  )"));
  ASSERT_TRUE(result.has_value());
  auto& stmts =
      *std::get<Directive<ParsedOp>::MethodD>(result->directives[0].inner)
           .func.body;
  // stmts: InstructionS(bra), LabelS(loop), InstructionS(ret)
  EXPECT_TRUE(
      std::holds_alternative<Statement<ParsedOp>::LabelS>(stmts[1].inner));
  EXPECT_EQ(std::get<Statement<ParsedOp>::LabelS>(stmts[1].inner).label,
            "loop");
}

TEST(Parser, InvalidInstrReturnsError) {
  auto result = parse_module(make_func("not_an_opcode.b32 %r0, %r1;"));
  // should parse module but with invalid_directives > 0 or return error
  // depends on error recovery strategy
  if (result.has_value()) {
    EXPECT_GT(result->invalid_directives, 0u);
  }
  // alternatively: EXPECT_FALSE(result.has_value());
}