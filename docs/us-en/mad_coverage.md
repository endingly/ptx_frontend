# Floating `mad` coverage

This slice extends the existing `Mad::RnF32` seed through the canonical YAML, typed Resolved IR, and target-aware checker. It covers the explicit-rounding forms in [PTX 9.3 §9.7.3.7](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mad), retains integer and carry `mad` variants and the source opcode, and does not add arithmetic execution.

| Form | Minimum | Contract |
| --- | --- | --- |
| `mad.{rn,rz,rm,rp}{.ftz}{.sat}.f32` | PTX 2.0 / SM 20 | Rounding is required. `.ftz` and `.sat` are independently optional. Each source admits a typed floating literal or same-width `.f32`/`.b32` register; the destination is a same-width `.f32`/`.b32` register. |
| `mad.{rn,rz,rm,rp}.f64` | PTX 1.0 / SM 13 | Rounding is required. No `.ftz` or `.sat`. Each source admits a typed floating literal or same-width `.f64`/`.b64` register; the destination is a same-width `.f64`/`.b64` register. |

Integer literals or integer register containers, wrong widths, destination sinks, missing rounding, unsupported FP64 flags, and the wrong operand count are rejected. The documented PTX 1.4 requirement makes rounding mandatory for `mad.f64`; it is not the introduction floor for explicit rounded FP64 forms. CUDA 13.3.73 `ptxas` accepts explicit `.rn` and `.rz` FP64 forms at PTX 1.3 with an `sm_90` assembler override, consistent with the PTX 1.0 model floor. That historical probe establishes syntax and contract evidence only, not old-GPU code generation.

The omitted-rounding `mad{.ftz}{.sat}.f32` syntax for `sm_1x`, legacy omitted `mad.f64`, and the PTX 3.0/3.1 errata/default behavior are deliberately excluded. They require target-profile semantics the explicit contemporary forms do not need, so the checker rejects omitted rounding rather than treating it as a universal default.

CUDA 13.3.73 `ptxas` evidence used `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`. Its 32-module modern matrix accepted all four rounding forms for FP32 and FP64, floating immediates, and matching bit containers; it rejected 14 invalid flag, type, shape, and sink cases. [Explicit floating DIV](div_coverage.md), [reciprocal and square-root forms](unary_float_coverage.md), [floating transcendentals](transcendental_coverage.md), and [floating MIN/MAX](min_max_coverage.md) are now modelled separately. The existing ADD/SUB audit remains outside this document's scope.
