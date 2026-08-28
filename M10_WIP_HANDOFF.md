# M10 I16 WIP handoff

> **Temporary handoff file: delete this file when M10-I16 is completed and
> verified.**

This commit preserves the unfinished M10-I16 slice for
`ldmatrix.sync.aligned.m8n8.x2.shared.b16`.

## Included work

- Registers `ldmatrix` as partially supported.
- Adds the frozen PTX 6.5 / SM 75 variant with a two-register `b32`
  destination fragment and a shared-memory address.
- Adds Python model/generator coverage.
- Adds C++ resolved-IR and checker positive/negative coverage.

## Verification state

- `git diff --check` passes.
- The Python suite passes locally with 101 tests.
- The C++ test has not been compiled locally because the sandbox lacks the
  CMake toolchain; verify it in GitHub Actions.
- The branch currently has an inherited, unrelated M10-I02 CI failure in
  `ResolvedModule.ResolvesAndChecksM10CacheHintEvictionSlice`: the runtime
  eviction-priority lookup does not strip the exact `.L1::` prefix.

## Before completing M10-I16

1. Run the Python suite and both Debug/Release GitHub Actions jobs.
2. Confirm the generated `Ldmatrix::SyncAlignedM8n8X2SharedB16` API compiles
   and the new C++ test passes after the inherited I02 blocker is fixed.
3. Review the PTX section reference and frozen target constraints against the
   authoritative spec.
4. Remove this handoff file in the commit that completes/verifies I16.
