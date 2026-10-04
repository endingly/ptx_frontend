# Generated-model build scalability

This is a local build-cost baseline, not an ISA-conformance test, runtime
benchmark, or CI timing gate. It measures the frontend library with tests and
runtime benchmarks disabled. The measurement-only YAML change is not a shipped
opcode expansion.

## Revision and environment

The measured source is `bacdfe008188c6302dcbaa668561092f09390b45`, archived
independently of the working tree on 2026-09-15. This includes execution-predicate
revalidation. Documentation changes made during measurement do not enter that
archive.

| Setting | Recorded value |
| --- | --- |
| Build | Debug; Ninja; 8 parallel compilation jobs |
| Compiler | GCC/G++ 15.2.0, Ubuntu `15.2.0-16ubuntu1` |
| CMake / Ninja / Python | 4.2.3 / 1.13.2 / 3.14.4 |
| Host CPU | AMD Ryzen 7 H 260; 6 cores / 12 logical CPUs exposed |
| Memory | Approximately 23 GiB RAM and 8 GiB swap exposed |
| cgroup limits | CPU `max 100000`; memory `max` |
| Compiler cache | ccache 4.12.3 launcher present; bypassed with `CCACHE_DISABLE=1` |
| Dependencies | Existing vcpkg-installed tree; manifest installation disabled |

The existing ccache statistics at preparation were 12,590 cacheable calls,
4,611 hits, and 7,979 misses. Those are historical host counters, not measurement
hits; the experiment neither clears nor relies on that cache. OS page caches
are not flushed. Dependency discovery, local Python-package preparation, and
configuration are separate from timed frontend generation and compilation.

The reused `x64-linux` dependency tree contained fmt 12.1.0 and magic-enum
0.9.7. GTest 1.17.0 and Google Benchmark 1.9.5 were also installed but were not
built or exercised in this experiment. The source manifest pins vcpkg baseline
`256acc64012b23a13041d8705805e1f23b43a024`. No dependency downloads or cache
purges are part of the timings.

## Method

Use a single isolated source/build pair for the four scenarios. Keep compiler,
flags, dependency prefix, and parallelism fixed; do not run competing builds.

1. **Clean:** configure an empty output directory, time `resolved_ir_codegen`,
   then time the frontend-library build with its generated files already present.
   Report those stages separately rather than silently excluding generation.
2. **No-op:** repeat the same frontend-library build without changing inputs.
3. **Handwritten implementation:** insert `static_assert(true);` inside
   `resolve_fields()` in `submod/resolved_ir/src/ptx_resolved_ir.cpp`. This
   behavior-neutral edit measures implementation-only invalidation. Restore the
   file and return to a built baseline outside the next timed scenario.
4. **Canonical YAML form:** insert the following additional variant immediately
   after `abs_f32` in `python/code_gen/resources/ptx_spec/arithmetic.yaml`, then
   measure regeneration and compilation separately. This controlled form-growth
   probe reuses an existing operand primitive; it is not a feature acceptance
   claim or a modification to the working branch's canonical database.

```yaml
      - name: abs_f16
        availability: {ptx: "6.5", sm: 53}
        modifiers:
          - {name: type, kind: type, domain: scalar_types, presence: fixed, value: f16}
        operands: $unary_scalar_register
        examples:
          - {ptx: "abs.f16 %h0, %h1;", valid: true}
```

Count executed C++ object compilations from the build log, not the total number
of Ninja steps: generation, archive creation, and linking are separate work.
Record generated-file content hashes as well as timestamps, because rewriting
unchanged outputs can also invalidate dependents. Measure installed-consumer
compilation separately from library compilation; do not run consumer functional
tests for this documentation-only change.

The generation topology remains the existing one: category-partitioned
implementation sources plus shared descriptors/dispatch and a common public
model header containing instruction definitions, the `ResolvedInstruction`
union, and reference visitors. Variant counts describe model size, not whole-op
coverage. See [the extension guide](yaml_instruction_spec.md) for acceptance
requirements.

## Library results

All figures are single-run wall-clock seconds, not a statistical comparison
against another revision. The post-generation build includes archive creation
and, for the clean case, lexer generation; it is not compiler CPU time alone.

