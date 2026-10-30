# Floating reciprocal and square-root coverage

PTX 1.0–1.3 additionally accepts `rcp.f32`, `sqrt.f32`, and `rsqrt.f32` as approximate+FTZ, `sqrt.f64` as round-to-nearest-even without FTZ, and `rsqrt.f64` as approximate without FTZ. FP64 needs SM >=13; only `sqrt.f64` permits exact generic SM10/11/12 plus `map_f64_to_f32`. Bare `rcp.f64` remains excluded: the manual describes an old omitted-mode default, while CUDA 13.3.33 rejects the reproduced form. This is a documented manual/current-assembler discrepancy. Source omission and typed interpretation are separate; see [the public contract](../resolved_ir_design.md#historical-omitted-floating-controls).

The generated model covers the explicit forms of [reciprocal](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rcp), [square root](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sqrt), and [reciprocal square root](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rsqrt), including the special [RCP](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rcp-approx-ftz-f64) and [RSQRT](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rsqrt-approx-ftz-f64) FP64 approximate-FTZ instructions. Approximate and rounded modes are separate typed variants; the frontend resolves and validates them but does not execute numerical operations.

| Form | Minimum | Contract |
| --- | --- | --- |
| `rcp.approx{.ftz}.f32`, `sqrt.approx{.ftz}.f32`, `rsqrt.approx{.ftz}.f32` | PTX 1.4 / all target architectures | Fixed `.approx`; FP32 `.ftz` is optional. |
| `rcp.rnd{.ftz}.f32`, `sqrt.rnd{.ftz}.f32` | PTX 2.0 / SM 20 | Required typed `rn`, `rz`, `rm`, or `rp`; FP32 `.ftz` is optional. |
| `rcp.rn.f64`, `sqrt.rn.f64` | PTX 1.4 / SM 13 | Fixed typed round-to-nearest; no `.ftz`. |
| `rcp.{rz,rm,rp}.f64`, `sqrt.{rz,rm,rp}.f64` | PTX 2.0 / SM 20 | Required directed rounding; no `.ftz`. |
| `rsqrt.approx.f64` | PTX 1.4 / SM 13 | Fixed `.approx`; no `.ftz`. |
| `rcp.approx.ftz.f64` | PTX 2.1 / SM 20 | Fixed `.approx` and required typed `.ftz`. |
| `rsqrt.approx.ftz.f64` | PTX 4.0 / SM 20 | Fixed `.approx` and required typed `.ftz`. |

Each source accepts a floating literal or same-width native floating/bit register container, and the destination accepts the matching native/bit container. Integer literals and integer containers, wrong widths, sinks, omitted modes at PTX >=1.4, mixed approximate/rounding spellings, and all rounded FP64 `.ftz` spellings are rejected. Current CUDA 13.3.73 `ptxas` permissively accepts `rcp.rn.ftz.f64`; the canonical PTX syntax excludes it, so the frontend rejects it.

CUDA 13.3.73 evidence used `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`: 60 matrix modules yielded 39 expected accepts, 20 rejects, and the one documented permissive FP64-FTZ acceptance; four focused special-FP64 immediate/container probes all passed.

[Floating transcendentals](transcendental_coverage.md) and [floating MIN/MAX](min_max_coverage.md) are now modelled separately. [Floating and mixed ADD/SUB](add_sub_coverage.md) completes the reconciliation; see that document for the ADD/SUB contracts.

The confirmed missing mapped-FP64 availability alternative is narrowly
`div.rn.f64`, `rcp.rn.f64`, and `sqrt.rn.f64` on SM10/11/12 with
`map_f64_to_f32` ([#287](https://github.com/endingly/ptx_frontend/issues/287)).
This does not assert that other FP64 instructions have the same alternative.
