# TCGEN specialized synchronization

The frontend models `tcgen05.commit` and both operand-free specialized fences from fixed PTX 9.3. Commit has eight written forms: CTA group 1 or 2, generic or explicit `.shared::cluster` barrier-address spelling, and multicast absent or present. A multicast form requires a scalar General-class `.b16`, `.u16`, or `.s16` register mask; an immediate mask is not accepted. Each bit selects the destination `%cluster_ctarank`, and the mbarrier signal targets the same shared-memory offset in that CTA. The two fence directions, `before_thread_sync` and `after_thread_sync`, have no operands or CTA-group qualifier. These ten forms have exact final instruction classes and share a generated typed descriptor path.

```ptx
.version 9.0
.target sm_110a
.address_size 64
.visible .entry kernel() {
  .shared .align 8 .b64 barrier;
  .reg .b16 %mask;
  tcgen05.fence::before_thread_sync;
  tcgen05.commit.cta_group::1.mbarrier::arrive::one.multicast::cluster.b64 [barrier], %mask;
  tcgen05.fence::after_thread_sync;
  ret;
}
```

Commit records the protocol for prior applicable async TCGEN operations issued by the same thread and matching CTA group. On their completion, the system signals `mbarrier::arrive::one`, count one, at cluster scope. The barrier is accessed through the generic proxy even with explicit `.shared::cluster` spelling. A known barrier must be eight-byte aligned and in shared memory; dynamic pointers retain the obligation to resolve inside the cluster-shared window. The frontend checks known scalar pointer and mask declarations, source form, target, and per-function-body CTA-group consistency. A different function body may use the other group. It does not prove prior operations exist, mbarrier initialization, progress, peer CTA presence, dynamic address validity, cross-function agreement, or control-flow ordering.

`before_thread_sync` orders prior async TCGEN operations before later TCGEN and execution-ordering operations; `after_thread_sync` orders later operations after prior TCGEN and execution-ordering operations. They do not complete operations or replace `fence.proxy.async`. Commit's mbarrier-arrive-one completion identity is distinct from Tensor Memory load/store waits, bulk groups, generic async groups, and complete-tx-bytes.

These forms require exact `sm_100a` from PTX 8.6, family `sm_100f` from 8.8, or exact `sm_110a`/family `sm_110f` from 9.0. Generic and unrelated targets do not substitute. Official 13.3.73 assembler evidence corroborated all eight layouts, both fences, compatible 16-bit register masks, and these valid target endpoints. Direct literal masks failed with matched register controls; the assembler does not prove runtime mask values or synchronization behavior. The encoded descriptor/layout foundation and supported TCGEN MMA slices have separate, partial coverage contracts.

Source: [CUDA 13.3 / PTX 9.3 commit and fence clauses](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-commit). [Copy and shift](tensor_memory_copy_shift_coverage.md) are eligible prior TCGEN work for a matching group; their source forms do not themselves signal the barrier.