| Scenario | Configure / reconfigure | Resolved-IR generation | Subsequent library build | C++ translation units compiled |
| --- | ---: | ---: | ---: | ---: |
| Clean frontend objects | 3.25 | 43.94 | 92.55 | 29 |
| No input changes | — | 0 | 0.04 | 0 |
| Handwritten implementation edit | — | 0 | 18.08 | 1 |
| One additional YAML form | 3.09 | 37.31 | 70.45 | 18 |

The first configuration attempt selected an unrelated Python environment and
failed before compilation. The reported successful configure is a retry with
an explicit interpreter and pinned local Python package; it can reuse compiler
discovery from that attempt. The clean **frontend objects** and generated
outputs were absent before their measured stages. Preparation failures, restoring
the implementation edit, and SDK installation are not included in the table.
Separate stage timings are not one uninterrupted end-to-end stopwatch reading.

| Generated-model measure | Baseline | YAML probe |
| --- | ---: | ---: |
| Opcodes / variants | 71 / 365 | 71 / 366 |
| `resolved_ir.gen.hpp` bytes | 499,538 | 500,143 |
| Three public headers, total bytes | 517,565 | 518,170 |
| All 13 generated files, total bytes | 16,040,402 | 16,054,262 |

Only five generated files changed content: the public model header and the
syntax, resolution, checker-descriptor, and arithmetic implementation sources.
The other eight retained their hashes. In particular, the checker and resolution
public headers retained their content but received new timestamps. All 18
Resolved-IR translation units recompiled, including categories other than
arithmetic; the other 11 frontend translation units did not. This experiment
does not separate the cost of changed shared-header content from the cost of
rewriting unchanged generated outputs.

The model-header SHA-256 changed from
`f7309e1e3e8c1acf1f5cf4ec4e671fc3295ab9473d457209c4a784281f1aa333`
to `fbfee5b046870003902c14d6c5317e0502f2bba6dd53343b359a00b3c407d28e`.

## Consumer and memory observations

Each installed SDK was consumed from a separate, initially empty build directory
using the pinned `submod/resolved_ir/test/package_consumer` project and only its
`ptx_frontend_model_consumer` target. No executable or CTest was run.

| Installed SDK | Consumer build wall time | C++ translation units |
| --- | ---: | ---: |
| Baseline | 13.71 s | 1 |
| YAML probe | 13.73 s | 1 |

These are separate clean consumer compilations, not an incremental consumer
invalidation experiment. The 0.02-second difference is not evidence of an
effect from the added form. SDK installation and the untimed restoration of the
baseline library are excluded.

`/usr/bin/time` was unavailable. One additional, isolated direct GCC compilation
of the largest generated source by byte size,
`resolved_ir_checker_descriptor.gen.cpp` (5,538,307 bytes), used Python
`resource.getrusage(resource.RUSAGE_CHILDREN)`. Its recorded maximum RSS was
466,020 KiB (about 455 MiB), with a 4.77-second wall time. This is a sampled
single-compilation process/child high-water mark, **not** the maximum across all
translation units, an eight-job aggregate, or a parallel build memory limit.
Those whole-build memory metrics were not collected.

## Reproduction commands

Use a Python environment with the pinned project's build dependencies already
installed and an existing compatible vcpkg tree. Prepare the local package
outside the timed stages so its namespace resolves to the archived code, not
an unrelated editable install. Substitute the three absolute environment paths
below; do not install or download dependencies during measurement.

```sh
ptx_python=/absolute/path/to/venv/bin/python
ptx_deps=/absolute/path/to/vcpkg_installed
ptx_toolchain=/absolute/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
ptx_run=$(mktemp -d)
mkdir "$ptx_run/src"
git archive bacdfe008188c6302dcbaa668561092f09390b45 | tar -x -C "$ptx_run/src"
"$ptx_python" -m pip install --no-deps --no-build-isolation \
  --target "$ptx_run/python-package" "$ptx_run/src/python"
export PYTHONPATH="$ptx_run/python-package" CCACHE_DISABLE=1
cmake -S "$ptx_run/src" -B "$ptx_run/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
  -DPTX_FRONTEND_BUILD_BENCHMARKS=OFF \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-15 -DCMAKE_CXX_COMPILER=/usr/bin/g++-15 \
  -DPython3_EXECUTABLE="$ptx_python" \
  -DCMAKE_TOOLCHAIN_FILE="$ptx_toolchain" -DVCPKG_MANIFEST_MODE=OFF \
  -DVCPKG_INSTALLED_DIR="$ptx_deps" -DCMAKE_PREFIX_PATH="$ptx_deps/x64-linux" \
  -DCMAKE_INSTALL_PREFIX="$ptx_run/install" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
time -p cmake --build "$ptx_run/build" --parallel 8 --target resolved_ir_codegen --verbose
time -p cmake --build "$ptx_run/build" --parallel 8 --target ptx_frontend_resolved_ir --verbose
time -p cmake --build "$ptx_run/build" --parallel 8 --target ptx_frontend_resolved_ir --verbose
```

