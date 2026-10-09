# Dense unscaled f8f6f4 TCGEN MMA coverage

This page specifies the dense, non-WS, non-sparse,
non-convolution `.kind::f8f6f4` frontend slice. The fixed
[CUDA 13.3 PTX 9.3 `tcgen05.mma` specification](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
is the source. The accepted Table 43/45 descriptor catalogue owns field
encodings; the known-operation query never reads live descriptor registers.

The canonical order is
`tcgen05.mma.cta_group::{1,2}.kind::f8f6f4 [d-tmem], a, b-desc,
idesc, {disable-output-lane}, enable-input-d`. A is a shared descriptor
register or bracketed Tensor Memory address. The optional mask contains
four or eight 32-bit bit-registers for group 1 or 2. Four A-placement/mask
layouts and two located group values yield eight typed source topologies.
There is no source `scale-input-d`, `.satfinite`, block-scale operand or
collector in this slice. The frontend accepts the printed canonical modifier
order; the adjacent `.mma.kind::f8f6f4.cta_group` spelling is excluded.

A/B shared descriptors are scalar General B64/U64/S64 registers and `idesc`
is a scalar General B32/U32/S32 register. Literal zero is not a source
descriptor role. The predicate retains ordinary register identity and
written negation, or integer constant zero/nonzero truth. Mask entries
also admit F32 declarations as bit carriers. Missing, explicitly empty,
wrong-sized and wrong-typed masks are distinct. Owned source ranges,
declarations and binding must remain checkable after the syntax tree dies.

Table 42 gives dense K32, D F16/F32 and any of E4M3, E5M2, E2M3, E3M2,
E2M1 independently for A and B. All 25 defined input pairs are admitted by
the pair-domain rule. Group 1 permits M64/128 and N8..256 in steps of 8;
group 2 permits M128/256 and N16..256 in steps of 16. Four closed rows cover
the two groups and two output types. K is implicit, never an extra source
argument. F/D/B/A datapath paths follow group/M =
(1,64)/(1,128)/(2,128)/(2,256). On the F half path, only caller-supplied
A/D 0/16 lane-half facts can establish their alignment.

Table 45 defines D codes 0 F16 and 1 F32, independent A/B codes 0 E4M3,
1 E5M2, 3 E2M3, 4 E3M2 and 5 E2M1. Saturation bit 3 must be zero;
transpose and negate bits are supported. Sparse is a defined field but a
separate violation for this dense operation. Defined-field failures,
operation violations and Missing facts are reported separately. A supplied
known word is an independent assertion, not an authentication of an opaque
source register.

For each shared role, Table 43 defined fields and context remain separately
checked. Normal A/B major and swizzle follow the accepted rule. E4M3/E5M2
are eight-bit; E2M3/E3M2 are six-bit; E2M1 is four-bit. Table 54 permits
transpose, but Table 57 has no 4/6-bit transposed row. A transposed low-bit
shared A or B therefore requires the corresponding
`ATransposeLayoutRule` / `BTransposeLayoutRule` **Missing**. The universal
128-byte/32-byte-atomic swizzle exclusion remains a hard A/B violation.
Eight-bit transposed shared roles follow the established role-specific
major/swizzle rule. Table 55's B-transpose N restriction applies **only
when B is eight-bit**: group 1 N16..256 step16, group 2 N32..256 step32.
It does not depend on A width. Tensor-Memory A has no shared-A-word/Table 57
obligation; its placement and path facts still apply.

Each caller-known four- or six-bit A/B role also carries an independent
`ALowBitPackingRule` / `BLowBitPackingRule` **Missing**, even when A lives in
Tensor Memory or the role is not transposed. The manual's low-bit packing
diagrams specifically name **mxf8f6f4**, so this unscaled slice does not
copy their padding or format rules. D F16 uses the lower 16 bits of a 32-bit
Tensor Memory word, but the known query cannot inspect actual matrix bytes,
allocation, completion or numerical GPU behavior. A report with no supplied
fact violations can still have unresolved obligations.

The current target catalogue gives four ordinary unscaled branches:
exact `sm_100a` from PTX 8.6, enabled `sm_100f` family from 8.8, exact
`sm_110a` from 9.0, and enabled `sm_110f` family from 9.0. Target context
is an explicit profile identity and feature intersection, not a numeric SM
comparison. The manual's historical `sm_101a`/`sm_101f` spellings lack
entries in the current catalogue; they are not silently mapped to `sm_110`.
The i8 exact-only exclusion and f16/tf32 D-scale gates do not apply here.

This is a bounded source and known-facts representation. Scaled MX kinds,
sparse and WS forms, collector, ashift, convolution, physical low-bit
packing proof, live descriptor bits, Tensor Memory lifetime and GPU execution
remain outside it. TMA and MMA can coexist in one module, but this slice does not prove
execution or cross-instruction completion.
