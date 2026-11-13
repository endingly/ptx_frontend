# Frontend fuzz expansion: architecture and acceptance review

Baseline: `c77d9bc5f580030a2c26c72ab5888eab96fe4011`, committed and pushed on
`audit/ptx93-manual-conformance` before fuzz work. The preceding PTX audit/fix
records remain historical evidence and are not modified by this review.

The user authorized expanding fuzzing on this branch after that push. Reviewer
assignment is Authority Sol, requested and accepted `gpt-6.1-sol` / `high`;
effective runtime telemetry is not exposed. The implementation owner is a
separate Implementation Sol worker. This reviewer authors only this review
record. Role and acceptance rules are in
[Sol](../../.agents/sol.md) and
[ptx-core-review](../../.agents/skills/ptx-core-review/SKILL.md).

**Architecture and the exact implementation below are accepted.** This decision
does not authorize a public semantic/API change or automatically accept a later
diff. Findings that require production behavior changes need focused semantic
adjudication and independent actual-diff review.

## Existing boundary inspected

`submod/cst/fuzz/lexer_cst_fuzz.cpp` checks finite raw lexing and lossless CST
source text. It does not currently fuzz syntax lowering, binding, declaration
semantics, resolution or owned validation. `submod/cst/CMakeLists.txt` exposes
the default-OFF Clang-only `PTX_FRONTEND_BUILD_FUZZERS` option, requires a
separate `BUILD_TESTING=OFF` tree, and instruments lexer/CST with
`fuzzer-no-link` only. Existing ordinary CTest smoke/depth/recovery tests remain
useful and must not be removed or weakened.

The reviewed public routes are `PtxSyntaxParser::parseInstruction/parseModule`,
`resolveInstruction`, the module resolution APIs and both `validateModule`
overloads. `ResultWithDiagnostics` can contain a recovered module AST together
with diagnostics. `resolveModuleOnly` defers final checking; targetless or
available-context success is not strict validity. Default strict validation can
correctly return `MissingValidationContext`. These differences constrain the
fuzzer's invariants, rather than providing a reason to reject arbitrary inputs.

## Approved bounded stage

Retain `fuzz_lexer_cst` and add a `fuzz_frontend` target for arbitrary bytes through
syntax AST instruction/module parsing and the complete module
parse/bind/declare/resolve/check path. A clean AST may proceed to semantic
resolution; recovery diagnostics remain normal and need not be resolved as a
complete source module. Instruction-only parsing/resolution may exercise
fragment APIs without claiming module validity.

Strong semantic ownership checks are conditional on **successful strict
resolution/validation**, with the same `RequireCompleteContext` policy and
retained module configuration. After that condition holds:

- Validate the owned module after source, parser and AST storage have died.
- Copy, assign and move owned modules and revalidate them under that same
  policy/context.
- Clone instructions into an owned module copy, retain their operation
  identities and revalidate the resulting module. Avoid a standalone check
  under a different default context as a substitute for module validation.

Exercise the AST ownership boundary too: destroy the parser and its owned input
storage before resolution consumes the AST. The existing parser owns a source
copy, so keeping only the original libFuzzer input buffer alive must not mask a
borrow from parser storage. Source/AST ownership and owned-IR ownership are
separate checks.

Parse, recovery, unknown opcode, binding, declaration, operand, target and
availability diagnostics are expected outcomes. Do not assert arbitrary source
must validate, require two different context policies to agree, authenticate
every current value against erased lexical history, or treat a deferred draft
as a valid complete program. Unexpected exceptions, sanitizer failures and
actual invariant failures must surface as findings; do not blanket-catch them
and relabel them as expected source diagnostics.

This stage does not add arbitrary public-IR mutation, a new semantic taxonomy,
new public AST/IR API, grammar-directed generation framework or global
compatibility mode. It does not invoke ptxas per mutation or use the shared
generator model as an independent correctness oracle. Finite assembler
comparisons remain a separate optional workflow.

## Build and instrumentation contract

Keep a dedicated opt-in Clang/libFuzzer build with ASan and UBSan. Instrument
the implementation of base, lexer, CST, syntax, binding, semantic and resolved
IR, including generated resolved C++ sources, with coverage and sanitizers.
`common` is header-only/INTERFACE: instrument its inline code through compiled
consumers without adding sanitizer options to its exported interface.

