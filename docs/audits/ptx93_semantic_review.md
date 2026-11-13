# PTX 9.3 semantic boundary and authority review

**Current outcome:** the requested NVIDIA assembler comparison confirmed C1 at
the baseline: 16-bit whole hardware vector reads, demonstrated by six source
forms. The subsequently authorized fix is **accepted for the exact diff recorded
below**; all 28 supported-target comparison fixtures now agree. The subsequently
authorized bounded immediate-ST extension is also **accepted** in the final
record: all twelve fresh natural compiler modules now pass. Remaining NVIDIA
dialect limits are explicit; exhaustive conformance remains unestablished.

Audit-only review of baseline `1b44ad8212aa254610b3afb447c9e774df03710d`
on `audit/ptx93-manual-conformance`. This record does not change the SDK support
contract or accept an implementation diff. Assigned role: Authority Sol;
dispatch requested and accepted `gpt-6.1-sol` / `high`. Effective runtime model
telemetry is not exposed. Review instructions: [.agents/sol.md](../../.agents/sol.md)
and [ptx-core-review](../../.agents/skills/ptx-core-review/SKILL.md).

Normative source: [NVIDIA CUDA 13.3 archived PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html),
retained as `/tmp/ptx-isa-9.3-official.html`, SHA-256
`40026f79b65fee3f8c8b3e717c6f80c6385e8d97215318d6e4b7f6106f58d5a4`.
The source sections below were inspected independently of the other domain
agents' summaries. Source inspection and tests read are not executed checks.

## Checked source scope

| Official section | Evidence inspected | Bounded assessment |
|---|---|---|
| Chapter 8: memory operations, scopes, proxies, ordering and axioms | Official sections `scope-and-applicability`, `memory-operations`, `memory-consistency-state-spaces`, `operation-types`, `scope`, `proxies`, `morally-strong-operations`, `axioms`; `docs/sdk_support.md:22–26,169–174` | Execution axioms, physical aliasing, live multimem addresses, observed values, races and completion are **intentional outside-static**. Source representations and legality of qualifiers remain static obligations owned by the corresponding instruction review. This is a boundary classification, not a complete audit of every Chapter 8 axiom or instruction. |
| Chapter 10: all 33 special-register subsections | `submod/base/src/ptx_special_register.cpp:30–169`; `submod/base/include/ptx_special_register.hpp:11–84`; `submod/resolved_ir/src/ptx_resolved_ir_bound_operands.cpp:926–979,4040–4063`; `submod/resolved_ir/src/ptx_resolved_ir_checker.cpp:1288–1322,2066–2088`; target capability tables in `submod/base/src/ptx_target.cpp:23–35` | **Consistent within checked scope** for the official predefined names, current scalar element types, vector widths, x/y/z scalar selectors, indexed-family bounds, and intrinsic version/SM floors. Historical scalar reads have instruction-specific overrides. This does not establish all type/form/target cross-products or all public IR mutation behavior. Historical whole-vector reads and a contradictory example remain qualified below. |
| Chapter 12: five backend pragma string descriptions, with generic directive §11.4.7 | `submod/resolved_ir/src/ptx_pragma_metadata.hpp:40–185`; `submod/resolved_ir/test/test_pragma_metadata.cpp:18–202`; `docs/sdk_support.md:3593,3700,8855–8862` | **Consistent within checked scope** for retaining opaque advisory strings with actual placement scope and the generic PTX 2.0 floor. Backend optimization effects and backend string interpretation are **intentional outside-static**. No string-specific ptxas behavior was executed here. |

The Chapter 10 comparison included `%tid`, `%ntid`, `%laneid`, `%warpid`,
`%nwarpid`, `%ctaid`, `%nctaid`, `%smid`, `%nsmid`, `%gridid`, all seven
cluster identities, all five lane masks, `%clock`/`%clock_hi`/`%clock64`, both
eight-member performance-monitor families, 32 environment registers, all three
global timers, three named and two indexed reserved-shared-memory registers,
three shared-memory-size registers, and `%current_graph_exec`. Metadata matches
each corresponding section's Syntax, PTX ISA Notes, and Target ISA Notes.

