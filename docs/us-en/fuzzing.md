# Bounded frontend fuzzing

[Current support](../sdk_support.md) remains authoritative. This opt-in developer
build requires Clang, libFuzzer, ASan/UBSan runtimes, Flex, Python generator
dependencies, fmt and magic_enum. Use the same dependency/toolchain options as an
ordinary build (for example the vcpkg toolchain from the CI presets). On Linux,
`PTX_FRONTEND_LLD_LINKER` can explicitly select a compatible `ld.lld`.
Keep a separate build tree with `BUILD_TESTING=OFF`; normal GCC and installed SDK
consumers do not inherit instrumentation or fuzz runtime flags.

```sh
CC=clang CXX=clang++ cmake -S . -B out/build/fuzz-asan -G Ninja \
  -DPTX_FRONTEND_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE="-O1 -gline-tables-only -DNDEBUG" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build out/build/fuzz-asan --parallel 2 \
  --target fuzz_lexer_cst fuzz_frontend fuzz_frontend_canary
python3 fuzz/run_smoke.py --runs 2000 --seconds 60 --seed 93
```

`fuzz_lexer_cst` checks finite lexing and lossless CST input retention.
`fuzz_frontend` caps source input at 64 KiB, parses standalone instruction and
module syntax, and sends only diagnostic-free module ASTs through binding,
declaration/operand resolution and strict validation. Parse/recovery, binding,
type, unsupported-operation and missing-context errors are normal results.
Unexpected exceptions and successful-resolution invariant violations fail the
run. There are no per-token filters, per-input ptxas processes, or public-IR
mutation campaigns.

The AST must survive destruction of parser/source buffers before resolution.
After strict success, source/AST-free owned validation, module copy construction,
copy assignment, move construction/assignment, function copies/moves, and
instruction clones must preserve `RequireCompleteContext` validation. Draft or
available-context success is not used as a validity oracle. The internal
`fuzz_frontend_canary` proves both a versioned C1 hardware-vector `mov` and scalar
immediate `st` reach strict success and the ownership stages; it also replays
invalid bytes, recovery, missing context, depth limits and the input cap. The
same canary is registered with CTest in ordinary `BUILD_TESTING=ON` builds.

Implementation instrumentation includes base, lexer, CST, syntax, binding,
semantic and resolved IR, including generated resolved sources; common inline
code is instrumented in those consumers. Flags are private and do not change the
exported target interfaces. Sanitizer-built libraries belong to the isolated
fuzz build; use a normal build for SDK installation.

The helper copies tiny hand-authored `fuzz/corpus` seeds, existing CST recovery
seeds and the project MIT natural inputs in `corpus/ptx93_real_inputs/generated`
and `corpus/m12` to ignored `out/fuzz-smoke`. Original corpus files stay read-only.
It runs the canary, deterministic initial-corpus replay (`-runs=0`) for each
fuzzer, then two fixed-seed campaigns, each limited to 2,000 executions or 60
seconds, 2 GiB RSS, five seconds per input and a 64 KiB mutation length.
The lexer/CST harness retains its existing larger input contract; the frontend
harness rejects oversized input before parsing. Parser tree depth is 128 and
nested block depth is 256, as documented in current support.

`run-metadata.json` records binary/runner/build-configuration hashes and the exact
ASan/UBSan environment. `summary.json` records exact commands, return codes and wall durations;
`seeds.json` records original source paths, sizes and SHA-256 hashes.
`initial-corpora.json` records every actual initial scratch file and hash for each
target, including retained discoveries when resuming. Logs,
mutated corpus and failure artifacts stay in scratch. For an identical initial
campaign, pass a fresh `--scratch out/fuzz-smoke-fresh`; reusing scratch retains
previous discoveries. A fixed seed/budget makes smoke reproducible on a given
build, but wall limits and coverage changes can alter generated mutations.
The dictionary is `fuzz/ptx.dict`.

Replay a failure using the exact binary and sanitizer environment:

```sh
ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  out/build/fuzz-asan/fuzz_frontend out/fuzz-smoke/fuzz_frontend/artifacts/crash-HASH
```

Preserve the failing bytes, command, log, toolchain and revision; minimize with
libFuzzer's `-minimize_crash=1 -exact_artifact_path=out/minimized` before changing
production semantics. A normal strict rejection is not a finding. Report
concrete minimized findings separately for semantic review.

The `Frontend fuzz smoke` workflow uses the existing Linux dependency setup and
an isolated Clang/ASan/UBSan build. Both campaigns are limited to 2,000 runs or
30 seconds each; the job has a 45-minute ceiling and retains failure logs,
corpus/artifacts and build configuration for seven days. This smoke does not
establish exhaustive PTX semantic coverage or replace deterministic regressions.
