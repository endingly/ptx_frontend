# `mbarrier.try_wait` qualifier coverage

The frontend models the `.sem.scope` pair for all ten existing
`mbarrier.try_wait` shapes in [PTX ISA 9.3 §9.7.14.16.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-mbarrier-test-wait-mbarrier-try-wait).
Both qualifiers must be explicit and ordered: `.acquire|.relaxed` immediately
followed by `.cta|.cluster`, before the object space and `.b64`. A lone
qualifier, reversed pair, or `.shared::cluster` wait object is rejected.
The synchronization scope and the `.shared::cta` object space are distinct.

Each paired form retains its unqualified counterpart's token or parity
operand, generic/`.shared`/`.shared::cta` address choices, and optional
`timeHint` operand. `timeHint` is a `.u32` register or immediate in
nanoseconds. Explicit `.phase_type::primary` retains all six layouts: ordinary,
report-predicate, and report-value results, each with or without `timeHint`.
Explicit `.phase_type::conditional` requires `.parity` and has only the
ordinary with/without-hint layouts. Immediate parity must be `0` or `1`, and
the object address must be aligned to eight bytes. Owned resolved IR retains
the explicit qualifier values, locations, and chosen operand layout.

All `try_wait` forms require `sm_90`. The explicit pair starts at PTX 8.0;
`.relaxed` starts at PTX 8.6, and `.cluster` scope additionally requires the
cluster capability. Phase-type and report forms require PTX 9.3. Unqualified
forms remain distinct in the source model; their effective semantics default
to acquire at CTA scope. The frontend validates instruction-local contracts
but does not simulate suspension, the time limit, phase progress, or memory
visibility at runtime.
