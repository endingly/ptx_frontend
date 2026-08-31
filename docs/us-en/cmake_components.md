# Installed CMake Components

The installed `ptx_frontend` package exposes two non-target components in addition to the C++ target components.

## `ptx_spec`

Consumers can request the generic PTX ISA specification with:

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec)
```

The package then defines:

- `ptx_frontend_PTX_SPEC_DIR`, the installed directory containing the public PTX instruction YAML files;
- `ptx_frontend_PTX_SPEC_SCHEMA`, the installed `ptx-instr-v1.schema.yaml` path.

The canonical PTX specification lives in `python/code_gen/resources/ptx_spec` and is packaged as Python `code_gen` data. The CMake `ptx_spec` component installs independent raw data at `share/ptx_frontend/ptx_spec` and `share/ptx_frontend/ptx-instr-v1.schema.yaml`. `instructions/ptx_spec` remains only as a source-tree compatibility symlink.

The repository-specific C++ backend policy remains at `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml`; it is deliberately not a Python package resource and is not part of `ptx_spec`.

## `codegen`

Consumers that generate C++ artifacts can request:

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec codegen)
```

The `codegen` component installs only the CMake helper and provides `ptx_frontend_generate()` through the package config. Before configuration, install a `ptx_frontend` wheel with exactly the same version as the CMake package into the Python environment selected for CMake:

```sh
/path/to/python -m pip install /path/to/ptx_frontend-<same-version>.whl
cmake -S . -B build -DPython3_EXECUTABLE=/path/to/python
```

The helper invokes `python -m code_gen`. Generation requires the consumer to select a backend specification explicitly:

```cmake
ptx_frontend_generate(
    TARGET generate_my_backend
    SPEC_DIR "${ptx_frontend_PTX_SPEC_DIR}"
    BACKEND_SPEC "${CMAKE_CURRENT_SOURCE_DIR}/my_backend.yaml"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
add_dependencies(my_target generate_my_backend)
```

`TARGET` is required so a project may generate more than one backend or output
directory. The helper declares generated files, but the target is not built by
default; attach it to the consumer target with `add_dependencies()`.

Installed codegen does not fall back to `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` and does not require the original repository checkout. The current Python import packages remain `base`, `code_gen`, and `ir`; namespacing them under `ptx_frontend` is tracked separately.

The CMake package verifies the raw spec/schema and the selected Python distribution's `code_gen` import and matching version before setting `ptx_frontend_ptx_spec_FOUND` or `ptx_frontend_codegen_FOUND`.
