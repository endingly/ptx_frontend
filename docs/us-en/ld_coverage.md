# LD Coverage

This document records the PTX 9.3 `ld` and `ld.global.nc` forms accepted by
the frontend. The machine-readable authority is
`python/code_gen/resources/ptx_spec/data_movement_and_conversion.yaml`; this
document describes its public boundary, not simulator execution or GPU
conformance.

The normative sources are NVIDIA's PTX ISA 9.3 [load instruction](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld)
and [non-coherent global load](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld-global-nc).

## Contract

`ld` accepts generic and explicit `.const`, `.global`, `.local`, `.param`, and
`.shared` addressing, including the documented `.param::entry` and
`.param::func` direction/context rules. Scalar types include the integer,
bit-size, and floating forms through `.f64`, plus `.b128` at PTX 8.3 / SM 70.
The `.sys` scope with `.b128` requires PTX 8.4. Destinations retain PTX's
equal-or-wider register rule; alignment is checked whenever the address is
statically known.

The qualified shared forms `.shared::cta` and `.shared::cluster` retain their
state-space identity. They require PTX 7.8; `::cta` requires SM 30 and
`::cluster` requires SM 90 with cluster capability. Omitting the shared
sub-qualifier keeps PTX's `::cta` default without fabricating a cluster
address.

Weak (including omission), volatile, relaxed, and acquire forms are distinct
semantic alternatives. Relaxed/acquire require a scope and permit known global
or shared addresses; volatile additionally permits local addresses at PTX 9.1.
MMIO requires a global address and `.sys` scope; its legal semantic alternatives
and target minima are represented by the generated memory-consistency
descriptor rather than an opcode-specific checker branch. Cache operators and
cache-control hints have distinct semantic restrictions, described below.

Global cache forms cover the documented cache operators, L1 eviction priorities,
L2 eviction priorities (alone and combined with L1), L2 prefetch sizes, and
`L2::cache_hint` with its required `b64` cache-policy operand. L1 eviction
requires PTX 7.4 / SM 70; L2 prefetch requires PTX 7.4 / SM 75 (256B requires
SM 80); cache policy requires PTX 7.4 / SM 80; and L2 eviction priority requires
PTX 8.8 / SM 100 with a 256-bit `.v8` 32-bit or `.v4` 64-bit vector. Other
vector forms retain legacy 128-bit layouts and the target-qualified modern
256-bit layouts; only modern layouts admit partial-load sinks.
The same controls are available for a generic spelling when declaration
provenance is unknown or known-global; known local, shared, constant, or
parameter addresses are rejected. Eviction, cache-hint, and prefetch controls
admit weak, relaxed, and acquire load semantics. Prefetch without eviction or
cache hint additionally admits volatile. A `.ca`/`.cg`/`.cs`/`.lu`/`.cv` cache
operator is a separate weak-only branch:
it may combine with cache hint and prefetch where PTX permits, but it may not
combine with eviction controls or ordered/volatile/MMIO semantics.

`ld.global.nc` is explicit-global only and covers scalar/vector cache operators,
L1 eviction, L2 prefetch, `L2::cache_hint`, and L1+L2 eviction combinations.
Its base scalar/vector forms require PTX 3.1 / SM 32. L2 eviction is
modern-vector-only; the other families retain their scalar/vector forms and
`.b128` gates. It does not silently fall back to coherent `ld.global`.
Its cache-operator branch is ordered before `.nc` and is intentionally
disjoint from its L1/L2 eviction branch. Every cache-hint form, including one
combined with prefetch, requires a cache-policy operand; prefetch alone does not.
Ordinary LD accepts canonical L1-before-L2 ordering and the L2-before-L1 alias
shown in the PTX examples. NC accepts only L1-before-L2 ordering.

An address referencing a declaration with PTX `.attribute(.unified(...))` must
spell `[address].unified` for an admitted ordinary `ld` form. The suffix itself
requires PTX 8.0 / SM 90 and a global or generic address. The qualifier is
typed through CST, Syntax AST, Resolved IR, and checker operand views; validation
uses owned declaration identity after source and AST release. Conversely,
`.unified` is rejected for non-unified known declarations and for LD forms whose
syntax does not admit it.

## Deliberate boundary

CUDA 13.1 `ptxas` probes at PTX 8.0 / SM 90 reject LD and NC cache hints without
the policy operand. The frontend retains that correlated operand requirement;
these probes are compiler evidence, not PTX 9.3 GPU validation.

This is source acceptance and target-aware validation only. It does not allocate
unified memory, implement ordering, predict cache behavior, or simulate load
execution. The dedicated C++ LD test, Python model test, and installed-package
consumer cover accepted and rejected forms through public frontend contracts.