The manual says in [§10](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#special-registers)
that special registers are “predefined, read-only variables” accessed through
“mov or cvt instructions.” The bound scalar resolver rejects unknown identities
and unselected vectors, and checker projection independently rejects invalid
indexed identities and selectors. Historical scalar reads are described and
covered by source tests in
`submod/resolved_ir/test/test_resolved_module_memory_vectors.cpp:1743–1806`
and `submod/resolved_ir/test/test_cvt_special_register_sources.cpp:114–149`.
Those tests were read, not rerun by this reviewer.

## Boundary decisions

Chapter 8's model applies to programs “running on sm_70 or later” and excludes
texture, `ld.global.nc`, and surface accesses
([§8.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#scope-and-applicability)).
Its definitions of overlap, virtual aliases, initialization, morally strong
operations and execution orders do not require a static frontend to simulate
executions or reject every program with a runtime race. Conversely, preserving
written semantics, scope, proxy, address-space and instruction identity is part
of the static contract; an instruction audit must not dismiss such a failure
as runtime-only. No blanket memory-model conformance conclusion follows here.

Generic pragma semantics are explicitly bounded by
[§11.4.7](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#performance-tuning-directives-pragma):
“The interpretation of .pragma directive strings is implementation-specific
and has no impact on PTX semantics.” Chapter 12 describes strings “defined by
ptxas.” Thus accepting opaque `"mma_throughput"` at module scope under PTX 2.0
is not alone a semantic mismatch in this SDK's declared opaque-string contract.
The frontend preserves the string and scope without claiming the entry-only
optimization or its PTX 9.3 ptxas implementation. The same separation applies
to `nounroll`, `used_bytes_mask`, `enable_smem_spilling`, and `frequency`.

## Qualified observations requiring no production change in this audit

1. **Historical whole-vector reads: evidence gap / bounded unsupported scope.**
   [§10.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#special-registers-tid)
   says `%tid` was “Introduced in PTX ISA version 1.0 with type .v4.u16” and
   redefined as `.v4.u32` at PTX 2.0; `%ntid`, `%ctaid`, and `%nctaid` have
   equivalent notes. The compatibility text expressly permits 16-bit reads
   “of each component.” The SDK explicitly limits whole hardware vectors to
   intrinsic-U32 reads (`docs/sdk_support.md:6225–6227`), and the checker hard
   checks U32/S32/B32 at `ptx_resolved_ir_checker.cpp:2107–2123`, despite the
   vector MOV YAML having a width-16 compatibility entry at
   `data_movement_and_conversion.yaml:3040–3050`. Source inspection predicts
   `mov.v4.u16 V, %tid` rejection. A historical whole-vector production and
   current-reference interpretation need corroboration before calling this an
   incorrect accepted/rejected form; it is not established as a supported-form
   regression. The shared builder's full-module probes confirm rejection of
   PTX 1.0 / SM 10 `mov.v4.u16 V, %tid` with `Checking/InvalidVectorOperand`
   at `5:15–5:19`, while PTX 2.0 / SM 20 `mov.v4.u32 V, %tid` passes.
   This observed implementation boundary does not resolve the historical
   reference question or establish a supported-form regression. The subsequent
   current-target assembler comparison below establishes a concrete coverage
   difference without establishing historical-target conformance.
2. **Performance-monitor example: reference ambiguity.**
   [§10.26](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#special-registers-pm0-64-pm7-64)
   declares `.sreg .u64 %pm0_64` through `%pm7_64`, then illustrates
   `mov.u32 r1,%pm0_64` and `mov.u32 r1,%pm7_64`. The general
   [§9.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#type-information-for-instructions-and-operands)
   rule requires matching sizes; its width relaxation expressly names `ld`,
   `st`, and `cvt`, not `mov`. The registry's U64 type is correct and no
   normative special exception is stated. This isolated contradictory example
   does not establish a frontend defect. The full-module probe confirms
   `mov.u32` rejection with `Checking/OperandTypeMismatch` at `4:34–4:41`,
   while the matched `mov.u64` control passes.
3. **Floating multimem reduction example: reference ambiguity, existing interpretation.**
   Independently inspected
   [§9.7.9.15](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-multimem):
   its floating syntax specifies `.redop = { .add }` while `.redtype` includes
   `.f64`, but the example uses `multimem.red.relaxed.gpu.max.f64`.
   `data_movement_and_conversion.yaml:22271` and related floating `red`
   variants fix `.add`; `python/tests/spec/test_multimem_contract.py:70–94`
   checks the table interpretation, already recorded at
   `docs/sdk_support.md:3022–3031`. Following the explicit syntax domain is a
   defensible retained interpretation; this contradiction is not a new bug.

## Additional candidate adjudications and executed evidence

The reviewer independently inspected the coordinator's
`/tmp/ptx93-audit-evidence/initial-results.txt`, including complete fixture
sources and stage/kind/range diagnostics. These are executed full-module
`resolveAndValidateModule` observations from the shared probe builder, not
tests executed by this reviewer and not GPU execution. The final audit must
retain the probe source, build identity and results if it relies on them.
The exported records are now retained in
[`ptx93_probes/results.json`](ptx93_probes/results.json), with
[`probe.cpp`](ptx93_probes/probe.cpp),
[`build-notes.md`](ptx93_probes/build-notes.md), and complete fixtures.
The reviewer inspected the actual probe implementation: it rejects parse/lower
diagnostics before invoking complete `resolveAndValidateModule`, with separate
exit/stage diagnostics. All 22 retained fixture SHA-256 values were independently
checked against the result metadata and match. Nine additional finite
obligations cover vector/predicate limits, array extent initialization, envreg
family bounds, and address-size legality; their recorded outcomes agree with
the checked manual clauses. In particular `%envreg31` passes, while
`%envreg32` fails during binding. These are finite source acceptance checks.

| Candidate and matched control | Observed full-module outcome | Authority assessment |
|---|---|---|
| `.global .v4 .f64 V` / `.global .v2 .f64 V` | V4 rejects `DeclarationSemantics/UnsupportedStorageDeclaration`, range `4:1–4:20`, 128-bit limit message; V2 passes | **Candidate refuted.** §5.4.2 says “Vectors cannot exceed 128-bits in length; for example, .v4 .f64 is not allowed.” Although the first declaration pass checks width only for registers, `ptx_storage_declarations.cpp:1047–1053` enforces the storage limit; `ptx_resolved_module.cpp:1098` invokes it. A successful earlier phase is not successful full-module validation. |
| `bmsk.wrap.b32 %d,1,33` / width 2 | Both pass with zero diagnostics | **Consistent with the retained interpretation; wording qualification remains.** §9.7.1.23 computes `b1 = b & 0x1f`, explicitly handles `b >= 32` for clamp, and describes the “wrapped value of b.” The Notes range can describe effective width rather than an input precondition. It does not establish that an immediate 33 must be rejected. `arithmetic.yaml:3011–3024` and `sdk_support.md:2064–2067` implement the full input interpretation. No runtime result was checked. |
| Three consecutive `.file` example lines reusing index 1 / final line alone | Repeated ID rejects `Binding/DuplicateSymbol`, range `6:7–6:8`, previous `4:7–4:8`; single declaration passes | **Reference ambiguity / retained interpretation.** §11.5.3 says “Associates a source filename with an integer index” and its Examples show indices 1,2,1. It defines no replacement or redefinition semantics and does not explicitly say these are one program rather than alternative illustrations. `ptx_symbol_table.cpp:837–852` uses a unique metadata namespace; `sdk_support.md:8905–8916` explicitly records duplicate rejection and historical ptxas evidence. The example sequence alone does not establish a frontend defect. Historical assembler evidence was read, not rerun. |

The full-module probe also checks a valid ordinary module, an unresolved source
operand rejected during binding, and an unmatched body rejected during parsing.
These controls distinguish the relevant stages; they do not validate all
diagnostic paths or establish complete owned-IR coverage.

## Review of other audit evidence

The instruction and matrix/tensor reports were read as bounded source ledgers,
not independent proofs of all their listed families. The reviewer independently
checked the multimem conflict above, the BMSK clauses, the BRKPT documentation
finding below, and the dense-integer WGMMA N=240/256 interpretation against the
official Syntax, shape table, N/2 S32 fragment clause, and actual canonical YAML
entries. The WGMMA instruction Syntax ends at N=224 while the integer shape
table includes N=240/256; the retained dedicated-table interpretation remains
defensible and reference-qualified. The tensor fact catalog and source tests
explicitly preserve unresolved whole relations 17–21. No independent rederivation
of all matrix/tensor cross-products or all six retained interpretations was
performed here; those portions remain the domain reviewer's bounded evidence.

The grammar report was also read, including its withdrawn storage-vector
candidate and qualified `.file` candidate. The reviewer independently checked
those two cases' official clauses, source paths and runtime evidence above.
For its preprocessor boundary, §4.1 says “The C preprocessor cpp may be used to
process PTX source modules”; `ptx_lexer.l:250–253` diagnoses raw `#` input.
That sentence permits preprocessing but does not require the frontend to
implement a C preprocessor. No confirmed language defect follows without a
stronger claim that this API accepts unprocessed directives. Other lexical,
declaration, ABI and directive cross-products remain the grammar reviewer's
bounded source evidence rather than independently accepted exhaustive coverage.

The reviewer inspected `ptx93_example_probes.py`, JSON metadata and all seven
diagnostic records, retained raw HTML blocks, and nearby official syntax or
qualifier clauses. The 347 selected statements / 192 sections / 117 roots,
340 clean parses and seven diagnostics are correctly described as finite
parser evidence. The diagnostics correspond to missing separators/terminators,
the `try_wait loop` spelling inconsistent with `try_wait_loop:`, or single-colon
collector spellings inconsistent with the official qualifier syntax. No valid
input rejection is established by these seven records. Parsing undeclared
operands under an SM 120A wrapper establishes neither semantic legality nor
family target support. The 805 skipped candidates are not passes.

Two **confirmed documentation defects**, neither a frontend acceptance mismatch,
were independently checked:

- `sdk_support.md:2793` says the target catalog starts at SM 13 and SM 11 is
  unmodeled. `ptx_target.cpp:53–76` includes SM 10/11/12/13, and
  `test_miscellaneous_coverage.cpp:54–63` checks the official §9.7.20.1 BRKPT
  floor (reject SM 10, accept SM 11). The sentence is stale.
- `ptx93_inventory/missing_manual_links.tsv` identifies 29 archived-manual URL
  occurrences across nine distinct fragments absent from the retained official
  HTML. An independent stdlib HTML-ID check confirmed that none of these nine
  fragments exists among all HTML IDs. This is broken reference navigation,
  not proof that the linked SDK semantic claims are incorrect.

## Requested NVIDIA assembler comparison

The user subsequently requested local ptxas comparison of the disputed forms.
The shared comparison worker installed NVIDIA's official CUDA 13.3 component and
executed the retained 30 fixtures, including the coordinator's bounded extension
from `%tid` U16 to five related spellings. The reviewer independently read
[`ptx93_ptxas/results.json`](ptx93_ptxas/results.json), the exact fixtures,
[`run.py`](ptx93_ptxas/run.py), and
[`download_provenance.json`](ptx93_ptxas/download_provenance.json).
The actual tool reports **release 13.3, V13.3.33**; its SHA-256 is
`7fdd01a4cf50e30746da98989c9272a907f491e6fd7fecfda14642e4375f88fb`.
The reviewer independently checked all 30 fixture hashes, both local executable
hashes, and the downloaded archive hash against retained metadata. The worker's
official manifest/archive verification is recorded in the download provenance;
the reviewer did not repeat the download or execute the assembler.

All modern fixtures use complete kernels; live values feed volatile global
stores or an observable multimem reduction, and assembler invocations use
`-O0 --gpu-name=<fixture target>`. Positive controls accept in both tools; the
unknown-opcode control rejects in both. The two original historical wrappers
are retained separately and their old targets are rejected by the current
assembler driver before instruction checking. There were **28 comparable modern
fixtures: 22 agreed and six differed**, with all six differences caused by the
same whole-vector checker restriction. This finite comparison is not a
conformance percentage or a GPU-results check.

### C1 — confirmed current frontend/ptxas coverage difference

Exact source: [`tid-v9-3-u16.ptx`](ptx93_ptxas/fixtures/tid-v9-3-u16.ptx).
Its PTX 9.3 / SM 80 kernel declares `.reg .v4 .u16 values`, executes
`mov.v4.u16 values, %tid`, and stores that result using
`st.volatile.global.v4.u16 [addr], values`.

- NVIDIA ptxas V13.3.33 accepts with exit 0, no diagnostics and a 3,368-byte
  cubin; retained output SHA-256 is
  `af2c9edf1e06bfae64ac7c960d821d6dc1d613d99e1ca83c02fedb73f5018dc3`.
- The baseline frontend parses cleanly but rejects with
  `Checking/InvalidVectorOperand` at `8:20–8:24`: “Whole hardware vector MOV
  requires a four-lane intrinsic u32 source.” The error selects `%tid`, not
  the subsequent store or its output address.
- The matched [`tid-v9-3-u32.ptx`](ptx93_ptxas/fixtures/tid-v9-3-u32.ptx)
  changes the declaration, MOV and store element width to U32 and accepts in
  both tools.

The coordinator's five additional fixtures establish the same rejection with
U16 `%ntid`, `%ctaid`, and `%nctaid`, plus S16 and B16 `%tid`. Each assembles
successfully into a 3,368-byte cubin and each frontend check reports the same
`InvalidVectorOperand` message on the special-register operand:

| Exact retained fixture | Whole MOV source | Frontend diagnostic range |
|---|---|---|
| [`ntid-v9-3-u16.ptx`](ptx93_ptxas/fixtures/ntid-v9-3-u16.ptx) | U16 `%ntid` | `8:20–8:25` |
| [`ctaid-v9-3-u16.ptx`](ptx93_ptxas/fixtures/ctaid-v9-3-u16.ptx) | U16 `%ctaid` | `8:20–8:26` |
| [`nctaid-v9-3-u16.ptx`](ptx93_ptxas/fixtures/nctaid-v9-3-u16.ptx) | U16 `%nctaid` | `8:20–8:27` |
| [`tid-v9-3-s16.ptx`](ptx93_ptxas/fixtures/tid-v9-3-s16.ptx) | S16 `%tid` | `8:20–8:24` |
| [`tid-v9-3-b16.ptx`](ptx93_ptxas/fixtures/tid-v9-3-b16.ptx) | B16 `%tid` | `8:20–8:24` |

The source cause is `ptx_resolved_ir_checker.cpp:2107–2123`: the whole hardware
vector branch admits expected U32/S32/B32 only, even though the YAML vector MOV
variant has a width-16 special-register compatibility entry at
`data_movement_and_conversion.yaml:3040–3050`. This is a **confirmed missing
assembler-accepted form in the frontend**, with a practical consequence: code
using the current NVIDIA-accepted U16 whole-vector read cannot pass this
frontend. `sdk_support.md:6225–6227` explicitly describes the current U32-only
subset, so this is a supported-scope limitation rather than a regression against
the SDK's published subset. That qualification does not erase the observed
coverage difference. The six confirmed forms are one family-level finding,
not six unrelated defects.

The relevant manual evidence is [§10.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#special-registers-tid):
the historical `.v4.u16` definition and permission for “16-bit mov and cvt
instructions” to read “the lower 16-bits of each component,” together with
[§6.4.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#vectors-as-operands)'s
vector source/destination rule. The manual does not explicitly illustrate this
whole-vector spelling. The local assembler supplies concrete practical
corroboration for interpreting that compatibility permission as admitting it;
it is not a vendor-issued clarification of every historical form.

An authorized follow-up should address the whole hardware vector MOV checker
boundary and add this exact current U16/U32 pair as focused evidence, preserving
intrinsic identity and version/type checking. This findings-only task does not
authorize that implementation or a support-contract change. The demonstrated
scope is these six forms at PTX 9.3 / SM 80. Other register/type cross-products,
architectures, historical targets and whole hardware vector CVT forms remain
unexecuted and do not inherit a result automatically.

### Practical resolution of the other selected contradictions

| Compared forms | NVIDIA V13.3.33 and frontend observations | Focused disposition |
|---|---|---|
| Repeated `.file` example sequence and identical repeat, versus single declaration | Both repeats reject as duplicate file index/symbol; single declaration accepts | Current NVIDIA behavior corroborates the uniqueness interpretation. Do not interpret the example list as a valid replacement sequence for this tool. The manual's missing redefinition prose remains a reference issue. |
| `%pm0_64` and `%pm7_64`, U32 versus U64 MOV | Both tools reject U32; both accept U64 | Treat the U32 examples as unusable on this current NVIDIA assembler; the U64 declaration/same-width interpretation is corroborated. No frontend compatibility gap in these four fixtures. |
| `bfe.b32` versus U32/S32 | Both tools reject B32; both accept U32/S32 | The dedicated Syntax type list, rather than the isolated B32 example, matches current assembler behavior. |
| `bmsk.wrap` widths 2/33 and `bmsk.clamp` widths 32/33 | All four accept in both tools | Current assembler corroborates full-input admission; rejecting 33 merely from the Notes range would diverge from this tool. No runtime mask value was observed. |
| Floating `multimem.red.max.f64` versus `.add.f64` | Both reject MAX/F64 and accept ADD/F64 | Syntax-domain interpretation is corroborated; the MAX/F64 example is unusable on this current assembler. |
| `.reg .v2 .f64` versus `.v4 .f64`, and `.v2 .pred` | Both accept V2/F64 and reject oversized or predicate vector declarations | Checked declaration restrictions agree in these fixtures. |
| Original PTX 1.0/SM 10 and PTX 2.0/SM 20 vector fixtures | ptxas reports unsupported `gpu-name` for both old targets | Neither assembler result adjudicates the historical grammar, instruction or compatibility rule. Original frontend U16 rejection/U32 acceptance remains recorded separately. |

These direct observations provide practical tie-breaking for the requested
forms; they do not require generic precedence claims about prose, examples,
tables or an assembler in unrelated cases. The comparison used V13.3.33, not
the historical V13.3.73 observations quoted elsewhere in the SDK contract.

## Audit disposition before the authorized fix

The extended audit confirms **one current frontend/ptxas coverage difference
(C1)** and the two documentation defects above. The earlier manual-only review
found no unambiguous supported-form semantic mismatch; that earlier observation
must not be presented as absence of compatibility gaps after this comparison.
The remaining selected contradictions agree with current NVIDIA assembler
behavior, subject to the stated fixture and historical-target boundaries.
Exhaustive PTX 9.3 conformance remains unestablished. No source test suite,
assembler, executor, GPU test, or fuzzing was run directly by this reviewer;
shared finite frontend/assembler evidence was independently inspected, and the
artifact hashes and missing-fragment records independently checked. No
production/support contract change, commit or push was made. Candidates received
after this record require focused adjudication; no blanket approval attaches to
unrelated family claims. Core implementation acceptance is **not applicable**
because the authorized deliverable is findings-only.

Initial aggregate review (before the user-requested ptxas extension):
`ptx93_manual_audit.md` was read against this authority
record, the domain reports and retained evidence. Its 715 named sections,
213 instruction headings, 137 roots, 347 selected example candidates, seven
editorial diagnostics, 22 fixtures, and 29 missing-link occurrences across nine
fragments match the respective metadata. Its bounded conclusion and candidate
labels preserve the adjudications above without claiming exhaustive conformance
or implementation acceptance. No blocking overclaim was identified. The heading
count incorporates the coordinator's mechanical h1–h9 extraction refinement;
the 137 roots and example outcomes are unchanged. The nine additional
expectations' URL fields now identify the archived CUDA 13.3 manual, consistent
with the final aggregate's normative-source statement. This final check did not
rerun probes or enlarge the reviewed semantic scope.

## C1 follow-up design decision — implementation authorized, acceptance pending

The user subsequently authorized fixing C1, updating documentation and repeating
manual/ptxas checks with varied actual PTX inputs. Commit, push and PR remain
unauthorized. This reviewer retains Authority Sol (`gpt-6.1-sol` / `high`, prior
accepted selection; effective runtime telemetry unexposed) and does not author
the implementation. The earlier findings-only disposition applies to the audit
revision; it does not accept the forthcoming implementation.

**Design approved:** retain one four-lane predefined hardware vector identity,
the intrinsic U32 registry metadata and existing source spelling/ID/range/arity
and provenance checks. Native 32-bit U32/S32/B32 reads retain their current
behavior. Admit B16/U16/S16 whole reads of `%tid`, `%ntid`, `%ctaid`, and
`%nctaid` by reusing the exact operand/kind/instruction-width compatibility
descriptor already selected in `check_operand_descriptors`. Its effective U16
read type and PTX 1.0 / SM 0 availability govern this check only; no effective
type is stored into the owned IR or intrinsic registry. Compare that contextual
type to the selected instruction type using the established SameWidth scalar
compatibility rule. Do not add parallel opcode/kind/version spelling tables.

The whole-vector branch must still verify that the true intrinsic is a
four-lane U32 source. A scalar intrinsic such as `%gridid` cannot become a whole
vector merely because a scalar compatibility descriptor exists. Cluster vector
identities have no 16-bit descriptor, so native U32 versus instruction U16
remains incompatible; their 32-bit version/SM/capability gates remain intact.
No broader cluster narrowing, register-family expansion, IR/API/schema change,
new scalar producer or whole-vector CVT behavior is approved.

The PTX 1.0 floor for classic whole 16-bit source reads is an explicit source
interpretation based on each classic register's `.v4.u16` introduction in
§10.1/§10.2/§10.6/§10.7 and the vector MOV descriptor already present in the
baseline. It does not claim that modern ptxas can assemble old SM targets or
prove historical GPU execution. Classic native 32-bit reads retain their PTX
2.0 floor.

Acceptance requires actual-diff review and focused evidence for the changed
boundary: all four classic identities across B16/U16/S16; unchanged native32
controls; named and braced destinations including legal sinks; rejection of
cluster16, invalid arity/types and forged intrinsic identities/spellings/ranges;
version floors and existing cluster target/capability controls; AST-free owned
validation, copies/clones and relevant mutation behavior. Existing tests may be
reused where they already establish an invariant. Fresh frontend/ptxas
comparison and varied real-input evidence must identify their actual finite
scope and any remaining differences. Passing old audit probes alone is not
core acceptance. **Implementation acceptance remains pending.**

### Real-input exception adjudication during C1 verification

The varied whole-module corpus contains twelve fresh nvcc V13.3.33 emissions
from four ordinary CUDA sources over SM 80, SM 90A and SM 100, plus three pinned
external compiler-output fixtures. This reviewer inspected the source/result
packet and all fifteen fixture hashes. Eleven of the twelve fresh emissions
pass the rebuilt frontend, while all twelve assemble with the matching ptxas.
The exception is a real practical compatibility limit and must remain visible:
[`histogram_sm_100.ptx`](../../corpus/ptx93_real_inputs/generated/histogram_sm_100.ptx)
contains immediate stores at lines 58, 74, 77, 80 and 83; each is
`st.shared.b32 [register], 0`. The frontend reports five `Resolution` failures
for `ExplicitScalar`; ptxas accepts the unchanged complete compiler output.
The explicitly handwritten
[`st_immediate_minimized.ptx`](ptx93_real_inputs/st_immediate_minimized.ptx)
isolates the same rejection/acceptance, with raw results in its JSON companion.

**Scope decision:** preserve this exception and do not silently widen ST under
the C1 fix. The fixed official
[§9.7.9.11 Notes](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st)
explicitly says “Operand b must be in the .reg state space.” The actual YAML
`st_scalar.src` at `data_movement_and_conversion.yaml:1093–1105` is `kind: reg`,
consistent with that restriction. Current NVIDIA compilation/assembly therefore
demonstrates a source extension beyond this explicit fixed-reference
restriction. The practical consequence is that the frontend cannot consume
every current nvcc output; recording a reference reason does not erase that
limitation. Adopting a broader NVIDIA dialect would require a separate scoped
contract decision and source/owned-IR evidence. This independent ST observation
does not invalidate the narrow C1 correction or require all corpus inputs to
pass before C1 can be accepted.

The two raw historical matrix PTX files fail binding because `.loc 16` has no
corresponding `.file 16`, consistent with §11.5.4's declared-file requirement;
modern ptxas cannot adjudicate their SM 20 targets. The third external control,
decoded without PTX edits from the retained NVIDIA BSD-header C byte array,
has `.file 16`, a PTX 1.4 / SM 10 profile and passes complete frontend
validation; ptxas cannot assemble that retired target. These are separate
finite observations, not a claim that the historical target/profile or runtime
ABI has been executed. Provenance and complete raw outputs remain in
[`ptx93_real_inputs`](ptx93_real_inputs/README.md).

## Final C1 core acceptance

**Verdict: ACCEPT.** Reviewed against baseline
`1b44ad8212aa254610b3afb447c9e774df03710d` on
`audit/ptx93-manual-conformance`. The exact five-file tracked diff SHA-256 is
`9fa8101ebfcbaa5ee8750185c4362e1b4ff5c4730ccd92b0b82f99cfcfed301d`.
This reviewer independently inspected the actual checker/test/support-contract
and paired design-document changes; the implementation was authored by the
separate Implementation Sol worker. Reviewer assignment remains Authority Sol,
with the requested and accepted `gpt-6.1-sol` / `high` selection; effective
runtime telemetry is not exposed. There are no blocking findings for this diff.

The accepted change is narrow: the whole hardware vector path retains its
intrinsic four-lane U32 identity/spelling/provenance checks, and replaces its
native32 instruction-type whitelist with the already contextualized
`special_register_type` and established scalar compatibility rule. Existing
descriptors therefore admit classic B16/U16/S16 reads and their PTX 1.0 floor,
while native32 floors, cluster restrictions and scalar/brace source rules
remain intact. No registry, YAML, public IR/API or generator contract changes
were introduced. SDK support now describes this exact classic16 boundary;
paired English/Chinese design notes describe instruction-local compatibility
without rewriting owned intrinsic metadata. The stale BRKPT statement and
29 broken official fragment occurrences were also repaired; an independent
check found all 168 current archived-manual fragment links resolve.

Verification independently inspected:

- The baseline red log records the two new positive/ownership tests failing at
  the original C1 `InvalidVectorOperand` gate. The fixed focused executable
  reports **18/18 passing** across `VectorMov` and `MovCompleteness`.
- New tests cover all four classic identities × B/U/S16 and native B/U/S32,
  named and braced destinations, AST release, copies/moves/clones, version
  floors, cluster target/capability restrictions, invalid type/arity and forged
  identity/location payloads. Range tests preserve existing nonempty/contained
  ownership semantics; C1 does not add exact token-length authentication.
- The fresh `/tmp/ptx93-postfix-ptxas/results.json` repeats the **same 30 fixture
  hashes** as the baseline comparison. All **28 supported-target cases agree**
  between fixed frontend and NVIDIA ptxas V13.3.33, including all six original
  C1 source forms. The two retired targets remain unavailable to this assembler;
  their frontend historical acceptance is not assembler/GPU execution evidence.
- Fixed frontend executable SHA-256 is
  `bb86076da340ab9a007425a4771a62eeac302c1126bb224d089a04da705b1a97`.
  It matches the actual local binary and both fresh comparison and real-input
  result metadata. The real-input result's checker-source hash matches the
  reviewed current checker file. All fifteen real-input fixture hashes were
  independently verified; the accepted external embedded PTX was independently
  checked against the original C array bytes excluding its terminal NUL.
- The corpus has 11/12 fresh compiler outputs accepted by the frontend,
  12/12 assembled by ptxas, and one accepted external historical control.
  The immediate-ST exception and two missing-debug-file upstream inputs remain
  preserved with their actual failing stages; none was rewritten to manufacture
  a passing corpus result. Those documented limits do not block C1 acceptance.
- `git diff --check` passes. The full C++/Python suite, GPU execution and fuzzing
  were not run for this bounded correction; no missing concrete risk required
  expanding the focused verification.

This acceptance is limited to the exact reviewed five-file diff and its stated
contract. Audit/probe/corpus artifacts document finite observations; they do not
establish complete PTX 9.3 or NVIDIA-dialect compatibility. Immediate store
sources remain a separate unadopted extension. Material later semantic changes
require focused re-review. Acceptance does not authorize commit, push or PR.

## Immediate-ST extension decision — approved contract, final acceptance below

The user subsequently explicitly requested filling the immediate-ST gap. This
supersedes the earlier decision to leave it outside C1; it does not retroactively
change the official manual. The accepted C1 patch remains preserved. This
reviewer retains the Authority Sol/high assignment and remains separate from
the Implementation Sol author. No commit, push or PR is authorized.

The reviewer independently inspected the fresh NVIDIA V13.3.33 evidence in
`ptx93_st_immediate/matrix.json` (386 runs) and `frontiers.json` (28 runs),
including diagnostic summaries, representative complete sources and all
recorded first-matrix source hashes. These are finite assembler observations,
not independent runtime-value verification. The observations corroborate more
than zero-only B32: integer nonzero/negative/truncating values, ordinary floating
numeric/exact-word sources, ordinary address spaces, qualified shared forms,
ordered/volatile/cache forms, call-input/return parameter stores, and current
tool source floors down to PTX 6.3 / SM 75. Unsupported older targets do not
establish historical instruction rejection.

**Approved source contract:** widen ordinary synchronous scalar ST **data-source
fields only** to existing `RegOrImm`, retaining all register permissions and
their equal-or-wider policy. Admit the existing numeric producer/converter
domain: B/U/S8/16/32/64 integer literals and legal evaluated integer expressions,
with normal use-width narrowing and retained original integer bits; F32/F64
numeric floating constants/expressions and their existing exact-word conversion;
B32's existing same-width bare `0f` producer, and B64's existing decimal/F64
floating producer. Integer tokens do not acquire F32/F64 permission, nor do
floating literals acquire integer-type permission. Exact F32 expression/sign
restrictions remain the existing converter contract. No shared converter
permission is broadened merely to support ST.

The permission applies to existing synchronous scalar generic/explicit global,
local, shared, qualified shared, parameter and `.param::func` source roles and
their already legal qualifiers/cache layouts. Existing instruction/type/target
floors, address alignment/provenance/write permissions, ordering rules, readonly
inputs, call adjacency and return ABI checks remain applicable. No speculative
immediate-only availability floor is introduced; measured tool floors and
unmeasured historical execution remain separately documented.

The immediate data stays programmer-expressed: preserve its typed use-width
bits, original integer source bits where produced, and source ranges in the
owned immediate arm. No synthetic temporary register, extra MOV, or hidden
instruction rewriting is approved. The generated public C++ source fields
become `WithLocs<RegOrImm>`; consumers must rebuild and select the register or
immediate arm. Update source-of-truth YAML, generated model expectations,
consumer references and paired documentation consistently.

**Deliberate bounds:** scalar B128 immediate remains unsupported because the
current immediate payload has only 64 bits; its existing register source stays
supported. The assembler's B128 acceptance is evidence of a remaining model
limit, not silently claimed support. Vector ST, asynchronous/multimem store
families and auxiliary cache-policy immediates receive no new permission. The
frontier assembler accepts an immediate cache policy, but that is a separate
operand contract and remains register-only here.
The assembler also accepts some integer tokens above UINT64_MAX; the existing
64-bit literal/evaluator domain is retained rather than introducing arbitrary
precision. These observed assembler-positive exclusions must be reported as
remaining compatibility limits.

**Canonical current-data invariant approved:** the common immediate checker may
enforce the existing use-width payload contract: a known immediate type must
have a nonzero width of at most 64 bits and its payload bits must fit that
width. F32/F64 current words require absent integer-source metadata and clear
integer-negative flags, as the existing floating converters produce. Separate
64-bit integer-origin caches retain their current source-consistency checks.
Do not require erased lexical history for otherwise valid origin-free current
words or introduce a global compatibility mode/new semantic taxonomy. This
shared integrity enforcement needs focused evidence for existing unrelated
RegOrImm producers as well as ST mutations.

At design time, acceptance required actual-diff review, source/model/generator and
consumer checks, positive/negative numeric and qualifier boundaries, AST-free
copies/clones/current edits and malformed metadata, preserved C1 regressions,
fresh ptxas comparisons, and the unchanged natural histogram corpus. The
canonical converter and shared typed model are evidence to reuse, not permission
to infer every untested assembler cross-product or erase documented limits.

Verification-scope clarification: supplied-AST identity and canonical current
immediate data remain separate existing contracts. A coherent current edit of
both immediate bits and integer-origin cache need not fail validation against
the original AST merely because that data changed; a changed supplied source
identity is checked separately. Do not add exact original-value authentication
or erased lexical-history requirements to ST. Also retain the observed reversed
cache-order (`st.cg.global` and peers) difference separately: both register and
immediate controls encounter the pre-existing frontend ordering boundary, while
canonical `st.global.cg` controls exercise this extension. No additional modifier
order aliases are approved in this task.

## Final immediate-ST core acceptance

**Verdict: ACCEPT**, against baseline
`1b44ad8212aa254610b3afb447c9e774df03710d` on the audit branch. The accepted
cumulative tracked `git diff --binary` SHA-256 is
`41e6c01ec6c9c8c7a853dca9564fed4a0ef956e8fb7e71f053bca676720c98ba`;
the separately untracked new
[`test_st_immediate.cpp`](../../submod/resolved_ir/test/test_st_immediate.cpp)
SHA-256 is
`249cfb0dfc9b521a0a7e6c366773831ffa3f321eda5527da093c7cdf45af60ec`.
The scope comprises thirteen tracked source/test/documentation/consumer files
plus that new test. It preserves the previously accepted C1 behavior.
Reviewer role and selection remain Authority Sol, requested and accepted
`gpt-6.1-sol` / `high`; effective runtime telemetry is unexposed. The separate
Implementation Sol worker authored the implementation. This reviewer inspected
the actual final diff and did not author implementation code. No blocking
finding remains for these inputs.

The actual YAML changes exactly six ordinary synchronous scalar ST data-source
patterns to existing `RegOrImm`; the ten scalar variants inherit their existing
type, space, address, qualifier, version and target rules. Vector and auxiliary
policy fields retain their prior producers. The shared checker enforces
representable canonical payload widths and ordinary floating-origin metadata,
after the established half/FP8/packed checks so their precise diagnostics retain
priority. The existing numeric converter remains unchanged. Source expressions
are owned immediate values with their original integer cache and written range;
no temporary or extra instruction is inserted. Generated public scalar source
fields are now `WithLocs<RegOrImm>`, including fields inside selected operand
layouts. SDK and paired language design documentation state the explicit NVIDIA
extension, exact numeric producer bounds, remaining differences, and required
consumer rebuild/union-arm migration. The extension block is under the actual
ST contract, not the LD contract.

Five initial full-suite failures were independently adjudicated and repaired
without removing unrelated coverage. The two former scalar-ST rejection cases
became positive coverage, with unchanged vector source restrictions retained as
negative controls. The ADD type-error fixture now removes an unrelated stale
integer cache to isolate its intended type diagnostic. The fixed-value helper
test rejects malformed float-origin data, then separately checks canonical raw
word membership. The packed FP8 case retains its precise original diagnostic
because specialized checks precede the new general guard. No valid packed
producer or existing packed expectation was weakened.

Final verification independently read and checked:

- [`cpp-full-final.log`](ptx93_st_immediate/cpp-full-final.log): **2,081 resolved
  C++ tests**, **2,079 passed**, two existing instrumentation-dependent skips,
  no failures. The named skipped view tests are explicitly retained in the log.
- [`cpp-repaired-focused-final.log`](ptx93_st_immediate/cpp-repaired-focused-final.log):
  all twelve repaired/new ST cases pass, covering typed numeric producers,
  malformed current metadata, ownership/copy/assignment/clone, existing numeric
  roles and parameter/call rules. Selected Python checks report 132 passes plus
  the final model-method pass. The complete Python suite was not claimed.
- [`consumer-ctest-final.log`](ptx93_st_immediate/consumer-ctest-final.log): the
  genuinely registered installed consumer test passes **1/1**, exercising both
  source union arms after AST release while retaining optional-policy checks.
- [`frontend-comparison.json`](ptx93_st_immediate/frontend-comparison.json):
  all 422 source hashes match retained fixtures and all frontend processes have
  normal exit codes. There are **333 accepted agreements**, **59 rejected
  agreements**, and no unclassified/tool failures. The remainder is explicitly
  classified: ten B128-immediate limits, four over-64-bit integer-source limits,
  eight reversed cache-order cases, two auxiliary-policy cases, and six
  unsupported assembler targets. This is not 422 passing language forms.
- The separate [fresh original comparison](ptx93_st_postfix/ptxas_results.json)
  retains all thirty original sources: all 28 supported-target cases agree,
  with the two historical targets still unavailable to the assembler.
- The [fresh real-input results](ptx93_st_postfix/real_input_results.json)
  retain the same fifteen fixture hashes. **All twelve unchanged natural nvcc
  modules now pass complete frontend validation and ptxas**, including the
  original histogram's five immediate stores. The external embedded control
  also validates; the two original upstream files retain their missing-debug-
  file binding failures, and all three retired targets remain unavailable to
  current ptxas. No source was edited to manufacture these results.

Final frontend binary SHA-256 is
`1eb00c8e9a8e0834d699f8ec08838a889fb3cb71d7a87e6a39cbb72544718402`;
final checker source SHA-256 is
`b7d58f8d5a2d67e104cffda9708c73583415874a737ece668934cfebffc3a9fb`.
The final comparison uses this actual binary. The recorded two-line Doxygen-only
rebuild preserves the probe and production archive byte-for-byte; raw earlier
run metadata is retained truthfully rather than rewritten as a different run.
The comment-only relation is documented in
[`comment-rebuild-identity.json`](ptx93_st_immediate/comment-rebuild-identity.json)
and the final revision packet. Formatting/diff checks pass. The fixed official
manual inventory and finite parser-example outcomes remain separately bounded;
neither successful assembly nor these tests establish GPU runtime behavior.

Acceptance is limited to this exact cumulative diff, new test and approved
bounded compatibility contract. It does not adopt B128 immediate representation,
arbitrary-precision literals, auxiliary immediate policies, reversed modifier
aliases, vector immediate stores or every NVIDIA extension. Material later
semantic changes require focused independent review. No commit, push or PR is
authorized by this acceptance.
