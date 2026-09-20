# Installed CMake Components

The installed `ptx_frontend` CMake package exposes the C++ target components and one non-target data component: `ptx_spec`.

Each C++ component has the stable public target name
`ptx_frontend::<component>` both after `find_package` and when the source tree
is embedded with `add_subdirectory`. The underlying source-build targets use
the `ptx_frontend_` prefix, so they do not collide with generic target names in
the parent project; those implementation names are not a public API.

## `ptx_spec`

Consumers can request the generic PTX ISA specification with:

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec)
```

The package then defines:

- `ptx_frontend_PTX_SPEC_DIR`, the installed directory containing the public PTX instruction YAML files;
- `ptx_frontend_PTX_SPEC_SCHEMA`, the installed `ptx-instr-v1.schema.yaml` path.

The canonical PTX specification lives in `python/src/ptx_frontend/spec/resources/ptx_spec` and is also packaged as Python package data. The CMake `ptx_spec` component installs independent raw data at `share/ptx_frontend/ptx_spec` and `share/ptx_frontend/ptx-instr-v1.schema.yaml`. `instructions/ptx_spec` is the repository input directory used by the source build.

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

`ptx_frontend.code_gen` remains an implementation namespace for the frontend source build. Its packaged `cli`, `context`, `plan`, and `emit` modules form the deterministic in-tree generator: a frozen context projects backend aliases once, and one plan supplies listing, emission, and formatting order. New downstream code should not depend on those implementation APIs. Repository-only corpus tools live under `tools/corpus` and are excluded from the wheel. The wheel does not install a `ptx-frontend-codegen` console script.

## Test profiles

`BUILD_TESTING=ON` builds the normal `test_resolved_ir` suite. The default is
deliberately free of alternate generated fixtures, self-heal/topology checks,
embedded-parent checks, and installed-package consumers.

`PTX_FRONTEND_BUILD_CONSUMER_TESTS=ON` requires `BUILD_TESTING=ON` and adds that
consumer/integration group. The `ci-consumer-integration` configure, build, and
test presets select it and run the stable CTest `consumer` label. The normal
Debug and Release presets leave it off, so ordinary C++ and Python unit checks
do not indirectly configure or build consumer fixtures.

CI runs the consumer/integration profile automatically on pushes to `main`,
alongside the normal Debug/Release cache-prewarming jobs. It also supports
manual dispatch and gates wheel publication on version tags. Pushes to `dev`
run only the normal prewarming jobs. The consumer job restores compatible
compiler caches without publishing production cache seeds.
