# Frontend fuzz compiler-cache optimization

This change targets compilation in `Frontend fuzz smoke`. It preserves every
instrumented implementation layer, generated resolved-IR source, target, canary,
32-source seed replay and the two 2,000-execution/30-second campaign limits.
No frontend, public API, sanitizer or ordinary Linux build contract changes.
Baseline: `b2b7f714098f43356878c445c742827573ce79e8` (merged PR #308).

## Hosted evidence and scope

The [latest completed baseline fuzz run](https://github.com/endingly/ptx_frontend/actions/runs/43446635890) took
37:06, including 35:44 building fuzz
targets and 17 seconds running its validation step. It configured
`PTX_USE_CCACHE=OFF` and had neither persistent instrumented compiler objects nor
a trusted-main seed producer. The [prior fuzz run](https://github.com/endingly/ptx_frontend/actions/runs/43407884180)
took 32:31, including 31:12 building and 17 seconds validating. Both builds had
332 C++ object compilations, including 290 generated private resolved-IR files.
Existing [normal Linux production builds](https://github.com/endingly/ptx_frontend/actions/runs/43446635942)
already showed 323 hits and four misses (approximately 99% hits); this change
leaves those jobs alone.

There is no hosted before/after result for the new profile. Local measurements
below demonstrate correct reuse and invalidation of real instrumented project
compilations, not an end-to-end CI speedup. Initial profiles, compiler/recipe
changes and evicted entries still require full compilation. Main warming adds
one fully validated run per main push; its seed is usable only after success.

## Cache and trust boundaries

The fuzz job enables the existing `PTX_USE_CCACHE` launcher and stores only
ccache entries in `.cache/ccache-fuzz`. The `ccache-v1-...-frontend-fuzz-asan-...`
lineage includes OS, architecture, the installed compiler/tool identity, and a
SHA-256 recipe fingerprint over the fuzz workflow, shared setup action,
`cmake/frontend_fuzz.cmake`, `CMakeLists.txt`, and `vcpkg.json`. Hashing the whole
workflow is conservative: an operational-only edit can also create a cold
profile. Restore never broadens to another compiler, profile or ordinary
Debug/Release family. ccache's default source/header/argument checks remain on;
no object-tree restore or relaxed correctness settings are added.

After configure, build and every smoke phase succeed, PRs save an immutable
SHA-key snapshot under their PR merge ref. Successful main pushes save main
seeds. Exact hits skip uploads; manual runs only restore. The build job explicitly
uses `cache-mode: write`, matching existing Linux jobs, with `contents: read`.
GitHub's current [workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#jobsjob_idcache-mode)
explains that omitted low-trust defaults can silently skip cache saves. Cache
writes do not grant the build job `actions: write`.

A separate main-push-only job holds `actions: write` and reuses the trusted-main
cleanup helper. It verifies the current seed and its exact prefix/ref/opaque
Actions version before retaining current plus previous seeds. Main runs share a
non-cancelling concurrency group, including cleanup. Successful fuzz PR runs
join the existing trusted PR maintenance subscription, retaining two snapshots
per exact lineage/version. Closed-PR cleanup remains unchanged. These jobs do
not execute PR source or artifacts with write permission.

## Capacity evidence

The retained local instrumented build contains 332 object files totaling
1,435,012,296 bytes (about 1.34 GiB), with a largest object of 26,644,288 bytes.
This supports a conservative `CCACHE_MAXSIZE=2G` local cap (2,000,000,000 bytes,
about 1.86 GiB), rather than copying the normal job's 1G cap. Local Clang 19
objects are sizing evidence, not a prediction for hosted Clang 21.

The representative experiment's compressed cache and manifest storage is
recorded below. It is a small sample with several variants per TU, not an
estimate of the full cache archive. The configured cap is not a compressed
Actions transfer size. Current/previous retention bounds each main lineage to
two snapshots, and PR retention bounds each PR lineage to two; multiple active
PRs or obsolete profiles can still cause repository-quota eviction. Existing
cache usage was about 400 MB before introducing this profile, including main
ordinary Debug/Release archives of 314,142,878 and 52,631,211 bytes. Hosted archive
sizes, restore/save transfer times and cleanup counts must be assessed after
real runs; no cache deletion was executed during this implementation.

## Local cache-correctness experiment

Tools: Debian Clang/LLD/compiler-rt 19.1.7, GCC 14 libstdc++ headers, ccache
4.11.2, fmt 10.1.1 and magic_enum 0.9.7. Dependencies were unpacked or installed
under `/workspace/.ptx-ci-tools`; retained Clang provenance is in
`/workspace/.ptx-fuzz-toolchain/PROVENANCE.txt`. No system package installation or
system APT repository configuration changes were needed. CMake 4.4.5/Ninja 1.13.2 and Python 3.14.10
reconfigured the existing `out/build/fuzz-asan` tree with the ccache launcher.

The experiment extracted real commands from that tree's original compilation
database, preserved `-O1 -gline-tables-only -DNDEBUG -std=gnu++23` and
`-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all
-fno-omit-frame-pointer`, and substituted only scratch source/output paths and
restored dependency include paths. Each measured compile deleted its output
first and invoked ccache plus the actual compiler; warm measurements are not
Ninja no-ops. The cache started empty. Inputs were copies, including a shadow
copy of the real `ptx_target.hpp` dependency, leaving tracked sources intact.

| Instrumented translation unit | Cold seconds | Warm seconds | Changed source miss | Changed header miss | Effective `-O0` miss | Subsequent warm |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `ptx_target.cpp` | 1.0022 | 0.0070 | 0.6255 | 0.6902 | 0.6712 | 0.0080 |
| `resolved_ir_arithmetic_add.gen.cpp` | 10.1726 | 0.0137 | 11.7946 | 12.3944 | 9.2422 | 0.0345 |
| `frontend_fuzz.cpp` | 13.7710 | 0.0164 | 12.4430 | 14.2645 | 9.0549 | 0.0266 |

Every cold/source/header/effective-optimization case recorded one cache miss;
each immediately repeated case recorded one direct hit with identical object
SHA-256. The source/header probes append a real `static_assert`, rather than a
comment. The optimization probe places `-O0` after the original `-O1`, and its
object hash differs from the O1 header-change object. Rewriting the shadow header
with identical bytes and a new mtime still produced a direct hit for all three
TUs (0.0074/0.0266/0.0292 seconds). An unused `-D` probe correctly reused the
preprocessed result, demonstrating that a command-line change need not change
compiler output. No sloppiness or path-safety relaxation was enabled.

The 33 retained compilation measurements total 12 misses and 21 hits. The final
scratch cache occupied 6,052 KiB with zero cleanups; this includes preliminary
flag-probe entries as well as the measured variants. These sample bytes cannot
be scaled into a hosted archive-size or whole-build speedup claim. No full cold
rebuild was repeated.

Raw local evidence is under ignored `out/ci-fuzz-cache-proof/`: `run.py`,
`extra.py`, exact commands/counters/durations/object and input hashes in
`results.json` and `extra-results.json`, `ccache-config.txt`, final statistics,
original compilation/configuration files, the reconfigured Ninja commands,
source hashes, and tool-install/configure logs. The probe's uncompiled
file-existence control rows are not included in compilation counts or timings.
The installed Debian package filenames/versions/SHA-256 values are recorded in
`/workspace/.ptx-ci-tools/packages.json`.

## Validation and limits

The same existing build tree configured successfully with `PTX_USE_CCACHE=ON`.
All 332 emitted Ninja C++ commands use the ccache launcher and retain the exact
O1/private sanitizer flags. The layer counts are base 3, lexer 2, CST 8, syntax 3,
binding 1, semantic 2 and resolved IR 308, plus five fuzz/canary compilation
entries. Both fuzz executables still link libFuzzer/ASan/UBSan; the standalone
canary still links ASan/UBSan without the libFuzzer main. No production source,
generator, harness or `cmake/frontend_fuzz.cmake` changed.

The preserved instrumented binaries completed a fresh scratch run with the same
source baseline and `python3 fuzz/run_smoke.py --runs 2000 --seconds 30 --scratch
out/ci-fuzz-cache-smoke`: canary 0.268 s, lexer/CST replay 0.166 s and campaign
4.537 s, frontend replay 1.217 s and campaign 5.935 s, all return code zero.
The 32-source seed manifest, binary/configuration hashes, exact commands,
sanitizer environment and logs were freshly recorded in
`out/ci-fuzz-cache-smoke/`. Smoke metadata pins the retained original pre-launcher
`out/ci-fuzz-cache-proof/original-CMakeCache.txt` and
`original-compile-commands.json` snapshots (SHA-256 prefixes `c6319e67` and
`8ddb3453`); the later reconfigured Ninja commands separately prove the launcher,
and the preserved executable hashes match the smoke metadata. This reuses previously built binaries; it does not
claim a fresh full instrumented build, new coverage total, or another 40k campaign.

Five new focused Python tests pass against the actual workflow conditions:
PR/main/manual event and exact/prefix/miss save eligibility, failed/skipped
validation blocking, recipe/namespace isolation, trusted cleanup/concurrency,
and mixed ordinary/fuzz ref/version retention. The real metrics helper also
successfully captured the local cache's JSON. Action pins, YAML parsing, shell
syntax and `git diff --check` pass. Actionlint 1.7.11 passes with only its outdated
`ubuntu-26.04` runner-label and `cache-mode` key diagnostics explicitly ignored;
those current repository/platform features were checked separately. ShellCheck
is unavailable, so actionlint's optional ShellCheck integration was disabled.

No workflow was dispatched, hosted cache deleted, or Git staging/commit/push/PR
performed for this task. Actual hosted reuse, compressed transfer cost, first-seed
latency and quota pressure remain to be observed after normal authorized runs.
