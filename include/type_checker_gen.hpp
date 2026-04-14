// ============================================================
// AUTO-GENERATED -- do NOT edit by hand.
// Re-generate with:  python3 scripts/gen_type_checker_code.py
// ============================================================

#pragma once
#include <array>
#include <variant>
#include "type_checker.hpp"
#include "symbol_resolver.hpp"
#include "ptx_ir/instr.hpp"

namespace ptx_frontend {

/// TypeCheckerGen uses the ResolvedModule (symbol-resolved AST) to perform
/// per-instruction type checking.  Each check_<opcode>() method validates
/// the data modifiers against the PTX 9.2 specification.
///
/// Inherits from TypeChecker to reuse require_sm(), require_ptx(), error().
class TypeCheckerGen : public TypeChecker {
 public:
  TypeCheckerGen(const LegacySymbolTable& sym, const CompileTarget& target)
      : TypeChecker(sym, target) {}

  /// Check all instructions in a resolved module.
  void check_module(const ResolvedModule& mod);

  void check_setp(const InstrSetp<ResolvedOp>& instr);
  void check_set(const InstrSet<ResolvedOp>& instr);
  // selp: struct InstrSelp not available -- skipped
  // slct: struct InstrSlct not available -- skipped
  void check_bra(const InstrBra<ResolvedOp>& instr);
  // brx.idx: struct InstrBrxIdx not available -- skipped
  void check_call(const InstrCall<ResolvedOp>& instr);
  void check_ret(const InstrRet& instr);
  // exit: struct InstrExit not available -- skipped
  void check_trap(const InstrTrap& instr);
  void check_nanosleep(const InstrNanosleep<ResolvedOp>& instr);
  void check_mov(const InstrMov<ResolvedOp>& instr);
  void check_ld(const InstrLd<ResolvedOp>& instr);
  void check_st(const InstrSt<ResolvedOp>& instr);
  void check_prefetch(const InstrPrefetch<ResolvedOp>& instr);
  void check_prefetchu(const InstrPrefetch<ResolvedOp>& instr);
  void check_cvt(const InstrCvt<ResolvedOp>& instr);
  void check_cvt_pack(const InstrCvtPack<ResolvedOp>& instr);
  void check_cvta(const InstrCvta<ResolvedOp>& instr);
  void check_cp_async(const InstrCpAsync<ResolvedOp>& instr);
  void check_cp_async_commit_group(const InstrCpAsyncCommitGroup& instr);
  void check_cp_async_wait_group(const InstrCpAsyncWaitGroup<ResolvedOp>& instr);
  void check_cp_async_wait_all(const InstrCpAsyncWaitAll& instr);
  void check_createpolicy_fractional(const InstrCreatePolicyFractional<ResolvedOp>& instr);
  void check_fma(const InstrFma<ResolvedOp>& instr);
  void check_rcp(const InstrRcp<ResolvedOp>& instr);
  void check_sqrt(const InstrSqrt<ResolvedOp>& instr);
  void check_rsqrt(const InstrRsqrt<ResolvedOp>& instr);
  void check_sin(const InstrSin<ResolvedOp>& instr);
  void check_cos(const InstrCos<ResolvedOp>& instr);
  void check_lg2(const InstrLg2<ResolvedOp>& instr);
  void check_ex2(const InstrEx2<ResolvedOp>& instr);
  void check_tanh(const InstrTanh<ResolvedOp>& instr);
  void check_copysign(const InstrCopysign<ResolvedOp>& instr);
  void check_add(const InstrAdd<ResolvedOp>& instr);
  void check_sub(const InstrSub<ResolvedOp>& instr);
  void check_mul(const InstrMul<ResolvedOp>& instr);
  void check_mad(const InstrMad<ResolvedOp>& instr);
  void check_mul24(const InstrMul24<ResolvedOp>& instr);
  // mad24: struct InstrMad24 not available -- skipped
  void check_sad(const InstrSad<ResolvedOp>& instr);
  void check_div(const InstrDiv<ResolvedOp>& instr);
  void check_rem(const InstrRem<ResolvedOp>& instr);
  void check_abs(const InstrAbs<ResolvedOp>& instr);
  void check_neg(const InstrNeg<ResolvedOp>& instr);
  void check_min(const InstrMin<ResolvedOp>& instr);
  void check_max(const InstrMax<ResolvedOp>& instr);
  void check_clz(const InstrClz<ResolvedOp>& instr);
  void check_brev(const InstrBrev<ResolvedOp>& instr);
  void check_popc(const InstrPopc<ResolvedOp>& instr);
  void check_bfe(const InstrBfe<ResolvedOp>& instr);
  void check_bfi(const InstrBfi<ResolvedOp>& instr);
  void check_bmsk(const InstrBmsk<ResolvedOp>& instr);
  void check_dp4a(const InstrDp4a<ResolvedOp>& instr);
  void check_dp2a(const InstrDp2a<ResolvedOp>& instr);
  void check_add_cc(const InstrAddExtended<ResolvedOp>& instr);
  void check_addc(const InstrAddExtended<ResolvedOp>& instr);
  void check_sub_cc(const InstrSubExtended<ResolvedOp>& instr);
  void check_subc(const InstrSubExtended<ResolvedOp>& instr);
  void check_mad_cc(const InstrMadExtended<ResolvedOp>& instr);
  void check_madc(const InstrMadExtended<ResolvedOp>& instr);
  void check_and(const InstrAnd<ResolvedOp>& instr);
  void check_or(const InstrOr<ResolvedOp>& instr);
  void check_xor(const InstrXor<ResolvedOp>& instr);
  void check_not(const InstrNot<ResolvedOp>& instr);
  // cnot: struct InstrCnot not available -- skipped
  // lop3: struct InstrLop3 not available -- skipped
  void check_shl(const InstrShl<ResolvedOp>& instr);
  void check_shr(const InstrShr<ResolvedOp>& instr);
  void check_shf(const InstrShf<ResolvedOp>& instr);
  void check_prmt(const InstrPrmt<ResolvedOp>& instr);
  void check_ldmatrix(const InstrLdMatrix<ResolvedOp>& instr);
  void check_stmatrix(const InstrStMatrix<ResolvedOp>& instr);
  void check_mma_sync(const InstrMma<ResolvedOp>& instr);
  void check_cp_async_bulk_tensor(const InstrCpAsyncBulkTensor<ResolvedOp>& instr);
  void check_tcgen05_ld(const InstrTcgen05Ld<ResolvedOp>& instr);
  void check_tcgen05_st(const InstrTcgen05St<ResolvedOp>& instr);
  // tcgen05.mma: struct InstrTcgen05Mma not available -- skipped
  // tcgen05.mma.ws: struct InstrTcgen05MmaWs not available -- skipped
  void check_tcgen05_fence(const InstrTcgen05Fence<ResolvedOp>& instr);
  void check_tcgen05_commit(const InstrTcgen05CommitArrival<ResolvedOp>& instr);
  void check_tcgen05_wait(const InstrTcgen05Wait<ResolvedOp>& instr);
  void check_tcgen05_alloc(const InstrTcgen05Alloc<ResolvedOp>& instr);
  void check_tcgen05_dealloc(const InstrTcgen05Dealloc<ResolvedOp>& instr);
  void check_tcgen05_relinquish_alloc_permit(const InstrTcgen05Relinquish<ResolvedOp>& instr);
  void check_tcgen05_shift(const InstrTcgen05Shift<ResolvedOp>& instr);
  void check_brkpt(const InstrBrkpt& instr);
  // pmevent: struct InstrPmevent not available -- skipped
  void check_griddepcontrol_launch_dependents(const InstrGridDepControl& instr);
  void check_griddepcontrol_wait(const InstrGridDepControl& instr);
  void check_setmaxnreg(const InstrSetMaxNReg<ResolvedOp>& instr);
  // tex: struct InstrTex not available -- skipped
  // tld4: struct InstrTld4 not available -- skipped
  // cp.reduce.async.bulk: struct InstrCpReduceAsyncBulk not available -- skipped
  // cp.reduce.async.bulk.tensor: struct InstrCpReduceAsyncBulkTensor not available -- skipped
  // isspacep: struct InstrIsSpacep not available -- skipped
  // stacksave: struct InstrStacksave not available -- skipped
  // stackrestore: struct InstrStackrestore not available -- skipped
  // bar.sync: struct InstrBarSync not available -- skipped
  // bar.arrive: struct InstrBarArrive not available -- skipped
  void check_bar_red(const InstrBarRed<ResolvedOp>& instr);
  // bar.warp.sync: struct InstrBarWarpSync not available -- skipped
  void check_barrier_sync(const InstrClusterBarrier<ResolvedOp>& instr);
  void check_barrier_sync_aligned(const InstrClusterBarrier<ResolvedOp>& instr);
  void check_barrier_arrive(const InstrClusterBarrier<ResolvedOp>& instr);
  void check_barrier_arrive_aligned(const InstrClusterBarrier<ResolvedOp>& instr);
  void check_barrier_cluster_arrive(const InstrClusterBarrier<ResolvedOp>& instr);
  void check_barrier_cluster_wait(const InstrClusterBarrier<ResolvedOp>& instr);
  void check_membar(const InstrMembar& instr);
  void check_fence(const InstrFence<ResolvedOp>& instr);
  void check_atom(const InstrAtom<ResolvedOp>& instr);
  void check_red(const InstrRed<ResolvedOp>& instr);
  void check_vote(const InstrVote<ResolvedOp>& instr);
  void check_vote_sync(const InstrVote<ResolvedOp>& instr);
  // match.sync: struct InstrMatchSync not available -- skipped
  void check_activemask(const InstrActivemask<ResolvedOp>& instr);
  void check_redux_sync(const InstrReduxSync<ResolvedOp>& instr);
  void check_elect_sync(const InstrElectSync<ResolvedOp>& instr);
  void check_cp_async_bulk(const InstrCpAsyncBulk<ResolvedOp>& instr);
  void check_cp_async_bulk_commit_group(const InstrCpAsyncBulkCommitGroup& instr);
  void check_cp_async_bulk_wait_group(const InstrCpAsyncBulkWaitGroup<ResolvedOp>& instr);
  void check_mbarrier_init(const InstrMbarrierInit<ResolvedOp>& instr);
  void check_mbarrier_inval(const InstrMbarrierInval<ResolvedOp>& instr);
  void check_mbarrier_arrive(const InstrMbarrierArrive<ResolvedOp>& instr);
  void check_mbarrier_arrive_expect_tx(const InstrMbarrierExpectTx<ResolvedOp>& instr);
  void check_mbarrier_test_wait(const InstrMbarrierTestWait<ResolvedOp>& instr);
  void check_mbarrier_try_wait(const InstrMbarrierTryWait<ResolvedOp>& instr);
  // mbarrier.pending_count: struct InstrMbarrierPendingCount not available -- skipped
  void check_shfl_sync(const InstrShflSync<ResolvedOp>& instr);

};

}  // namespace ptx_frontend
