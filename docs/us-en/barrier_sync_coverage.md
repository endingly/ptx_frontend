# CTA `barrier.sync` and `barrier.arrive` coverage

The frontend models the `barrier{.cta}.sync{.aligned} a{, b}` and
`barrier{.cta}.arrive{.aligned} a, b` slices of [PTX ISA
9.3 §9.7.14.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar).
This is a separate `barrier` opcode, not an alias for `bar.sync` or
`barrier.cluster`.

`a` is a `.u32` immediate or register barrier identifier. An immediate must be
in `0..15`; the value of a register identifier remains a runtime obligation.
For `sync`, optional `b` is a `.u32` immediate or register participant count.
An immediate count must be a multiple of 32; omitting it means all threads in
the CTA participate. For `arrive`, `b` is required and an immediate must be
positive and a multiple of 32. Register counts cannot be checked statically.
The frontend preserves
whether `.cta` and `.aligned` were written, and retains the location of an
explicit `.aligned`. It does not prove CTA convergence or model barrier state.
On `sm_6x` and older targets, a `barrier` instruction without `.aligned` has
the aligned runtime restriction described by the ISA, while its resolved `.aligned` field
still records the source spelling as absent.

Unqualified `barrier.sync` and `barrier.arrive` require PTX 6.0 and `sm_30`;
their `.cta` forms require PTX 7.8 and `sm_30`. The `.cta` qualifier does not
change CTA barrier semantics. `arrive` does not wait for other participating
warps to arrive.
The existing `bar{.cta}` forms and `barrier.cluster` forms retain their distinct
contracts. Standalone `barrier{.cta}.red` and the remaining
`mbarrier` wait qualifier combinations are outside this slice.
