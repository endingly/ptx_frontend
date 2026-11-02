# Matrix reference interpretations

This note records bounded frontend interpretations of conflicting clauses in the
pinned [PTX ISA 9.3 reference](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
These are project engineering interpretations, not NVIDIA-issued errata. Each
interpretation states its own scope; it does not establish whole-matrix coverage
or hardware execution conformance. [中文](../../zh-han/converage/matrix_reference_interpretations.md)

## WMMA `m8n8k4` F64 fragments

For `wmma.load`, `wmma.mma` and `wmma.store` on this shape, each thread has one
64-bit register for A and B, and two 64-bit registers for C and D. The
[fragment table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-fragment)
lists a conflicting single-register F64 C/D entry. We adopt two: the
[shape definition](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)
makes C/D an 8×8 matrix with 64 F64 elements, requiring two elements per thread
across 32 threads. A is 8×4 and B is 4×8, requiring one element per thread.
The [store example](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-st)
and [MMA example](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-mma)
use two C/D registers and agree with this capacity argument. This resolves this
particular inconsistency; it is not a general precedence rule for examples.

```ptx
wmma.load.c.sync.aligned.m8n8k4.col.global.f64 {c0,c1}, [p], 8;
wmma.mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {d0,d1}, {a}, {b}, {c0,c1};
wmma.store.d.sync.aligned.m8n8k4.row.global.f64 [p], {d0,d1}, 8;
```

C/D fragments occupy 16 bytes and require 16-byte memory alignment. Leading
dimensions remain layout-dependent element strides, independently checked for
minimum and divisibility. Both row/column layouts, existing memory spaces,
rounding default `.rn` and explicit `.rn/.rz/.rm/.rp` remain available. The base
floor is PTX 7.0 / `sm_80`; `shared::cta` retains its separate PTX 7.8 floor.
Ordinary scalar F64 and compatible B64 data carriers retain their existing rules.
Physical element distribution across lanes/registers remains architecture-dependent.

The canonical rules already express this interpretation. Focused regressions in
[`test_wmma_coverage.cpp`](../../../submod/resolved_ir/test/test_wmma_coverage.cpp)
and [`test_wmma_coverage.py`](../../../python/tests/spec/test_wmma_coverage.py)
check the exact 36 static identities separately from source programs, reject
A/B pairs and C/D singletons/triples, and check independently mutated owned
fragments after syntax owners die. Parser acceptance of a brace pack alone does
not imply semantic acceptance. Owned copies/clones, supplied-source validation,
alignment, scalar width, target and stride controls remain separate checks.

## Standard F64 MMA shape spelling

The accepted dense F64 shape is `.m8n8k4`. The `.m8n84` spelling in the F64
[Syntax subsection](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-mma)
is treated as an editorial typo and is not a compatibility alias. The
[shape table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape),
dedicated [F64 fragment subsection](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-fragment-mma-884-f64),
ISA notes and examples consistently identify the full M/N/K shape `m8n8k4`.
The project retains this well-formed spelling without source rewriting or a lexer
special case. This is a bounded engineering interpretation, not a vendor erratum.

```ptx
mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {d0,d1}, {a}, {b}, {c0,c1};
```

This form has `MatrixShape{8,8,4}`, row/column A/B layouts, F64 scalar types,
and D/A/B/C register counts 2/1/1/2. Its minimum is PTX 7.0 / `sm_80`, with
default `.rn` and explicit `.rn/.rz/.rm/.rp` rounding. The separate F64
`m16n8k4`, `m16n8k8` and `m16n8k16` forms retain PTX 7.8 / `sm_90` floors.
They are adjacent controls, not aliases for this shape.

The canonical rules already implement this interpretation. Focused tests in
[`test_warp_matrix_mma_coverage.cpp`](../../../submod/resolved_ir/test/test_warp_matrix_mma_coverage.cpp)
and [`test_warp_matrix_mma_coverage.py`](../../../python/tests/spec/test_warp_matrix_mma_coverage.py)
join the exact raw/normalized/IR identity and distinguish generic CST/AST
acceptance from semantic resolution: `.m8n84`, `.m8n8`, `.m8n8k` and `.m8n8k5`
are parsed but rejected as unknown modifiers during resolution. No owned typo
variant exists. Its absence is checked through source selection; generated
shape/suffix metadata is immutable. Separate tests mutate actual owned fragments,
layout tags and coherently bound F32 references, retaining diagnostic ranges,
source association, AST-free copies/clones and independent target gates.
These checks do not authenticate live register contents or physical fragment placement.

## Ordered sparse low-bit kind separator

The accepted low-bit suffix is `.kind::f8f6f4`, including its leading dot, in the
`mma.sp::ordered_metadata.sync.aligned.m16n8k64` family. The missing dot in the
kind-domain definition `{kind::f8f6f4}` in the [sparse MMA subsection](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-sparse-mma)
is interpreted as damaged metalanguage, not an alternate source spelling.
The suffix separator and the subsection's fully written examples agree on
`.kind::f8f6f4`; the [shape table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)
identifies the ordered low-bit shape/type family. This is a project engineering
interpretation, not a vendor-issued erratum or a new modifier-order restriction.

```ptx
mma.sp::ordered_metadata.sync.aligned.m16n8k64.row.col.kind::f8f6f4.f32.e3m2.e2m3.f32
  {d0,d1,d2,d3}, {a0,a1,a2,a3}, {b0,b1,b2,b3}, {c0,c1,c2,c3}, meta, 0;
```

The existing catalogue contains 50 identities: each A/B type independently ranges
over `e4m3`, `e5m2`, `e3m2`, `e2m3`, `e2m1`, with equal F16/F16 or F32/F32
D/C types. A/B each use four B32 containers. F16 D/C each use two F16x2
registers; F32 D/C each use four F32 registers. Ordered metadata uses a B32
register and selector zero. Existing availability remains PTX 8.7 / `sm_120a`
or PTX 8.8 / the qualified `sm_120f` family; a generic `sm_120` profile does
not provide that qualification.

The canonical rules already express this interpretation. Focused tests in
[`test_warp_matrix_mma_coverage.cpp`](../../../submod/resolved_ir/test/test_warp_matrix_mma_coverage.cpp)
and [`test_warp_matrix_mma_coverage.py`](../../../python/tests/spec/test_warp_matrix_mma_coverage.py)
check the 50 static identities separately from five representative source tuples.
Missing separators and unknown kinds reach resolution and reject as unknown
modifiers. Duplicate dots produce parser diagnostics even when recovery returns
CST/AST values. Native `.sp`, unequal D/C types and a block-scaled kind without
its separate syntax reject variant selection. Selector values 1–3 fail the range
check independently in source and coherently mutated owned state. No accepted
alias or mutable generated kind/order field is introduced.

Owned fragment counts, physical scalar widths, metadata binding identity, source
association, copies/clones after AST destruction, and qualified targets remain
separate controls. Checking a container's type or a metadata register's identity
does not authenticate live low-bit packing or ordered metadata contents. Those
runtime obligations remain outside this spelling interpretation.

## Dense integer WGMMA N boundary

Dense integer WGMMA includes `m64n240k32` and `m64n256k32`. The integer Syntax
list in the [instruction subsection](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-instructions-wgmma-mma)
ends at N=224, while the [shape table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-shape)
explicitly includes N=240/256 and the [integer fragment contract](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-register-fragment-wgmma-64n32)
consistently requires N/2 S32 D registers. We adopt the dedicated shape and
fragment contract over this incomplete Syntax enumeration. This is a bounded
engineering interpretation, not a vendor-issued erratum or a blanket table
precedence rule. See also [WGMMA coverage](wgmma_coverage.md).

The integer N grid is `8,16,24,32,48,64,...,256`, with step 16 after 32.
N=240 uses 120 scalar S32 D registers and N=256 uses 128. Integer N=232 and
248 are off-grid despite being multiples of eight; N=264 is outside the range.
Dense integer K remains 32. Floating and sparse families retain separate grids
and selector domains.

The two boundary sizes retain all four S8/U8 A/B type pairs, both A placements
(shared descriptor or four B32 register lanes), and plain/satfinite forms:
32 distinct canonical identities. B is a B64 shared descriptor in both placements.
D is read/write. The 128-lane accumulator is an explicit scalar brace pack;
it grants no ordinary named-vector permission. Same-sign forms retain PTX 8.0,
mixed-sign forms PTX 8.4, and all require exact `sm_90a`. Generic `sm_90` or
higher numeric SM profiles do not inherit that target qualification.

The canonical rules already include these identities. Focused C++ regressions in
[`test_wgmma_coverage.cpp`](../../../submod/resolved_ir/test/test_wgmma_coverage.cpp)
run all 32 source templates, inspect exact classes and static topology, and
separately mutate real owned fragment counts, layout tags and descriptor carriers
after parser/AST destruction. Source cardinalities 119/121 and 127/129 reject;
the independent current 129-lane check remains within guarded view access.
Copies/clones, supplied-source association and matched target/version controls
remain separate evidence. The standard normalization harness independently checks
the exact 32-key projection. Fence/commit/wait identities remain unchanged;
these checks do not authenticate descriptor contents, physical fragment packing,
dynamic protocol completion or GPU results.

## K96 encoded scale IDs and physical layout facts

For dense MX4 and MX NV K96, the adopted Table 47 instruction-word domain for
scale A/B IDs is **{0,2}**, rejecting **1/3**. A occupies bits 29–30 and B bits
4–5, each a two-bit dimensionless selection code; bit 31 is the dense K choice.
Deep K96 figures label `SFA_ID`/`SFB_ID` positions 0/1/2/3. Their conflicting
1/3 labels do not widen the serialized field domain. The project gives the
instruction-descriptor table precedence for encodings and uses the deep clauses
for physical layout facts. This engineering interpretation retains the reference
discrepancy; it is not a NVIDIA-issued erratum. See [MX4](tcgen_mma_mx4_coverage.md),
[MX NV](tcgen_mma_mxnv_coverage.md) and the [family summary](tcgen_mma_family_coverage.md).

Three quantities remain distinct:

- **Encoded ID:** a dimensionless word-field code, independently checked as 0/2.
- **Illustrated physical layout index:** a figure's placement label, without an
  established numeric conversion from the encoded ID.
- **Alignment guarantee:** a caller claim in bytes about the selected sub-column.

K96 block32 has three factors with A `Mx3` and B `3xN`. NV block16 has six
factors with A `Mx6` and B `6xN`. Both require a four-byte sub-column alignment
guarantee and retain `LAYOUT_DEFINED_PLACEMENT`. Stronger positive power-of-two
guarantees, such as 8 or 16 bytes, are allowed. Encoded ID 2 with alignment 4 or
8 is valid: do **not** compute `ID % alignment`, multiply the ID into a byte
offset, equate encoded and physical indices, or normalize forbidden 1/3 into 0/2.
No new encoded-to-physical mapper or allocation rule is introduced.

Supplied alignment 0, 2 or 3 is a known invalid guarantee. Omitted role, layout
identity or alignment remains an explicit missing obligation, not an implicit
zero or complete validity. Reports may accept every supplied fact while retaining
missing layout/live-content obligations. Correct bits and caller layout facts do
not authenticate opaque source `idesc` contents, actual packing, scale contents,
allocation or completion. Word/fact value copies and owned-source copies remain independent.

Known dense K96 retains group 2, M256 and exact `sm_103a` from PTX 8.8, with
existing scale-type restrictions. Known K96 plus written `.scale_vec::2X` or
`.scale_vec::4X` retains the existing empty target intersection with those vector
forms' exact `sm_100a` requirement and unresolved physical layout-rule obligations.
Table 59 factor counts do not create missing physical rows. Source vector forms
can remain legal on `sm_100a` while K is unknown. This interpretation adds no
selector spelling, target permission, word authentication or optional Tensor solver.

Existing code already expresses this decision. Focused C++/Python regressions
check both field masks, all three block rows, ID/alignment combinations, wrong
layouts, missing facts and source/known-word separation. Live contents and GPU
execution remain outside these checks.
