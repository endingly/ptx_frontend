# ST Coverage

This document records the PTX 9.3 `st` forms accepted by the frontend. The
machine-readable authority is
`python/code_gen/resources/ptx_spec/data_movement_and_conversion.yaml`; this
is a frontend contract, not simulator execution or GPU conformance.

The normative source is NVIDIA's PTX ISA 9.3 [store instruction](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st).

## Contract

`st` accepts generic and explicit `.global`, `.local`, `.param`, and `.shared`
addressing, including `.param::func` return-address direction/context checks.
Scalar types include the integer, bit-size, and floating forms through `.f64`,
plus `.b128` at PTX 8.3 / SM 70. The `.sys` scope with `.b128` requires PTX
8.4. Sources retain PTX's equal-or-wider register rule: the checker validates
the selected instruction width; PTX store execution uses the source's low bits.
The checker also retains known
address alignment and write-permission validation.

The qualified shared forms `.shared::cta` and `.shared::cluster` retain their
state-space identity. They require PTX 7.8; `::cta` requires SM 30 and
`::cluster` requires SM 90 with cluster capability. An unqualified `.shared`
form retains PTX's `::cta` default.

Weak (including omission), volatile, relaxed, and release are distinct semantic
alternatives. Relaxed/release require a scope and permit known global or shared
addresses; volatile additionally permits local addresses at PTX 9.1. MMIO
requires a global address and `.sys` scope. The descriptor records the legal
MMIO semantic values and their target availability, including the PTX 9.3 /
SM 75 release+MMIO form, so the common checker never switches on `st`.
Cache operators and cache-control hints have distinct semantic restrictions.

Global cache forms cover cache operators, L1 eviction priorities, L2 eviction
priorities (alone and combined with L1), and `L2::cache_hint` with its required
`b64` cache-policy operand. L1 eviction requires PTX 7.4 / SM 70, cache policy
requires PTX 7.4 / SM 80, and L2 eviction requires PTX 8.8 / SM 100 with a
256-bit `.v8` 32-bit or `.v4` 64-bit vector. Other vector forms retain legacy
128-bit layouts and target-qualified modern 256-bit layouts; only modern
layouts admit partial-store sinks.
Those controls are also accepted from a generic spelling only for unknown or
known-global provenance; a known local, shared, constant, or parameter address
is rejected. L1/L2 eviction and `L2::cache_hint` admit weak, relaxed, and
release stores. A `.wb`/`.cg`/`.cs`/`.wt` cache operator is instead weak-only: it may
combine with cache hint, but not with eviction controls or ordered/volatile/MMIO
semantics. Combined eviction qualifiers use canonical L1-before-L2 ordering.

PTX 9.3 does **not** define an `[address].unified` syntax for ordinary `st`.
The frontend therefore rejects that spelling rather than inventing a store-side
qualifier from the declaration attribute. A known unified declaration is also
rejected as a read-only store address, including AST-free revalidation. It
neither allocates unified memory nor weakens normal address-space or alignment
diagnostics.

## Deliberate boundary

CUDA 13.1 `ptxas` probes at PTX 8.0 / SM 90 reject a cache hint without its policy
operand. The frontend retains that correlated operand requirement; this is
compiler evidence, not PTX 9.3 GPU validation.

This coverage performs source acceptance and target-aware validation only. It
does not implement memory ordering, cache behavior, unified allocation, or
store execution. The dedicated C++ ST test, Python model test, and installed
consumer exercise the accepted/rejected public frontend contracts.
