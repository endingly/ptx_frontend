# Scalar ST immediate follow-up validation

The frontend now accepts immediate data in ordinary synchronous scalar stores,
including the five original `st.shared.b32 [register], 0` instructions in the
unmodified nvcc `histogram_sm_100.ptx` fixture. All twelve natural compiler
outputs in the [real corpus](../../../corpus/ptx93_real_inputs/README.md) now
pass full parsing, resolution and validation, and all twelve assemble with
NVIDIA ptxas V13.3.33.

This is an explicit compiler/assembler compatibility extension. The archived
PTX ISA 9.3 ST Notes still say the data operand must be in `.reg`; that manual
wording has not been reinterpreted as permitting immediates. The maintained
[SDK ST contract](../../sdk_support.md#st-coverage--contract) states the exact
producer domains, API migration and retained limitations. The
[earlier real-input results](../ptx93_real_inputs/README.md) and
[C1 validation](../ptx93_postfix/README.md) remain historical observations before
this separately authorized extension.

## Implementation and review

Six canonical scalar operand patterns now use the existing `reg_or_imm` role.
The ten generated scalar variants reuse the existing typed numeric converters
and `RegOrImm` representation. No temporary register or replacement store is
introduced. Integer data retains selected-width bits and evaluated integer
origin; floating data follows the existing numeric conversion and exact-word
rules. Common owned validation rejects excess payload bits and contradictory
floating-origin metadata.

Consumers of scalar ST data fields must handle the register and immediate
alternatives and rebuild with matching headers and libraries. Selected
cache-policy layouts retain their existing layout selection. See the paired
[English](../../us-en/resolved_ir_design.md#scalar-store-immediate-representation)
and [Chinese](../../zh-han/resolved_ir_design.md#标量存储立即数表示) design notes.

The [independent core review](../ptx93_semantic_review.md) accepts the final
source, API and regression changes. The final revision identity and checks
are pinned in [revision.json](revision.json); an earlier C1 acceptance does not
cover this extension. No commit, push or publication is part of this delivery.

After the final runtime runs, only two checker Doxygen lines changed. The
[rebuild identity record](../ptx93_st_immediate/comment-rebuild-identity.json)
confirms identical probe and production-library bytes. The revision record
preserves the run's original checker hash and verifies that reversing those
comment edits reproduces it; the raw run metadata is unchanged.

## Regression and public API verification

The full Resolved IR executable ran **2,081 tests from 275 suites: 2,079 passed,
two existing tests skipped, zero failures**. The
[final full log](../ptx93_st_immediate/cpp-full-final.log) names both skips.
The seven new ST tests cover integer and floating producers, state spaces and
qualifiers, invalid public IR, register-only roles, owned lifetime, copying,
cloning, source identity and parameter call staging. A
[focused final selection](../ptx93_st_immediate/cpp-repaired-focused-final.log)
also passes all twelve selected new and repaired cases.

The initial full run is preserved as
[cpp-resolved-final.log](../ptx93_st_immediate/cpp-resolved-final.log), despite
its earlier filename. Its five failures were repaired: two stale scalar-ST
negative expectations now have scalar-positive and vector-negative controls;
two mutation fixtures now distinguish invalid floating-origin caches from
their intended type/constraint checks; existing precise packed-floating
diagnostics retain priority over the new generic guard. No valid packed
producer was removed.

Focused Python spec, normalization, model and generator checks passed
[132 tests](../ptx93_st_immediate/python-focused.log); the final generated
scalar-source API assertion also passed its
[model rerun](../ptx93_st_immediate/python-model-final.log). The
[installed public consumer](../ptx93_st_immediate/consumer-ctest-final.log)
passes 1/1 against matching generated headers and library. It reads immediate
and register ST sources after AST release and retains cache-policy negative
validation. The [format checks](format.log) pass for the tracked source tree
and new audit/test C++ files, including the final repairs; `git diff --check`
also passes.

These results cover the full Resolved IR test executable, focused Python
checks and one installed consumer. Other components' complete test suites and
Release configurations were not run for this extension.

## Fresh differential and real-input evidence

- The [scalar-store investigation](../ptx93_st_immediate/README.md) retains 422
  matched assembler/frontend cases, including register controls, integer and
  floating producers, state spaces, qualifiers, parameter call staging,
  target floors and malformed producers. The final comparison distinguishes
  333 accepted and 59 rejected agreements, 24 documented compatibility
  differences, and six cases unavailable in the current assembler's target
  set. There are no unclassified differences or frontend tool failures.
- All 30 [original comparison cases](ptxas_results.json) have the same input
  hashes as the baseline. Nineteen accepted and nine rejected current-target
  cases still agree; two retired-target cases cannot be adjudicated by this
  ptxas version.
- [Fresh real-input results](real_input_results.json) record all fifteen
  unchanged corpus fixtures. The twelve natural outputs now pass both tools.
  The embedded historical NVIDIA output also passes the frontend. The two raw
  historical matrix outputs still fail missing debug-file bindings; all three
  historical outputs target SM10/SM20, which this ptxas does not support.
  These are separate outcomes, not current-tool invalid-PTX verdicts.
- All 347 selected official-example parser outcomes are identical to the
  baseline: 340 clean parses and seven previously reviewed malformed official
  fragments. [The summary](example_summary.json) pins both probe and source
  identities and references the unchanged full source/selection record.
- The refreshed [manual inventory](inventory.json) still has 715 named
  sections, 213 instruction headings and 137 roots, with no missing canonical
  root. All 168 SDK archived-manual links resolve to existing anchors;
  [the missing-link list](missing_manual_links.tsv) contains only its header.

The 24 compatibility differences are ten B128 immediate cases, four integer
sources beyond the evaluated 64-bit domain, eight reversed cache/state-order
cases (both register and immediate), and two immediate cache-policy operands.
They remain explicit limits. Vector, asynchronous and multimem stores retain
their existing source contracts. Current assembler observations do not prove
historical assembler behavior, stored runtime values or GPU execution.

## Reproduce

Use the pinned compiler, dependency and probe build instructions from the
[initial probe record](../ptx93_probes/build-notes.md) and
[ST investigation](../ptx93_st_immediate/README.md). The production CMake
project is unchanged; the audit hook supplies the standalone probes and focused
test target.

```sh
python docs/audits/ptx93_ptxas/run.py \
  --ptxas /tmp/ptx93-nvcc/bin/ptxas \
  --frontend /tmp/ptx93-audit-build/ptx93_probe \
  --output-dir /tmp/ptx93-st-original-comparison
python docs/audits/ptx93_example_probes.py /tmp/ptx-isa-9.3-official.html \
  --probe /tmp/ptx93-audit-build/ptx93_parse_probe \
  --output /tmp/ptx93-st-official-examples.json
python docs/audits/ptx93_manual_inventory.py /tmp/ptx-isa-9.3-official.html \
  --repo . --output /tmp/ptx93-st-inventory
python tools/corpus/ptx93_real_inputs.py run \
  --frontend /tmp/ptx93-audit-build/ptx93_probe \
  --ptxas /tmp/ptx93-nvcc/bin/ptxas \
  --output /tmp/ptx93-st-real-inputs.json
```

These finite checks do not establish exhaustive PTX 9.3 conformance. Fuzzing
expansion and GPU execution are outside this change.
