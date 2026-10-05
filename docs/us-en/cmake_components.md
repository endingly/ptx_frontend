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

The canonical PTX specification lives in `python/src/ptx_frontend/spec/resources/ptx_spec` and is also packaged as Python package data. The CMake `ptx_spec` component installs independent raw data at `share/ptx_frontend/ptx_spec` and `share/ptx_frontend/ptx-instr-v1.schema.yaml`. `instructions/ptx_spec` is the repository input directory used by the source build.

The C++ backend mapping is sourced from `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` and installed at `share/ptx_frontend/ptx_cpp_backend_spec/ptx_frontend.yaml`. Its schema is installed at `share/ptx_frontend/ptx-cpp-backend-v2.schema.yaml`. The four resource paths are checked when `ptx_spec` is requested, and the exported paths are relative to the package's installed prefix so the package can be relocated.

## Python model reuse

The installed CMake package does not export a code-generation component or a `ptx_frontend_generate()` helper. Code generation is an implementation detail of the frontend source build and of downstream projects that own their own generators.

Python consumers that need the normalized PTX specification model should use the public `ptx_frontend.spec` namespace:

```python
from ptx_frontend.spec import load_packaged_spec_database
from ptx_frontend.spec.model import InstructionSpec

database = load_packaged_spec_database()
```

`ptx_frontend.spec` is the downstream-facing Python API. It exposes the reusable instruction model, database loaders, normalization helpers, and resource accessors while preserving the same underlying model types used by the frontend itself. Consumers should treat the `ptx-instr/v1` schema as the stable data contract.

`ptx_frontend.code_gen` remains an implementation namespace for the frontend source build. Its packaged `cli`, `context`, `plan`, and `emit` modules form the deterministic in-tree generator: a frozen context projects backend aliases once, and one plan supplies listing, emission, and formatting order. New downstream code should not depend on those implementation APIs. Repository-only corpus tools live under `tools/corpus` and are excluded from the wheel. The wheel does not install a `ptx-frontend-codegen` console script.

## Test profiles

`BUILD_TESTING=ON` builds the active C++ suites, including
`resolved_ir_smoke` and `test_resolved_ir`. The GCC and Clang Debug/Release presets enable
testing. Check the installed `resolved_ir` component with
`examples/resolved_ir_consumer` in a separate build directory and installed
package prefix.
