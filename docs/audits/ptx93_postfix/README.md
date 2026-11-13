# Post-fix validation of PTX 9.3 whole hardware-vector MOV

**Status: C1 fixed and independently accepted for the reviewed patch.** The
original [baseline audit](../ptx93_manual_audit.md) and
[assembler comparison](../ptx93_ptxas/report.md) remain pre-fix evidence. This
record describes the changed working tree and does not overwrite those results.

## Exact revision and accepted scope

The base commit remains `1b44ad8212aa254610b3afb447c9e774df03710d` because no commit
was requested. SHA-256 of the five-file tracked `git diff --binary` is
`9fa8101ebfcbaa5ee8750185c4362e1b4ff5c4730ccd92b0b82f99cfcfed301d`.
[revision.json](revision.json) pins every changed file, the checker and binary
hashes, final official-manual fetch, and all result identities.

The checker reuses the existing instruction-local compatibility descriptor.
Four-lane whole `%tid`, `%ntid`, `%ctaid`, and `%nctaid` sources now support
B16/U16/S16 reads, including their existing PTX 1.0 compatibility floor. Native
U32 identity and owned metadata stay unchanged. Existing B32/U32/S32 reads keep
their original rules; cluster vectors gain no 16-bit exception. Provenance,
arity, type, version, target and capability rejection remain checked.

The independent [core acceptance record](../ptx93_semantic_review.md) accepts
this exact tracked diff. It does not authorize a commit, push or publication.

## Regression and differential checks

- New positive ownership and malformed-IR tests failed against the baseline
  gate: [focused_red.log](focused_red.log). The full focused selection passed
  all 18 VectorMov/MovCompleteness tests after the fix:
  [focused_green.log](focused_green.log). The tests include 48 named/brace
  classic-vector and 16/32-bit source combinations, AST release, clone, floors,
  unsupported cohorts, and malformed owned-reference metadata.
- The same 30 assembler comparison inputs were regenerated and their hashes
  compared with the baseline. **All 28 supported-target comparisons agree**;
  the six former modern compatibility failures now assemble and validate.
  Modern ptxas cannot adjudicate the two SM10/SM20 cases. The historical U16
  frontend acceptance follows the reviewed descriptor/manual interpretation,
  not a claim of historical assembler execution. Exact outputs are in
  [ptxas_results.json](ptxas_results.json); fixture references point to the
  unchanged baseline input files.
- All 347 selected official-example parser results match the previous batch:
  340 clean parses and seven diagnostics attributable to malformed official
  example fragments. [example_summary.json](example_summary.json) retains the
  new run identity and references the full baseline source/selection record.
- A fresh official-manual download had the same SHA-256 as the initial archived
  PTX 9.3 document. The [structural inventory](inventory.json) remains 715 named
  sections, 213 instruction headings and 137 roots. These are inventory units,
  not conformance percentages.
- The corrected SDK has **zero missing anchors among its 168 archived-manual
  fragment links**. [missing_manual_links.tsv](missing_manual_links.tsv) is
  empty apart from its header. The stale BRKPT catalog wording was also fixed;
  its SM11 instruction floor is unchanged. English/Chinese design notes now
  explain contextual checking without rewriting intrinsic register identity.

## Real compiler inputs and observed limits

The new [real-input collection](../../../corpus/ptx93_real_inputs/README.md)
contains 15 original/compiler-produced PTX fixtures with source, compiler,
target, flags, upstream commit, license and SHA-256 provenance. It includes four
ordinary CUDA workloads compiled for three profiles, plus three frozen upstream
NVIDIA outputs. All 12 natural outputs regenerate byte-for-byte under the
recorded compiler/path conditions; all 15 fixture/source/license hashes verify.

The fixed full frontend accepts **11 of 12 natural outputs**; NVIDIA ptxas
13.3.33 accepts all 12. The remaining `-O0` histogram contains direct immediate
`st.shared.b32 [register], 0` sources, which NVIDIA tools permit even though
archived PTX 9.3 §9.7.9.11 says operand b must be in `.reg`. The exact natural
input and a minimal witness remain visible as a separate, unadopted NVIDIA
source extension. C1 does not silently widen store-source domains.

Of the three frozen upstream NVIDIA inputs, one complete embedded compiler
output passes. Two raw older outputs fail missing debug-file bindings; modern
ptxas cannot assemble any of their retired GPU targets. These observations are
not reclassified as successful validation or current-tool language rejection.
The final count is **12 frontend acceptances and three rejections among the 15
vendored inputs**. Full results and the independent boundary adjudication are
in the [real-input report](../ptx93_real_inputs/README.md) and core review.

## Reproduce

With the pinned dependencies described in the earlier probe build notes and
GTest 1.17 available, configure a separate build with
`CMAKE_PROJECT_TOP_LEVEL_INCLUDES` pointing to [focused-hook.cmake](focused-hook.cmake),
then build `ptx93_vector_tests`, `ptx93_parse_probe`, and `ptx93_probe` and run the
focused executable. Production CMake files are unchanged.

```sh
python docs/audits/ptx93_ptxas/run.py \
  --ptxas /tmp/ptx93-ptxas/cuda_nvcc-linux-x86_64-13.3.33-archive/bin/ptxas \
  --frontend /tmp/ptx93-audit-build/ptx93_probe \
  --output-dir /tmp/ptx93-postfix-ptxas
python docs/audits/ptx93_manual_inventory.py /tmp/ptx-isa-9.3-official.html \
  --repo . --output /tmp/ptx93-postfix-inventory
python docs/audits/ptx93_example_probes.py /tmp/ptx-isa-9.3-official.html \
  --probe /tmp/ptx93-audit-build/ptx93_parse_probe \
  --output /tmp/ptx93-postfix-examples.json
python tools/corpus/ptx93_real_inputs.py check --nvcc /tmp/ptx93-nvcc/bin/nvcc
```

Use local tool paths where different. No full C++/Python suite, GPU execution,
or fuzz expansion is claimed. The positive and negative finite checks and the
real-input outcomes do not establish exhaustive PTX 9.3 conformance.
