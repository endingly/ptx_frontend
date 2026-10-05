# Active resolved IR runtime benchmarks

`symbol_table_scaling.cpp` preserves the earlier benchmark's source
generation, case names, validation, and checksum logic. Its frontend include and
link target select `resolved_ir`. The optional root `PTX_FRONTEND_BUILD_BENCHMARKS`
option is `OFF` by default; the executable is not installed or registered as a
CTest timing gate.

Build a Release benchmark with the optional vcpkg `benchmarks` feature and run
the smallest cases before the full range:

```sh
cmake -S . -B /tmp/ptx-runtime-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DPTX_FRONTEND_BUILD_BENCHMARKS=ON \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build /tmp/ptx-runtime-build --target \
  frontend_symbol_table_scaling -j 6
timeout 300s /tmp/ptx-runtime-build/submod/resolved_ir/benchmark/\
frontend_symbol_table_scaling \
  --benchmark_filter='symbol_table_scaling/(parse|resolve_module)/ordinary/single/N1000' \
  --benchmark_min_time=1x --benchmark_repetitions=5 \
  --benchmark_out=/tmp/ptx-runtime-small.json --benchmark_out_format=json
```

The generated cases use 1,000, 2,000, 4,000, and 8,000 logical registers,
ordinary or compact declarations, and single or nested scopes. The two corpus
cases use `corpus/m12/natural_kernel_sm80.ptx`; override
`PTX_BENCHMARK_CORPUS_FILE` during configuration to select another file.
`parse` measures syntax parsing, AST count validation, and result destruction.
`resolve_module` starts from a pre-parsed AST and includes its internal binding,
resolution and checks, output count validation, and result destruction. Those
operations are separate measurements and their times should not be subtracted
or added.
Binding and direct lookup cases are also retained from the historical driver.
Each selected case validates its output, and a validation error gives the
executable a nonzero exit status. Retain Google Benchmark JSON and the exact
build and host metadata when comparing revisions.