Capture each command's stdout/stderr separately. Apply the handwritten edit,
repeat the library command, then restore and rebuild outside the next timing.
Apply the YAML insertion above, time reconfiguration, generation, and the library
build separately. Preserve each SDK with `cmake --install` outside timed stages.
For each SDK, configure the pinned consumer project with the same compiler,
Debug mode, toolchain, `VCPKG_INSTALLED_DIR`, and disabled manifest/cache; set
`CMAKE_PREFIX_PATH` to that SDK. Time
`cmake --build <consumer-build> --parallel 8 --target ptx_frontend_model_consumer --verbose`.

For the RSS sample, read the selected source's entry from `compile_commands.json`,
run its compiler command in the recorded working directory with an isolated
object output, and sample `RUSAGE_CHILDREN` in a fresh Python process around
`subprocess.run`. Keep this additional compile outside the four scenario timings.
The original local logs, exact patches, hashes, and environment snapshot were
retained under `/tmp/ptx136-measure-20260915-144810-123299`; that temporary path
is not a portable dependency of these instructions.

## Decision

Retain the current layout. A handwritten implementation edit remained local,
and the no-op build did no compiler work. The YAML probe did expose broad
Resolved-IR invalidation: a 605-byte model-header increase accompanied 18
recompiled translation units. That is evidence for monitoring form-growth
costs, not evidence of a historical regression, a required global IR redesign,
or an established size/time threshold.

If repeated measurements show that this cost blocks contributor workflows,
investigate content-preserving generation, narrower full-union dependencies,
or category-specific public model headers as separate experiments. Compare
those alternatives on identical inputs before selecting one. Do not change the
existing generation self-heal, topology, embedded-parent, or installed-package
contracts merely to improve a stopwatch result, and do not add a hard CI timing
gate from this single shared-host Debug run.

## 2026-09-29 test-build follow-up

The earlier library-only measurement does not predict the cost of building the
Resolved IR tests. A new, uncommitted candidate splits generated declarations
and definitions by opcode, offers full opcode headers under
`model/<category>/<opcode>.gen.hpp`, and moves single-op test cases into
separate translation units. Category headers aggregate the opcode headers;
the model-only headers remain separate so the AST module can stay incomplete.
The candidate retains all 887 distinct GTest cases across 130 suites.

The following are matched, single-run **clean** builds of the
`test_resolved_ir` target from empty Ninja build directories on the same host.
The baseline is `origin/main` at `1fe66d6`; the candidate is the uncommitted
`perf/issue-203-parallelism-restart` worktree based on that commit. Both used
GCC 15.2, Debug with C and C++ debug flags set to `-g0`, the same vcpkg
toolchain, tests enabled, ccache disabled, and six parallel build jobs. Each
build used the Python package from its own source worktree. Configuration is
excluded from the build times. No other build ran concurrently.

| Layout | Clean test-target build | Compiled C++ objects | Build directory | Sampled peak compiler RSS sum | Lowest sampled `MemAvailable` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Main baseline | 450 s | 123 | 4.7 GiB | 15,632 MiB | 6,397 MiB |
| Per-op candidate | 530 s | 251 | 5.3 GiB | 15,645 MiB | 6,280 MiB |
| Per-op candidate with targeted module-test shards | 494 s | 261 | 5.4 GiB | 10,964 MiB | 10,849 MiB |
| Above plus six-way memory/vector test split (discarded) | 565 s | 266 | 5.7 GiB | 10,311 MiB | 11,117 MiB |

The candidate is 80 seconds (17.8%) slower and shows no meaningful memory
headroom improvement in this clean-build comparison. Its generated objects
grew from 25 to 93, and test objects from 78 to 138. Summed compiler object
wall durations from Ninja's log grew by approximately 207 and 288 seconds
for those groups, respectively. These overlapping durations are not CPU time.
The test split consists of 55 single-op files
(204 cases), four cross-op files (10 cases), and eight module files (211
cases); other existing tests remain in place. An audit found no wholly
redundant module-level test case. One duplicated Membar subassertion was
removed from a Fence test while the dedicated Membar assertion was retained.

