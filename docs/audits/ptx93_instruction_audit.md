# PTX 9.3 instruction-family evidence audit

Audit-only record for baseline `1b44ad8212aa254610b3afb447c9e774df03710d`
on `audit/ptx93-manual-conformance`. The normative source is NVIDIA's archived
CUDA 13.3 PTX ISA 9.3 manual, retained as
`/tmp/ptx-isa-9.3-official.html` (SHA-256
`40026f79b65fee3f8c8b3e717c6f80c6385e8d97215318d6e4b7f6106f58d5a4`). Manual
section text came from `/tmp/ptx93-manual/sections/`; its index came from
`/tmp/ptx93-manual/index.tsv`.

## Scope and method

This worker reviewed the 15 ordinary instruction-family headings in §9.7.1–.10,
§9.7.13–.14, and §9.7.18–.20. The review excludes texture, surface, warp and
warpgroup matrix, TCGEN, and tensor-specific operations. Tensor-map operations
and §9.7.9.26.5 tensor-copy forms remain with the tensor review. Control-flow
brace and predication syntax (§9.7.13.1–.2) remains with the grammar review.

The evidence path is official syntax/description/notes → canonical instruction
YAML → normalizer and generated checker inputs → relevant tests. The central
normalization points inspected were
`python/src/ptx_frontend/spec/normalize/instruction.py`, `operands.py`,
`modifiers.py`, `constraints.py`, and `availability.py`. Target profile evidence
is in `submod/base/src/ptx_target.cpp`; instruction availability is carried by
the canonical variants and checked through the generated Resolved IR path.

The ledger below maps all 15 assigned family roots to canonical models and
focused test locations. This is an inventory and evidence map, not a semantic
pass over every form or cross-product. The manual has many nested instruction
and behavior sections. Source/test inspection is distinguished from executed
evidence. No tests, build, assembler, frontend probe, fuzz run, or GPU execution
was run by this worker. The coordinator owns probes; its executed frontend
probe results are recorded separately below. A YAML root, schema check,
positive example, or family test does not by itself establish complete manual
conformance.

## Family ledger

YAML basenames in this table are under
`python/src/ptx_frontend/spec/resources/ptx_spec/`.

