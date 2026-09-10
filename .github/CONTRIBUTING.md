# CI workflows and cache ownership

| Workflow | Events | Checks |
| --- | --- | --- |
| `linux-ci.yml` | PR opened/updated/reopened, monthly schedule, manual dispatch | Full GCC Debug and Release build/test presets; check names remain `Debug` and `Release` |
| `python-and-package-consumer.yml` | Same full-acceptance events | Python tests, CI helper tests, and the complete installed-package consumer; check name remains `test` |
| `integration-smoke.yml` | Push to `main` or `dev` | Full GCC Debug/Release builds for cache warming; Debug installed-package consumer only |
| `release-wheel.yml` | Push of a `v*` tag | Existing wheel build, smoke, and release publication |

Full PR acceptance remains the merge gate. Integration cache warming does not establish
the same coverage and is not a substitute for validating a direct push. Scheduled
and manually dispatched full runs remain available; separate event/workflow
concurrency groups keep an integration push from cancelling them. Release
publication behavior is unchanged.

## Integration cache warming

Both integration jobs configure and build the same normal presets as PRs:
`ci-linux-gcc-debug` and `ci-linux-gcc-release`. This warms production libraries,
unit-test executables, and default-build generated fixtures in their actual PR
build directories, with separate compiler-cache namespaces for each build type.
The integration jobs do not invoke the C++ test suite or Python tests. Building
test executables may still run their normal CMake/GoogleTest discovery commands.

Debug additionally runs just the existing installed-package acceptance test:

```sh
ctest --preset ci-python-and-package-consumer \
  -R '^ptx_frontend\.package_consumer$' --output-on-failure --no-tests=error
```

Its normal and relocated consumer builds use the same scratch paths and compiler
arguments as the PR package job. The outer test registration forwards both
compilers and compiler launchers with the script's `PTX_CMAKE_*` argument names.
It also forwards the parent's module-scanning policy (OFF; this project does not
use C++ modules), avoiding unnecessary module flags that prevent caching these
consumers. This lets nested builds actually use ccache when enabled. Integration
uses this consumer instead of the separate smoke consumer, whose Debug object
cache could not be reused at the full consumer's different working directory.
Release builds only. The cost
of broader prewarming is a full build for each configuration on integration
pushes; it avoids repeating full test execution, not the necessary compilation.

## Local library-only smoke

With the usual Python generator dependencies and vcpkg environment installed:

```sh
cmake --preset ci-integration-smoke
cmake --build --preset ci-integration-smoke
ctest --preset ci-integration-smoke --output-on-failure --no-tests=error
```

The equivalent local workflow is `cmake --workflow --preset ci-integration-smoke`.
This lightweight preset is not the integration workflow's cache-warming build.
The build selects only `resolved_ir`. Its dependency closure builds all installed
project libraries and generated public headers without building unit-test
executables. Configuration still enables testing and requires its dependencies;
this is not a dependency-free or generator-free build.

The smoke preset opts into `PTX_FRONTEND_ENABLE_PACKAGE_SMOKE_TEST` and selects
exactly `ptx_frontend.package_consumer_smoke`. This installs the package, checks
public headers/resources and private-file exclusions, separately configures and
builds the existing public consumer, then runs it. The consumer checks meaningful
parser/resolver/checker behavior, owned declaration metadata, and FMA metadata,
not merely linking. An empty CTest selection is an error.

The existing `ptx_frontend.package_consumer` remains the full test, including
relocation, non-default data paths, and negative package-discovery cases. It
explicitly disables smoke-only behavior even when the additional smoke test is
registered. Each test has its own install/build scratch directories.

Smoke and normal Debug presets share `out/build/ci-linux-gcc-debug` and its install
prefix so their compiler-cache objects use matching build paths. Do not configure
or build these presets concurrently in one checkout. Reconfigure with
`ci-linux-gcc-debug` to disable the optional smoke registration before normal
acceptance. Release uses its own build directory.

## Cache reuse and limitations

The workflows share `.github/actions/setup-linux` for system packages, Python
dependencies, and vcpkg setup. Cache identity includes the installed toolchain,
build tools, OS release, and `ImageOS`, but excludes `ImageVersion`: an image
revision alone does not invalidate caches. vcpkg uses the manifest's exact builtin baseline. Dependency
archives are separate from source downloads. In both matrix workflows, every
Debug/Release job saves a missing exact vcpkg binary-cache key after successful
configuration. Installed toolchains can differ within one matrix, so a fixed Debug
writer cannot populate every Release key. Jobs sharing a key may race to save;
the cache action handles duplicate saves without failing the job.
Only Debug writes shared APT, pip, and vcpkg source-download caches. The separate
Python/package job remains restore-only for dependency caches.

Integration and PR Debug jobs share a compiler-cache namespace; Release has its
own shared namespace. The Python/package job restores Debug caches but does not
upload a smaller library/consumer-only snapshot that could supersede a full-build
snapshot. Full-build jobs publish per-run snapshots so caches can advance after
source changes; this does not bypass ccache content validation.

Prewarming covers the normal Debug/Release build graphs and the Debug package
consumer's compiled paths. It does not promise hits for changed sources or
headers, compiler flags, test-only reconfiguration variants, or objects evicted
from the cache. This repository has no Clang acceptance matrix to warm.
Matching build paths are intentional: ccache normally hashes the working
directory for Debug compilations. See the [ccache path-hashing contract](https://ccache.dev/manual/latest.html#config_hash_dir).

GitHub allows PRs to restore default/base-branch caches, but PR merge-ref caches
cannot warm the default branch or sibling PRs. Keeping integration-branch cache
writes avoids relying on that impossible direction of reuse. See the
[GitHub cache access rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache).
New cache-key namespaces initially miss; eviction and runner/toolchain changes
can also cause misses. Cache hits and end-to-end savings require observation on
hosted runs; local smoke success alone does not prove them.

Third-party action references are pinned and checked across workflows and local
composite actions. CI helper regression tests run in the Python acceptance job.
Workflow linting, helper tests, both full build presets, and the existing
Debug package consumer are the relevant local checks for cache-warming changes; frontend
behavior changes still require the ordinary project gates.
