# CTA `barrier.sync` coverage

The frontend models the `barrier{.cta}.sync{.aligned} a{, b}` slice of [PTX ISA
9.3 §9.7.14.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar).
This is a separate `barrier` opcode, not an alias for `bar.sync` or
`barrier.cluster`.

`a` is a `.u32` immediate or register barrier identifier. An immediate must be
in `0..15`; the value of a register identifier remains a runtime obligation.
Optional `b` is a `.u32` immediate or register participant count. An immediate
count must be a multiple of 32; a register count cannot be checked statically.
Omitting `b` means all threads in the CTA participate. The frontend preserves
whether `.cta` and `.aligned` were written, and retains the location of an
explicit `.aligned`. It does not prove CTA convergence or model barrier state.
On `sm_6x` and older targets, an unqualified `barrier.sync` has the aligned
runtime restriction described by the ISA, while its resolved `.aligned` field
still records the source spelling as absent.

`barrier.sync` requires PTX 6.0 and `sm_30`; `barrier.cta.sync` requires PTX
7.8 and `sm_30`. The `.cta` qualifier does not change CTA barrier semantics.
The existing `bar{.cta}` forms and `barrier.cluster` forms retain their distinct
contracts. Standalone `barrier{.cta}.arrive` and `.red` and the remaining
`mbarrier` wait qualifier combinations are outside this slice.
