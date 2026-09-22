# Floating and mixed `add`/`sub` coverage

This slice is the [PTX 9.3 §9.7.3.3/§9.7.3.4, §9.7.4.1/§9.7.4.2 and §9.7.5.1/§9.7.5.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions) reconciliation for [`add`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-add) and [`sub`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sub). It **audits** the existing scalar, packed, half/bfloat, and mixed-precision cohorts against assembler evidence rather than adding a second implementation. Only one confirmed contract gap was found, and it is fixed here.

| Form | Audited contract |
| --- | --- |
| `add{.rnd}{.ftz}{.sat}.f32 d, a, b` | Destination register; both sources admit a floating literal. `.sat` legal, no `.ftz` or `.sat` on the FP64 sibling. `.rn`/`.rz` ungated; `.rm`/`.rp` PTX 1.0 / SM 20. |
| `add{.rnd}{.ftz}.f32x2 d, a, b` | PTX 8.6 / SM 100. Operands are **exactly `.b64`**; no `.sat`, no immediates. |
| `add{.rnd}.f64 d, a, b` | PTX 1.0 / SM 13. Both sources admit a floating literal; no `.ftz` or `.sat`. |
| `add{.rn}{.ftz}{.sat}.{f16,f16x2} d, a, b` | PTX 4.2 / SM 53. RN-only; `.f16` accepts `.f16` or `.b16`, `.f16x2` accepts `.f16x2` or `.b32`. |
| `add{.rn}.{bf16,bf16x2} d, a, b` | PTX 7.8 / SM 90. RN-only, no `.ftz` and no `.sat`; binds the exact `.b16`/`.b32` bit containers. |
| `add{.rnd}{.sat}.f32.{f16,bf16} d, a, c` | PTX 8.6 / SM 100. The **narrow source sits in the middle position** and is register-only; the FP32 addend is immediate-capable. |

`sub` carries the mirrored table.

## Confirmed gap and fix

The mixed-precision **FP32 addend/subtrahend** was declared register-only, rejecting legal spellings such as `add.f32.f16 %f0, %h1, 1.0;` and `sub.f32.bf16 %f0, %b1, 1.0;`. The assembler accepts them, so this was a false reject in a source position the manual leaves immediate-capable. The FP32 operand now admits a floating literal while the narrow source and the destination stay registers, matching the assembler on all four counts.

## Audited and confirmed already correct

These were the likely defect sites, and probing showed the model already agrees with the assembler. Recording them matters as much as the fix, because they are the parts a future reader would otherwise re-suspect:

- The shared `$binary_float_register` pattern is **not** merely width-sensitive. BF16 rejects `.f16` and BF16x2 rejects `.f16x2`, matching the assembler's `Arguments mismatch`.
- `f32x2` binds exactly `.b64`. `.f64` and `.u64` are rejected by both the assembler and the frontend, so the semantic packed suffix does not admit arbitrary same-width registers. `.reg .f32x2` is not a declaration format at all.
- Scalar FP32/FP64 admit floating literals in both source positions and reject literal destinations.
- Mixed operand order is enforced: the narrow source must be the middle operand.
- Half is RN-only but admits `.ftz` and `.sat`; BF16 admits neither and is RN-only; FP64 and `f32x2` admit no saturation.

## Evidence

CUDA 13.3.73 `ptxas` evidence used `/usr/local/cuda/bin/ptxas -arch=<arch> <module>.ptx -o <temporary>.o`. The 49-module matrix had 27 expected accepts, 22 rejects, and no assembler divergence. Version floors were confirmed in both directions: `add.bf16` rejects `.version 7.5` with `Feature 'add.bf16' requires PTX ISA .version 7.8 or later`, and `add.f32x2` and the mixed form reject `.version 8.5` with the corresponding `requires PTX ISA .version 8.6 or later`. Target floors were confirmed at `sm_90`, where both the packed and mixed cohorts report `requires .target sm_100 or higher`.

One floor is **not** assembler-verifiable: this `ptxas` does not define `sm_53` as a `-arch` value at all, so the half cohort's PTX 4.2 / SM 53 minimum rests on the manual and on frontend checker tests rather than on assembler evidence. That is a toolchain limitation, not a frontend contract difference.

The existing MUL and FMA regression, including mixed FMA, `.f32x2`, ReLU/OOB and the documented assembler-compatibility exceptions, is unchanged.

The Issue 142 reconciliation now covers every cohort in §9.7.1 through §9.7.5 that this issue scoped. No operation here gains execution semantics.
