# Frontend fuzz expansion: local validation

The branch `audit/ptx93-manual-conformance` contains a new full-frontend fuzz
target alongside the existing lexer/CST target. Both completed 20,000 local
executions with ASan/UBSan enabled and no reported findings. These are bounded
robustness checks, not a PTX semantic-conformance proof.

## Revision and scope

The previously accepted PTX audit and immediate-store work was committed and
pushed as `c77d9bc5f580030a2c26c72ab5888eab96fe4011` before this expansion.
New fuzz changes remain uncommitted on that same branch.

[delivery.json](frontend_fuzz_validation/delivery.json) pins the 24 implementation,
seed, workflow and maintained support files. Their tracked diff SHA-256 is
`836593861c98eb84b0c644e76499b3ec27eb9e32f82af08682c31ff3f3932aaa`;
the complete delivery-content SHA-256 is
`1692524328780836381450cdfcc87bdedc52a36ffffbed2a9dabfd64f436d379`.
Review and validation records are separate evidence files. The independent
[core review](frontend_fuzz_review.md) owns design and final acceptance.

`fuzz_frontend` parses instruction/module ASTs, then sends clean module ASTs
through binding, resolution and strict validation. Expected diagnostics return
normally. ASTs must survive parser/source destruction; after strict success,
owned IR must survive AST destruction and module/function copy, assignment,
move and instruction clone operations under the same complete-context policy.
Input is capped at 64 KiB. The internal canary requires actual function and
instruction clone counts for a whole 16-bit hardware-vector MOV and an immediate
scalar ST, alongside malformed, recovery, depth and size-limit cases.

Public semantics and SDK APIs are unchanged. Public-IR mutation and an assembler
subprocess per generated input are outside this stage.

## Build and instrumentation

Local tools were Clang/LLD 19.1.7 with compiler-rt and GCC 14.2 libstdc++ headers,
using an isolated user prefix; [package provenance and hashes](frontend_fuzz_validation/toolchain-provenance.txt)
are retained. The Release fuzz build uses `-O1 -gline-tables-only`,
`BUILD_TESTING=OFF`, private `fuzzer-no-link,address,undefined` instrumentation,
nonrecovering sanitizer checks and frame pointers.

All 327 library implementation translation units across base, lexer, CST,
syntax, binding, semantic and resolved IR carry these flags. This includes all
290 generated resolved-IR sources; common inline code is instrumented through
its consumers. Five harness/canary translation units bring the compile database
to 332 entries. Both fuzz executables link libFuzzer/ASan/UBSan; the standalone
canary links ASan/UBSan. The generated SDK exports contain no sanitizer options
or fuzz executables. [Captured Ninja commands](frontend_fuzz_validation/ninja-commands.txt)
and the [independent verification summary](frontend_fuzz_validation/verification-summary.json)
retain the evidence and binary/configuration hashes.

The explicit final target build succeeded with `ninja: no work to do`, followed
by successful execution of all three binaries. The exit status of the earlier
detached cold-build session was unavailable; the explicit final build confirms
the complete target graph is current. No hosted CI run is claimed.

## Observed runs

The helper used seed 93, at most 20,000 executions or 90 seconds per campaign,
a 64 KiB mutation length, five seconds per input and a 2 GiB RSS limit.
All five helper phases exited zero. The canary completed in 0.569 seconds.

| Target | Initial replay | Campaign executions | Campaign wall time | Final coverage / features | Peak RSS |
| --- | --- | ---: | ---: | ---: | ---: |
| lexer/CST | 32 seeds, 33 calls, passed | 20,000 | 54.940 s | 2,859 / 13,190 | 456 MiB |
| full frontend | 32 seeds, 33 calls, passed | 20,000 | 52.889 s | 22,459 / 71,764 | 793 MiB |

Coverage and feature counts are libFuzzer runtime measurements, not opcode,
grammar or semantic coverage percentages. No sanitizer/crash markers or failure
artifacts were observed. The original 32 seeds comprise nine tiny fixtures,
five existing CST fixtures and 18 project MIT natural compiler outputs. All
original hashes and both initial scratch corpora matched; source files were
unchanged. The largest original seed was 9,540 bytes.

Retained raw evidence includes the [canary](frontend_fuzz_validation/canary.log),
[lexer replay](frontend_fuzz_validation/fuzz_lexer_cst-replay.log),
[lexer campaign](frontend_fuzz_validation/fuzz_lexer_cst-campaign.log),
[frontend replay](frontend_fuzz_validation/fuzz_frontend-replay.log),
[frontend campaign](frontend_fuzz_validation/fuzz_frontend-campaign.log),
[commands and outcomes](frontend_fuzz_validation/summary.json),
[runtime identities](frontend_fuzz_validation/run-metadata.json),
[source seeds](frontend_fuzz_validation/seeds.json) and
[initial corpus hashes](frontend_fuzz_validation/initial-corpora.json).
The verification summary snapshots the review document before its final
acceptance append; that historical document hash is separate from the pinned
delivery files.

The ordinary GCC build passed the new [CTest canary](frontend_fuzz_validation/fuzz-gcc-canary.log)
and 39 focused existing regressions:
[22 CST](frontend_fuzz_validation/fuzz-gcc-cst.log),
[four syntax](frontend_fuzz_validation/fuzz-gcc-syntax.log), and
[13 ownership/ST](frontend_fuzz_validation/fuzz-gcc-regressions.log).
The four new C++ files pass clang-format 21 and `git diff --check` passes.
Other complete component suites were not repeated for this harness-only change.

## Reproduce and continue

The aligned [English](../us-en/fuzzing.md) and [Chinese](../zh-han/fuzzing.md)
guides describe dependencies, isolated builds, budgets, replay and minimization.
With the recorded build available, the exact local campaign command is:

```sh
python fuzz/run_smoke.py --build out/build/fuzz-asan \
  --scratch out/fuzz-local-verified --runs 20000 --seconds 90 --seed 93
```

Use a fresh scratch path for the same initial corpus. Reusing it retains
discoveries; the helper records every actual initial file/hash. Mutated corpora
remain under ignored `out/`. The new CI workflow uses 2,000 runs or 30 seconds
per target and a 45-minute job ceiling, retaining failure evidence for seven
days. Its configuration was checked locally; hosted execution awaits publication
of the new fuzz changes. GPU execution, long campaigns and exhaustive semantic
equivalence were not performed.
