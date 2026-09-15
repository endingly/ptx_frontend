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
