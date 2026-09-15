# PTX Frontend

PTX Frontend is a C++23 library for parsing and analysing a deliberately
limited subset of NVIDIA PTX. It provides a lossless concrete syntax tree, a
typed Syntax AST, lexical binding, declaration validation, and generated
Resolved IR with target-aware checking for modelled instructions.

It is a frontend, not an assembler, code generator, or GPU executor. It does
not claim complete PTX ISA coverage; consult the coverage documents for the
supported surface and its exclusions.

## Documentation

- [English documentation](docs/us-en/) and [简体中文文档](docs/zh-han/) cover
  design decisions, public contracts, and per-family support boundaries.
- [Installed CMake components](docs/us-en/cmake_components.md) describe downstream targets and public PTX-spec data.
- [Resolved IR design](docs/us-en/resolved_ir_design.md) describes resolution
  and validation entry points.
- The [project Wiki](https://github.com/endingly/ptx_frontend/wiki) provides additional project material.

## Build and install

The reproducible local path uses CMake presets, Ninja, GCC/G++, and the vcpkg
manifest. Install CMake 3.28 or newer, a C++23 compiler, Python 3, Flex, and
`clang-format`; initialize vcpkg and export its location before configuring.

```sh
export VCPKG_ROOT=/path/to/vcpkg
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements.txt

cmake --preset ci-linux-gcc-debug -DBUILD_TESTING=OFF
cmake --build --preset ci-linux-gcc-debug
cmake --install out/build/ci-linux-gcc-debug
```

The installed prefix for this preset is `out/install/ci-linux-gcc-debug`.
`ci-linux-gcc-release` provides the corresponding Release build. See the
[CMake component documentation](docs/us-en/cmake_components.md) for embedded
builds, Python specification-model use, and optional consumer coverage.

## Use from an installed package

Point CMake at the installed prefix through `CMAKE_PREFIX_PATH`. Make `fmt` and
`magic_enum` available through your toolchain or package search paths, then link
the needed component:

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS resolved_ir)

add_executable(example main.cpp)
target_compile_features(example PRIVATE cxx_std_23)
target_link_libraries(example PRIVATE ptx_frontend::resolved_ir)
```

Here is a small parse-and-resolve example:

```cpp
#include <iostream>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

int main() {
  constexpr std::string_view source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.entry kernel() {
  .reg .u32 %r<2>;
  add.u32 %r0, %r1, 1;
}
)ptx";

  ptx_frontend::PtxSyntaxParser parser{source};
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    for (const auto& diagnostic : parsed.diagnostics)
      std::cerr << diagnostic.message << '\n';
    return 1;
  }

  auto module = ptx_frontend::resolved_ir::resolveModule(*parsed);
  if (!module) {
    for (const auto& diagnostic : module.error())
      std::cerr << diagnostic.message << '\n';
    return 1;
  }

  std::cout << "resolved functions: " << module->functions.size() << '\n';
}
```

Use `resolveAndValidateModule` or `validateModule` when your application needs
an explicit complete target-validation contract; see the [Resolved IR
design](docs/us-en/resolved_ir_design.md) for their distinct guarantees.

## License

Licensed under the [MIT License](LICENSE).