Compiler RSS was sampled once per second by adding `cc1plus` process RSS; it
can count shared pages more than once and is not a cgroup memory high-water
mark. These local measurements do not establish safety on a smaller CI runner.
The historical table above used a different test scope and must not be
compared directly with these test-target build times.

The third row is one additional fresh build with the same flags, compiler,
toolchain, and six-job limit. It applies the test-only partition from PR #209
to two measured hotspots: typed-projection instantiations in
`test_module_snapshot.cpp` and source-association cases in
`test_module_source_associations.cpp`. The latter also avoids copying the
complete `ResolvedInstruction` union when creating an extra known `Ret`.
Test objects increase from 138 to 148, but the longest of these newly
partitioned objects is 66.5 seconds, compared with 144.0 and 146.5 seconds
for the original two files. The whole target improves by 36 seconds relative
to the unsplit candidate, while remaining 44 seconds slower than main.
The sampled compiler RSS sum falls by 4,681 MiB relative to the unsplit
candidate. All 887 tests across 130 suites pass. This single local result
supports retaining the targeted test split for further review; it does not
meet the issue's clean-build speed objective or prove safe uncapped CI builds.

The fourth row is a further isolated experiment on the third-row candidate.
It split the 2,432-line `test_resolved_module_memory_vectors.cpp` into six
semantic source files while preserving the 48 test bodies and the full
887-case inventory. The original file took 102.2 seconds in the third-row
parallel build and 80.6 seconds with a 3,328-MiB maximum RSS when compiled
alone. Despite smaller individual sources, the fresh target build became
71 seconds slower. Summed test-object durations increased from 1,743.1 to
2,118.6 seconds across all test objects, and sampled compiler RSS fell by only
653 MiB. Repeated parsing of the aggregate header in five additional units is a
plausible contributor, not an isolated measured cause. The six-way split was
reverted; this row records the negative result rather than a shipped layout.
A separate `-fsyntax-only -ftime-report` probe of the original giant file
reported 15.70 of 21.44 seconds under template instantiation. That probe
omits code generation and is not a substitute for the complete-build timing;
it indicates that reducing source lines alone cannot remove its template cost.

Before the targeted test split, a separate trial used a **test-only**, opt-in
PCH for 29 source files that include
the aggregate Resolved IR header. It did not apply the aggregate PCH to the
whole target. With the same `-g0` and six-job settings, the clean build was
stopped after approximately 400 seconds at step 170 of 269: the sampled
compiler RSS sum had reached 17,800 MiB and `MemAvailable` had fallen to
4,427 MiB. Because the build was stopped, there is no valid completion time
or test result for this PCH variant. The experimental CMake wiring was
removed. A separate exploratory `-g2` run at Ninja's local default of 14
jobs reached a 21,914 MiB compiler RSS sum and only 121 MiB of
`MemAvailable`; it was also stopped and is not a clean-build comparison.
Local parallelism may be raised for convenience, but these observations do
not justify removing the Debug CI parallel cap.

## 2026-09-29 local Clang comparison

The C++ source after the targeted module-test partition is unchanged by the
documentation-only commit `092aac3`. The following additional clean
`test_resolved_ir` builds use the same source, Ninja, Debug `-g0` for C and
C++, disabled ccache, vcpkg toolchain, and local host as the third row above.
Each run starts from a separate empty build directory; configuration is
excluded. Clang is Ubuntu Clang 21.1.8, compared with GCC 15.2.0. The same
261 C++ objects are compiled in each run, and all 887 Resolved IR tests pass.

| Compiler | Parallel jobs | Clean target build | Sampled peak compiler RSS sum | Lowest sampled `MemAvailable` | Build directory |
| --- | ---: | ---: | ---: | ---: | ---: |
| GCC 15.2 | 6 | 494 s | 10,964 MiB | 10,849 MiB | 5.4 GiB |
| Clang 21.1.8 | 6 | 492 s | 4,947 MiB | 17,031 MiB | 3.0 GiB |
| Clang 21.1.8 | 14 | 346 s | 8,865 MiB | 13,898 MiB | 3.0 GiB |

