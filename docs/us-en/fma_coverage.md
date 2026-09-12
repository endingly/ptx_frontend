# FMA Coverage

This document states the complete PTX 9.3 `fma` contract modelled by the
frontend. It supplements the [syntax coverage matrix](syntax_coverage.md); it
is not a claim that every PTX opcode or simulator execution is supported. The
machine-readable instruction specification is
`python/code_gen/resources/ptx_spec/arithmetic.yaml`.

The normative reference is NVIDIA's PTX ISA 9.3 archive: [floating FMA
§9.7.3.6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-fma),
[half/bfloat FMA
§9.7.4.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-fma),
and [mixed-precision FMA
§9.7.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-fma).

All rows below are modelled through syntax, Resolved IR, and target-aware
checking. None has simulator execution semantics.

| Variants | PTX / target minimum | Modifiers | Operand contract |
| --- | --- | --- | --- |
| `fma_rn_f32`, `fma_directed_f32` | 2.0 / `sm_20` | `.rn`, `.rz`, `.rm`, or `.rp` is required; `.ftz` and `.sat` are independently optional | `.f32` destination and sources; every source is floating register-or-immediate and a same-width bit register is also accepted |
| `fma_rn_f64`, `fma_directed_f64` | 1.4 / `sm_13` | one of all four rounding modes is required; no flags | `.f64` destination and sources; every source is floating register-or-immediate and a same-width bit register is also accepted |
| `fma_f32x2` | 8.6 / `sm_100` | one of all four rounding modes is required; `.ftz` optional | all operands are exact `.b64` registers |
| `fma_rn_f16`, `fma_rn_f16x2` | 4.2 / `sm_53` | `.rn` required; `.ftz` and `.sat` independently optional | register-only: `.f16`/`.b16` for scalar, `.f16x2`/`.b32` for packed |
| `fma_half_relu` | 7.0 / `sm_80` | `.rn` and `.relu` required; `.ftz` optional | register-only `.f16`/`.b16`, or `.f16x2`/`.b32` packed values |
| `fma_half_oob`, `fma_half_oob_relu` | 8.1 / `sm_90` | `.rn` and `.oob` required; the former may add `.sat`, the latter requires `.relu`; neither permits `.ftz`, and `.sat` with `.relu` is rejected | register-only `.f16`/`.b16`, or `.f16x2`/`.b32` packed values |
| `fma_bf16`, `fma_bf16x2` | 7.0 / `sm_80` | `.rn` required; `.relu` optional; `.ftz` and `.sat` are not allowed | exact `.b16` scalar or `.b32` packed registers only |
| `fma_bf16_oob`, `fma_bf16x2_oob` | 8.1 / `sm_90` | `.rn` and `.oob` required; `.relu` optional; `.ftz` and `.sat` are not allowed | exact `.b16` scalar or `.b32` packed registers only |
| `fma_mixed_f32_f16`, `fma_mixed_f32_bf16` | 8.6 / `sm_100` | one of all four rounding modes required; `.sat` optional | destination and accumulator `c` are `.f32` or `.b32`; multiplicands `a`/`b` are register-only `.f16`/`.b16`, or exact `.b16` for the bf16 form; `c` may be a floating immediate |

## Deliberate compatibility boundaries

The half-precision prose says that `.rn` is the default, while the FMA model
requires it: NVIDIA `ptxas` 13.3.33 rejects an omitted `.rn`. The explicit
rounding modifier is therefore part of the accepted contract.

PTX syntax prints `fma.rnd.oob.{relu}.type`, without `.sat`. `ptxas` 13.3.33
also accepts `.sat` for half (`.f16`/`.f16x2`) OOB forms. The `fma_half_oob`
row intentionally records that assembler-compatible extension; it does not
generalize it to bf16 or permit it together with `.relu`.

The mixed-precision manual example contains a repeated/trailing `.sat`. The
model accepts only the canonical modifier sequence represented by its variants:
one rounding modifier, an optional single `.sat`, then `.f32` and `.f16` or
`.bf16`.

For floating `.f32` and `.f64` sources, cross-width hexadecimal bit literals
(`0f` for `.f64`, `0d` for `.f32`) are accepted and converted from their lexical
precision. Decimal literals are evaluated as binary64 before conversion to the
operand type, including finite decimal values that narrow to `.f32` infinity.
Narrowing uses round-to-nearest, ties-to-even, independently of the instruction's
rounding or `.ftz`. Same-width bit literals retain their exact payload. Across
precisions, the frontend quiets NaNs and retains their sign and high payload bits;
this is a deterministic representation policy, not an ISA guarantee of payload
conversion.

The public C++ IDs `Fma::RnF32`, `Fma::RnF64`, and `Fma::RnF16` remain available.
The three source fields of `RnF32` and `RnF64` now hold `WithLocs<RegOrImm>`;
consumers must distinguish register references from typed immediates.

## Verification

[C++ resolution tests](../../submod/resolved_ir/test/test_select_variant.cpp)
exercise all 70 canonical modifier combinations (including the half OOB `.sat`
extension), invalid combinations, and floating-literal conversion boundaries.
[Availability tests](../../submod/resolved_ir/test/test_ptx_resolved_ir_checker.cpp)
check each of the 16 variants at its minimum PTX/SM and independently below each
minimum. [Module tests](../../submod/resolved_ir/test/test_resolved_module_instruction_types.cpp)
check declared operand types, exact bit containers, and each mixed operand
position. The [installed C++ consumer](../../submod/resolved_ir/test/package_consumer/main.cpp)
parses, resolves, and checks all 16 variants using only public installed APIs.

The shared Python tests check PTX-section taxonomy in
`python/tests/code_gen/test_ptx_spec_taxonomy.py` and generated Resolved-IR
operand layouts in `python/tests/ir/test_resolved_ir.py`. The installed-wheel
smoke test, `python/tests/code_gen/wheel_smoke.py`, checks the 16 variants and
the independent mixed-precision operand types.

The coverage stated here is defined by the three official PTX 9.3 FMA sections
and the implementation/tests above, not a manual coverage ledger. The older
common-kernel corpus and actual opcode coverage are distinct: corpus presence
is not used as evidence of this closure.
