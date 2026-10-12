# CTA `barrier.sync`, `barrier.arrive`, and `barrier.red` coverage

The frontend models `barrier{.cta}.sync{.aligned} a{, b}`,
`barrier{.cta}.arrive{.aligned} a, b`, and the three
`barrier{.cta}.red` operations of [PTX ISA
9.3 §9.7.14.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar).
This is a separate `barrier` opcode, not an alias for `bar.sync` or
`barrier.cluster`.

`a` is a `.u32` immediate or register barrier identifier. An immediate must be
in `0..15`; the value of a register identifier remains a runtime obligation.
For `sync`, optional `b` is a `.u32` immediate or register participant count.
An immediate count must be a multiple of 32; omitting it means all threads in
the CTA participate. The same optional count applies to `red`. For `arrive`,
`b` is required and an immediate must be positive and a multiple of 32.
Register counts cannot be checked statically.

`barrier{.cta}.red.popc{.aligned}.u32 d, a{, b}, {!}c` writes the
population count to a `.u32` register. The
`barrier{.cta}.red.{and|or}{.aligned}.pred p, a{, b}, {!}c` forms write
the conjunction or disjunction to a predicate register. `c` is a predicate
register with optional negation. These reductions wait for the participating
warps and write the result to each waiting thread. The frontend preserves
the predicate's typed, owned negation; it does not compute the result.
The frontend preserves whether `.cta` and `.aligned` were written, and retains the location of an
explicit `.aligned`. It does not prove CTA convergence or model barrier state.
On `sm_6x` and older targets, a `barrier` instruction without `.aligned` has
the aligned runtime restriction described by the ISA, while its resolved `.aligned` field
still records the source spelling as absent.

Unqualified `barrier.sync`, `barrier.arrive`, and `barrier.red` require PTX 6.0 and `sm_30`;
their `.cta` forms require PTX 7.8 and `sm_30`. The `.cta` qualifier does not
change CTA barrier semantics. `arrive` does not wait for other participating
warps to arrive.
The existing `bar{.cta}` forms and `barrier.cluster` forms retain their distinct
contracts. The ISA warns against mixing `red` with `sync` or `arrive`
on the same active barrier; the frontend does not prove that runtime
protocol. The remaining `mbarrier` wait qualifier combinations are outside this slice.
