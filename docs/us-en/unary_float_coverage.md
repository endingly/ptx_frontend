# Floating reciprocal and square-root coverage

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

Each source accepts a floating literal or same-width native floating/bit register container, and the destination accepts the matching native/bit container. Integer literals and integer containers, wrong widths, sinks, omitted modes, mixed approximate/rounding spellings, and all rounded FP64 `.ftz` spellings are rejected. Current CUDA 13.3.73 `ptxas` permissively accepts `rcp.rn.ftz.f64`; the canonical PTX syntax excludes it, so the frontend rejects it.

CUDA 13.3.73 evidence used `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`: 60 matrix modules yielded 39 expected accepts, 20 rejects, and the one documented permissive FP64-FTZ acceptance; four focused special-FP64 immediate/container probes all passed. Historical omitted-mode forms before PTX 1.4 and `.target map_f64_to_f32` behavior remain excluded because they require target-profile semantics outside the explicit model.

[Floating transcendentals](transcendental_coverage.md) are now modelled separately. MIN/MAX and the existing ADD/SUB audit remain outside this document's scope.
