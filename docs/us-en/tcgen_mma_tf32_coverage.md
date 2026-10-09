# TCGEN dense tf32 MMA coverage

This page describes the dense, non-weight-stationary, non-convolution
`.kind::tf32` source form and its caller-known operational query. The fixed
[PTX 9.3 `tcgen05.mma` clause](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
and the accepted descriptor catalogue set the field and source rules. A finite
complete-module CUDA 13.3 assembler checkpoint corroborated source spelling;
assembly alone cannot prove GPU behavior or live descriptor register values.

The canonical spelling is `tcgen05.mma.cta_group::1/2.kind::tf32`.
Group 1 and 2 each allow shared-descriptor or Tensor Memory A, optional output
lane mask, and optional D scale. These are eight structural operand layouts and
two typed group values, or sixteen source topologies. The checkpoint assembled
all sixteen at `sm_100a`/PTX9.3. The exact adjacent written-order alias
`.mma.kind::tf32.cta_group::1/2` is also approved as the same typed selected
form. Group 1 alias assembly was observed; extending the same adjacent swap to
group 2 is an Authority grammar inference, not a separate assembler result.
Other permutations and duplicate qualifiers remain invalid.

Shared A/B descriptors use scalar General B64/U64/S64 **registers**, and the
instruction descriptor uses a scalar General B32/U32/S32 register. The
assembler accepted direct literal zero in each descriptor role, but the fixed
source names register operands; tool acceptance does not broaden the frontend
role. `enable-input-d` accepts a scalar predicate register with optional
written negation or an integer constant with zero/nonzero truth, including 2.
Special predicate registers remain excluded. The mask may be absent; when
present it has exactly four group-1 or eight group-2 scalar General 32-bit
register entries. B32/U32/S32/F32 declarations are compatible as **mask bits**;
F32 here does not request floating conversion. An explicit empty vector is
invalid. The optional D scale is an immediate whose **original** evaluated
integer is 0..15; a low-bit alias of a wider value is outside this range.
Located owned payloads must retain and recheck these roles after the parser's
syntax tree is released.

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
fixes D=F32 and A=B=TF32. Dense group 1 has M=64/128 and N=8..256 in steps
of 8; group 2 has M=128/256 and N=16..256 in steps of 16. K=8 is uniquely
determined and is not a separate source operand. The accepted Table 45
descriptor catalogue owns encoded D/A/B type fields. The operational query
invokes its defined-field validators first, then reports checked
known facts, proven violations and still-missing facts separately. Live source
registers stay opaque; caller-supplied descriptor words are independent claims
and do not authenticate those registers.

For caller-known shared words, [Table 57](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
requires the 128-byte swizzle with **32-byte atomicity** for a transposed
32-bit A or B matrix. A and B are checked independently; normal K-major
shared operands use the accepted non-transposed swizzle domain. Tensor Memory A
has no A-shared-word obligation. The non-WS F/D/B/A paths are selected by
group/M: (1,64), (1,128), (2,128), (2,256). On the F half path, independently
supplied A/D Tensor Memory lane-half identities can be compared when A is in
Tensor Memory. Address spelling alone cannot derive those identities or prove
allocation history.

The source/target checkpoint supports plain tf32 on exact `sm_100a` from
PTX8.6, the `sm_100f` family including `sm_103a/f` from PTX8.8, and the
`sm_110f` family including `sm_110a/f` from PTX9.0. Optional D scaling has
the narrower exact-100a/100f-family gate; scaled forms were rejected on
`sm_110a/f`. Generic `sm_100`, `sm_110` and `sm_120a/f` are excluded.
Below-floor assembler cases whose no-MMA `.target` controls failed are
directive-confounded and are not independent MMA-floor evidence.

This slice leaves other dense kinds, sparse and weight-stationary
spellings, block scaling, convolution and runtime
descriptor/memory validation for later work. Its selected tf32 view
borrows from the exact `Tcgen05MmaTf32` form and preserves the existing f16
view and generated identity. The module's CTA-group consistency check remains
limited to TCGEN instructions. TMA omitted/group-1/group-2 forms may coexist
in one body and must not inherit that TCGEN-only uniform-group rule.

The current ordinary non-WS dense and sparse forms also support [typed A collector and ashift controls](tcgen_mma_a_collector_coverage.md); their caller-known history checks remain conditional.
