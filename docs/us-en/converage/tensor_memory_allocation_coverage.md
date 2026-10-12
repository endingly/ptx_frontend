# Tensor Memory allocation coverage

The frontend models the PTX 9.3 `tcgen05` allocation-management trio:
`alloc`, `dealloc`, and `relinquish_alloc_permit`. Both
`.cta_group::{1,2}` values are supported. `alloc` accepts both an explicit
`.shared::cta` result-slot qualifier and the omitted generic qualifier. The
result is a four-byte value written to a separate shared-CTA slot; it is not
an allocation token returned directly in a register.

```ptx
.version 8.6
.target sm_100a
.address_size 64
.entry kernel() {
  .shared .align 4 .b32 slot;
  .reg .b32 %taddr;
  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 [slot], 32;
  ld.shared.b32 %taddr, [slot];
  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %taddr, 32;
  tcgen05.relinquish_alloc_permit.cta_group::1.sync.aligned;
  ret;
}
```

| Contract | Static frontend boundary |
| --- | --- |
| CTA group | Typed `TcgenCtaGroup::One` or `Two`; all supported TCGEN operations in one kernel/function body must agree. Separate bodies may choose different groups. A standalone instruction has no enclosing-body proof. |
| `nCols` | Scalar `.b32`/`.u32`/`.s32` general register, or integer immediate converted to unsigned 32 bits. Known converted values are 32, 64, 128, 256, or 512. Dynamic values retain the range, power-of-two, and group-uniformity obligation. |
| `taddr` | Distinct owned `TensorMemoryAddress` wrapping a scalar 32-bit register or converted immediate. The frontend does not infer a live allocation from its bits. Bits 31:16 are the lane address; bits 15:0 are the column address. |
| `alloc` result | Bracketed `ResolvedAddress` with a separate result-slot role, known shared-CTA provenance and four-byte natural alignment. An unresolved generic pointer retains a shared-window obligation. Known incompatible spaces and misalignment fail. |
| Action metadata | Each generated form carries a typed allocation action and permission effect. Group value maps to one issuing warp or one warp in each peer CTA. These describe obligations; they do not simulate runtime permission state. |

Integer immediates narrow **at use**. For example, `4294967328` and
`-4294967264` both convert to the legal count 32. The owned value retains
the original integer source and converted bits, and checker revalidation
requires them to agree after the syntax tree is destroyed.

Availability uses exact qualified targets: `sm_100a` from PTX 8.6, the
`sm_100f` family from PTX 8.8, and `sm_110a` plus the `sm_110f` family from
PTX 9.0. Generic `sm_100`, the `sm_120` family, and unknown targets do not
substitute for these profiles.

The frontend checks instruction-local types, known values, source provenance,
target gates, and per-body group consistency. It cannot prove peer CTA
participation, runtime allocation size evolution, deallocation before exit,
permission after relinquishment, or consistency across device-function calls.
Tensor Memory register `ld`, `st`, `ld.red`, and matching waits have a
[separate transfer contract](tensor_memory_transfer_coverage.md). Copy, shift,
and specialized commit/fence operations have separate coverage contracts. No GPU execution or
numerical result is modelled here.

Dedicated C++ and Python tests and an installed-package consumer exercise the
allocation contract after releasing the syntax tree. `ptxas` 13.3.73 accepted sampled allocation
syntax at `sm_100a`/PTX 8.6, `sm_100f`/PTX 8.8, and
`sm_110a`/`sm_110f`/PTX 9.0; it also rejected sampled invalid counts,
carriers, and early or generic targets. These assembler samples do not prove
GPU execution or runtime collective behavior.
