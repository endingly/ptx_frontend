# PTX 9.3 WGMMA coverage

The frontend models the five WGMMA spellings as 2,151 exact logical forms: 1,092 dense `wgmma.mma_async`, 1,056 sparse `wgmma.mma_async.sp`, and one form each for `wgmma.fence`, `wgmma.commit_group`, and `wgmma.wait_group`. The canonical source is the packaged `asynchronous_warpgroup_matrix_multiply_accumulate.yaml` resource. All forms require the `sm_90a` target family. Dense MMA and controls start at PTX 8.0, sparse MMA at PTX 8.2, and mixed `.s8.u8` / `.u8.s8` forms at PTX 8.4. A generic `sm_90` target does not satisfy the `sm_90a` requirement.

Dense forms cover `.f16`, `.bf16`, `.tf32`, FP8 `.e4m3`/`.e5m2`, integer `.s8`/`.u8` with optional `.satfinite`, and `.b1.and.popc`. Sparse forms cover the corresponding floating and integer types with one `.b32` metadata register and a type-specific selector: 0 or 1 for `.f16`/`.bf16` and `.tf32`, only 0 for FP8 and integer. Floating A/B immediate scales must each be exactly -1 or 1; the `.f16`/`.bf16` descriptor forms also expose legal transpose controls. The A source is either a 64-bit opaque shared-memory descriptor or a four-register fragment; B is always a shared-memory descriptor. The D register fragment is read/write. Its cardinality is shape and accumulator-type dependent, reaching 64 registers for packed `.f16` and 128 for `.f32`/`.s32` at N=256. Ordinary vector and selector operand limits remain separate.

`MatrixInstructionDescriptor` retains M/N/K, element and register types, row/column placement, exact fragment counts, A source placement, sparse metadata kind, and a `WarpGroup128` participation identity. `ResolvedSharedMatrixDescriptor` keeps the bound `.b64` register opaque: its runtime address, alignment, swizzle, layout, and cross-warp uniformity are obligations for the producer and consumer. Each WGMMA semantic form is a final `Instruction` class with direct typed operands and immutable `matrix_topology`. MMA classes return that topology through `matrix_descriptor()`; protocol-control classes have no matrix descriptor. `Instruction::check()` verifies mutable operands, selected operand layout and target availability. The independent `WgmmaGroup` completion identity and issue/register-fence/commit/wait actions expose protocol obligations; the frontend does not prove dynamic warpgroup convergence, sequence ordering, completion before register reads, or GPU results.

`wgmma.wait_group` takes a nonnegative integer constant in a `.u64` owned immediate. PTX 9.3 states no numerical maximum, so the frontend does not impose an undocumented 0..7 or 32-bit cap. Its source sign and converted bits remain available for checker consistency. CUDA 13.3 `ptxas` accepts a complete module with wait count 2^32, while it rejects `-1`; assembler acceptance is sampled evidence rather than a new ISA bound.

The archived PTX 9.3 integer dense syntax list omits N=240 and N=256, while the WGMMA shape table and integer fragment table include them. The frontend includes both and CUDA 13.3 `ptxas` assembles representative integer forms at those sizes. This follows the shape and fragment contract and records the syntax-list discrepancy instead of silently dropping two valid sizes.

Code generation keeps all 2,151 exact class identities and emits bounded 64-form shards. Public catalogue getters return spans backed by static storage, independent of source AST lifetime. [The installed matrix consumer](../../examples/matrix_consumer/README.md) exercises WGMMA forms across a shard boundary and the control/MMA descriptor distinction.

Run the C++ and Python checks separately; the configured CTest preset excludes `python_test`:

```sh
PYTHONPATH="$PWD/python/src" ctest --preset ci-linux-clang-debug --parallel 1 --output-on-failure
PYTHONPATH="$PWD/python/src" ctest --test-dir out/build/ci-linux-clang-debug -C Debug -R '^python_test$' --parallel 1 --output-on-failure
```

Reference: [NVIDIA PTX ISA 9.3, asynchronous warpgroup-level matrix instructions](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-instructions).
