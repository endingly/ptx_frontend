# PTX ISA 9.3 matrix, tensor, and resource audit

**Result: bounded source audit; no new confirmed parser, operand-type, or target-availability
mismatch found in the checked families.** This is not a whole-frontend conformance verdict. Existing
manual ambiguities, conditional tensor-map facts, and runtime obligations remain open at their
documented boundaries.

## Reference and baseline

- Repository revision: `1b44ad8212aa254610b3afb447c9e774df03710d` on
  `audit/ptx93-manual-conformance`.

- Normative source: [NVIDIA archived PTX ISA 9.3, CUDA
  13.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).

- Retained raw response: `/tmp/ptx-isa-9.3-official.html`, 3,577,575 bytes, SHA-256
  `40026f79b65fee3f8c8b3e717c6f80c6385e8d97215318d6e4b7f6106f58d5a4`. The local source record gives
  retrieval time `2026-11-13T03:16:34.344384Z`, title `1. Introduction — PTX ISA 9.3 documentation`,
  and 715 named section anchors (716 extracted section elements including the
  unnamed wrapper; `/tmp/ptx93-manual/source.json`).

- Current support claims
  were read from [the maintained SDK contract](../sdk_support.md), which remains the project's sole
  support-claim authority. The contract was treated as a claim set to check, not as normative proof.

## Method and evidence limits

I derived the family inventory from the archived manual's chapter 9 anchors, including Syntax,
operand descriptions, type/shape tables, PTX ISA Notes, and Target ISA Notes. I traced each family
to the packaged YAML input, its relevant normalizer or resolved-model projection, current
support-contract statements, and focused Python/C++ test sources. The static variant counts below
describe repository data and assertions, not a manual-derived denominator or proof of every
Cartesian product. The audit did not run tests, build, `ptxas`, or GPU execution. No implementation
files, support claims, or test expectations were changed. I found no concrete mismatching source
candidate to send to the shared probe worker.

The finite manual clauses checked were:

- §9.7.15: family syntax, matrix shape/type/fragment tables, rounding and selector domains, movement
  shapes, address spaces, alignment, and PTX/target notes.

- §9.7.16: dense/sparse shapes, element
  types, register fragments and shared layouts, metadata, proxy controls, group/wait behavior, and
  target notes.

- §9.7.17: allocation and transfer shapes/counts, descriptor and layout tables, MMA
  kind/shape/scale/mask rules, group consistency, specialized synchronization, and target notes.

- §5.5 and §9.7.9.26.5–.6/.27 plus §9.7.14.17: tensor rank/mode/direction, coordinates, completion,
  reduction type matrix, optional operands, target restrictions, replacement field/type/ordinal
  rules, and 128-byte proxy copy.

- §5.3 and §9.7.11–.12: opaque resource declaration/use limits,
  texturing mode, geometry and query families, coordinate/result shapes, surface data/type/cache
  matrix, indirect-resource gates, and PTX/target notes.

I checked these clauses against family-level canonical rows and targeted cases; I did not compare
every row in every manual table to an independently generated accept/reject corpus.

Classifications below use **consistent in checked scope**, **reference ambiguity**, **intentional
outside-static**, **evidence gap**, and **pending**. “Consistent in checked scope” means source
inspection found the cited contract and test evidence aligned with the checked manual clauses; it is
not exhaustive acceptance evidence.

## Family ledger

### Warp-level matrix families

The official §9.7.15 inventory inspected was `mma.sync`, `mma.sp` (ordered metadata and block-scaled
forms included), `wmma.load`, `wmma.store`, `wmma.mma`, `ldmatrix`, `stmatrix`, and `movmatrix`.
Relevant manual anchors: [warp-level matrix
overview](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions),
[`mma`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-mma),
[`wmma`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma),
and the movement clauses nested under the §9.7.15.5 anchor.

