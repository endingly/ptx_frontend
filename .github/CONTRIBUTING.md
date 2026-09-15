# CI workflows and cache ownership

| Workflow | Events | Checks |
| --- | --- | --- |
| `linux-ci.yml` | PR opened/updated/reopened, monthly schedule, manual dispatch | Full GCC Debug and Release build/test presets; check names remain `Debug` and `Release` |
| `python-and-package-consumer.yml` | PR opened/updated/reopened, monthly schedule, manual dispatch | Python unit tests and formatting; check name remains `test` |
| `integration-smoke.yml` | Push to `main` or `dev`; manual dispatch | Pushes refresh normal Debug/Release production and unit-test caches; manual dispatch runs opt-in consumer/integration coverage |
| `release-wheel.yml` | Push of a `v*` tag | Consumer/integration validation, then wheel build, smoke, and release publication |

Normal Debug/Release and Python unit coverage remain the regular merge checks.
Consumer/integration coverage is manually selectable and mandatory before tag
publication. Separate event/workflow concurrency groups keep an integration
push from cancelling PR coverage.

## Formatting and naming

CI runs `clang-format-21 --dry-run --Werror` on tracked handwritten C and C++
sources. Generated and vendor-owned files are excluded: regenerate them through
their owning pipeline instead of formatting them by hand. New C++ interfaces use
`snake_case`; existing public spellings remain compatible unless an explicitly
reviewed API migration says otherwise. The maintained [naming and legacy
audit](../docs/us-en/code_conventions.md) records the current exceptions.

## Integration cache warming

Pushes configure the normal `ci-linux-gcc-debug` and `ci-linux-gcc-release`
presets, build `ptx_frontend_resolved_ir`, then build and run the normal
`test_resolved_ir` suite in a disposable compiler cache. These presets explicitly
leave `PTX_FRONTEND_BUILD_CONSUMER_TESTS` off. The persistent seed contains only
production objects; a successful push refreshes it only after the matching unit
test completes.

Manual dispatch runs the independent `ci-consumer-integration`
preset. It enables `PTX_FRONTEND_BUILD_CONSUMER_TESTS`, builds only the production
resolved-IR target and modern-operand fixture executable, and selects CTest tests
by the stable `consumer` label. The group covers generated-fixture self-heal and
topology checks, embedded-parent use, and the full installed-package consumer.
It restores compatible caches but never saves a main seed.

## Local consumer and integration coverage

With the usual Python generator dependencies and vcpkg environment installed:

```sh
cmake --workflow --preset ci-consumer-integration
```

This uses an independent build/install directory and `BUILD_TESTING=ON` together
with `PTX_FRONTEND_BUILD_CONSUMER_TESTS=ON`. The label selection includes the full
installed-package test (relocation, non-default data paths, and negative discovery
cases), not a reduced smoke duplicate. Do not configure or build the same preset
directory concurrently.

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
Only Debug writes shared APT, pip, and vcpkg source-download caches. The Python
unit and manual consumer jobs restore dependency caches without owning a main seed.

Integration and PR Debug jobs share a compiler-cache namespace; Release has its
own shared namespace. Manual and tag consumer jobs restore Debug caches but do not
upload a smaller consumer-only snapshot that could supersede a production seed.
Trusted-main production jobs publish per-run snapshots so caches can advance after
source changes; this does not bypass ccache content validation.

Prewarming covers the normal Debug/Release build graphs and their normal unit
tests, not opt-in consumer fixtures. It does not promise hits for changed sources
or headers, compiler flags, opt-in reconfiguration variants, or objects evicted
from the cache. This repository has no Clang acceptance matrix to warm.
Matching build paths are intentional: ccache normally hashes the working
directory for Debug compilations. See the [ccache path-hashing contract](https://ccache.dev/manual/latest.html#config_hash_dir).

GitHub allows PRs to restore default/base-branch caches, but PR merge-ref caches
cannot warm the default branch or sibling PRs. Keeping integration-branch cache
writes avoids relying on that impossible direction of reuse. See the
[GitHub cache access rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache).
New cache-key namespaces initially miss; eviction and runner/toolchain changes
can also cause misses. Cache hits and end-to-end savings require observation on
hosted runs; local validation success alone does not prove them.

Third-party action references are pinned and checked across workflows and local
composite actions. Workflow linting, Python unit tests, local Debug tests,
cloud Release coverage, and opt-in consumer coverage are the relevant CI checks;
frontend behavior changes still require the ordinary project gates.
