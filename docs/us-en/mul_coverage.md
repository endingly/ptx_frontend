# MUL Coverage

This document states the complete PTX 9.3 `mul` contract modelled by the
frontend. It supplements the [syntax coverage matrix](syntax_coverage.md); it
does not claim simulator execution or physical-GPU behavior. The canonical,
machine-readable source is `python/code_gen/resources/ptx_spec/arithmetic.yaml`.

The normative sources are NVIDIA's PTX ISA 9.3 archive: [integer MUL
§9.7.1.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-mul),
[floating MUL
§9.7.3.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mul),
and [half/bfloat MUL
§9.7.4.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-mul).

All rows are modelled through syntax, Resolved IR, and target-aware checking.
They are frontend contracts only; no row supplies simulator semantics.

| Forms | Minimum PTX / target | Modifiers | Operand contract |
| --- | --- | --- | --- |
| Integer `.lo` and `.hi` for `.u16/.u32/.u64/.s16/.s32/.s64` | 1.0 / all targets | Exactly one of `.lo` or `.hi` | Destination and sources use the selected width; sources are register-or-immediate and same-width bit registers are accepted |
| Integer `.wide` for `.u16/.s16` | 1.0 / all targets | `.wide` | A 32-bit destination; 16-bit register-or-immediate sources |
| Integer `.wide` for `.u32/.s32` | 1.0 / all targets | `.wide` | A 64-bit destination; 32-bit register-or-immediate sources |
| `.f32` | 1.0 / all targets; `.rm/.rp` require `sm_20` | omitted rounding defaults to `.rn`; `.rn/.rz` universally available; `.rm/.rp`, `.ftz`, and `.sat` follow the legal form | Floating register-or-immediate sources and same-width bit-register compatibility |
| `.f32x2` | 8.6 / `sm_100` | omitted rounding defaults to `.rn`; all four roundings and `.ftz` are legal; `.sat` is not | all operands are exact `.b64` registers |
| `.f64` | 1.0 / `sm_13` | omitted rounding defaults to `.rn`; all four roundings are legal; neither `.ftz` nor `.sat` is | Floating register-or-immediate sources and same-width bit-register compatibility |
| `.f16` | 4.2 / `sm_53` | omitted rounding defaults to `.rn`; explicit `.rn`, `.ftz`, and `.sat` are independently legal | register-only `.f16` or same-width `.b16` values |
| `.f16x2` | 4.2 / `sm_53` | omitted rounding defaults to `.rn`; explicit `.rn`, `.ftz`, and `.sat` are independently legal | all operands are exact `.b32` registers |
| `.bf16` | 7.8 / `sm_90` | omitted rounding defaults to `.rn`; only explicit `.rn` is legal; no `.ftz` or `.sat` | all operands are exact `.b16` registers |
| `.bf16x2` | 7.8 / `sm_90` | omitted rounding defaults to `.rn`; only explicit `.rn` is legal; no `.ftz` or `.sat` | all operands are exact `.b32` registers |

## Deliberate boundaries

The archive restricts integer `.wide` to 16- and 32-bit types. Consequently,
`.wide.u64` and `.wide.s64` are rejected rather than inferred from the
same-width `.hi/.lo` families.

The floating and half/bfloat syntax makes rounding optional, with `.rn` as the
default. The YAML records that default instead of requiring a redundant `.rn`.
It still rejects directed half/bfloat rounding, `.sat` on `.f32x2` and bfloat
packed forms, and bfloat `.ftz`/`.sat`. Packed forms retain their documented
exact bit-container widths, so a same-width-looking but wrong container is
rejected by the checker.

The long-standing public `Mul::RnF32`, `Mul::LoU32`, `Mul::HiU32`,
`Mul::WideU32`, and `Mul::WideS32` resolved-IR names remain available. This
coverage adds forms without creating a separate public support API.

## Verification sources

[Dedicated C++ MUL tests](../../submod/resolved_ir/test/test_mul_completeness.cpp)
exercise every family through parsing, resolution, declared-operand checking,
target minima, and invalid modifier/container cases. The [installed consumer
MUL test](../../submod/resolved_ir/test/package_consumer/mul_completeness.cpp)
uses only installed public headers for a new integer semantic form and a
negative packed-container check. The focused Python database test verifies the
canonical section, variants, modifier domains, availability, and operand
layouts. These sources define the frontend boundary; they do not verify a
simulator or hardware execution result.