| Official root | Canonical model | Focused evidence inspected |
| --- | --- | --- |
| §9.7.1 Integer arithmetic | `arithmetic.yaml` | `submod/resolved_ir/test/test_integer_arithmetic_completeness.cpp`; `python/tests/spec/test_ptx_spec_taxonomy.py`; BFE resolver test below. Checked the newly added `clmad` availability and the documented range controls for `fns`, `bfe`, `bfi`, `szext`, and `bmsk`. |
| §9.7.2 Extended-precision integer arithmetic | `arithmetic.yaml` | `submod/resolved_ir/test/test_carry_completeness.cpp`, `test_carry_arithmetic.cpp`; support record `docs/sdk_support.md` §“Extended-precision integer coverage”. |
| §9.7.3 Floating point | `arithmetic.yaml` | `test_div_completeness.cpp`, `test_unary_float_completeness.cpp`, `test_transcendental_completeness.cpp`, `test_min_max_completeness.cpp`, `test_abs_neg_completeness.cpp`, `test_mad_completeness.cpp`; matching Python spec tests. |
| §9.7.4 Half precision floating point | `arithmetic.yaml` | `test_half_fma_modifiers.cpp`, `test_half_set_ftz.cpp`, `test_scalar_half_immediates.cpp`, `test_min_max_completeness.cpp`; corresponding Python spec tests. |
| §9.7.5 Mixed precision floating point | `arithmetic.yaml` | `test_add_sub_audit.cpp`, `test_remaining_bf16_fma_set_sources.cpp`; corresponding Python spec tests and `docs/sdk_support.md` §“Floating and mixed add/sub coverage”. |
| §9.7.6 Comparison and selection | `comparison_and_selection.yaml` | `test_set_completeness.cpp`, `test_setp_completeness.cpp`, `test_selp_completeness.cpp`, `test_slct_completeness.cpp`, and explicit packed `setp` source tests. |
| §9.7.7 Half comparison | `comparison_and_selection.yaml` | `test_set_half_completeness.cpp`, `test_scalar_half_sources.cpp`, and `test_remaining_bf16_fma_set_sources.cpp`. |
| §9.7.8 Logic and shift | `logic_and_shift.yaml` | `test_select_variant_and.cpp`, `test_select_variant_lop3.cpp`, `test_select_variant_shf.cpp`, `test_select_variant_shl.cpp`, `test_select_variant_shr.cpp`; `test_bitwise_signedness.cpp` covers the shared constant-expression contract. |
| §9.7.9 Data movement and conversion | `data_movement_and_conversion.yaml` | `test_ld_completeness.cpp`, `test_ldu_completeness.cpp`, `test_st_completeness.cpp`, `test_conversion_completeness.cpp`, `test_cvt_pack_sources.cpp`, `test_bulk_async_coverage.cpp`, `test_multimem_coverage.cpp`, `test_prefetch_completeness.cpp`, `test_createpolicy_completeness.cpp`, `test_applypriority_discard_completeness.cpp`. Tensor-specific subroots are outside this worker's scope. |
| §9.7.10 Fabric | `parallel_synchronization_and_communication.yaml` | `python/tests/spec/test_fabric_contract.py`; `submod/resolved_ir/test/test_fabric_coverage.cpp`; corresponding support section in `docs/sdk_support.md`. |
| §9.7.13 Control flow | `control_flow.yaml` | `test_call_tables.cpp`, `test_call_staging.cpp`, `test_call_target_visibility.cpp`, `test_control_target_list_availability.cpp`, `test_select_variant_exit.cpp`, `test_select_variant_ret.cpp`; brace and `@` syntax was delegated. |
| §9.7.14 Parallel synchronization and communication | `parallel_synchronization_and_communication.yaml` | `test_atomic_reduction_coverage.cpp`, `test_cta_barrier_numeric.cpp`, `test_mbarrier_test_wait_qualifiers.cpp`, `test_mbarrier_try_wait_qualifiers.cpp`, `test_legacy_vote.cpp`, `test_legacy_shfl.cpp`, `test_warp_sync_completeness.cpp`, `test_red_async_default.cpp`, `test_multimem_coverage.cpp`. `.tensormap.cp_fenceproxy` remains with tensor review. |
| §9.7.18 Stack manipulation | `stack_manipulation.yaml` | `submod/resolved_ir/test/test_stack_manipulation.cpp`; `python/tests/ir/test_stack_manipulation.py`. |
| §9.7.19 Video | `video.yaml` | `submod/resolved_ir/test/test_video_family.cpp`, `test_video.cpp`; `python/tests/spec/normalize/test_video_family.py` and `test_video.py`. The specialized family normalizer was inspected at `python/src/ptx_frontend/spec/normalize/operands.py`. |
| §9.7.20 Miscellaneous | `miscellaneous.yaml` | `submod/resolved_ir/test/test_miscellaneous_coverage.cpp`; `python/tests/ir/test_miscellaneous_spec.py`; target profile tests in `test_ptx_target_identity.cpp`. |

## Targeted findings

### Interpretation note: `bmsk.wrap` width 33

Official §9.7.1.23 (`/tmp/ptx93-manual/sections/integer-arithmetic-instructions-bmsk.txt`)
states in Notes: “The bitmask width specified by operand b is limited to range
0..32 in .clamp mode and to range 0..31 in .wrap mode.” The same section's
semantics first sets `b1 = b & 0x1f` and defines the wrapped result from `b1`,
which provides a result for a width above 31. This leaves a wording tension for
`.wrap` values above 31.

