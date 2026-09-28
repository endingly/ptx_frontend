# Resolved IR compilation granularity benchmark

## Rebased comparison against the current PR base

On 2026-09-28, the same host compared `main@ad5f99f` with #204 at `6b982ee`.
The later removal of a Python-only fixed opcode-count assertion does not affect
these C++ build inputs. Each tree used a fresh build directory, GCC 15.2.0, the
`ci-linux-gcc-debug` preset, four Ninja jobs, `CCACHE_DISABLE=1`, and the same
preinstalled vcpkg dependencies. Generation, production compilation, and the
`test_resolved_ir` target were timed as sequential commands; configure and
CTest runtime were excluded. The trees were built one after the other to avoid
competition from these benchmark builds.

| Tree | Generation | Production | Test target | Total |
| --- | ---: | ---: | ---: | ---: |
| `main@ad5f99f` | 40.080 s | 146.472 s | 436.484 s | 623.036 s |
| #204 at `6b982ee` | 46.492 s | 145.403 s | 537.206 s | 729.101 s |

In this run the candidate's test build was 23.1% slower and the three measured
phases together were 17.0% slower. The production-library phase was 1.069 s
faster, while generation took 6.412 s longer. The candidate compiled 138
Resolved IR test translation units versus 76 on the base. These are one-run
local wall times, not a CI timing gate; they do not support the earlier claim
of a clean-build speedup on the current base.

To check whether the test-build direction repeated, both warm production builds
were left intact and only their `test_resolved_ir.dir/test/*.cpp.o` files were
removed. A second `test_resolved_ir -j4` build compiled only test objects and
linked the executable. Ninja's recorded completion times were 426.777 s for
the base's 76 test objects and 474.311 s for the candidate's 138 test objects.
The candidate was 11.1% slower in this repeat. The two observations agree on
the direction, but not the precise size, of the local test-build regression.

For an atom-local invalidation probe, both warm builds temporarily renamed the
`atom_global_add_u32` variant in the parallel-synchronization spec. The probe
built `ptx_frontend_resolved_ir`, compared generated source bytes before and
after, recorded actual C++ object compiles, then restored the spec and rebuilt
the library. Both probe and restoration builds succeeded.

| Tree | Probe production build | C++ objects rebuilt | Changed implementation source | Unchanged implementation sources |
| --- | ---: | ---: | --- | --- |
| `main@ad5f99f` | 178.215 s | 9 | whole parallel-synchronization category | — |
| #204 at `6b982ee` | 161.221 s | 9 | `atom` shard | `mbarrier`, `red`, `residual` shards |

Only the candidate's atom implementation shard recompiled: the other three
shard sources remained byte-identical and their objects did not recompile.
The variant rename also changed shared generated declarations and descriptors,
so unrelated non-shard objects rebuilt in both trees. The probe demonstrates
shard-local implementation invalidation, while its 9.5% shorter wall time is
one local observation rather than a guaranteed speedup.

To reproduce the clean measurements, configure separate worktrees at the two
revisions with the same preset, compiler, four-job limit, disabled ccache, and
preinstalled vcpkg tree. Time these targets in order in each fresh build:

```sh
PYTHONPATH="$SOURCE_ROOT/python/src" cmake --preset ci-linux-gcc-debug \
  -S "$SOURCE_ROOT" -B "$BUILD_DIR" -DVCPKG_MANIFEST_MODE=OFF \
  -DVCPKG_INSTALLED_DIR="$VCPKG_INSTALLED_DIR"
CCACHE_DISABLE=1 PYTHONPATH="$SOURCE_ROOT/python/src" \
  cmake --build "$BUILD_DIR" --target resolved_ir_codegen -j4
CCACHE_DISABLE=1 PYTHONPATH="$SOURCE_ROOT/python/src" \
  cmake --build "$BUILD_DIR" --target ptx_frontend_resolved_ir -j4
CCACHE_DISABLE=1 PYTHONPATH="$SOURCE_ROOT/python/src" \
  cmake --build "$BUILD_DIR" --target test_resolved_ir -j4
```

Then run the atom probe separately in each warm build. The script is taken from
the candidate worktree, but generation uses the selected tree's Python package:

```sh
CCACHE_DISABLE=1 PYTHONPATH="$SOURCE_ROOT/python/src" \
  python "$CANDIDATE_ROOT/tools/benchmark_issue203_atom_invalidation.py" \
  --source-root "$SOURCE_ROOT" --build-dir "$BUILD_DIR" \
  --output /tmp/issue203-atom-probe.json --jobs 4
```

## Historical pre-rebase comparison

The measurements below compare the original `8e9eebf` tree with intermediate
and final candidates before #202 was merged. They remain useful for that
earlier tree, but are not a before/after comparison for the current PR base.

This benchmark compares the original tree with generated source/header sharding
and narrower Resolved IR test translation units. Each clean build used a fresh
build directory, GCC 15.2.0, the `ci-linux-gcc-debug` preset, four Ninja jobs,
and `CCACHE_DISABLE=1`. Generation, production compilation, and the
`test_resolved_ir` target were timed as three sequential commands on the same
host. Configure time and CTest runtime are outside these measurements.

