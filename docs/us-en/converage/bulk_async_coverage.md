# Non-tensor bulk asynchronous data movement

The frontend models the non-tensor, non-multimem forms in [PTX ISA 9.3
§9.7.9.12, .14, .26.4, and .26.6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
They resolve to owned, typed `Cp` or `St` variants and pass through target-aware
checking. Ordinary non-bulk `cp.async` retains its existing three-operand
public shape. `red.async`, tensor-map transfers, and multimem transfers have
separate contracts.

| Form | Completion and operands | Earliest PTX / target |
| --- | --- | --- |
| `cp.async.bulk.shared::cluster.global.mbarrier::complete_tx::bytes` | Shared destination, global source, 32-bit byte size, shared mbarrier object address; optional multicast 16-bit `ctaMask` and/or L2 cache-hint 64-bit policy | 8.0 / SM 90 |
| `cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes` | Same base operands; optional L2 policy and/or `.ignore_oob` with two 32-bit ignore-byte operands | 8.6 / SM 90; `.ignore_oob` 9.2 |
| `cp.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes` | Shared destination and source, byte size, shared mbarrier object address | 8.0 / SM 90 |
| `cp.async.bulk.global.shared::cta.bulk_group` | Global destination, shared source, byte size; optional L2 policy and/or `.cp_mask` with 16-bit `byteMask` | 8.0 / SM 90; `.cp_mask` 8.6 / SM 100 |
| `cp.async.bulk.commit_group`, `wait_group{.read} N` | Independent bulk-group identity; `N` is an integer constant and `.read` waits for source reads | 8.0 / SM 90 |
| `cp.reduce.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes` | Shared reduction with byte size and mbarrier object address | 8.0 / SM 90 |
| `cp.reduce.async.bulk.global.shared::cta.bulk_group` | Global reduction with byte size; optional L2 cache-hint 64-bit policy | 8.0 / SM 90 |
| `cp.async.bulk.prefetch.L2.global` | Global address, 32-bit byte size; optional L2 cache-hint 64-bit policy | 8.0 / SM 90 |
| `st.async[.weak][.shared::cluster].mbarrier::complete_tx::bytes` | Scalar or v2/v4 register data and shared mbarrier object address | 8.1 / SM 90; scalar `.b128` 9.2 |
| `st.async[.mmio].release.{gpu,sys}[.global]` | Global scalar data, no mbarrier; `.mmio` requires `.sys` | 8.7 / SM 100 |
| `st.bulk[.weak][.shared::cta]` | Zero-fill size in u32/u64 or an immediate and literal init value `0` | 8.6 / SM 100; u32 size register 9.0 |

The shared reduction permits `add` with `u32/s32/u64`, `min/max` with
`u32/s32`, `inc/dec` with `u32`, and `and/or/xor` with `b32`. The global
reduction permits `add` with `u32/s32/u64/f32/f64`, `min/max` with
`u32/s32/u64/s64/f16/bf16`, `inc/dec` with `u32`, and `and/or/xor` with
`b32/b64`. Global `add.f16` and `add.bf16` require `.noftz`.

PTX 9.3 accepts explicit `.weak` on bulk copies and
`.relaxed.{cta,cluster,gpu,sys}...b128` on copies touching global memory.
Shared-to-shared relaxed copies permit only `.cta` or `.cluster`. These copy
qualifiers require `sm_90a`, a member of the `sm_100f` family at or above
SM 100, or a member of the `sm_110f` family at or above SM 110. The older
unqualified weak copy forms remain available on base SM 90. Reduction
`.relaxed.scope` is a separate PTX 9.3 addition available on base SM 90;
shared reductions permit `.cta/.cluster`, global reductions all four scopes.
When omitted, reduction scope follows the ISA's default `.relaxed.sys`.
The multicast architecture list is a performance recommendation, not an
additional legality gate.

For bulk copy, global reduction, and prefetch forms, `.L2::cache_hint` accepts
the ordinary operands with or without a final 64-bit `cache_policy` register.
The policy operand requires the hint. `st.async` destinations require a register
base; a symbol base or an immediate-only address is rejected.

Root `st.async` inherits the `st` wider-source-register rule from PTX 9.4.1.
Each scalar or explicit vector lane may use an otherwise compatible equal or
wider register, including B64/B128 Bit carriers; narrower registers reject.
Integer/Bit compatibility and floating-format restrictions remain independent:
F32/F64 data requires its matching native floating type or a sufficiently wide
Bit carrier, so F64 does not substitute for F32 data. Bit-typed data retains
its ordinary fundamental compatibility. Sources remain register-only.

The instruction type and written arity define transfer size, static alignment
and mbarrier completion bytes, irrespective of actual carrier widths. Shared v2
permits instruction elements through 64 bits; v4 remains limited to 32-bit
elements. Four B128 scalar carriers in a v4.u32 brace list therefore retain a
16-byte logical transfer, without declaring an oversized ordinary vector.
Scalar instruction `.b128` keeps its PTX 9.2 / SM 90 gate independently of a
wider carrier used for another logical type. Address/barrier and completion
contracts above are unchanged. Distinct
[`multimem.st.async` / `multimem.red.async`](multimem_coverage.md) data remains
same-width; sharing suffix words does not make them root `st` instructions.
This is static source/type validation, not execution of chopping or a runtime
complete-tx protocol proof.

The checker enforces known address spaces, static alignment (16 bytes for
bulk copy/reduce/prefetch, 8 bytes for a barrier), modifier/operand coupling,
supported type tuples, immediate divisibility, and target availability.
`st.bulk` size must be an 8-byte multiple no larger than 16,777,216 bytes.
Owned module validation compares a register size's cached type with its bound
declaration; standalone instruction checking defers that declaration check.
For register sizes and addresses, runtime values, remote-CTA provenance,
group/phase completion protocol, and cache behavior remain consumer
obligations. This frontend does not execute instructions.

Root shared `st.async` v2/v4 data sources accept ordinary named register vectors alongside scalar brace lanes. The declaration itself must remain at most 128 bits even when each carrier is wider than the logical element. Completion bytes and transfer alignment still derive from logical type times arity. Named data does not relax the independent register-base address requirement.