The canonical operand pattern at
`python/src/ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml:252-255` makes
width an unconstrained `reg_or_imm`; neither `bmsk` variant at `:3017-3024` adds
an immediate range constraint. The positive source module at
`submod/resolved_ir/test/test_integer_arithmetic_completeness.cpp:112-113`
contains `bmsk.wrap.b32 %b0, %u1, 33;`; the test then asserts whole-module
resolution and checking succeeds at `:128-130`. The current model and existing
test therefore infer acceptance.

Minimal probe source:

```ptx
.version 9.3
.target sm_80
.entry k() {
  .reg .b32 %d;
  bmsk.wrap.b32 %d, 1, 33;
}
```

The Notes could be read as imposing a literal operand precondition of `b <= 31`
for `.wrap`, while the pseudocode defines the operation after masking `b` to
five bits. Authority Sol adjudicated that acceptance is consistent with the
complete semantics: the `.wrap` result uses `b1 = b & 0x1f`; the `.clamp` path
explicitly handles `b >= 32`; and no separate instruction-level immediate
rejection rule is established. The Notes can describe the effective width
after wrapping. The maintained interpretation in `docs/sdk_support.md:2064-2067`
accepts the full 32-bit source domain.

The coordinator executed both source cases through parse and
`resolveAndValidate` at PTX 9.3 / `sm_80`, as recorded in
`/tmp/ptx93-audit-evidence/initial-results.txt`:

| Width | Parse | `resolveAndValidate` | Result |
| --- | --- | --- | --- |
| `2` | Pass | Pass | Accepted |
| `33` | Pass | Pass | Accepted |

These are frontend source-validation probes, not GPU execution or assembler
results. They confirm the current code inference and its conformance to the
adjudicated complete semantics. Retain the wording tension as an interpretation
rationale; it is not a remaining suspected code defect.

### Retained reference conflict: `bfe.b32` example

Official §9.7.1.20 syntax lists only `.u32`, `.u64`, `.s32`, and `.s64`, but the
Examples line says `bfe.b32 d,a,start,len;`. The canonical syntax at
`python/src/ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml:2801-2845`
models the four listed integer types.
`submod/resolved_ir/test/test_select_variant_bfe.cpp:25-38` accepts those
four types and explicitly rejects `bfe.b32`. The current model inference
follows Syntax and rejects the conflicting example. This is an existing,
documented reference interpretation (`docs/sdk_support.md:1155-1159`), not a new
gap found by this audit.

### Support-document drift: `brkpt` target catalog

Official §9.7.20.1 says `brkpt requires sm_11 or higher`. The target catalog in
`submod/base/src/ptx_target.cpp:53-76` includes `sm_10`, `sm_11`, `sm_12`, and
`sm_13`. `submod/resolved_ir/test/test_miscellaneous_coverage.cpp:54-63`
checks `brkpt` fails at SM 10 and passes at SM 11, matching the manual. However,
`docs/sdk_support.md:2793` says the target catalog starts at `sm_13` and treats
`sm_11` as unmodeled. The implementation and focused test agree with the manual;
the SDK support sentence is stale. This is a documentation inconsistency, not a
frontend acceptance mismatch. The only authorized write for this assignment is
this audit report, so the support document was not edited.

## Limits and disposition

No confirmed instruction implementation mismatch was established in this
worker's scope. The bmsk width Notes and pseudocode retain a wording tension,
but the Authority Sol interpretation and coordinator's passing frontend probes
support the existing full-semantics acceptance. The BFE example is already
resolved in the support contract. The `brkpt` finding is limited to support-doc
drift.

This audit does not assert semantic completion of the 15 roots: many operand,
modifier, target, version, deprecation, and cross-product combinations were not
individually inspected. The family tests named above were read, not run, and do
not prove every form listed in the manual. The coordinator's two bmsk source
probes were run; no other execution is claimed here. Syntax and directive coverage,
matrix/tensor/texture/surface coverage, live memory behavior, asynchronous
protocol execution, and GPU execution are outside this report's completed
checks. There is no independent architecture or core acceptance claim here.
