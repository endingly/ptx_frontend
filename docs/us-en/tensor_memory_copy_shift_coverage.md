# Tensor Memory copy and shift

The frontend models the source-visible PTX 9.3 `tcgen05.cp` and `tcgen05.shift` forms as exact final instruction classes. Copy has **36** legal tuples: six shape/multicast pairs, two written CTA groups, and three paired format states. Shift has two group identities and accepts the two documented modifier orders for each group. Each semantic identity has direct typed operands, without an opcode-owner storage alternative.

| Copy shape | Written multicast |
| --- | --- |
| `.128x256b`, `.4x256b`, `.128x128b` | omitted |
| `.64x128b` | `.warpx2::02_13` or `.warpx2::01_23` |
| `.32x128b` | `.warpx4` |

The format pair is either absent, `.b8x16.b6x16_p32`, or `.b8x16.b4x16_p64`. Destination and source format tokens retain separate source locations in the owned form. A partial pair, two source formats, or a forbidden shape/multicast combination fails validation. Copy takes exactly `[taddr], s-desc`; shift takes exactly `[taddr]`. Shift has an implicit `.31x256b` movement shape, not a written qualifier. Both `tcgen05.shift.cta_group::1.down` and `tcgen05.shift.down.cta_group::1` are accepted as one group-1 identity, and similarly for group 2.

`[taddr]` uses the existing bracketed Tensor Memory address with U32 use conversion and retained 64-bit integer source. It admits a scalar General 32-bit register or converted integer immediate, without ordinary pointer offsets. For shift, the known converted lane component in bits 31:16 must be divisible by 32. The frontend checks this for known immediates; a register-valued lane remains a runtime obligation. It does not invent a byte-address or column restriction.

`s-desc` is a scalar General B64/U64/S64-compatible register. The copy form exposes a borrowed, distinct opaque TCGEN Table 43 descriptor-role view of that owned register, with known declaration and module-binding checks after AST release. The view ends with the owned instruction payload. A register's spelling does not reveal encoded descriptor contents: fixed/reserved bits, shared-memory base, leading stride, swizzle, padding, allocation and layout validity remain runtime obligations. This role is separate from WGMMA shared descriptors and TensorMap. The [TCGEN descriptor field and layout API](tcgen_descriptor_coverage.md) queries caller-supplied known words independently; it does not decode or authenticate this borrowed register's runtime contents.

Copy uses exact `sm_100a` from PTX 8.6, family `sm_100f` from 8.8, or exact `sm_110a` / family `sm_110f` from 9.0. Shift uses only exact `sm_100a` from 8.6, exact `sm_103a` from 8.8, or exact `sm_110a` from 9.0. Generic, unrelated family, and `sm_120` targets do not substitute. The group qualifier participates in per-function-body group consistency with allocation, transfer and commit; separate bodies may use different groups. Both operations are eligible prior same-thread asynchronous TCGEN work for a matching `tcgen05.commit` mbarrier-arrive-one protocol. They do not themselves signal a barrier or prove completion. Specialized fences and `fence.proxy.async` retain their separate ordering and visibility obligations.

Official 13.3.73 assembler evidence accepted all 36 copy tuples, both shift orders for both groups, compatible U64/S64 descriptor carriers, and the supported target endpoints. Partial format pairs and tested invalid carriers and targets failed with matched controls. The PTX 8.5 `sm_100a` directive control failed, so those two below-floor instruction candidates were skipped and supply no isolated instruction-floor evidence. Assembly does not prove runtime descriptor layout, decompression behavior, data motion, peer participation, or completion. No GPU execution was performed.

Sources: [fixed CUDA 13.3 / PTX 9.3 copy](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-cp), [shift](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-shift), and [shared-memory descriptor Table 43](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#shared-memory-descriptor).
