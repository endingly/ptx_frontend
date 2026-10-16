# PTX Frontend

PTX Frontend is a pre-1.0 C++23 library for parsing and analysing NVIDIA PTX.
It provides a lossless concrete syntax tree, a
typed Syntax AST, lexical binding, declaration validation, and generated
Resolved IR with target-aware checking for modelled instructions.

It is a frontend, not an assembler, code generator, or GPU executor. It does
not claim complete PTX ISA coverage; consult the coverage documents for the
supported surface and its exclusions.

Technical guides, architecture, coverage boundaries, and contribution details
live in the [GitHub Wiki](https://github.com/endingly/ptx_frontend/wiki).
The Wiki links the detailed [English](docs/us-en/) and [简体中文](docs/zh-han/)
reference documents maintained with the source.

## Build and install

The reproducible local path uses CMake presets, Ninja, GCC/G++ or Clang, and the vcpkg
manifest. Install CMake 3.28 or newer, Ninja, a C++23 compiler,
Python 3.14 or newer, Flex, and Bison 3.8 or newer;
initialize vcpkg and export its location before configuring.

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

The supported source grammar is parsed by a generated Bison C++ parser using
the existing lexer and lossless CST. Generated parser files stay private to the
build; installed-library consumers do not need Bison or its generated headers.
See the [parser design](docs/us-en/syntax_ast_design.md#generated-parser) and its
[简体中文版本](docs/zh-han/syntax_ast_design.md#生成式-parser) for grammar scope,
recovery, and parser ownership/reuse contracts. This does not expand PTX ISA
coverage or imply a parser-performance guarantee.

Compiled components are static by default. Configure with
`-DBUILD_SHARED_LIBS=ON` to build shared components, or
`-DBUILD_SHARED_LIBS=OFF` to select static components explicitly. Use separate
build and install directories for the two modes. Linux shared builds install
seven component libraries; the public component target names stay the same.
See the [Wiki build guide](https://github.com/endingly/ptx_frontend/wiki/Build-Install-and-CI) for dependency,
runtime-loading, platform, and ABI requirements.
`ci-linux-gcc-release` provides the corresponding Release build. The
`ci-linux-clang-debug` and `ci-linux-clang-release` presets provide Clang 21
builds with the same test profiles and are used by GitHub Actions. See the
[Wiki build guide](https://github.com/endingly/ptx_frontend/wiki/Build-Install-and-CI) for embedded
builds, Python specification-model use, and optional consumer coverage.

## Use from an installed package

Point CMake at the installed prefix through `CMAKE_PREFIX_PATH`. Make `fmt` and
`magic_enum` available through your toolchain or package search paths, then link
the needed component:

```cmake
cmake_minimum_required(VERSION 3.28)
project(ptx_example LANGUAGES CXX)
find_package(ptx_frontend 0.7.0 CONFIG REQUIRED COMPONENTS resolved_ir)

add_executable(example main.cpp)
target_compile_features(example PRIVATE cxx_std_23)
target_link_libraries(example PRIVATE ptx_frontend::resolved_ir)
```

Here is a small parse-and-validate example (`main.cpp`):

```cpp
#include <iostream>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Parse and validate a module, reporting frontend diagnostics. */
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

  auto module = ptx_frontend::resolved_ir::resolveAndValidateModule(*parsed);
  if (!module) {
    for (const auto& diagnostic : module.error())
      std::cerr << diagnostic.message << '\n';
    return 1;
  }

  std::cout << "resolved functions: " << module->functions.size() << '\n';
}
```

Configure your consumer with `-DCMAKE_PREFIX_PATH=/absolute/path/to/out/install/ci-linux-gcc-debug`
and a toolchain that finds the dependencies, then build and run it. Expected
output is `resolved functions: 1`. This checks frontend structure and modeled
constraints; it does not prove dataflow or GPU execution.
See the [Wiki](https://github.com/endingly/ptx_frontend/wiki/Resolved-IR-and-Checking)
for resolution-only and owned-IR validation APIs.

## License

Licensed under the [MIT License](LICENSE).
