# PTX ISA 9.3 manual conformance audit

> **Current follow-up:** the scalar ST immediate compatibility extension now
> accepts all twelve natural nvcc corpus outputs. See its
> [follow-up validation](ptx93_st_postfix/README.md) for the current patch and
> evidence. The earlier [C1 validation](ptx93_postfix/README.md) and findings
> below retain their original phase's results.

**Status: bounded audit completed; exhaustive conformance remains unestablished.**
The initial manual/source and frontend-probe phase confirmed two classes of
documentation defects. A subsequent NVIDIA ptxas comparison confirmed one
current-profile compatibility/coverage gap: six 16-bit whole hardware-vector
MOV forms assemble but the frontend rejects them. The maintained support
authority remains [SDK support](../sdk_support.md); this
record reports evidence and does not expand or change that contract.

## Baseline and official source

- Repository baseline: `1b44ad8212aa254610b3afb447c9e774df03710d`, latest
  `origin/main` when checked; includes merged #307.
- Working branch: `audit/ptx93-manual-conformance`.
- Audit date: 2026-11-13 (Asia/Shanghai).
- Normative source: [NVIDIA archived CUDA 13.3 / PTX ISA 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
- Direct retrieval succeeded after the environment update, approximately
  03:16 UTC / 11:16 Asia/Shanghai. No mirror or rolling newer manual was used.
- Document title: `1. Introduction — PTX ISA 9.3 documentation`; section 1.3
  identifies version 9.3 and its changes.
- Retained HTML: 3,577,575 bytes, SHA-256
  `40026f79b65fee3f8c8b3e717c6f80c6385e8d97215318d6e4b7f6106f58d5a4`.
  The raw HTML is a temporary external input, not a vendored copy of the manual.

## Method and scope

The manual supplies the source-side inventory and quoted rules. The current SDK
contract, canonical YAML, normalizers, handwritten frontend paths, and existing
tests supply the implementation-side evidence. A shared generator input is not
an independent correctness oracle. Earlier qualified acceptance at `a81ca95`
and its 313 classified obligations are historical evidence, not new executions
or a denominator for this audit.

The review distinguishes **consistent within checked scope**, **confirmed
mismatch**, **evidence gap**, **reference ambiguity**, **intentional outside-static**,
and **pending**. Source inspection, tests read, current probes, and historical
assembler observations remain separate evidence classes.

| Official scope | Work performed and retained evidence |
| --- | --- |
| Chapters 1–3 | Version/context and static-versus-runtime boundary; no GPU execution claim. |
| Chapters 4–7 and 11 | Bounded lexer, grammar, declaration, common operand, binding, ABI, and directive trace; [grammar review](ptx93_grammar_audit.md). Instruction-specific combinations were not exhaustively enumerated. |
| Chapter 8 | Static representation versus runtime memory-model obligations; [semantic review](ptx93_semantic_review.md). Runtime executions, races and protocol completion are outside this static audit. |
| Chapter 9, ordinary families | Official root census plus focused clause/code/test comparisons across 15 family sections; [instruction review](ptx93_instruction_audit.md). A family ledger is not a per-form proof. |
| Chapter 9, matrix/tensor/resources | MMA/WMMA/WGMMA/TCGEN, tensor/TMA, texture and surface clause groups and retained interpretations; [matrix and tensor review](ptx93_matrix_tensor_audit.md). Full table cross-products were not regenerated. |
| Chapter 10 | All 33 special-register subsections compared with registry metadata and use checks; selected current/historical probes; [semantic review](ptx93_semantic_review.md). |
| Chapter 12 | Generic opaque advisory pragma contract versus ptxas-specific string interpretation; [semantic review](ptx93_semantic_review.md). |
| Chapter 13 and 14 | Release history/context and notices, not additional executed static obligations. Historical behavior is only assessed where separately identified. |

## Results

### Structural census: no missing instruction root found

[Inventory metadata](ptx93_inventory/inventory.json),
[all named sections](ptx93_inventory/manual_sections.tsv), and
[instruction-heading/root crosswalk](ptx93_inventory/instruction_roots.tsv)
record 715 named sections, including runtime/context/history sections.
A mechanical census of 213 instruction headings containing code spellings
produced 137 distinct root names. All have canonical YAML entries, and all 137
canonical roots occur in those headings.

These units do not count legal instruction forms, operand/type/modifier/target
combinations, passing programs, or proof obligations. They establish name-level
inventory correspondence only. They do not establish full opcode semantics or
full grammar conformance.

### Executed evidence

- [Official example parser batch](ptx93_examples_review.md): 347 selected
  statement candidates from 192 sections and 117 roots; 340 clean parses,
  seven parser diagnostics, no timeout. All seven diagnostic candidates were
  checked against the raw official text and neighboring syntax/context. They
  contain editorial errors or incomplete fragments, not established valid-input
  rejections. All 1,152 candidate selection/skip decisions and full source blocks
  are retained in [the JSON record](ptx93_examples_results.json).
- [Focused frontend probes](ptx93_probes/build-notes.md): both audit executables
  built successfully and 22 fixtures were executed. The retained
  [results](ptx93_probes/results.json) include positive controls, malformed syntax,
  binding/type errors, declaration limits, source-context checks, and reference
  ambiguities. The nine additional rule-level
  [expectations](ptx93_probes/expectations.json) all matched observed behavior.
- Production libraries and the probes were built with GCC 14.2 / Python 3.14.10,
  `-O0 -g0`, four compile jobs and four generator jobs. The portable audit CMake
  hook was reconfigured/rebuilt successfully. The example batch was repeated
  with byte-identical JSON; its recorded executable hash matches the retained
  parser executable.
- The subsequent [NVIDIA assembler comparison](ptx93_ptxas/report.md) executed
  30 complete fixtures with verified official ptxas release 13.3 / V13.3.33 and
  the same full frontend. Of 28 modern comparable cases, 22 agree and six show
  the one 16-bit whole-vector MOV coverage gap. Two retired-target cases are
  unavailable to this assembler, not language rejections. Tool/download hashes,
  exact commands and outputs are retained in that comparison directory.
- The full C++/Python suite, nvcc and GPU execution were not run in this audit.
  Existing tests and earlier assembler observations remain separate evidence.

### Confirmed current-tool coverage gap

**C1 — 16-bit whole hardware-vector MOV source.** Under PTX 9.3 / `sm_80`,
NVIDIA ptxas V13.3.33 accepts U16 whole-vector reads of `%tid`, `%ntid`, `%ctaid`
and `%nctaid`, and S16/B16 whole-vector reads of `%tid`. The full frontend rejects
all six with `Checking/InvalidVectorOperand`:
`Whole hardware vector MOV requires a four-lane intrinsic u32 source.`
The matching U32 whole-vector control passes both tools. These complete kernels
store the output through a parameter-derived pointer with a volatile store;
`-O0` assembly produces cubins. This is not an unused-value-only comparison.

The shared gate is at
`submod/resolved_ir/src/ptx_resolved_ir_checker.cpp:2107–2127`;
`docs/sdk_support.md:6225–6227` already publishes a U32/S32/B32-only subset.
The new observation therefore establishes a concrete missing assembler-accepted
family within the goal of broader PTX 9.3 compatibility, rather than a regression
against an already promised 16-bit whole-vector API. Historical SM10/SM20 behavior
still lacks current-tool evidence. The exact
[U16 `%tid` fixture](ptx93_ptxas/fixtures/tid-v9-3-u16.ptx),
[comparison results](ptx93_ptxas/results.json), and
[authority adjudication](ptx93_semantic_review.md) preserve the evidence.
No checker or support-contract change is included in this audit.

### Confirmed documentation defects

**D1 — stale BRKPT target-catalog statement.** `docs/sdk_support.md:2793` says
that the catalog starts at `sm_13` and that `sm_11` is not separately modeled.
The current catalog at `submod/base/src/ptx_target.cpp:53–76` includes `sm_10`,
`sm_11`, and `sm_12`; the focused BRKPT test at
`submod/resolved_ir/test/test_miscellaneous_coverage.cpp:54–63` rejects SM10 and
accepts SM11, consistent with official §9.7.20.1. The support sentence is stale;
this finding does not establish an implementation defect. See the
[instruction review](ptx93_instruction_audit.md).

**D2 — stale official-manual fragment links.** Of 168 archived-manual fragment
link occurrences in the support document, 29 occurrences use nine fragments
absent from the actual HTML IDs. This includes old TCGEN and combined barrier
anchors. The check considers every HTML `id`, not only section IDs. The exact
support-document lines and missing fragments are retained in
[missing_manual_links.tsv](ptx93_inventory/missing_manual_links.tsv).
This impairs evidence navigation; it does not prove missing frontend behavior.

The audit records these defects; it does not modify the maintained support
contract or repair them as part of a findings-only task.

### Candidate adjudications and retained boundaries

| Candidate | Observed or inspected evidence | Disposition |
| --- | --- | --- |
| Non-register vector declaration over 128 bits | `.global .v2 .f64` accepted; `.v4 .f64` rejected by the later storage-resolution path. Register width/predicate and array boundary probes also matched their rules. | Suspected gap refuted; a permissive intermediate stage is not full-frontend overacceptance. |
| `bmsk.wrap` immediate width 33 | Widths 2 and 33 accepted by both frontend and ptxas; clamp 32/33 also accepted by both. | Current assembler positively corroborates the full-input interpretation; numerical runtime values were not measured. |
| Historical whole-vector `%tid` U16 read | PTX1.0 U16 vector rejected; PTX2.0 U32 vector control accepted. | Bounded unsupported/reference gap remains; current manual's explicit compatibility language concerns component reads. Historical whole-vector conformance is not established. |
| `%pm0_64` / `%pm7_64` with `mov.u32` | Both tools reject U32 and accept the U64 controls; official declaration is U64 but examples use U32. | Current assembler ties the practical choice toward the existing width rule, not the contradictory U32 example. |
| Repeated `.file` index | Both tools accept a single declaration and reject repeated indices, including identical repetitions. | Current assembler corroborates uniqueness; the example list is not evidence of supported sequential replacement. |
| Floating `multimem.red.max.f64` and `bfe.b32` | Both tools reject the contradictory examples; floating add and BFE U32/S32 controls pass both. | Current assembler corroborates the adopted syntax domains for these cases. Matrix table/example conflicts were separately source-inspected and were not assembled in this batch. |
| Preprocessor lines, fixed-address syntax, DWARF recognition, optional tensor relations | API/source-production/recognition/conditional-fact limits remain explicitly qualified. | Clarifications or remaining obligations, not an inference of full support or invented syntax from absent detail. |

Core semantic candidate dispositions were reviewed under the repository's
[ptx-core-review](../../.agents/skills/ptx-core-review/SKILL.md) workflow by the
assigned Authority Sol reviewer; scope, evidence and selection record are in
[the semantic review](ptx93_semantic_review.md). No implementation diff is being
accepted by this report.

## Reproduction and artifact checks

```sh
curl -fsSL https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html \
  -o /tmp/ptx-isa-9.3-official.html
python docs/audits/ptx93_manual_inventory.py /tmp/ptx-isa-9.3-official.html \
  --repo . --output docs/audits/ptx93_inventory
```

The standard-library-only inventory tool includes the official HTML's deeply
nested `h7`/`h8` headings and derives manual names from HTML headings
and separately reads raw canonical opcode entries. It also checks the maintained
contract's archived fragment links. It does not import generated descriptors or
return semantic pass/fail results. Re-run it against the pinned source checkout
and compare the recorded HTML hash before comparing results.

Probe build and run commands are in [build-notes.md](ptx93_probes/build-notes.md).
The example-batch command is in [its review](ptx93_examples_review.md). Executable
code, fixtures, source hashes, decisions and outcomes are retained as audit
artifacts; downloaded dependencies and binaries remain temporary.

## Remaining evidence and overall disposition

This work establishes a manual-derived structural inventory, bounded code/test
crosswalks, concrete example-parser evidence, and focused positive/negative
frontend executions. The added assembler comparison confirms the one C1
coverage gap described above. **It does not certify that every PTX 9.3 grammar production, legal form,
or invalid modifier/type/target cross-product is handled correctly.** Full
form/obligation enumeration, all documented reference boundaries, historic
windows, and consumer-dependent source/semantic guarantees remain separately
qualified in the domain reports. Current ptxas behavior is now used as a
versioned practical compatibility reference for the selected contradictions,
with unsupported-target outcomes kept distinct from instruction rejection.

The support document remains authoritative for published scope, including five
optional whole tensor-map relations that conservatively remain Unresolved and
runtime/physical-linkage exclusions. The finite agreement cases and the confirmed
coverage gap neither close those boundaries nor establish all remaining behavior.
Fuzz expansion remains deferred. Production source, canonical specifications and
existing tests are unchanged; no commit, push, PR or environment publication was
performed for this audit.