Library compile instrumentation uses `fuzzer-no-link`; intended fuzz executables
link the libFuzzer and sanitizer runtimes. Sanitizer errors must not recover
silently. Preserve ordinary GCC/default-OFF builds and the separate
`BUILD_TESTING=OFF` fuzz-tree restriction. Do not leak fuzz/sanitizer flags into
installed public interfaces or silently ship an instrumented package as the
ordinary SDK. No minimum Clang version beyond actual required capabilities is
invented; record the selected compiler, standard library, linker and runtime
versions and verify them concretely.

The frontend harness has a documented 64-KiB raw input budget. This is a fuzz
resource bound, not a language size limit or PTX rejection rule. Existing parser
tree/block guards remain observable; do not add per-token structural skips that
hide bugs. Bound campaigns with explicit input length, per-input timeout,
memory/RSS and total-run/time settings, and preserve the actual invocation and
completion count.

## Corpus, replay and useful canaries

Use tiny legal and malformed seeds plus existing project-authored MIT natural
PTX directories such as `corpus/m12` and
`corpus/ptx93_real_inputs/generated`. Read those source directories without
changing or duplicating their fixtures. Mutable coverage corpus, minimized
findings and libFuzzer artifacts belong in scratch/output directories. Do not
implicitly seed raw historical upstream directories or add unreviewed mutation
artifacts to the repository.

Provide deterministic seed replay and a bounded fixed-seed local/CI smoke
invocation for both targets. A small internal test-visible stage result or
equivalent canary is approved; it is not a public SDK API. Known valid versioned
seeds, including C1 whole-vector and immediate-ST cases, must demonstrably reach
successful strict validation and the AST-free owned/copy/clone checks. Merely
asserting the fuzz entry returns zero is insufficient: that could pass while
all deep work is skipped. Malformed bytes, recovery and boundary-depth seeds
must also be replayed as expected diagnostic inputs.

Keep CI smoke bounded and reproducible; do not make a long campaign or an
assembler download/subprocess per mutation a mandatory PR gate. Record new
findings with exact bytes, sanitizer/invariant output, compiler/build identity
and a deterministic minimized replay. Promote a repaired finding to a focused
regression only after its actual contract is established. A clean finite run is
not proof of absence of bugs or PTX conformance.

## Acceptance criteria

Before acceptance, inspect the actual diff and verify:

1. The invariants above use the documented public API/context guarantees and
   the positive canary proves deep-path reachability.
2. Clang compile/link commands demonstrate coverage, ASan and UBSan on every
   compiled frontend layer and generated resolved sources; sanitizing only the
   entry-point translation unit is insufficient.
3. Deterministic seed replay and bounded campaigns complete for both targets
   without sanitizer/invariant findings, or findings are preserved and resolved
   under a separately reviewed contract.
4. The ordinary GCC/off build and meaningful harness/regression smoke checks
   still work. Reuse valid existing results; expand tests only for concrete
   changed behavior or unresolved risk.
5. Corpus inputs remain unchanged, scratch outputs stay separate, and docs/CI
   state the actual finite scope, commands and resource limits.

Implementation acceptance is limited to the final record below. Technical
approval does not itself authorize a further commit, push or publication.

## Final actual-diff review

**Verdict: ACCEPT; no blocking findings.** Reviewed against
`c77d9bc5f580030a2c26c72ab5888eab96fe4011` on the audit branch. The accepted
24-file implementation delivery is identified by:

- Tracked `git diff --binary` SHA-256:
  `836593861c98eb84b0c644e76499b3ec27eb9e32f82af08682c31ff3f3932aaa`.
- Delivery content identifier:
  `1692524328780836381450cdfcc87bdedc52a36ffffbed2a9dabfd64f436d379`.
- Exact file list and per-file hashes in
  [`delivery.json`](frontend_fuzz_validation/delivery.json). All 24 source hashes
  were independently checked against the workspace. This review record and
  archived validation evidence are separate from that implementation manifest.

The verifier's worktree snapshot includes an earlier draft hash of this review
record. That snapshot remains historical; the accepted implementation identity
is the separate 24-file delivery, excluding this final review append.

