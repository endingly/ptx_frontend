# FMA Coverage

This document states the complete PTX 9.3 `fma` contract modelled by the
frontend. It supplements the [syntax coverage matrix](syntax_coverage.md); it
is not a claim that every PTX opcode or simulator execution is supported. The
machine-readable instruction specification is
`python/src/ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml`.

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
| `fma_rn_f16`, `fma_rn_f16x2` | 4.2 / `sm_53` | optional `.rn` (default RN); `.ftz` and `.sat` independently optional | register-only: `.f16`/`.b16` for scalar, `.f16x2`/`.b32` for packed |
| `fma_half_relu` | 7.0 / `sm_80` | optional `.rn` (default RN); `.relu` required; `.ftz` optional | register-only `.f16`/`.b16`, or `.f16x2`/`.b32` packed values |
| `fma_half_oob`, `fma_half_oob_relu` | 8.1 / `sm_90` | optional `.rn` (default RN); `.oob` required; the latter requires `.relu`; neither permits `.ftz` or `.sat` | register-only `.f16`/`.b16`, or `.f16x2`/`.b32` packed values |
| `fma_bf16`, `fma_bf16x2` | 7.0 / `sm_80` | optional `.rn` (default RN); `.relu` optional; `.ftz` and `.sat` are not allowed | exact `.b16` scalar or `.b32` packed registers only |
| `fma_bf16_oob`, `fma_bf16x2_oob` | 8.1 / `sm_90` | optional `.rn` (default RN); `.oob` required; `.relu` optional; `.ftz` and `.sat` are not allowed | exact `.b16` scalar or `.b32` packed registers only |
| `fma_mixed_f32_f16`, `fma_mixed_f32_bf16` | 8.6 / `sm_100` | one of all four rounding modes required; `.sat` optional | destination and accumulator `c` are `.f32` or `.b32`; F16 multiplicands `a`/`b` accept `.f16`/`.b16` registers or numeric half constants; the current bf16 form retains exact `.b16` registers; `c` may be a floating immediate |

## Deliberate compatibility boundaries

The half/bfloat section defines RN as the default, including OOB forms. The
frontend accepts omitted or explicit `.rn`; other rounding modes remain invalid.
OOB syntax admits optional `.relu`, and excludes `.sat` and `.ftz`.
Historical `ptxas` 13.3.33 rejection of omitted `.rn` and acceptance of half
OOB `.sat` are supplemental tool divergences, rather than frontend grammar rules.

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

The public C++ IDs `FmaRnF32`, `FmaRnF64`, and `FmaRnF16` remain available.
The three source fields of `RnF32` and `RnF64` now hold `WithLocs<RegOrImm>`;
consumers must distinguish register references from typed immediates.

The nine half/bfloat classes (`FmaRnF16`, `FmaRnF16x2`, `FmaHalfRelu`,
`FmaHalfOob`, `FmaHalfOobRelu`, `FmaBf16`, `FmaBf16x2`, `FmaBf16Oob`,
`FmaBf16x2Oob`) now expose instance `WithLocs<RoundingMode> rounding`, replacing
static rounding constants. Read `value.rounding.value` and `.locs`: omitted RN
has value `Rn` and empty locations; explicit RN retains its token location.
Coherent current RN presence edits remain valid; supplied AST association checks
the retained syntax identity independently. `FmaHalfOob::saturate` is removed;
there is no SAT field or implicit SAT permission. These generated C++ layout
changes require rebuilding consumers. The 26 register-only source roles and
all carrier/type/availability contracts remain unchanged; modifier validation
does not execute FMA arithmetic, FTZ, saturation, ReLU or OOB semantics.

## Verification

[C++ resolution tests](../../../submod/resolved_ir/test/test_select_variant_fma.cpp)
exercise all 92 canonical modifier combinations: 48 half/bfloat products and
44 ordinary/mixed products. Invalid combinations and floating-literal conversion
boundaries remain covered.
[Availability tests](../../../submod/resolved_ir/test/test_select_variant_fma.cpp)
check each of the 16 variants at its minimum PTX/SM and independently below each
minimum. [Module tests](../../../submod/resolved_ir/test/test_resolved_module_instruction_types.cpp)
check declared operand types, exact bit containers, and each mixed operand
position. [Owned-control tests](../../../submod/resolved_ir/test/test_half_fma_modifiers.cpp)
check all 48 half products, default presence, invalid modes, source association,
availability, and independent copies/clones after AST death. The installed
[consumer](../../../examples/resolved_ir_consumer/main.cpp) reads all nine
instance rounding fields and validates copies/clones without source storage.

The shared Python tests check PTX-section taxonomy in
`python/tests/code_gen/test_ptx_spec_taxonomy.py` and generated Resolved-IR
operand layouts in `python/tests/ir/test_resolved_ir.py`. The installed-wheel
smoke test, `python/tests/code_gen/wheel_smoke.py`, checks the 16 variants and
the independent mixed-precision operand types.

The coverage stated here is defined by the three official PTX 9.3 FMA sections
and the implementation/tests above, not a manual coverage ledger. The older
common-kernel corpus and actual opcode coverage are distinct: corpus presence
is not used as evidence of this closure.

Mixed F32/F16 multiplicands now use RegOrImm and accept numeric floating constants converted once to binary16. The F32 accumulator keeps its existing numeric path. Explicit half-arithmetic register-only forms and the current BF16 mixed-source representation remain separate; literal conversion does not execute the FMA rounding/saturation result.
