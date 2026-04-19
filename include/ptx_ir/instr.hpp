#pragma once
#include <numeric>
#include <optional>
#include "ptx_ir/details.hpp"

namespace ptx_frontend {

// ============================================================
// § 9  Instruction<Op>  ←  ast.rs  generate_instruction_type!(...)
//      Using std::variant for the closed sum type.
// ============================================================

template <OperandLike Op>
using Opt = std::optional<Op>;  // models Option<T> arguments

template <OperandLike Op>
struct InstrAbs {
  TypeFtz data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrActivemask {
  Op dst;
};
template <OperandLike Op>
struct InstrAdd {
  ArithDetails data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrAddExtended {
  CarryDetails data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrSubExtended {
  CarryDetails data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrMadExtended {
  MadCarryDetails data;
  Op dst, src1, src2, src3;
};
template <OperandLike Op>
struct InstrAnd {
  ScalarType data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrAtom {
  AtomDetails data;
  Op dst, src1 /*addr*/, src2;
};
template <OperandLike Op>
struct InstrAtomCas {
  AtomCasDetails data;
  Op dst, src1, src2, src3;
};
template <OperandLike Op>
struct InstrBarWarp {
  Op src;
};
template <OperandLike Op>
struct InstrBar {
  BarData data;
  Op src1;
  Opt<Op> src2;
};
template <OperandLike Op>
struct InstrBarRed {
  BarRedData data;
  Op dst1, src_barrier, src_predicate, src_negate_predicate;
  Opt<Op> src_threadcount;
};
template <OperandLike Op>
struct InstrBfe {
  ScalarType data;
  Op dst, src1, src2 /*u32*/, src3 /*u32*/;
};
template <OperandLike Op>
struct InstrBfi {
  ScalarType data;
  Op dst, src1, src2, src3 /*u32*/, src4 /*u32*/;
};
template <OperandLike Op>
struct InstrBmsk {
  BmskMode data;
  Op dst, src_a, src_b;
};

// Bra takes an Ident, not a full operand
template <typename Id>
struct InstrBra {
  Id src;
};

template <OperandLike Op>
struct InstrBrev {
  ScalarType data;
  Op dst, src;
};

// Call has its own args struct
template <OperandLike Op>
struct InstrCall {
  CallDetails data;
  CallArgs<typename Op::id_type /* if Op = ParsedOperand<Id> */> arguments;
};
// NOTE: In C++ without Rust's associated types, you may need to pass Id as
// a second template parameter for Call. See usage note below.

template <OperandLike Op>
struct InstrClz {
  ScalarType data;
  Op dst /*u32*/, src;
};
template <OperandLike Op>
struct InstrCos {
  FlushToZero data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrCpAsync {
  CpAsyncDetails data;
  Op src_to /*shared*/, src_from /*global*/;
};
struct InstrCpAsyncCommitGroup {};
template <OperandLike Op>
struct InstrCpAsyncWaitGroup {
  Op src_group;
};
struct InstrCpAsyncWaitAll {};
template <OperandLike Op>
struct InstrCreatePolicyFractional {
  CreatePolicyFractionalDetails data;
  Op dst_policy;
};
template <OperandLike Op>
struct InstrCvt {
  CvtDetails data;
  Op dst, src;
  Opt<Op> src2;
};
struct CvtPackDetails {
  ScalarType from;  // source type (e.g., S32)
  ScalarType to;    // destination element type (e.g., S16)
};
template <OperandLike Op>
struct InstrCvtPack {
  CvtPackDetails data;
  Op dst /*u32*/, src1 /*s32*/, src2 /*s32*/, src3 /*b32*/;
};
template <OperandLike Op>
struct InstrCvta {
  CvtaDetails data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrDiv {
  DivDetails data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrDp4a {
  Dp4aDetails data;
  Op dst /*b32*/, src1, src2, src3;
};
template <OperandLike Op>
struct InstrEx2 {
  TypeFtz data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrFma {
  ArithFloat data;
  Op dst, src1, src2, src3;
};
template <OperandLike Op>
struct InstrLd {
  LdDetails data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrLg2 {
  FlushToZero data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrMad {
  MadDetails data;
  Op dst, src1, src2, src3;
};
template <OperandLike Op>
struct InstrMax {
  MinMaxDetails data;
  Op dst, src1, src2;
};
struct InstrMembar {
  MemScope data;
};
template <OperandLike Op>
struct InstrMin {
  MinMaxDetails data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrMov {
  MovDetails data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrMul {
  MulDetails data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrMul24 {
  Mul24Details data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrMad24 {
  MadDetails data;
  Op dst, src1, src2, src3;
};
template <OperandLike Op>
struct InstrNanosleep {
  Op src;
};
template <OperandLike Op>
struct InstrNeg {
  TypeFtz data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrNot {
  ScalarType data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrOr {
  ScalarType data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrPopc {
  ScalarType data;
  Op dst /*u32*/, src;
};
template <OperandLike Op>
struct InstrPrmt {
  Op dst, src1, src2, src3;
};
template <OperandLike Op>
struct InstrRcp {
  RcpData data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrRem {
  ScalarType data;
  Op dst, src1, src2;
};
struct InstrRet {
  RetData data;
};
template <OperandLike Op>
struct InstrRsqrt {
  TypeFtz data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrSelp {
  ScalarType data;
  Op dst, src1, src2, src3 /*pred*/;
};
template <OperandLike Op>
struct InstrSet {
  SetData data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrSetBool {
  SetBoolData data;
  Op dst, src1, src2, src3 /*pred*/;
};
template <OperandLike Op>
struct InstrSetp {
  SetpData data;
  Op dst1 /*pred*/;
  Opt<Op> dst2 /*pred*/;
  Op src1, src2;
};
template <OperandLike Op>
struct InstrSetpBool {
  SetpBoolData data;
  Op dst1;
  Opt<Op> dst2;
  Op src1, src2, src3;
};
template <OperandLike Op>
struct InstrShflSync {
  ShflSyncDetails data;
  Op dst;
  Opt<Op> dst_pred;
  Op src, src_lane, src_opts, src_membermask;
};
template <OperandLike Op>
struct InstrShf {
  ShfDetails data;
  Op dst, src_a, src_b, src_c;
};
template <OperandLike Op>
struct InstrShl {
  ScalarType data;
  Op dst, src1, src2 /*u32*/;
};
template <OperandLike Op>
struct InstrShr {
  ShrData data;
  Op dst, src1, src2 /*u32*/;
};
template <OperandLike Op>
struct InstrSin {
  FlushToZero data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrSqrt {
  RcpData data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrSt {
  StData data;
  Op src1, src2;
};
template <OperandLike Op>
struct InstrSub {
  ArithDetails data;
  Op dst, src1, src2;
};
struct InstrTrap {};
template <OperandLike Op>
struct InstrXor {
  ScalarType data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrTanh {
  ScalarType data;
  Op dst, src;
};
template <OperandLike Op>
struct InstrVote {
  VoteDetails data;
  Op dst, src1 /*pred*/, src2 /*u32*/;
};
template <OperandLike Op>
struct InstrReduxSync {
  ReduxSyncData data;
  Op dst, src, src_membermask /*u32*/;
};
template <OperandLike Op>
struct InstrLdMatrix {
  LdMatrixDetails data;
  Op dst, src;
};
struct InstrGridDepControl {
  GridDepControlAction data;
};
template <OperandLike Op>
struct InstrMma {
  MmaDetails data;
  Op dst, src1, src2, src3;
};
template <OperandLike Op>
struct InstrCopysign {
  ScalarType data;
  Op dst, src1, src2;
};
template <OperandLike Op>
struct InstrPrefetch {
  PrefetchData data;
  Op src;
};
template <OperandLike Op>
struct InstrSad {
  ScalarType data;
  Op dst, src1, src2, src3;
};
template <OperandLike Op>
struct InstrDp2a {
  Dp2aData data;
  Op dst, src1, src2, src3;
};

// fence
template <OperandLike Op>
struct InstrFence {
  FenceDetails data;
  // present only for fence.proxy.tensormap::generic.acquire.scope [addr], size
  std::optional<Op> addr;
  std::optional<Op> size;
};

// red
template <OperandLike Op>
struct InstrRed {
  RedDetails data;
  Op addr, src;
};

// mbarrier（独立 struct，操作数不同）
template <OperandLike Op>
struct InstrMbarrierInit {
  Op addr, count;
};
template <OperandLike Op>
struct InstrMbarrierInval {
  Op addr;
};
template <OperandLike Op>
struct InstrMbarrierArrive {
  MbarrierArriveDetails data;
  std::optional<Op> token;  // 输出 token（non-drop, non-noComplete 变体）
  Op addr;
  std::optional<Op> count;
};
template <OperandLike Op>
struct InstrMbarrierTestWait {
  MbarrierTestWaitDetails data;
  Op done, addr, token_or_parity;
};
template <OperandLike Op>
struct InstrMbarrierTryWait {
  MbarrierTryWaitDetails data;
  Op done, addr, token_or_parity;
  std::optional<Op> timeout_ns;
};
template <OperandLike Op>
struct InstrMbarrierExpectTx {
  MbarrierExpectTxDetails data;
  Op addr, tx_count;
};
template <OperandLike Op>
struct InstrMbarrierCompleteTx {
  MbarrierCompleteTxDetails data;
  Op addr, tx_count;
};

// stmatrix
template <OperandLike Op>
struct InstrStMatrix {
  StMatrixDetails data;
  Op addr;
  std::vector<Op> srcs;  // 1/2/4 个 B16 寄存器
};

// wgmma
template <OperandLike Op>
struct InstrWgmmaMmaAsync {
  WgmmaMmaAsyncDetails data;
  std::vector<Op> d_srcs;  // 用于 scale_d=1 时的 C（同时作为输出 D）
  Op a;                    // register 或 smem desc
  Op b;                    // smem desc (SFA)
};

// tcgen05 子族
template <OperandLike Op>
struct InstrTcgen05Alloc {
  Tcgen05AllocDetails data;
  Op tptr, ncols;
};
template <OperandLike Op>
struct InstrTcgen05Dealloc {
  Tcgen05DeallocDetails data;
  Op tptr, ncols;
};
template <OperandLike Op>
struct InstrTcgen05Ld {
  Tcgen05LdStDetails data;
  std::vector<Op> dsts;
  Op tptr;
  std::optional<Op> offset;
};
template <OperandLike Op>
struct InstrTcgen05St {
  Tcgen05LdStDetails data;
  Op tptr;
  std::optional<Op> offset;
  std::vector<Op> srcs;
};
template <OperandLike Op>
struct InstrTcgen05Wait {
  Tcgen05WaitDetails data;
};
template <OperandLike Op>
struct InstrTcgen05Shift {
  Op tptr;
};
template <OperandLike Op>
struct InstrTcgen05CommitArrival {
  Op tptr, mbar;
};
template <OperandLike Op>
struct InstrTcgen05Relinquish {};
template <OperandLike Op>
struct InstrTcgen05Fence {
  Tcgen05FenceDetails data;
};
template <OperandLike Op>
struct InstrTcgen05Cp {
  Tcgen05CpDetails data;
  Op dst, src, count;
};
template <OperandLike Op>
struct InstrTcgen05MbarrierExpectTx {
  Op mbar, tx_count;
};

// cluster barrier
template <OperandLike Op>
struct InstrClusterBarrier {
  ClusterBarrierDetails data;
};

// setmaxnreg
template <OperandLike Op>
struct InstrSetMaxNReg {
  SetMaxNRegDetails data;
  Op count;
};

// getctarank
template <OperandLike Op>
struct InstrGetCtaRank {
  Op dst, addr;
};

// elect.sync
template <OperandLike Op>
struct InstrElectSync {
  Op dst;
  Op membermask;
};

// discard
template <OperandLike Op>
struct InstrDiscard {
  DiscardDetails data;
  Op addr;
};

// brkpt
struct InstrBrkpt {};

// movmatrix
template <OperandLike Op>
struct InstrMovMatrix {
  MovMatrixDetails data;
  Op dst, src;
};

// cp.async.bulk
template <OperandLike Op>
struct InstrCpAsyncBulk {
  Op dst, src, size;
};
template <OperandLike Op>
struct InstrCpAsyncBulkTensor {
  CpAsyncBulkTensorDetails data;
  Op dst, tensormap;
  std::vector<Op> coords;                  // 1–5 維
  std::optional<std::vector<Op>> offsets;  // im2col offsets（可选）
};

// cp.async.bulk.commit_group / wait_group（类似现有 CommitGroup/WaitGroup）
struct InstrCpAsyncBulkCommitGroup {};
template <OperandLike Op>
struct InstrCpAsyncBulkWaitGroup {
  Op n;
};

// tensormap
template <OperandLike Op>
struct InstrTensormapReplace {
  TensormapReplaceDetails data;
  Op tmap_ptr, new_val;
};
template <OperandLike Op>
struct InstrTensormapCpFenceProxy {
  TensormapCpFenceProxyDetails data;
  Op addr;
};

// prefetchu.L1
template <OperandLike Op>
struct InstrPrefetchu {
  CacheLevel level;
  Op addr;
};

// clusterlaunchcontrol
template <OperandLike Op>
struct InstrClusterLaunchControl {
  ClusterLaunchControlDetails data;
  std::optional<Op> dst;  // query_cancel 有 pred 输出
  Op addr;
};

// Instruction variant — Op = ParsedOp for the parsed stage
template <OperandLike Op>
using Instruction = std::variant<
    InstrAbs<Op>, InstrActivemask<Op>, InstrAdd<Op>, InstrAddExtended<Op>,
    InstrSubExtended<Op>, InstrMadExtended<Op>, InstrAnd<Op>, InstrAtom<Op>,
    InstrAtomCas<Op>, InstrBarWarp<Op>, InstrBar<Op>, InstrBarRed<Op>,
    InstrBfe<Op>, InstrBfi<Op>, InstrBmsk<Op>,
    InstrBra<typename Op::id_type>,  // <-- Ident only
    InstrBrev<Op>,
    InstrCall<Op>,  // NOTE: CallArgs needs Id; adapt as needed
    InstrClz<Op>, InstrCos<Op>, InstrCpAsync<Op>, InstrCpAsyncCommitGroup,
    InstrCpAsyncWaitGroup<Op>, InstrCpAsyncWaitAll,
    InstrCreatePolicyFractional<Op>, InstrCvt<Op>, InstrCvtPack<Op>,
    InstrCvta<Op>, InstrDiv<Op>, InstrDp4a<Op>, InstrEx2<Op>, InstrFma<Op>,
    InstrLd<Op>, InstrLg2<Op>, InstrMad<Op>, InstrMad24<Op>, InstrMax<Op>,
    InstrMembar, InstrMin<Op>, InstrMov<Op>, InstrMul<Op>, InstrMul24<Op>,
    InstrNanosleep<Op>, InstrNeg<Op>, InstrNot<Op>, InstrOr<Op>, InstrPopc<Op>,
    InstrPrmt<Op>, InstrRcp<Op>, InstrRem<Op>, InstrRet, InstrRsqrt<Op>,
    InstrSelp<Op>, InstrSet<Op>, InstrSetBool<Op>, InstrSetp<Op>,
    InstrSetpBool<Op>, InstrShflSync<Op>, InstrShf<Op>, InstrShl<Op>,
    InstrShr<Op>, InstrSin<Op>, InstrSqrt<Op>, InstrSt<Op>, InstrSub<Op>,
    InstrTrap, InstrXor<Op>, InstrTanh<Op>, InstrVote<Op>, InstrReduxSync<Op>,
    InstrLdMatrix<Op>, InstrGridDepControl, InstrMma<Op>, InstrCopysign<Op>,
    InstrPrefetch<Op>, InstrSad<Op>, InstrDp2a<Op>, InstrFence<Op>,
    InstrRed<Op>, InstrMbarrierInit<Op>, InstrMbarrierInval<Op>,
    InstrMbarrierArrive<Op>, InstrMbarrierTestWait<Op>,
    InstrMbarrierTryWait<Op>, InstrMbarrierExpectTx<Op>,
    InstrMbarrierCompleteTx<Op>, InstrStMatrix<Op>, InstrWgmmaMmaAsync<Op>,
    InstrTcgen05Alloc<Op>, InstrTcgen05Dealloc<Op>, InstrTcgen05Ld<Op>,
    InstrTcgen05St<Op>, InstrTcgen05Wait<Op>, InstrTcgen05Shift<Op>,
    InstrTcgen05CommitArrival<Op>, InstrTcgen05Relinquish<Op>,
    InstrTcgen05Fence<Op>, InstrTcgen05Cp<Op>, InstrTcgen05MbarrierExpectTx<Op>,
    InstrClusterBarrier<Op>, InstrSetMaxNReg<Op>, InstrGetCtaRank<Op>,
    InstrElectSync<Op>, InstrDiscard<Op>, InstrBrkpt, InstrMovMatrix<Op>,
    InstrCpAsyncBulk<Op>, InstrCpAsyncBulkTensor<Op>,
    InstrCpAsyncBulkCommitGroup, InstrCpAsyncBulkWaitGroup<Op>,
    InstrTensormapReplace<Op>, InstrTensormapCpFenceProxy<Op>,
    InstrPrefetchu<Op>, InstrClusterLaunchControl<Op>>;

// ============================================================
// § 11  Statement<Op>  ←  ast.rs::Statement
// ============================================================

template <OperandLike Op>
struct Statement {
  using IdType = typename Op::id_type;  // for IdentLike constraint on Label

  struct LabelS {
    IdType label;
  };

  struct VariableS {
    MultiVariable var;
  };

  struct InstructionS {
    std::optional<PredAt> pred;
    Instruction<Op> instr;
  };

  struct BlockS {
    std::vector<Statement<Op>> stmts;
  };

  using Inner = std::variant<LabelS, VariableS, InstructionS, BlockS>;
  Inner inner;

  static Statement Label(IdType id) { return {LabelS{id}}; }
  static Statement Var(MultiVariable v) { return {VariableS{v}}; }
  static Statement Instr(std::optional<PredAt> p, Instruction<Op> i) {
    return {InstructionS{p, std::move(i)}};
  }
  static Statement Block(std::vector<Statement<Op>> s) {
    return {BlockS{std::move(s)}};
  }
};

template <OperandLike Op>
struct Function {
  MethodDeclaration func_directive;
  std::vector<TuningDirective> tuning;
  std::optional<std::vector<Statement<Op>>> body;  // nullopt = declaration only
};

template <typename Op, typename Id = Ident>
struct Directive {
  struct VariableD {
    LinkingDirective linking;
    Variable var;
  };
  struct MethodD {
    LinkingDirective linking;
    Function<Op> func;
  };
  using Inner = std::variant<VariableD, MethodD>;
  Inner inner;
};

// ============================================================
// § 13  Module  ←  ast.rs::Module
// ============================================================

struct Module {
  std::pair<uint8_t, uint8_t> ptx_version;  // e.g. {8, 5}
  uint32_t sm_version;                      // e.g. 900 for sm_90
  std::vector<Directive<ParsedOp>> directives;
  size_t invalid_directives = 0;

  static Module empty() {
    return {{1, 0}, 0, {}, std::numeric_limits<size_t>::max()};
  }
};

};  // namespace ptx_frontend