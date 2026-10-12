# Installed CMake Components

The installed `ptx_frontend` CMake package exposes the C++ target components and one non-target data component: `ptx_spec`.

Each C++ component has the stable public target name
`ptx_frontend::<component>` both after `find_package` and when the source tree
is embedded with `add_subdirectory`. The underlying source-build targets use
the `ptx_frontend_` prefix, so they do not collide with generic target names in
the parent project; those implementation names are not a public API.

The active resolved-IR target is `ptx_frontend::resolved_ir`.
`ptx_frontend::ptx_frontend` links this target. Its interface uses direct
semantic-form classes and `std::unique_ptr<Instruction>` ownership.

## Static and shared libraries

`BUILD_SHARED_LIBS` defaults to `OFF`. Set `-DBUILD_SHARED_LIBS=ON` when
configuring the source build to select shared libraries, or explicitly set
`-DBUILD_SHARED_LIBS=OFF` for static archives. For an embedded build, set the
variable before `add_subdirectory(ptx_frontend)`. Use separate build and install
prefixes for each mode; an installed package contains the selected mode.

The seven compiled components (`base`, `lexer`, `cst`, `syntax`, `binding`,
`semantic`, and `resolved_ir`) follow this setting. On Linux, shared mode
produces seven `libptx_frontend_<component>.so` libraries. `common` and the
`ptx_frontend` umbrella remain interface targets. Target names, component
selection, and transitive dependencies are identical in both modes.

CMake enables position-independent code for the shared components. Any static
dependency linked into them, including a static `fmt`, must also have been
built with position-independent code. The project does not change imported
dependency build settings. Downstream package discovery still requires `fmt`
and `magic_enum`.

Installed Linux shared components append `$ORIGIN` to their install runtime
search paths so sibling libraries remain discoverable when the prefix moves.
Consumers must make the installed library directory discoverable to their
runtime loader; linking imported CMake targets normally gives build-tree
executables the appropriate runtime path. Deployments must also provide any
shared third-party dependencies through their normal loader configuration.
Existing Linux Clang CI continues to use the default static mode. Shared mode
has been verified locally with a full Release build, C++ tests, and independent
installed consumers after moving the installation to a new prefix.

Shared builds are verified on Linux. Windows shared configuration fails with
an explicit diagnostic because DLL export/import support has not been audited;
macOS shared builds are unvalidated. Shared mode adds no stable ABI or SONAME
versioning guarantee. Rebuild consumers using matching installed headers,
libraries, and a compatible C++ toolchain when the frontend changes. Existing
`SameMinorVersion` CMake package-version compatibility remains unchanged.

## `ptx_spec`

Consumers can request the generic PTX ISA specification with:

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec)
```

The package then defines:

- `ptx_frontend_PTX_SPEC_DIR`, the installed directory containing the public PTX instruction YAML files;
- `ptx_frontend_PTX_SPEC_SCHEMA`, the installed `ptx-instr-v1.schema.yaml` path;
- `ptx_frontend_PTX_CPP_BACKEND_SPEC`, the installed C++ backend mapping YAML path;
- `ptx_frontend_PTX_CPP_BACKEND_SCHEMA`, the installed backend schema path.

The canonical PTX specification lives in `python/src/ptx_frontend/spec/resources/ptx_spec` and is also packaged as Python package data. The CMake `ptx_spec` component installs independent raw data at `share/ptx_frontend/ptx_spec` and `share/ptx_frontend/ptx-instr-v1.schema.yaml`. `instructions/ptx_spec` is the repository source-build spelling; the tracked
`instructions` symlink aliases the Python resource directory, so both paths
refer to the same source data. Source generation tracks YAML files recursively below that directory, excluding `.schema.yaml` files as the Python loader does. Changes to the file set also invalidate generation so removed inputs cannot leave obsolete generated outputs.

The C++ backend mapping is sourced from `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` and installed at `share/ptx_frontend/ptx_cpp_backend_spec/ptx_frontend.yaml`. Its schema is installed at `share/ptx_frontend/ptx-cpp-backend-v2.schema.yaml`. The four resource paths are checked when `ptx_spec` is requested, and the exported paths are relative to the package's installed prefix so the package can be relocated.

## Python model reuse

The installed CMake package does not export a code-generation component or a `ptx_frontend_generate()` helper. Code generation is an implementation detail of the frontend source build and of downstream projects that own their own generators.

Python consumers that need the normalized PTX specification model should use the public `ptx_frontend.spec` namespace:

```python
from ptx_frontend.spec.database import load_packaged_spec_database
from ptx_frontend.spec.model import InstructionSpec

database = load_packaged_spec_database()
```

`ptx_frontend.spec` is the downstream-facing Python API. It exposes the reusable instruction model, database loaders, normalization helpers, and resource accessors while preserving the same underlying model types used by the frontend itself. Consumers should treat the `ptx-instr/v1` schema as the stable data contract.

`ptx_frontend.code_gen` remains an implementation namespace for the frontend source build. Its packaged `cli`, `context`, `plan`, and `emit` modules form the deterministic in-tree generator: a frozen context projects backend aliases once, and one plan supplies stable listing and manifest order. Artifact emission may finish in a different order when `--jobs` exceeds one; generated bytes do not depend on worker order. The CLI defaults to six workers, and `--jobs 1` uses serial emission. The source build sets a single-process artifact-writer budget with the `PTX_FRONTEND_CODEGEN_JOBS` CMake cache variable (default `6`). One aggregate build command selects dirty categories from contributor and membership timestamps, then generates those categories and required shared outputs in one Python process. A separate configure-time description comes from one normalized spec snapshot and assigns source dependencies to their contributing categories. The source build keeps this graph in `cmake/ptx_resolved_ir_codegen.cmake`; `submod/resolved_ir/CMakeLists.txt` consumes its generated source and include paths. New downstream code should not depend on those implementation APIs. Repository-only corpus tools live under `tools/corpus` and are excluded from the wheel. The wheel does not install a `ptx-frontend-codegen` console script.

## Test profiles

The Linux Clang 21 configure presets select `/usr/bin/llvm-ar-21` and
`/usr/bin/llvm-ranlib-21` for static archives. The shared Linux CI setup installs
these tools through the `llvm-21` package. Local preset users need that package
too, or can override `CMAKE_AR` and `CMAKE_RANLIB` on the CMake command line.
GCC presets and configurations without these presets retain CMake's toolchain
selection, including builds embedded with `add_subdirectory`.

`BUILD_TESTING=ON` builds the active C++ suites, including
`resolved_ir_smoke` and `test_resolved_ir`. The GCC and Clang Debug/Release presets enable
testing. Check the installed `resolved_ir` component with
`examples/resolved_ir_consumer` in a separate build directory and installed
package prefix.

## Prerequisites and test boundaries

Python installation requires Python 3.14 or newer. Loaders are in
`ptx_frontend.spec.database`, model types in `ptx_frontend.spec.model`, and
resource helpers in `ptx_frontend.spec.resources`; the namespace root does not
re-export these APIs. Linux Clang configuration requires `ld.lld-21` or
`ld.lld` in addition to the preset's LLVM archive tools; CI installs `lld-21`.
CTest presets exclude `python_test`; installed C++ public API consumer verification runs as a separate
Clang Debug Linux CI step. Run the Python unit lane separately.
