# Installed CMake Components

The installed `ptx_frontend` CMake package exposes the C++ target components and one non-target data component: `ptx_spec`.

## `ptx_spec`

Consumers can request the generic PTX ISA specification with:

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec)
```

The package then defines:

- `ptx_frontend_PTX_SPEC_DIR`, the installed directory containing the public PTX instruction YAML files;
- `ptx_frontend_PTX_SPEC_SCHEMA`, the installed `ptx-instr-v1.schema.yaml` path.

The canonical PTX specification lives in `python/code_gen/resources/ptx_spec` and is also packaged as Python package data. The CMake `ptx_spec` component installs independent raw data at `share/ptx_frontend/ptx_spec` and `share/ptx_frontend/ptx-instr-v1.schema.yaml`. `instructions/ptx_spec` remains only as a source-tree compatibility symlink.

The repository-specific C++ backend policy remains at `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml`; it is deliberately not part of the public `ptx_spec` component.

## Python model reuse

The installed CMake package does not export a code-generation component or a `ptx_frontend_generate()` helper. Code generation is an implementation detail of the frontend source build and of downstream projects that own their own generators.

Python consumers that need the normalized PTX specification model should use the public `ptx_frontend.spec` namespace:

```python
from ptx_frontend.spec import load_packaged_spec_database
from ptx_frontend.spec.model import InstructionSpec

database = load_packaged_spec_database()
```

`ptx_frontend.spec` is the downstream-facing Python API. It exposes the reusable instruction model, database loaders, normalization helpers, and resource accessors while preserving the same underlying model types used by the frontend itself. Consumers should treat the `ptx-instr/v1` schema as the stable data contract.

`ptx_frontend.code_gen` remains an implementation/compatibility namespace for the frontend source build. New downstream code should not depend on it. Frontend-only generator modules (`cli.py`, `gen_*.py`, and repository corpus-generation helpers) live under the source-only `python/code_gen/_frontend` directory and are deliberately excluded from the wheel. The wheel also does not install a `ptx-frontend-codegen` console script.
