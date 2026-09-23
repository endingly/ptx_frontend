# Conversion consumer

This example consumes the installed `ptx_frontend` package through its public
CMake target `ptx_frontend::resolved_ir` and the `ptx_spec` data component,
requesting package version `0.2.0`. Configuration checks that the installed PTX
and C++ backend YAML/schema paths all exist.
The source covers a targeted module with these representative forms:

- `isspacep.shared::cluster` with a `.u32` address;
- `prmt.b32.rc16` with a selector register and an immediate selector;
- `cvt.pack.sat.u2.s32.b32` with an immediate carry value;
- ordinary `cvt.rmi.s32.f32`;
- scaled `cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.f32`;
- typed `testp.normal.f32`, `copysign.f32`, and FP32/BF16 transcendental forms;
- binary and ternary FP32 `min`/`max` modifier and operand layouts;
- mixed FP32/F16 `add` and FP32/BF16 `sub`, with register and floating-immediate
  addend/subtrahend values;
- ordinary `set` with typed floating comparison and Boolean predicate constant;
- ordinary `selp.s32` and the retained `selp.u32` alternative, including
  complemented register and integer predicate sources.

The module uses PTX 9.3 and `sm_121a`, which supplies the exact target context
for the scaled `s2f6x2` form. The program also checks the public scalar and
rounding enum values at compile time. It resolves the syntax while parser state
is alive, moves the result into an owned resolved module, and validates that
owned model after the source, parser, and AST have left scope. Runtime checks
inspect the typed instruction variants and revalidate deliberate invalid
modifier, layout, property, comparison, and selection type mutations, including
in Release builds.

Configure and build it from the repository checkout after installing the
library to a fresh prefix:

```sh
fresh_prefix=/absolute/path/to/fresh-ptx-frontend
cmake --preset ci-linux-gcc-debug
cmake --build out/build/ci-linux-gcc-debug
cmake --install out/build/ci-linux-gcc-debug \
  --prefix "$fresh_prefix"

dep_prefix="$PWD/out/build/ci-linux-gcc-debug/vcpkg_installed/x64-linux"
cmake -S examples/conversion_consumer -B /tmp/conversion-consumer-build \
  -DCMAKE_BUILD_TYPE=Release \
  -Dptx_frontend_DIR="$fresh_prefix/lib/cmake/ptx_frontend" \
  -DCMAKE_PREFIX_PATH="$fresh_prefix;$dep_prefix"
cmake --build /tmp/conversion-consumer-build
/tmp/conversion-consumer-build/conversion_consumer
```

For the current `ci-linux-gcc-debug` cache, `CMAKE_INSTALL_LIBDIR` is `lib`,
so the package configuration is installed under
`<fresh-prefix>/lib/cmake/ptx_frontend`. If a different configured install
profile uses `lib64`, substitute that directory in `ptx_frontend_DIR`.
`ptx_frontend_DIR` and `CMAKE_PREFIX_PATH` must point to the same fresh install
prefix used for the final verification. The dependency prefix supplies `fmt`
and `magic_enum` when they are not available through the system package search
path.
