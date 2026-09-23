# Floating `min`/`max` coverage

The generated model completes the floating [minimum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-min) and [maximum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-max) forms of [PTX 9.3 §9.7.3.11–§9.7.3.12](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions) and their half/bfloat cohorts in §9.7.4.7 and §9.7.4.8. Sign/magnitude controls are typed: `.xorsign.abs` and `.abs` are distinct tokens with distinct slot identities (`xorsign_abs` and `abs`), so `.xorsign` cannot be spelled without `.abs`, and a consumer never has to infer that a paired operation is an absolute-magnitude operation from an `abs` field reading false. The frontend models parsing, owned resolution, and target validation; it does not execute comparison or selection.

| Form | Minimum | Contract |
| --- | --- | --- |
| `min{.ftz}{.NaN}{.xorsign.abs}.f32 d, a, b` | base PTX 1.0 / all target architectures; `.ftz` PTX 1.4; `.NaN` PTX 7.0 / SM 80; `.xorsign.abs` PTX 7.2 / SM 86 | Paired sign/magnitude. Register destination; both sources admit a floating literal or a same-width register. |
| `min{.ftz}{.NaN}{.abs}.f32 d, a, b, c` | PTX 8.8 / SM 100 | Three sources; `.abs` alone, `.xorsign` forbidden. Register destination; all three sources admit a floating literal or a same-width register. |
| `min.f64 d, a, b` | PTX 1.0 / SM 13 | No FP32-only flags. Register destination; both sources admit a floating literal or a same-width register. |
| `min{.ftz}{.NaN}{.xorsign.abs}.{f16,f16x2} d, a, b` | PTX 7.0 / SM 80 | `.ftz` optional; register-only. `.f16` takes `.f16` or `.b16`, `.f16x2` takes `.f16x2` or `.b32`. |
| `min{.NaN}{.xorsign.abs}.{bf16,bf16x2} d, a, b` | PTX 7.0 / SM 80 | No `.ftz`; register-only, and BF16 binds the `.b16`/`.b32` bit containers only. |

`max` carries the identical table. The two-source and three-source FP32 forms are **two operand layouts of one variant**, selected purely by operand count, and each layout declares the modifier slots it rejects. That is why `min.xorsign.abs.f32` is accepted at three operands and rejected at four, while `min.abs.f32` is the reverse: the coupled spelling belongs to the two-source topology and the three-source cohort takes `.abs` alone. Layout selection is shape-only, so this legality is enforced by the checker rather than by variant selection. Variant exclusivity stays conservative over the full syntactic modifier language the resolver matches against, because `select_variant_name` does not consult layout constraints.

Only the scalar FP32 and FP64 cohorts admit literals, and only in source positions; the half and bfloat cohorts stay register-only. Integer literals and integer register containers, wrong widths, sinks, literal destinations, `.ftz` on any BF16 cohort, `.abs` on FP64 or on any non-FP32 cohort, and every arity other than three or four for the FP32 cohort are rejected.

CUDA 13.3.73 `ptxas` evidence used `/usr/local/cuda/bin/ptxas -arch=<arch> <module>.ptx -o <temporary>.o`. Its 58-module matrix had 35 expected accepts, 23 rejects, and no assembler divergence. The rejections are corroborated by the assembler's own diagnostics, which name the same contracts the model enforces: `Modifier '.abs' requires modifier '.xorsign'` for the bare two-source spelling, `Modifier '.xorsign' requires modifier '.abs'` for the bare fragment, `Illegal modifier '.xorsign' for instruction 'min'` for the three-source form, and `Illegal modifier '.ftz' for instruction 'min'` for the BF16 cohorts. Literal admissibility is confirmed in both directions: `min.f32 %f0, 1.0, %f1;`, `min.f32 %f0, %f1, 0f00000000;` and `min.f64 %d0, %d1, 1.0;` are accepted, while `min.f32 1.0, %f1, %f2;` fails with `Result register required for instruction 'min'`, `min.f32 %f0, 1, %f1;` with `Arguments mismatch`, and every half/bfloat literal spelling with `Arguments mismatch`. The floors are confirmed in both directions where the toolchain permits: `.target sm_80` accepts `.NaN` and rejects it at `sm_75` (`Modifier '.NaN' requires .target sm_80 or higher`), `sm_86` accepts `.xorsign.abs` and rejects it at `sm_80`, and the three-source form requires `sm_100` (`Feature 'min.f32 with 3 input operands' requires .target sm_100 or higher`). Paired `.version` probes place the version floors at 7.0 and 7.2 (`Feature '.abs' requires PTX ISA .version 7.2 or later`, rejected at 7.1) and 8.8 for the three-source form.

The audit also recorded an assembler-permissive container result the model follows rather than contradicts: `min.f16x2` accepts both `.f16x2` and `.b32` operands, so the packed half cohort binds same-width containers instead of an exact bit container. The pre-existing `mul_half_x2` binds an exact `.b32` container for the same operand shape, which this probe suggests is an over-restriction; that is untouched here.

## Installed-surface change

Two generated public types change shape, deliberately and confined to the opcodes this slice rewrites:

- `Min::NanF32` / `Max::NanF32` become `Min::F32` / `Max::F32`, because the frozen `.NaN` seeds are folded into the general FP32 cohort rather than duplicated.
- `Min::F32` and `Max::F32` now carry `Operands = std::variant<BinaryOperands, TernaryOperands>`, and `ResolvedOperandLayoutTag{0}` selects the two-source layout while `{1}` selects the three-source one.

For consumers, `Min::NanF32` / `Max::NanF32` references migrate to `Min::F32` / `Max::F32`. Given a `Min::F32` value named `value`, select the variant alternative matching its operand count:

```cpp
const auto& binary_operands = std::get<Min::F32::BinaryOperands>(value.operands);  // two-source
// Or, when the instruction has three sources:
const auto& ternary_operands = std::get<Min::F32::TernaryOperands>(value.operands);
```

These public shape changes define an explicit API break in the installed C++ package at version `0.1.0`; its CMake package compatibility is `SameMinorVersion`. No compatibility shim or parallel legacy representation is provided, so consumers must migrate to the renamed cohort and operand variant. The Python wheel version is `0.1.0b0`, a beta prerelease; the installed C++ package remains at `0.1.0`. These are independent package versions with no lockstep requirement. The repository has no dedicated migration or changelog document, so this migration note stays here.

Additions to shared checker structures are **appended** rather than inserted, so existing aggregate initializers keep compiling: `checker::ModifierValueView` gains `slot` as its final member, and `checker::OperandLayoutDescriptor` gains `forbidden_modifiers` alongside the new `CheckDiagnosticKind::ModifierNotAllowedForLayout`. The modifier diagnostic also reports the offending modifier's own source range when it is still retained, falling back to the context range otherwise.

[Floating and mixed ADD/SUB](add_sub_coverage.md) reconciles the remaining §9.7.3–§9.7.5 cohorts. It gains no execution semantics here.
