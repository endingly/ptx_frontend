# CI workflows and cache ownership

| Workflow | Events | Checks |
| --- | --- | --- |
| `linux-ci.yml` | PR opened/updated/reopened, monthly schedule, manual dispatch | Full Clang Debug and Release build/test presets; check names remain `Debug` and `Release` |
| `python-and-package-consumer.yml` | PR opened/updated/reopened, monthly schedule, manual dispatch | Python unit tests and formatting; check name remains `test` |
| `integration-smoke.yml` | Push to `main` or `dev`; manual dispatch | Pushes refresh normal Clang Debug/Release production and unit-test caches |
| `release-wheel.yml` | Push of a `v*` tag | Clang Debug tests and installed public API consumer, then wheel build, smoke, and release publication |

Normal Debug/Release and Python unit coverage remain the regular merge checks.
The installed public API consumer runs in the PR Debug job and before tag
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

Pushes configure the normal `ci-linux-clang-debug` and `ci-linux-clang-release`
presets, build `ptx_frontend_resolved_ir`, then build and run the normal
`test_resolved_ir` suite in a disposable compiler cache. A successful main push
refreshes its compiler-cache seed only after the matching unit test completes.
GitHub Actions currently compiles project code with Clang 21; the GCC presets
remain available for local validation.

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
unit and release jobs restore dependency caches without owning a main seed.

Integration and PR Debug jobs share a compiler-cache namespace; Release has its
own shared namespace. The tag job restores Debug caches but does not upload a
smaller snapshot that could supersede a production seed.
Trusted-main production jobs publish per-run snapshots so caches can advance after
source changes; this does not bypass ccache content validation.

Prewarming covers the normal Debug/Release build graphs and their normal unit
tests, not the standalone installed consumer. It does not promise hits for changed
sources or headers, compiler flags, opt-in reconfiguration variants, or objects
evicted from the cache.
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
cloud Release coverage, and installed-consumer coverage are the relevant CI checks;
frontend behavior changes still require the ordinary project gates.
