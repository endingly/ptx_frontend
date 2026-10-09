# TCGEN dense f16 MMA coverage

This page describes the first source slice of the four
[`tcgen05.mma` spellings in fixed PTX 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma).
The initial slice is dense, non-weight-stationary kind f16 without block scaling, with both
CTA groups and both A placements. The optional lane mask and optional D scale
are source topology choices settled by the fixed manual, a matched complete-module
assembler corpus and an independent Authority checkpoint. The distinct
[dense tf32 form](tcgen_mma_tf32_coverage.md) and distinct
[dense i8 form](tcgen_mma_i8_coverage.md) and the canonical
[dense unscaled f8f6f4 form](tcgen_mma_f8f6f4_coverage.md) and
[dense MX8 block-scaled form](tcgen_mma_mx8_coverage.md) and
[dense MX4 block-scaled form](tcgen_mma_mx4_coverage.md) and
[dense MX NV four-bit form](tcgen_mma_mxnv_coverage.md), and
[ordinary sparse f16/tf32/f8f6f4/i8 forms](tcgen_mma_sparse_coverage.md),
and [sparse MX forms](tcgen_mma_sparse_mx_coverage.md)
are also covered. Their ordinary non-WS f16/tf32/f8f6f4/i8 forms include
[typed A collector and ashift controls](tcgen_mma_a_collector_coverage.md).
Weight-stationary, MX collectors, remaining block scaling
and convolution remain open.

Shared A/B descriptors use scalar General B64/U64/S64 **registers**; the
instruction descriptor uses General B32/U32/S32 registers. The assembler also
accepted literal zero in these positions, but the documented register role
remains the frontend rule. `enable-input-d` accepts a scalar predicate register
with or without written negation, or an integer predicate constant with
zero/nonzero truth, including 2. The accepted owned predicate representation
retains the truth value, location and written negation when applicable; it does
not retain the original integer magnitude. Special predicate registers are not
part of this first source contract.

The output-lane mask may be absent. A present mask has exactly four General
scalar 32-bit register entries for group 1 or eight for group 2; an explicit
empty vector is invalid. B32/U32/S32/F32 declarations are compatible as
32-bit **mask bits**. F32 here is not floating arithmetic or conversion.
Literal vector entries and wrong-width, vector or predicate register entries
are invalid. The optional D scale is an immediate with original evaluated
integer value 0..15, without low-bit aliases. The assembler accepted 2^32 as
an alias of zero, but the fixed source range remains stricter. Scaled forms
use the narrower exact100a/family100f target lineage. The single observed
`.mma.kind.cta_group` order is an alias of canonical `.mma.cta_group.kind`;
other modifier permutations are not admitted.

The operational catalogue uses [Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
for group 1 M64/128, N8..256 step8, K16 and group 2 M128/256,
N16..256 step16, K16. D=f16 requires A/B f16; D=f32 permits each A/B type in
the documented f16/bf16 domain. The combined table does not settle mixed
f16×bf16 pairing, which remains an explicit obligation instead of an invented
rejection or an assertion of full legality. K is uniquely determined and is
not independently specified in the instruction descriptor.

For supplied shared-descriptor facts, [Table 57](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
is checked per A and B role: normal K-major 16-bit matrices use the accepted
swizzle domain; transposed MN-major matrices exclude the 128-byte swizzle with
32-byte atomicity. The existing descriptor catalogue alone owns Table 43 and
Tables 45–48 encoded fields. Its defined-field validation must precede a
generated operational query, which separately reports completed
checks, operational violations and missing facts, including invalid enum
safety. A live descriptor register is opaque; caller-known
word values do not authenticate it or prove shared-memory contents.

The F/D/B/A path selection comes from **§9.7.17.10.5**, separate from Table 58
shared-swizzle atoms. The F half path can compare independently supplied A/D
Tensor Memory lane-half facts when A is in Tensor Memory. Missing address or
layout facts remain obligations; source syntax and assembly cannot prove
allocation history, completion or actual lane agreement. The accepted typed
TCGEN group rule is uniform only within each function's TCGEN body. TMA's
omitted/1/2 group mixtures have a different signal-routing contract and do
not join this check.

The generated exact `Tcgen05MmaF16` class owns its direct typed operands, with
eight structural layouts for the sixteen group, A-placement, mask-presence and
scale-presence cases. `tcgen_mma_f16_view` borrows selected roles from a
`const Instruction&` and returns absence for other forms or a mismatched layout
tag. The checker rechecks owned
register, predicate, mask, immediate and address metadata after AST release.
A separate complete-module assembler checkpoint informed source spelling; it
does not prove runtime descriptor contents or GPU execution.
