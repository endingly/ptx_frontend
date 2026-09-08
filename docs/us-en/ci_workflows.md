# CI workflows and cache ownership

| Workflow | Events | Checks |
| --- | --- | --- |
| `linux-ci.yml` | PR opened/updated/reopened, monthly schedule, manual dispatch | Full GCC Debug and Release build/test presets; check names remain `Debug` and `Release` |
| `python-and-package-consumer.yml` | Same full-acceptance events | Python tests, CI helper tests, and the complete installed-package consumer; check name remains `test` |
| `integration-smoke.yml` | Push to `main` or `dev` | GCC Debug production-library build and installed public API smoke |
| `release-wheel.yml` | Push of a `v*` tag | Existing wheel build, smoke, and release publication |

Full PR acceptance remains the merge gate. Integration smoke does not establish
the same coverage and is not a substitute for validating a direct push. Scheduled
and manually dispatched full runs remain available; separate event/workflow
concurrency groups keep an integration push from cancelling them. Release
publication behavior is unchanged.

## Integration smoke

With the usual Python generator dependencies and vcpkg environment installed:

```sh
cmake --preset ci-integration-smoke
cmake --build --preset ci-integration-smoke
ctest --preset ci-integration-smoke --output-on-failure --no-tests=error
```

The equivalent local workflow is `cmake --workflow --preset ci-integration-smoke`.
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
dependencies, and vcpkg setup. Cache identity includes the installed toolchain
and runner image; vcpkg uses the manifest's exact builtin baseline. Dependency
archives are separate from source downloads. Designated jobs write shared
dependency/download caches; other jobs restore and use them.

Smoke, full Debug, and the Python/package job share the Debug compiler-cache
namespace. Release has a separate namespace. Per-run compiler snapshots can
advance after source changes; this does not bypass ccache content validation.
Integration smoke warms shared library objects, not the entire unit-test or
Release object set. Its consumer uses a separate scratch path, so reuse by the
full package consumer is not guaranteed. This repository has no Clang acceptance
matrix to warm.

GitHub allows PRs to restore default/base-branch caches, but PR merge-ref caches
cannot warm the default branch or sibling PRs. Keeping integration-branch cache
writes avoids relying on that impossible direction of reuse. See the
[GitHub cache access rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache).
New cache-key namespaces initially miss; eviction and runner/toolchain changes
can also cause misses. Cache hits and end-to-end savings require observation on
hosted runs; local smoke success alone does not prove them.

Third-party action references are pinned and checked across workflows and local
composite actions. CI helper regression tests run in the Python acceptance job.
Workflow linting, helper tests, a fresh library-only smoke build, and the existing
full package consumer are the relevant local checks for this split; frontend
behavior changes still require the ordinary project gates.
