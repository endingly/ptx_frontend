# Floating `min`/`max` coverage

The generated model completes the floating [minimum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-min) and [maximum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-max) forms of [PTX 9.3 §9.7.3.11–§9.7.3.12](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions) and their half/bfloat cohorts in §9.7.4.7 and §9.7.4.8. Approximate sign/magnitude controls are typed: `.xorsign.abs` is one coupled token rather than two independent flags, so the manual's coupling cannot be spelled around. The frontend models parsing, owned resolution, and target validation; it does not execute comparison or selection.

| Form | Minimum | Contract |
| --- | --- | --- |
| `min{.ftz}{.NaN}{.xorsign.abs}.f32 d, a, b` | base PTX 1.0 / all target architectures; `.ftz` PTX 1.4; `.NaN` PTX 7.0 / SM 80; `.xorsign.abs` PTX 7.2 / SM 86 | Paired sign/magnitude; register operands only. |
| `min{.ftz}{.NaN}{.abs}.f32 d, a, b, c` | PTX 8.8 / SM 100 | Three sources; `.abs` alone, `.xorsign` forbidden. |
| `min.f64 d, a, b` | PTX 1.0 / SM 13 | No FP32-only flags. |
| `min{.ftz}{.NaN}{.xorsign.abs}.{f16,f16x2} d, a, b` | PTX 7.0 / SM 80 | `.ftz` optional; `.f16` takes `.f16` or `.b16`, `.f16x2` takes `.f16x2` or `.b32`. |
| `min{.NaN}{.xorsign.abs}.{bf16,bf16x2} d, a, b` | PTX 7.0 / SM 80 | No `.ftz`; BF16 binds the `.b16`/`.b32` bit containers only. |

`max` carries the identical table. The two-source and three-source FP32 forms are **two operand layouts of one variant**, selected purely by operand count, and each layout declares the modifier slots it rejects. That is why `min.xorsign.abs.f32` is accepted at three operands and rejected at four, while `min.abs.f32` is the reverse: the coupled spelling belongs to the two-source topology and the three-source cohort takes `.abs` alone. Layout selection is shape-only, so this legality is enforced by the checker rather than by variant selection.

`.xorsign` without `.abs` is not spellable at all, and no FP32-only flag reaches FP64. Integer literals and integer register containers, wrong widths, sinks, `.ftz` on any BF16 cohort, `.abs` on FP64 or on any non-FP32 cohort, and every arity other than three or four for the FP32 cohort are rejected.

CUDA 13.3.73 `ptxas` evidence used `/usr/local/cuda/bin/ptxas -arch=<arch> <module>.ptx -o <temporary>.o`. Its 45-module matrix had 27 expected accepts, 18 rejects, and no assembler divergence. The rejections are corroborated by the assembler's own diagnostics, which name the same contracts the model enforces: `Modifier '.abs' requires modifier '.xorsign'` for the bare two-source spelling, `Modifier '.xorsign' requires modifier '.abs'` for the bare fragment, `Illegal modifier '.xorsign' for instruction 'min'` for the three-source form, and `Illegal modifier '.ftz' for instruction 'min'` for the BF16 cohorts. The floors are confirmed in both directions where the toolchain permits: `.target sm_80` accepts `.NaN` and rejects it at `sm_75` (`Modifier '.NaN' requires .target sm_80 or higher`), `sm_86` accepts `.xorsign.abs` and rejects it at `sm_80`, and the three-source form requires `sm_100` (`Feature 'min.f32 with 3 input operands' requires .target sm_100 or higher`). Paired `.version` probes place the version floors at 7.0 and 7.2 (`Feature '.abs' requires PTX ISA .version 7.2 or later`, rejected at 7.1) and 8.8 for the three-source form.

The audit also recorded an assembler-permissive container result the model follows rather than contradicts: `min.f16x2` accepts both `.f16x2` and `.b32` operands, so the packed half cohort binds same-width containers instead of an exact bit container.

The remaining Issue 142 work is the audit of the existing ADD/SUB and mixed-precision contracts. It gains no execution semantics here.
