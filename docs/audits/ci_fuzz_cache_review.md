# Fuzz CI compiler-cache review

## Assignment and scope

Authority Sol was explicitly selected as `gpt-6.1-sol` / `high`; effective
runtime model telemetry is not exposed. This independent design and eventual
actual-diff review applies the repository's
[core-review skill](../../.agents/skills/ptx-core-review/SKILL.md) and
[authority contract](../../.agents/sol.md). The reviewer does not author the
implementation. Baseline: `b2b7f714098f43356878c445c742827573ce79e8`.

The user authorized optimizing long CI runs. This scope does not authorize
publication or changing frontend semantics, public APIs, ordinary build
behavior, validation coverage, sanitizer flags, or fuzz budgets.

## Design decision

Approve a compiler-cache optimization for the existing sanitizer fuzz build.
Hosted runs `43446635890` and `43407884180` spent 35m44s and 31m12s building
fuzz targets; each replay/campaign step took 17s. The current workflow disables
ccache. This evidence supports optimizing compilation while retaining the full
validation sequence.

Use the existing CMake ccache launcher and an isolated cache directory and
lineage, partitioned by OS/architecture, installed compiler identity, and the
instrumented optimization/build profile. Compatible prefix restoration must
never include normal Debug/Release or incompatible compiler profiles. Retain
ccache's content, argument, dependency, and path validation; do not introduce
sloppiness or restore prebuilt object trees as a substitute for compilation.

PR snapshots may be saved under immutable SHA keys only after the build,
canary, seed replays, and both campaigns succeed. PR merge-ref caches do not
seed main or sibling PRs. A main-push invocation of the same workflow must
perform the same validation before publishing a trusted-main seed. Exact hits
need no new upload. Failed checks or missing/evicted caches must not bypass any
validation. Manual non-main runs must not publish trusted-main seeds.

Trusted-main retention uses a separate main-only cleanup job with narrowly
scoped write permission and the existing verified current-plus-previous helper.
Main concurrency must prevent overlapping seed publication/cleanup. Extend
successful-workflow PR maintenance to fuzz while preserving exact PR-ref and
opaque cache-version grouping. No PR-controlled source or artifacts execute in
the privileged cleanup job.

Retain all seven implementation layers and generated-source instrumentation,
three target/link modes, O1/debug flags, parallelism, canaries, source seeds,
replays, campaign budgets, runtime limits, and failure retention. Select cache
capacity from instrumented-object evidence. Reuse existing build/cache metrics
without allowing metrics collection to change the build's exit status.

## Acceptance evidence required

- Inspect actual compile/link commands for the unchanged sanitizer/coverage
  contract and private instrumentation; ordinary non-fuzz builds remain intact.
- Measure representative handwritten and generated implementation TUs with an
  empty cache and a warm cache after deleting outputs. Record commands, tool and
  configuration identities, counters, durations, object hashes, and storage.
  A Ninja no-op is not evidence of compiler-cache reuse.
- Show controlled scratch-only source/header/flag invalidation and subsequent
  warm hits. Preserve source trees and avoid a full cold rebuild solely to
  repeat already valid evidence.
- Check workflow save eligibility and trusted cleanup boundaries for PR, main
  push, manual/non-main, failures, exact hits, and compatible prefix hits. Test
  retention with mixed ordinary/fuzz lineages. Run applicable action-pin,
  workflow syntax, and focused helper checks.
- Retain real fuzz validation evidence against the delivered binaries, or
  document verified unchanged binary/source equivalence where reuse is valid.

## Final actual-diff acceptance

**Accept**, with no blocking findings. This verdict applies to the seven
implementation/test/documentation files below against the stated baseline,
reviewed independently from their author. Tracked `git diff --binary` SHA-256:
`8a7aafa424cf1b1d7d13d644ec3fe70b3614596a7710dbce5dd91145120aeb77`.
The per-file identities also pin the untracked tests and evidence report:

| File | SHA-256 |
| --- | --- |
| `.github/workflows/frontend-fuzz.yml` | `e8a6abfa1f98f404ab2bf9328ca09e89e919168bb81c1632d41ae2a01c65509a` |
| `.github/workflows/pr-cache-maintenance.yml` | `383565112ddd699eb68b5923a6d714991031ba6df9006d610cc2af4677649391` |
| `python/tests/ci/__init__.py` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `python/tests/ci/test_fuzz_cache.py` | `e0bd74dd7c384961351b1fca8f5a9b8ab32129c98ffe3ac1f6dfb6c2d643c353` |
| `docs/us-en/fuzzing.md` | `acef03287b881c424920ef67f90ec6098f6cc42f9667a8a3ead666fcd38c52fa` |
| `docs/zh-han/fuzzing.md` | `0ea549bdcfc0800d223e33561e1dc4861ea4609f8159e5b5e96736a6052a5b70` |
| `docs/audits/ci_fuzz_cache_optimization.md` | `74ed6133c34c0593a3a5f5ef4b6246743895ee282f193409fd912061c88a00f1` |

The actual workflow retains build/check failure propagation, exact-hit upload
avoidance, compatible-profile-only restoration, immutable PR/main snapshots,
and successful-main-only privileged cleanup. Its build job explicitly enables
scoped cache writes without acquiring `actions: write`. The maintenance workflow
continues to use trusted source and existing exact-ref/version retention helpers.
No semantic code, harness, generator, CMake instrumentation, or ordinary CI job
changed. Hashing the full workflow conservatively invalidates cache profiles
even for some operational changes; this is an acknowledged reuse limitation.

The reviewer independently ran the five workflow/retention tests under the
provisioned Python 3.14 environment, action-pin checking, and diff whitespace
checking. All passed. Unfiltered actionlint 1.7.11 produced only its known
`ubuntu-26.04` label and supported `cache-mode` syntax-version diagnostics;
no unrelated workflow diagnostic was hidden by those exceptions. Optional
ShellCheck was unavailable.

Independent inspection of retained commands confirms all 332 compile entries
use ccache and preserve O1/debug/coverage/ASan/UBSan/no-recover/frame-pointer
flags. Both fuzz links retain libFuzzer/ASan/UBSan, and the standalone canary
retains ASan/UBSan without libFuzzer main. All 331 recorded unique source hashes
and all 32 original seed hashes match the current files. The cache configuration
retains `hash_dir=true` and empty sloppiness/ignored-option settings.

The raw three-TU measurements prove output-deleting warm compilations are real
hits with identical object bytes: 1.0022/10.1726/13.7710 seconds cold versus
0.0070/0.0137/0.0164 seconds warm. Source/header changes miss then hit on repeat;
effective `-O0` placed after `-O1` misses, changes object bytes, then hits.
Identical header bytes rewritten with a new mtime still hit. These observations
support cache correctness and usefulness for the sampled compilations, not a
whole-build speedup estimate. Raw `results.json` SHA-256 is
`029cfc5fdffa6e1ca71002c87ed3ceee40630d1aecdf5fe784fff2fb324a5bbc`;
`extra-results.json` is
`7923796481f3d18ddb2d466814a2a65520f8c3ba477d5f77b493c189876abcc2`.

Fresh smoke evidence has five successful phases: canary, two seed replays, and
two campaigns that each completed 2,000 runs under the unchanged 30-second cap.
The executable and runner hashes match the recorded run. Its configuration
hashes match the retained original pre-launcher configuration snapshots; later
same-tree reconfiguration separately establishes the ccache commands. This is
valid reuse of unchanged instrumented binaries, not a claimed fresh full
instrumented rebuild. Raw smoke `summary.json` SHA-256 is
`93b152a42e7e57040569817408ac089bd46e73449dbb092d15a6fb6181fef890`.

The [implementation report](ci_fuzz_cache_optimization.md) records capacity,
toolchain, evidence paths, and remaining limits. Raw local proof remains in
ignored `out/ci-fuzz-cache-proof/` and `out/ci-fuzz-cache-smoke/`; no hosted
after-change run or full cold rebuild was performed. Hosted end-to-end savings,
compressed transfer costs, quota pressure, and first-seed latency remain
unmeasured. The new namespace starts cold until a successful main run publishes
a seed; eviction/toolchain/profile changes can still cause misses. Acceptance
does not authorize commit, push, PR creation, dispatch, or remote cache deletion.
