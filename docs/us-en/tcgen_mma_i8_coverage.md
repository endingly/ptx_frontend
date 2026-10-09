# Dense i8 TCGEN MMA coverage

This page describes the dense, non-weight-stationary, non-convolution
`.kind::i8` frontend slice. Its packaged source, owned form, operational
catalogue and borrowed view remain independent of live descriptor contents.
The fixed
[CUDA 13.3 PTX 9.3 `tcgen05.mma` section](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
is the normative source; the accepted Table 43/45 descriptor catalogue remains
the sole encoding authority.

The printed source order is
`tcgen05.mma.cta_group::{1,2}.kind::i8 [d-tmem], a, b-desc, idesc,
{disable-output-lane}, enable-input-d`. `a` is either a shared-memory
descriptor register or a bracketed Tensor Memory address. The optional mask
has four registers for group 1 and eight for group 2. There are four
structural layouts and two typed group values, or eight source topologies.
The i8 grammar has **no** `scale-input-d` operand or `.satfinite` source
qualifier. A separate four-module CUDA 13.3 assembler checkpoint accepted
canonical and adjacent `.mma.kind::i8.cta_group` orders for **both** groups.
Authority approved exactly this adjacent alias as the same typed selected
form, retaining written group locations. Other permutations and duplicate
qualifiers remain invalid. Both orders select the same typed owned form.

Shared A/B descriptors are scalar General B64/U64/S64 **registers**. `idesc`
is a scalar General B32/U32/S32 register. Their live contents remain opaque.
`enable-input-d` is a scalar ordinary predicate register with optional written
negation, or an integer predicate constant whose zero/nonzero truth is
preserved. Special predicate registers are excluded. An optional lane mask
contains exactly four/eight scalar General 32-bit registers; B32/U32/S32/F32
declarations are compatible as **bits**, not floating arithmetic. Absence and
an explicit empty vector are distinct. The owned resolved form must retain
source ranges, declarations and register identity after the syntax tree dies.

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
gives S8/U8 multiplicands, S32 accumulator/output and uniquely determined
K32. Dense group 1 permits M64/128 and N8,16,24,32, then N48..256 in steps
of 16; group 2 permits M128/256 and N32..256 in steps of 32. K is not a
separate source argument. A and B signedness are encoded independently in
Table 45. The fixed TCGEN text does not explicitly settle mixed S8×U8 or
U8×S8 pairing; a caller-known query must report that pair rule as an
**unresolved obligation**, neither a rejection nor a completed validation.
Same-type pairs may complete the pair check. The WGMMA same-type rule does
not govern this TCGEN form.

Table 45 bit 3 is the i8 saturation field. Both 0 (no saturation) and 1
(saturation) are defined; saturation is a caller-known encoded property, not
a source modifier or evidence of GPU numeric results. D type S32 is code 2;
A/B U8 and S8 are codes 0 and 1. Negate-A/B bits are forbidden for i8, while
transpose bits are supported. A known-word query validates defined fields
before operational rules, and reports field failures, proven operation
violations and missing facts separately. Caller-supplied known words are
independent assertions and do not authenticate live descriptor registers.

For shared operands, [Table 57](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
excludes 128-byte/32-byte-atomic swizzling for transposed 8-bit A **and** B;
each role is checked independently. [Table 55](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
adds a separate transposed-B N restriction: group 1 N16..256 in steps of 16,
group 2 N32..256 in steps of 32. Thus group-1 dense N8 and N24 are valid
shapes yet invalid with transposed B; N16, N32 and N48 are allowed when other
facts pass. Tensor Memory A has no A-shared-word obligation. The non-WS
F/D/B/A paths are selected by (group,M) = (1,64), (1,128), (2,128), (2,256).
On the F half path, independently supplied A/D lane halves 0 or 16 can be
compared when A is in Tensor Memory. Full D/B/A paths require zero; a known D
half is checked even with shared A. Source addresses do not prove the claimed
halves or allocation history.

The current explicit target catalogue projects the i8 source gate to exact
`sm_100a` from PTX 8.6 or exact `sm_110a` from PTX 9.0. Family-f targets,
including `sm_103a` despite its unrelated family features, do not inherit
i8 MMA. The historical `sm_101a` spelling mentioned in the manual is absent
from the current catalogue and is **not** silently mapped to `sm_110a` for
pre-9.0 PTX. Table 43's exact-103a byte-stride descriptor capability does
not grant an i8 operation on 103a.

The other fixed dense, sparse and WS source forms appear in the
[family matrix](tcgen_mma_family_coverage.md). Runtime descriptor contents,
allocation, completion and GPU behavior remain obligations. TMA's
omitted/group-1/group-2 group choices do not inherit the TCGEN-only uniform
group rule.

The current ordinary non-WS dense and sparse forms also support [typed A collector and ashift controls](tcgen_mma_a_collector_coverage.md); their caller-known history checks remain conditional.
