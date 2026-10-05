# Resolved instruction ownership measurements

This file preserves historical measurements from the former variant-based and
owned-instruction prototypes. The standalone runtime consumer and comparison
script described below were retired with the canonical direct-class IR
promotion; their commands are historical records and no longer run from this
tree. The nanosecond measurements below used a different harness, workload,
pipeline, and driver from the Google Benchmark symbol-table measurements in
`docs/us-en/build_scalability.md`, so their numbers are not directly comparable.

## Historical matched cold library build

One fresh `ptx_frontend_resolved_ir` build per configuration used Clang 21,
Ninja, six parallel jobs, ccache disabled, the same dependency tree, and the
same Debug `-g` or Release `-O3 -DNDEBUG` flags. The OFF baseline was revision
`20f8afb`; the ON prototype used that revision plus this experiment's source
edits. The same `/tmp/ptx-owned-baseline-results/run_timed.py` sampler measured
each build. Its RSS value is the largest sampled *individual compiler process*,
not the sum of parallel workers or a cgroup peak.

| Mode | OFF wall | ON wall | OFF peak individual RSS | ON peak individual RSS |
| --- | ---: | ---: | ---: | ---: |
| Debug | 197.538 s | 211.935 s (+7.29%) | 2948.25 MiB | 2040.14 MiB (-30.80%) |
| Release | 288.769 s | 301.378 s (+4.37%) | 1913.20 MiB | 1426.94 MiB (-25.42%) |

The ON build sharply reduced the central Release module, dispatch, and
availability translation units (97.046 to 9.967 s, 93.479 to 2.934 s, and
50.640 to 14.518 s). The 92 per-op translation units became more expensive;
their overlapping compile durations cannot be added to wall time. The Release
static archive fell from 95,687,662 to 79,982,662 bytes. These one-pass data
show a compiler-memory and aggregate-TU benefit with a small total build-time
regression. They do not establish a runtime or total-heap improvement. Detailed
logs and sampler JSON are under `/tmp/ptx-owned-baseline-results/` and
`/tmp/ptx-owned-experiment-*-build.{json,log}` in this workspace.

## Current owned-only runtime and storage measurement

Build and install the current owned-IR library from a baseline and candidate
revision using the same compiler, Release flags, corpus, and iteration count.
Build this standalone consumer against each installed package:

```sh
cmake -S tools/owned_ir_experiment -B /tmp/owned-runtime-baseline \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -Dptx_frontend_DIR=/tmp/owned-install-baseline/lib/cmake/ptx_frontend
cmake --build /tmp/owned-runtime-baseline -j2
/tmp/owned-runtime-baseline/ptx_owned_ir_runtime \
  corpus/m12/natural_kernel_sm80.ptx 200
```

Repeat for the candidate installation. The program parses once, checks that
resolution succeeds, then measures resolution plus validation, AST-free
validation, full module copying, and read-only opcode traversal. It reports
the instruction slot size, reserved body storage, and the sum of outer typed
record object sizes. These are not total heap or RSS: they exclude nested
field allocations, allocator metadata, other module fields, the retained AST,
and transient allocations. The harness keeps the AST alive for repeated
resolution, although validation does not read it.

Each operation has an untimed warm-up and five separate timed batches. Run
both executables in alternating order for several process runs on an otherwise
idle machine. Retain all raw JSON lines and report the median of the process
medians. `compare.py` performs the alternating order and emits raw records:

```sh
python3 tools/owned_ir_experiment/compare.py \
  /tmp/owned-runtime-baseline/ptx_owned_ir_runtime \
  /tmp/owned-runtime-candidate/ptx_owned_ir_runtime \
  corpus/m12/natural_kernel_sm80.ptx --iterations 200 --runs 5
```

### Historical installed-package OFF/ON Release runs

The earlier prototype used Clang 21 Release executables linked against separate OFF
and ON installations. The current owned-only source no longer offers that switch. Five alternating process runs per
configuration used 200 iterations for `natural_kernel_sm80.ptx` and 80 for
`common_kernel_sm90a.ptx`; each operation had an untimed warm-up and five timed
batches per process. Each value below is the median of the five process
medians, in nanoseconds per operation. Raw batches and process records are in
`/tmp/ptx-owned-runtime-natural-release.json` and
`/tmp/ptx-owned-runtime-common-release.json` in this workspace.

| Corpus (functions/instructions) | Operation | OFF | ON | Observed ON change |
| --- | --- | ---: | ---: | ---: |
| Natural (1/18) | Resolve and validate | 221,462 | 193,610 | -12.6% |
| Natural (1/18) | Validate existing module | 5,832 | 5,917 | +1.5% |
| Natural (1/18) | Copy full module | 3,476 | 3,591 | +3.3% |
| Natural (1/18) | Traverse opcode names | 56.3 | 13.7 | -75.6% |
| Common (32/92) | Resolve and validate | 954,886 | 939,521 | -1.6% |
| Common (32/92) | Validate existing module | 19,713 | 23,031 | +16.8% |
| Common (32/92) | Copy full module | 37,526 | 37,073 | -1.2% |
| Common (32/92) | Traverse opcode names | 272.8 | 71.0 | -74.0% |

The instruction vector element shrank from 2,896 to 16 bytes. For the natural
corpus, summed reserved body-vector storage was 92,672 bytes OFF and 512 bytes
ON; for the common corpus, 312,768 and 1,728 bytes. The sum of `sizeof` the
active outer typed records was 15,784 and 36,368 bytes respectively in both
configurations. OFF embeds those records in its variant body slots, so the
record-size sum overlaps the body-capacity figure; ON allocates the records
separately. The fields cannot be added as total memory, and exclude nested
allocations and allocator overhead.

The narrow opcode-name query was 74-76% faster in these runs, while validation
of the common corpus was 16.8% slower. Copy times were close and the resolve
result varied by corpus. The raw process medians vary, and these short,
fixed-order microbenchmarks on one machine do not establish a general runtime
speedup. Cache state and the AST and baseline module retained by the harness
also limit the conclusions.