The reviewer independently inspected the actual CMake, harness, canary, runner,
workflow, seeds/dictionary and paired documentation changes. The reviewer did
not author implementation code. The retained Authority Sol assignment has the
requested and accepted `gpt-6.1-sol` / `high` selection; effective runtime
telemetry remains unexposed. The source implementation was authored by the
separate Implementation Sol worker, and final runtime verification by a
separate read-only Luna verifier supplied the concrete evidence inspected here.

The implementation follows the approved contract. Only clean module ASTs enter
strict resolution. Source/parser storage dies before AST consumption; AST
storage dies before owned validation. Each module copy, assignment and move is
checked after its producing owner is released. Function copies/moves and
independent instruction clones are revalidated in the same owned module and
`RequireCompleteContext` policy. Ordinary diagnostics are accepted as normal
outcomes; the harness does not assert ISA validity for arbitrary inputs, compare
different policies or swallow unexpected exceptions. The positive C1/ST canary
uses actual source forms and requires one copied function and two instruction
clones per case, so empty/skipped deep work cannot satisfy it.

The default-OFF build retains the lexer/CST target, supports ordinary GCC/off
configuration and adds no installed public API. All seven compiled frontend
layers have private coverage/ASan/UBSan/nonrecovering flags, including generated
resolved sources; common inline code is instrumented through consumers. Export
files contain no sanitizer/fuzzer options or fuzz executables. The runner limits
scratch to descendants of the ignored root `out` directory, preserves source
seed inputs, records actual initial scratch state, hashes the executable/build/
runner inputs, bounds subprocesses and retains failures rather than converting
them into source rejection. Hosted workflow actions are pinned and campaigns
bounded; its 45-minute job ceiling accounts for cold compilation separately
from the short campaigns.

## Validation independently inspected

The [validation report](frontend_fuzz_validation.md) and its retained raw
evidence record the concrete local run. This reviewer read the logs and
metadata, checked all 32 original seed hashes, checked the six actual binary/
build/runner hashes against run metadata, and inspected the compile commands.

- Clang and LLD **19.1.7**, using system GCC **14.2.0** libstdc++, built the
  three executables. A final incremental build returned zero with no work
  remaining. The compile database contains 332 object commands; all **327
  implementation objects** across base, lexer, CST, syntax, binding, semantic
  and resolved IR carry the required flags. This includes **290 generated
  resolved-IR objects**. Link commands confirm ASan+UBSan+libFuzzer for both
  fuzz targets and ASan+UBSan without a fuzzer main for the canary.
- The sanitized canary completed successfully, proving two strict ownership
  paths and replaying rejected, malformed, depth and input-budget cases.
- Both deterministic replays completed with zero return codes. The engine
  reports 33 replay executions from **32 source seed files**; those are not 33
  independently authored PTX obligations. The largest source seed is 9,540 bytes.
- With fixed seed 93, a 65,536-byte mutation cap, five-second per-input timeout,
  2,048-MiB RSS limit and a 20,000-run/90-second per-target budget, both campaigns
  reached **20,000 runs** and returned zero. Lexer/CST took **54.940 seconds**
  with final coverage/features **2,859/13,190** and peak observed RSS 456 MiB.
  Frontend took **52.889 seconds** with **22,459/71,764** and peak RSS 793 MiB.
  No sanitizer/invariant findings, error markers or crash artifacts were found.
- The ordinary GCC canary passed CTest **1/1**; the affected CST **22**, syntax
  **4**, and owned/ST **13** regression checks also passed. Existing production
  semantics were not changed, so no unrelated full-suite replay was required.
- Original source seeds and their copied initial scratch inputs retain their
  recorded hashes. Discovered mutations remain in ignored scratch. Diff and
  formatting checks pass.

These are finite local robustness checks, not semantic conformance percentages
or proof of absence of bugs. Most arbitrary mutations may correctly fail before
strict validation; the canary separately proves the ownership path is reached.
The 64-KiB harness budget is not a new frontend language-size limit. No ptxas
process or shared generated model is used as an independent per-input oracle.
The hosted Clang 21 workflow was reviewed as configuration but was **not run**
in this session; local runtime evidence is Clang 19.1.7.

Acceptance is limited to the exact delivery and contract above. Material later
changes or new findings require focused review. The earlier audit/fixes were
committed and pushed as `c77d9bc5`; the new fuzz delivery remains uncommitted at
this review. Technical acceptance does not itself authorize another commit,
push or publication.