At equal six-job parallelism, the two-second wall difference is not meaningful
evidence of a compiler speed advantage; Clang's sampled compiler RSS sum is
6,017 MiB lower. With the local Ninja default of 14 jobs, the Clang build is
146 seconds faster than its six-job run and retains lower sampled compiler
RSS than six-job GCC. This is a benefit from using the memory headroom for
more concurrent work, not a same-parallelism compiler speedup. The Clang
build emitted repeated `-Wdefaulted-function-deleted` warnings from existing
headers; they were not suppressed for measurement. The warning count does not
affect the passing test result.

The RSS figures sum one-second samples of `cc1plus` for GCC or `clang++-21`
and `clang-21` for Clang. Shared pages can be counted more than once; they
are not process-tree or cgroup peaks. These single local runs do not establish
the best parallelism or memory safety for CI, nor do they show that the per-op
layout is faster than main under Clang.

## 2026-10-04 owned-instruction compile follow-up

This follow-up measures current main `386aebb` after #216 against uncommitted,
test-local and generator-emitted visitor optimizations on the same source checkout.
Each `test_resolved_ir` build started in a separate empty Ninja directory with
Clang 21.1.8, Debug, six jobs, disabled ccache, the same installed vcpkg tree,
and C/C++ Debug flags `-g0`. The test target additionally appends
`-gline-tables-only`; generated and library objects retain `-g0`. The timed
build includes generation, 263 C++ object compilations, and linking, but not
configuration. No other compilation ran concurrently.

| Source | Clean target build | C++ objects | Ninja steps |
| --- | ---: | ---: | ---: |
| Main baseline | 279.37 s | 263 | 280 |
| Test checker/include narrowing only | 278.26 s | 263 | 280 |
| Above plus filtered owned projections and eager index-based reference visitors (intermediate) | 277.22 s | 263 | 280 |
| Final: lazy index-based visitors and exact-op test includes | 254.40 s | 263 | 280 |

The first two candidate-to-baseline differences are within single-run noise.
The final candidate is 24.97 seconds (8.9%) faster in this matched single run.
Its test-object compilation interval fell from 142.37 to 119.86 seconds;
the resolved-library object interval stayed near 90 seconds. Summed object
wall durations from the overlapping Ninja jobs fell from 1,245.89 to
1,112.25 seconds. Those sums are neither CPU time nor total build time.
The test-only change removed an all-opcode visitor instantiated merely to
call the existing `OwnedInstruction::check`. Typed projection tests now copy
selected `OwnedInstruction` owners and leave an empty owner at each
unselected source position, avoiding a deep-copying variant of the selected
opcode record types. Selected ordinary tests include exact opcode leaves instead of
category or aggregate headers, while explicit public-header compatibility
tests retain those headers. The generated reference visitor dispatches by
canonical variant and operand-layout index inside a constrained generic
lambda, retaining callback order and the `std::bad_variant_access` fallback.

Isolated, sequential compiles using the same build's `compile_commands.json`
and a scratch object output show where the cost changed:

| Translation unit | Before | After |
| --- | ---: | ---: |
| Typed collective projection test | 27.52 s | 2.75 s |
| Typed module projection test | 20.75 s | 2.60 s |
| Generated Cp source | 28.73 s | 26.51 s |
| Generated Mbarrier source | 26.03 s | 24.03 s |

The projection comparison used baseline generated headers on both sides of
the test helper edit. The generated-source comparison regenerated only the
new reference visitors between compiles. The initial direct index switch
made two unchanged aggregate-header tests slower (5.34 to 8.29 seconds and
5.34 to 8.24 seconds). Making the switch body dependent on the exact opcode
restored them to 5.58 and 5.53 seconds, while retaining the generated-source
improvement. The final clean-build Ninja log's longest objects were Cp
(28.59 s), Mbarrier (24.73 s), and the instruction variants test (16.83 s);
the two projection objects no longer lead that list.
The final build discovers the same 910 tests in 133 suites as baseline, with
an identical GTest-list hash, and all 910 pass. The 119 resolved-IR Python
tests and 25 generation-plan tests also pass. Comparable whole-build
compiler RSS and cgroup memory peaks were not sampled in this follow-up.
The local build logs are under `/tmp/ptx-compile-baseline.hfBBvj`,
`/tmp/ptx-compile-candidate.Yarq9p`, `/tmp/ptx-compile-measured.IDV8uT`,
and `/tmp/ptx-compile-narrow.dBDSvt`; these temporary paths are not portable.

