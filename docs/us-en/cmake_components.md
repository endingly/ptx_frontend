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

The repository-specific C++ backend policy is deliberately not part of `ptx_spec`.

## `codegen`

Consumers that generate C++ artifacts can request:

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec codegen)
```

The `codegen` component installs the Python dependency closure used by `gen_all.py`, including its validation schemas, and provides `ptx_frontend_generate()` through the package config. Generation requires the consumer to select a backend specification explicitly:

```cmake
ptx_frontend_generate(
    SPEC_DIR "${ptx_frontend_PTX_SPEC_DIR}"
    BACKEND_SPEC "${CMAKE_CURRENT_SOURCE_DIR}/my_backend.yaml"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
```

Installed codegen does not fall back to `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` and does not require the original repository checkout. The current Python import packages remain `base`, `code_gen`, and `ir`; namespacing them under `ptx_frontend` is tracked separately.

The CMake package verifies the installed resources before setting `ptx_frontend_ptx_spec_FOUND` or `ptx_frontend_codegen_FOUND`, so a requested component does not succeed merely because its name is known.
