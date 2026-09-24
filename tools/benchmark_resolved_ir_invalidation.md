# Resolved IR incremental rebuild benchmark

The companion script temporarily adds `condition_code_effect: carry_out` to
`selp_u32` in `comparison_and_selection.yaml`. This changes the generated
SELP model leaf, or the category model header in older builds, without changing
C++ type names or layouts. It checks
that the header changed, builds the production library and then
`test_resolved_ir`, restores the spec and code generation, and writes JSON and
phase logs next to the requested output path.

Run each measurement from a warm `test_resolved_ir` target in a separate
checkout/build directory. For each configuration, use the same probe on
`origin/main` and on the candidate working tree. Run measurements serially:
`ccache --print-stats` reads cumulative counters from the selected cache, and
concurrent compilation would also distort wall time. The measurements below
used GCC 15.2.0, ccache 4.12.3, CMake 4.2.3, and four build jobs. The baseline
used the local default cache with a 5 GiB limit; the final candidate used a
fresh, dedicated 5 GiB cache directory for each configuration.

```sh
cmake --preset ci-linux-gcc-debug
cmake --build --preset ci-linux-gcc-debug --target test_resolved_ir -j4
CCACHE_DIR=/tmp/issue196-candidate-debug-cold-ccache \
CCACHE_MAXSIZE='5 GiB' \
python3 tools/benchmark_resolved_ir_invalidation.py \
  --source-root . --build-dir out/build/ci-linux-gcc-debug \
  --output /tmp/issue196-candidate-debug-cold.json --jobs 4
```

Use `ci-linux-gcc-release`, its build directory, and a separate fresh cache
directory for Release. For the baseline checkout, invoke the same script from
the candidate checkout with `--source-root` and `--build-dir` pointing at the
baseline. After a probe, rebuild the normal library and test targets before
testing or installing: the script restores the source and generated headers,
but the objects built during the probe still contain the temporary annotation.
The script counts source files in the test target, uses Ninja's recorded dependencies to
count test objects that include the generated aggregate model or instruction
union, and reports actual C++ compiler invocations and ccache deltas for each
phase. Source-TU counts differ because the candidate adds one compiled test
adapter.

| Tree and configuration | Test sources | Aggregate-dependent sources | Production wall | Production C++ invocations / hits / misses | Test wall | Test TU invocations / hits / misses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `origin/main`, Debug | 70 | 62 | 124.722 s | 9 / 0 / 9 | 429.509 s | 64 / 0 / 64 |
| Intermediate candidate, Debug | 71 | 34 | 101.597 s | 9 / 0 / 9 | 302.484 s | 40 / 0 / 40 |
| Final candidate, Debug | 71 | 29 | 96.861 s | 9 / 0 / 9 | 261.802 s | 35 / 0 / 35 |
| `origin/main`, Release | 70 | 62 | 118.957 s | 9 / 0 / 9 | 265.015 s | 64 / 0 / 64 |
| Final candidate, Release | 71 | 29 | 56.572 s | 9 / 0 / 9 | 166.631 s | 35 / 0 / 35 |

The intermediate candidate includes the comparison category split and the
initial test-adapter migrations. Its 40 test misses still exceed half of the
71 test source files, so the final candidate adds complete metadata projections
and moves three owned-module mutation checks into the compiled adapter.

The final candidate recorded zero ccache cleanups; its caches were 223.3 MiB
after Debug and 6.3 MiB after Release. The baseline Debug run recorded 49
cleanups across both phases in its previously used 5 GiB cache; its starting
size was not captured. Baseline Release recorded zero cleanups, but also used
the previously used default cache. All four final/baseline runs had zero hits.
The source-TU count is the primary structural result; the cache-state
difference limits precision of the wall-time comparison. The local 5 GiB cache
capacity also differs from CI's 1 GiB capacity.