The canonical source is [the matrix
YAML](../../python/src/ptx_frontend/spec/resources/ptx_spec/warp_level_matrix_multiply_accumulate.yaml),
normalized by [`matrix.py`](../../python/src/ptx_frontend/spec/normalize/matrix.py#L35) and
projected by [`resolved_ir.py`](../../python/src/ptx_frontend/ir/resolved_ir.py#L711).
[`test_warp_matrix_mma_coverage.py`](../../python/tests/spec/test_warp_matrix_mma_coverage.py#L31)
asserts 360 `mma` variants split 175 dense / 185 sparse.
[`test_wmma_coverage.py`](../../python/tests/spec/test_wmma_coverage.py#L43) asserts 708 `wmma`
variants split 424 load / 140 store / 144 MMA. Movement and owned-fragment coverage is in
[`test_warp_matrix_coverage.cpp`](../../submod/resolved_ir/test/test_warp_matrix_coverage.cpp#L15),
[`test_warp_matrix_mma_coverage.cpp`](../../submod/resolved_ir/test/test_warp_matrix_mma_coverage.cpp#L21),
and [`test_wmma_coverage.cpp`](../../submod/resolved_ir/test/test_wmma_coverage.cpp#L19).

**Assessment: consistent in checked scope.** Shape/type/fragment/availability contracts and focused
tests are present. Full independent manual-to-YAML cross-product enumeration was not regenerated;
known manual interpretations are listed below.

### WGMMA families

The §9.7.16 identity ledger includes `wgmma.mma_async`, `wgmma.mma_async.sp`, `wgmma.fence`,
`wgmma.commit_group`, and `wgmma.wait_group`. I inspected dense and sparse operation clauses and the
three async-proxy controls against [the WGMMA
YAML](../../python/src/ptx_frontend/spec/resources/ptx_spec/asynchronous_warpgroup_matrix_multiply_accumulate.yaml).
[`test_wgmma_contract.py`](../../python/tests/ir/test_wgmma_contract.py#L59) asserts 2,151
normalized variants;
[`test_wgmma_coverage.cpp`](../../submod/resolved_ir/test/test_wgmma_coverage.cpp#L22) covers owned
forms and protocol actions, with separate tests for the integer boundary and target controls. The
support summary is at [`sdk_support.md`](../sdk_support.md#L5075).

**Assessment: consistent in checked scope.** The dense-integer N=240/256 boundary remains a
documented reference ambiguity. Static checks were read but not executed.

### TCGEN Tensor Memory and synchronization families

The manual inventory includes `tcgen05.alloc`, `.dealloc`, `.relinquish_alloc_permit`; `.ld`, `.st`,
`.ld.red`, `.wait::ld`, `.wait::st`; `.cp`, `.shift`; `.fence::before_thread_sync`,
`.fence::after_thread_sync`, and `.commit` (§9.7.17.1, .7–.9, .11–.12). Canonical source and
generated typed forms are in [the tensor-memory
YAML](../../python/src/ptx_frontend/spec/resources/ptx_spec/tensor_memory_data_movement.yaml). I
inspected the source checks in
[`tcgen_allocation.py`](../../python/src/ptx_frontend/spec/normalize/tcgen_allocation.py#L21),
[`tcgen_load_store.py`](../../python/src/ptx_frontend/spec/normalize/tcgen_load_store.py#L25),
[`tcgen_copy_shift.py`](../../python/src/ptx_frontend/spec/normalize/tcgen_copy_shift.py#L23), and
[`tcgen_sync.py`](../../python/src/ptx_frontend/spec/normalize/tcgen_sync.py#L16).

The support detail and explicit protocol limits are in [`sdk_support.md`](../sdk_support.md#L4409),
[allocation](../sdk_support.md#L4624), [copy/shift](../sdk_support.md#L4691), and [register
transfers](../sdk_support.md#L4756). Focused C++ sources are `test_tcgen_allocation.cpp`,
`test_tcgen_transfer.cpp`, `test_tcgen_copy_shift.cpp`, and `test_tcgen_sync.cpp` under
[`submod/resolved_ir/test`](../../submod/resolved_ir/test).

**Assessment: consistent in checked scope.** Source roles, availability, and local shape/count rules
are checked. Allocation history, dynamic addresses, completion, and execution are intentionally
outside static proof.

### TCGEN MMA families

The four manual identities are `tcgen05.mma`, `.mma.sp`, `.mma.ws`, and `.mma.ws.sp`, each with the
manual's kind/layout/target restrictions in §9.7.17.2–.6 and §9.7.17.10. The current support ledger
records 22 `(family, kind)` identities and 68 operand layouts at
[`sdk_support.md`](../sdk_support.md#L4007). Canonical data is in the tensor-memory YAML;
[`tcgen_mma.py`](../../python/src/ptx_frontend/spec/normalize/tcgen_mma.py#L68) validates the typed
modifier and operand contracts. Focused source checks span
[`test_tcgen_mma.cpp`](../../submod/resolved_ir/test/test_tcgen_mma.cpp#L76),
[`test_tcgen_mma_sparse.cpp`](../../submod/resolved_ir/test/test_tcgen_mma_sparse.cpp#L58), and
kind-specific `test_tcgen_mma_*_contract.py` files. Required output masks and their cardinalities
are recorded at [`sdk_support.md`](../sdk_support.md#L4737).

**Assessment: consistent in checked scope.** Manual-table-driven shape, kind, selector, scale, and
target rules have focused contracts. Table 47's K96 code/diagram conflict remains an adopted
interpretation. Caller-known words do not authenticate live descriptors.

### Tensor-map, TMA, and caller-known facts

The inspected manual identities include tensor-map dimensions/modes (§5.5.1–.8),
`cp.async.bulk.tensor`, `cp.reduce.async.bulk.tensor`, `cp.async.bulk.prefetch.tensor`, bulk
completion (§9.7.9.26.5–.6), `tensormap.replace` (§9.7.9.27), and `tensormap.cp_fenceproxy`
(§9.7.14.17). The archived anchors are
[tensor-map](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensor-tensormap),
[tensor
copy](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-tensor),
[tensor
reduction](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-reduce-async-bulk-tensor),
[replace](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-tensormap-replace),
and [proxy
copy/fence](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-tensormap-cp-fenceproxy).

The source schema is [data movement
YAML](../../python/src/ptx_frontend/spec/resources/ptx_spec/data_movement_and_conversion.yaml).
Tensor-mode projection is in
[`resolved_ir.py`](../../python/src/ptx_frontend/ir/resolved_ir.py#L1028); the selected-use adapter
is generated from
[`known_fact_selected_adapter.cpp.j2`](../../python/src/ptx_frontend/code_gen/templates/tensor/known_fact_selected_adapter.cpp.j2#L29).
The [closeout ledger](../sdk_support.md#L4444) links manual requirements to contracts and tests.
Rank, direction, coordinate and owned-source cases are in
[`test_tensor_async_coverage.cpp`](../../submod/resolved_ir/test/test_tensor_async_coverage.cpp#L57);
reduction, im2col, no-offset, gather/scatter and target checks are in their corresponding
`test_tensor_*.cpp` sources; replacement and proxy checks are in `test_tensormap_replacement.cpp`.

**Assessment: consistent in checked scope.** The contract identifies 356 selected
canonical/cache-hint identities (180 loads, 18 stores, 128 reductions, 30 prefetches) and keeps
completion categories distinct. Known-value queries are conditional claims, not map decoders: the
22-rule catalog and five unresolved whole relations are documented at
[`sdk_support.md`](../sdk_support.md#L8531) and [`sdk_support.md`](../sdk_support.md#L8617), with
source tests in
[`test_tensor_map_known_facts.py`](../../python/tests/spec/test_tensor_map_known_facts.py#L170) and
`test_tensor_map_known_facts.cpp`. Rules 17–21 (`STORE_BOUNDS`, `IM2COL_SHAPE`, `IM2COL_INFO`,
`W_PROFILE`, `GATHER_SCATTER`) deliberately stay `Unresolved` after the listed local checks pass.
That is an intentional outside-static relation boundary, not five complete solvers or a confirmed
defect. The manual does not provide the full raw 128-byte descriptor layout.

### Texture and opaque resource families

The inspected identities are `tex`, `tld4`, `txq`, and `istypep` (§9.7.11), plus §5.3's opaque
`.texref`, `.samplerref`, and `.surfref` rules. The source catalog is [texture
YAML](../../python/src/ptx_frontend/spec/resources/ptx_spec/texture.yaml); contract normalization is
checked by [`test_texture_contract.py`](../../python/tests/spec/test_texture_contract.py#L25) and
owned resource/query behavior by
[`test_texture_owned.cpp`](../../submod/resolved_ir/test/test_texture_owned.cpp#L52). Modes,
identity, typed carriers, geometry and retained runtime limits are summarized in
[`sdk_support.md`](../sdk_support.md#L4880).

**Assessment: consistent in checked scope**, with reference ambiguities for bracket omission and
selected unified `tld4` geometries. Runtime object contents, configuration, live indirect resource
kind and sampling remain outside static proof.

### Surface and surface-query families

The §9.7.12 identities are `suld`, `sust`, `sured`, and `suq`; opaque resource identity follows
§5.3. Canonical source is [surface
YAML](../../python/src/ptx_frontend/spec/resources/ptx_spec/surface.yaml) and the typed domain is
enforced by [`surface.py`](../../python/src/ptx_frontend/spec/normalize/surface.py#L23).
[`test_surface_contract.py`](../../python/tests/spec/test_surface_contract.py#L26) checks the family
count and invalid mutations;
[`test_surface_owned.cpp`](../../submod/resolved_ir/test/test_surface_owned.cpp#L87) checks the
source matrix and owned/module behavior. The support summary is at
[`sdk_support.md`](../sdk_support.md#L3527).

**Assessment: consistent in checked scope.** The maintained inventory states 340 typed forms over
1,864 legal written combinations. The syntax/type tables govern documented example conflicts; no
implementation mismatch was identified.

## Reference ambiguities already carried by the support contract

These are not new defects. They are specific places where the fixed manual's sections disagree
internally or where a formal clause and the current assembler observations differ. The baseline
already records a bounded interpretation; a semantic change belongs to Authority Sol review.

1. **WMMA F64 fragment count:** §9.7.15.4 fragment-table text conflicts with matrix capacity and the
   `wmma.mma`/`wmma.store` examples for `m8n8k4`. The maintained interpretation uses one A/B F64
   register and two C/D registers; [the rationale and focused tests](../sdk_support.md#L2363) make
   this local precedence choice explicit.

2. **Dense F64 MMA spelling:** one line of the `mma`
   Syntax subsection says `.m8n84`; the shape table, F64 fragment subsection and examples identify
   `.m8n8k4`. The contract accepts only `.m8n8k4` and records the choice at [the F64 spelling
   interpretation](../sdk_support.md#L2403).

3. **Ordered sparse low-bit kind:** the sparse `mma.sp`
   allowed-kind metalanguage omits a dot in one domain list while its syntax/examples use
   `.kind::f8f6f4`. The baseline uses the fully written token and records the 50-identity scope at
   [the interpretation](../sdk_support.md#L2439).

4. **WGMMA integer N:** the dense integer
   instruction Syntax list stops at N=224, while the shape table and fragment contract include
   N=240/256. The existing implementation follows the dedicated shape/fragment rows; details and
   edge tests are at [the WGMMA boundary](../sdk_support.md#L2483).

5. **TCGEN K96 scale IDs:**
   Table 47's encoded field domain is `{0,2}`, while K96 layout diagrams label positions `0,1,2,3`.
   The contract keeps encoded IDs distinct from physical-layout indices and alignment claims at [the
   K96 interpretation](../sdk_support.md#L2523).

6. **Texture source details:** the formal texture
   Syntax lines, descriptive text/Notes and examples disagree on bracket omission, `a2d` offset
   padding, and some unified `tld4` geometries. The baseline calls out which prose/Notes
   interpretation it uses, with recorded assembler observations, at [texture
   coverage](../sdk_support.md#L4880). Surface examples also contain a formatted-reduction type and
   omitted-boundary conflict with the Syntax; the support contract follows the form table in
   [surface coverage](../sdk_support.md#L3527).

The tensor cache-control page records another formal-syntax/`ptxas` difference: PTX 9.3 makes
`.L2::cache_hint` optional and requires it when a policy is written, while CUDA 13.3.73 `ptxas`
rejected the hint-only probes. Exact probes are retained, and the support contract follows formal
Syntax at [`sdk_support.md`](../sdk_support.md#L8457). I did not rerun `ptxas`; that record is
supplied repository evidence only.

## Priority follow-up and stopping point

- **No confirmed mismatch to reproduce.** The most audit-sensitive places are the six existing
  manual interpretation points above and the separate conditional fact boundaries. They should be
  reviewed as semantic decisions, not recast as parser/type/target bugs from generated-model
  self-consistency.

- **Remaining evidence gap:** this bounded pass did not rederive every possible
  manual modifier/type/shape cross product into an independent conformance generator, nor execute
  the focused tests or compare a full binary against the manual. The inventory assertions and source
  tests are useful coverage evidence, not the normative denominator.

- **Runtime limits:** WGMMA
  warpgroup convergence and completion; TCGEN allocation, descriptor/register contents, collector
  history, and protocol completion; TMA descriptor bytes, full bounds/shape relations, barrier
  locality, synchronization and memory effects; and texture/surface object contents and sampling
  remain unproved by source acceptance. The checked support sections retain these limits.