| Tree | Generation | Production | Test target | Total |
| --- | ---: | ---: | ---: | ---: |
| Original `8e9eebf` | 53.561 s | 184.099 s | 481.177 s | 718.837 s |
| Generated sharding and initial leaf includes | 41.571 s | 150.493 s | 441.278 s | 633.342 s |
| Final candidate, including test partitioning | 35.221 s | 120.364 s | 403.345 s | 558.930 s |

The final test phase is 16.2% faster than the original; all three phases
together are 22.2% faster. The final candidate compiles 138 Resolved IR test
source files rather than 76. The extra small opcode-specific files did not
raise the measured clean-build critical path. All 854 GTest cases retain their
original suite/name and body; focused CTest passed 854/854.

To repeat the clean measurement, configure a new directory and time each
build command separately:

```sh
export PYTHONPATH="$PWD/python/src"
cmake --preset ci-linux-gcc-debug -B out/build/issue203-benchmark \
  -DVCPKG_MANIFEST_MODE=OFF \
  -DVCPKG_INSTALLED_DIR=/absolute/path/to/vcpkg_installed
CCACHE_DISABLE=1 cmake --build out/build/issue203-benchmark \
  --target resolved_ir_codegen -j4
CCACHE_DISABLE=1 cmake --build out/build/issue203-benchmark \
  --target ptx_frontend_resolved_ir -j4
CCACHE_DISABLE=1 cmake --build out/build/issue203-benchmark \
  --target test_resolved_ir -j4
ctest --test-dir out/build/issue203-benchmark -j4 -E '_NOT_BUILT' \
  --output-on-failure
```

The incremental probe adds one temporary annotation to the `selp` source
specification, regenerates, compiles production and tests, then restores the
specification and generated headers. Each row below used a warmed test target
and an empty dedicated ccache directory with a 1 GiB limit. The object counts
are actual compiler invocations, not just Ninja's planned dependencies.

| Tree | Production objects / wall | Test objects / wall | Aggregate-dependent test sources |
| --- | ---: | ---: | ---: |
| Original `8e9eebf` | 9 / 198.984 s | 40 / 483.498 s | — |
| Generated sharding and initial leaf includes | 9 / 129.571 s | 36 / 391.192 s | 34 |
| Final candidate | 9 / 103.596 s | 37 / 318.918 s | 34 |

The final test rebuild is 34.0% faster than the original and 18.5% faster
than the intermediate candidate. One more test object compiles than in the
intermediate candidate, but the formerly large opcode tests compile as smaller
units. Support-only tests have no generated opcode dependencies. Single-opcode
tests depend only on their opcode's three generated leaf headers; mixed-opcode
tests depend on only the leaf headers they exercise. Tests that require module
resolution, whole-model dispatch, or public aggregate-header compatibility
retain the aggregate dependency.

Run the probe with a fresh cache after warming the normal target:

```sh
CCACHE_DIR=/tmp/issue203-probe-cache CCACHE_MAXSIZE=1G \
  python tools/benchmark_resolved_ir_invalidation.py \
  --source-root "$PWD" --build-dir "$PWD/out/build/issue203-benchmark" \
  --output /tmp/issue203-invalidation.json --jobs 4
```

After the probe, rebuild the normal production and test targets before using
that build directory for tests or installation: the script restores sources
and generated headers, but the probe's object files contain the temporary
annotation. These timings are local measurements rather than a CI timing gate.

## Scoped PCH experiment

Two test-only PCH configurations were measured in fresh GCC Debug build
directories with the same four-job, disabled-ccache clean-build procedure.
The first applied a private PCH to 56 opcode-local select test objects. The
second retained that PCH and added a separate private PCH to 27 aggregate-
consuming test objects that actually rebuilt for the SELP probe. Neither PCH
included generated, category, aggregate, or instruction-union headers, even
transitively. All configurations passed 854/854 CTest cases.

| Test configuration | Clean test target | SELP test rebuild | PCH rebuilds during SELP probe | Observed `could_not_use_precompiled_header` count |
| --- | ---: | ---: | ---: | ---: |
| No PCH | 403.345 s | 318.918 s | — | 0 |
| Opcode PCH | 358.981 s | 325.376 s | 0 | 1 |
| Opcode and module PCHs | 383.978 s | 303.570 s | 0 | 28 |

The combined configuration saved only 4.8% of the actual SELP test rebuild,
below the agreed 10%–25% target. Its two PCH artifacts totalled about 456 MiB
and it gave back about 25 seconds of the opcode-only cold-build gain. The
`could_not_use_precompiled_header` values are ccache observations under the
repository's current CI-style environment, not a claim about every possible
ccache configuration. The trial PCH build changes were withdrawn; the final
candidate uses no PCH. Ccache's [PCH configuration requirements](https://ccache.dev/manual/4.12.2.html#_precompiled_headers)
are relevant if a future experiment revisits this approach.
