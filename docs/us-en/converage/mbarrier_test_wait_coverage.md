# `mbarrier.test_wait` qualifier coverage

The frontend models the `.sem.scope` pair for each of the ten existing
`mbarrier.test_wait` shapes in [PTX ISA 9.3 §9.7.14.16.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-mbarrier-test-wait-mbarrier-try-wait).
Both qualifiers must be written, in order, before the shared-space qualifier and
`.b64`: `.acquire|.relaxed` followed by `.cta|.cluster`. A lone qualifier, the
reverse order, and `.shared::cluster` wait objects are rejected. The scope
qualifier names the synchronization scope; `.shared::cta` names the object's
address space. They are independent.

The ten paired forms retain the existing token and parity operands for generic,
`.shared`, and `.shared::cta` addresses. Explicit `.phase_type::primary` retains
its ordinary, report-predicate, and report-value layouts. Explicit
`.phase_type::conditional` requires `.parity` and has no report operands. The
frontend checks eight-byte address alignment and immediate phase parity in
`0..1`; it preserves the explicit qualifier values and their source locations
in owned resolved IR.

The basic explicit pair starts at PTX 8.0 and `sm_80`. `.relaxed` starts at PTX
8.6 and requires `sm_90`; `.cluster` requires `sm_90` and the cluster
capability. Phase-type and report forms require PTX 9.3 and `sm_90`.
Unqualified forms remain distinct: absence is retained in the source model,
while their effective semantics are acquire at CTA scope. The frontend checks
instruction-local syntax, operands, and target availability; it does not prove
mbarrier phase progress, participating threads, or runtime memory visibility.
`mbarrier.try_wait` qualifier forms are outside this coverage.
