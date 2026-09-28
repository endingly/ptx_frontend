# Ordinary `fence` coverage

The frontend models the thread-fence syntax in
[PTX ISA 9.3 §9.7.14.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-membar-fence):
`fence{.sem}.scope;`. A scope is required. The supported scopes are `.cta`,
`.gpu`, `.sys`, and `.cluster`; the supported explicit semantics are `.sc`,
`.acq_rel`, `.acquire`, and `.release`. The frontend also accepts
`fence.scope.sem;`, as used in the ISA's cluster example and accepted by CUDA
13.3 `ptxas`. Both modifier orders resolve to the same typed variant and values.

| Form | Minimum PTX | Minimum target | Extra capability |
| --- | --- | --- | --- |
| Omitted semantics, `.sc`, or `.acq_rel` with CTA/GPU/SYS scope | 6.0 | `sm_70` | None |
| Omitted semantics, `.sc`, or `.acq_rel` with cluster scope | 7.8 | `sm_90` | `cluster` |
| `.acquire` or `.release` with CTA/GPU/SYS scope | 8.6 | `sm_90` | None |
| `.acquire` or `.release` with cluster scope | 8.6 | `sm_90` | `cluster` |

When `.sem` is omitted, owned IR retains `MemoryConsistency::Omitted` with no
semantic-qualifier source location; its effective ISA behavior is `.acq_rel`.
Explicit `.sc` uses `MemoryConsistency::Sc`. Explicit `fence.acq_rel.cta` and
`fence.cta.acq_rel` retain the existing `Fence::AcqRelCta` variant. Other
ordinary forms use disjoint CTA, GPU/SYS, and cluster variants. No form has
operands. The checker enforces the target and capability limits on owned IR;
the frontend does not execute fences.

The separate, operand-free `fence.mbarrier_init.release.cluster;` variant
requires PTX 8.0, `sm_90`, and the `cluster` capability. It restricts the
fence's release effect to prior `mbarrier.init` operations on objects in
`.shared::cta` state space, as specified by the ISA. Its `.mbarrier_init`,
`.release`, and `.cluster` controls are fixed typed fields in owned IR.
Omitting or reordering them, changing the semantics or scope, or supplying an
operand is rejected. The ordinary `fence.release.cluster;` form remains a
different variant with a PTX 8.6 floor.

Two further operand-free, non-proxy forms require PTX 8.6, `sm_90`, and the
`cluster` capability: `fence.acquire.sync_restrict::shared::cluster.cluster;`
and `fence.release.sync_restrict::shared::cta.cluster;`. The acquire form
restricts ordering to operations on objects in `.shared::cluster`; the release
form restricts it to `.shared::cta`. Each has fixed typed semantics, restriction,
and cluster scope in owned IR. Swapping the semantics or restricted state space,
changing the scope, reordering qualifiers, or adding an operand is rejected.
These variants are distinct from the existing
`fence.proxy.async::generic.*.sync_restrict` forms.
