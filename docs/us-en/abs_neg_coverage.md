# `abs` and `neg` floating-point coverage

This slice models the floating forms of [PTX 9.3 `abs`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-abs), [half/bfloat `abs`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-abs), [PTX 9.3 `neg`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-neg), and [half/bfloat `neg`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-neg). They use the generated YAML, typed Resolved IR, and target-aware checker path; the frontend does not execute the operations.

| Form | Minimum | Source and storage contract |
| --- | --- | --- |
| `abs{.ftz}.f32`, `neg{.ftz}.f32` | PTX 1.0 / all target architectures; `.ftz` PTX 1.4 | A floating literal or a same-width `.f32`/`.b32` register source; the destination is a same-width `.f32`/`.b32` register. |
| `abs.f64`, `neg.f64` | PTX 1.0 / SM 13 | A floating literal or a same-width `.f64`/`.b64` register source; the destination is a same-width `.f64`/`.b64` register. |
| `abs{.ftz}.f16`, `abs{.ftz}.f16x2` | PTX 6.5 / SM 53 | Register-only. Scalar `.f16` accepts `.f16` or `.b16`; packed `abs.f16x2` accepts `.f16x2` or `.b32`. |
| `neg{.ftz}.f16`, `neg{.ftz}.f16x2` | PTX 6.0 / SM 53 | Register-only. Scalar `.f16` accepts `.f16` or `.b16`; packed `neg.f16x2` requires `.b32` for both operands. |
| `abs`/`neg` `.bf16`, `.bf16x2` | PTX 7.0 / SM 80 | Register-only and no `.ftz`; scalar requires exact `.b16`, packed requires exact `.b32`. |

Integer literals, wrong-width or integer register containers, unsupported modifiers, extra operands, and destination sinks are rejected. The checker keeps the base FP32 instruction minimum at PTX 1.0 and applies the PTX 1.4 requirement only when the optional `.ftz` value is present. CUDA 13.3.73 `ptxas` directly reports that `.ftz` needs PTX 1.4 at `.version 1.3`; that assembler cannot demonstrate the historical SM minima, which are checked by the frontend target tests.

The recorded CUDA 13.3.73 `sm_90` probe command was `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`. Its 146 modules cover modern positive forms, FP literals, physical containers, forbidden modifiers, source/destination mismatches, and sink rejection. The manual's exact `.b32` contract for `neg.f16x2` governs the model even where a current assembler is permissive about native `.f16x2` containers.

[Explicit floating MAD](mad_coverage.md), [DIV](div_coverage.md), [reciprocal/square-root forms](unary_float_coverage.md), [floating transcendentals](transcendental_coverage.md), and [floating MIN/MAX](min_max_coverage.md) are now modelled separately. The remaining Issue 142 work is an audit of existing ADD/SUB and mixed-family contracts. None of those operations gains execution semantics here.
