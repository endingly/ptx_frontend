# Frontend symbol-table benchmarks

These Google Benchmark cases exercise the real parser, `bindSymbols()`,
`SymbolTable::lookup()`, and `resolveModule()`. They live with the module that
provides the complete frontend pipeline, not in the installed public library.

## Optional build

The root option `PTX_FRONTEND_BUILD_BENCHMARKS` is `OFF` by default. Enabling it
selects the optional vcpkg manifest feature `benchmarks`, which installs Google
Benchmark at the repository's pinned vcpkg baseline. No benchmark dependency or
executable is exported by the frontend package, and timing is not a CTest gate.

```sh
cmake --preset ci-linux-gcc-release -DPTX_FRONTEND_BUILD_BENCHMARKS=ON
cmake --build --preset ci-linux-gcc-release --target frontend_symbol_table_scaling
out/build/ci-linux-gcc-release/submod/resolved_ir/benchmark/frontend_symbol_table_scaling \
  --benchmark_list_tests=true
```

For an existing build that disables automatic manifest installation, install
the feature explicitly into the same dependency tree before configuring:

```sh
"$VCPKG_ROOT/vcpkg" install --x-feature=benchmarks \
  --x-install-root=out/build/ci-linux-gcc-release/vcpkg_installed
```

## Comparing installed revisions

Build/install each frontend revision with the same compiler, build type, flags,
and dependency versions into separate prefixes. Build this same driver source
against each exported package; do not mix headers and libraries from different
revisions. Standalone mode also uses the root vcpkg manifest's optional feature.

```sh
cmake -S submod/resolved_ir/benchmark -B /tmp/frontend-benchmark-before \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -Dptx_frontend_DIR=/path/to/before/lib/cmake/ptx_frontend
cmake --build /tmp/frontend-benchmark-before -j2
```

Repeat with the after prefix and a different build directory. Check the
registered case names with `--benchmark_list_tests=true`, then start with the
smallest workload under an external timeout. For example:

```sh
timeout 300s /tmp/frontend-benchmark-before/frontend_symbol_table_scaling \
  --benchmark_filter='.*1000.*' \
  --benchmark_min_time=1x --benchmark_repetitions=5 \
  --benchmark_out=/tmp/frontend-before.json --benchmark_out_format=json
```

`--benchmark_min_time=1x` fixes each repetition at one iteration for expensive
baseline cases. Use longer automatic sampling for short operations when noise
matters. Run before and after serially without competing builds, and retain
compiler/build metadata alongside the framework's JSON/CSV output in PR or CI
artifacts. Machine-specific output and one-off timing summaries do not belong
in this source directory.

## Workloads and interpretation

Cases cover 1,000, 2,000, 4,000, and 8,000 logical registers, ordinary versus
compact declarations, and a single scope versus two functions with nested
blocks. Declaration-only and reference-bearing sources use the same logical
names. A compact group remains one stored declaration, not N expanded records.
The generated `corpus/m12/natural_kernel_sm80.ptx` provides a representative
corpus control; reading its source file is outside the measured operation.
Override `PTX_BENCHMARK_CORPUS_FILE` at configuration time to use another PTX
source. A missing or invalid file makes a selected corpus case fail, not pass
with an empty workload.

Parsing, declaration-only binding, binding with references, direct lookups,
and complete module resolution are separate measurements. Module resolution
includes its own binding and other checks; do not add or subtract these times
as though they were disjoint stages. Correctness checks must reject parse,
binding, resolution, identity, or count failures instead of reporting early
failure as a speedup. Benchmark errors result in a nonzero process exit.

Google Benchmark owns timing, repetitions, aggregates, and result output.
Inspect both ordinary and compact cases: an index can improve growth with many
stored symbols while adding constant overhead for small or compact tables.
Use Release measurements for operational conclusions; Debug runs are useful
only as explicitly labeled comparative evidence. Complexity estimates and
absolute milliseconds are not pass/fail thresholds on shared CI machines.