These measurements cover current main, not the pending matrix or tensor
branches. A matrix layout with shared physical storage must retain its
logical-to-storage variant mapping when reconciling the reference
visitor generator; current main's direct variant index applies only where
logical and physical alternatives coincide. The single-opcode Cp and
Mbarrier sources remain material compile costs. Their 7.8 and 5.8 MiB
generated definitions contain descriptors, resolver, checker, and owner
bridges together; this experiment does not establish a safe or faster
generation-topology split.

## 2026-10-04 descriptor partition prototype

The preceding final layout at `125615b3ba48a0b45030d8806a5f76796a26396f`
was the baseline for an uncommitted prototype that has since been withdrawn.
The experimental `PTX_RESOLVED_IR_DESCRIPTOR_PARTITION_OPCODES` CMake cache list selected
canonical opcodes for separate private syntax, resolved, and checker descriptor
storage. Its default was empty. Only `cp` and `mbarrier` were selected for these
measurements; the generator also passed a selection test with `add`. Existing
public typed descriptor getters retained their signatures and forwarded to private
accessors for selected opcodes. The six emitted storage struct bodies for the
two samples were byte-identical to the baseline bodies.

The baseline and selected builds used Clang 21.1.8, Ninja, Debug `-g0`, the
same vcpkg dependency tree, disabled ccache, six jobs, and the
`test_resolved_ir` target. Tests additionally used `-gline-tables-only`.
Configuration is excluded. Baseline artifacts are in
`/tmp/ptx-compile-narrow.dBDSvt`; prototype artifacts, compiler traces, and
logs are in `/tmp/ptx-descriptor-proto.JZ9pyk`. These are local temporary
paths, not portable reproduction inputs.

| Clean target | Baseline | Selected prototype |
| --- | ---: | ---: |
| One-run wall time | 254.40 s | 246.89 s |
| C++ objects / Ninja steps | 263 / 280 | 266 / 283 |
| Resolved IR tests | 910 / 133 suites | 911 / 134 suites |

The prototype adds two descriptor objects and one opcode-descriptor lifetime
test object. All original 910 tests remain in the discovered list; the new
test also passes. The whole-target wall difference is one run on a shared
host and cannot be attributed to the split: the new test object and changed
build scheduling also affect it. Both selected-op object pairs were examined
with isolated sequential compiler invocations using their respective
`compile_commands.json` entries and scratch object outputs:

| Opcode | Baseline source | Prototype typed + descriptor sources | Isolated typed-source peak RSS, baseline → prototype |
| --- | ---: | ---: | ---: |
| Cp | 25.974 s | 24.105 + 2.863 = 26.968 s | 1,468,576 → 1,446,336 KiB |
| Mbarrier | 22.698 s | 22.070 + 2.561 = 24.631 s | 1,263,028 → 1,213,404 KiB |

The separate descriptor compiler processes peaked at 249,664 KiB for Cp and
240,652 KiB for Mbarrier. The table's RSS figures are single-process high-water
marks, not a six-job or whole-build memory peak. Serialized combined compile
time increased by 0.994 s for Cp and 1.933 s for Mbarrier. Separate Clang
`-ftime-trace` runs likewise recorded `ExecuteCompiler` times of 29.631 s
baseline versus 27.065 + 2.907 = 29.972 s for Cp, and 25.899 s versus
24.131 + 2.741 = 26.872 s for Mbarrier. The trace's top function
instantiations still include `std::expected<T>` construction and nested
`std::variant` move/copy visitors at roughly 2–2.5 s per event. Nested trace
events overlap and must not be summed as independent work.

The selected build passed all 911 tests, a second build did no work, and an
installed `examples/conversion_consumer` configured, linked, and ran. Private
accessor headers were absent from the install. Switching a separate build from
selected to empty regenerated the original layout and removed all four
partition artifacts from both the output manifest and the generated directory.
The measured samples do not support enabling descriptor partitioning by
default. All prototype code, tests, build changes, and lexer filename changes
have been withdrawn; only this paired measurement record is retained. The
experimental option is not available in the current code. Any future layout
proposal needs new measurements and core review. The `.gen.hpp` / `.gen.cpp`
naming requirement applies to Python-generated files, not Flex lexer outputs.
