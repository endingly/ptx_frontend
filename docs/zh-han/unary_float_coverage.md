# 浮点 reciprocal 与 square-root 覆盖

生成模型覆盖 [reciprocal](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rcp)、[square root](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sqrt)、[reciprocal square root](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rsqrt) 的 explicit form，也覆盖特殊的 [RCP](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rcp-approx-ftz-f64) 与 [RSQRT](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rsqrt-approx-ftz-f64) FP64 approximate-FTZ instruction。approximate 与 rounded mode 是独立 typed variant；frontend 进行 resolution 和 validation，不执行数值 operation。

| Form | Minimum | Contract |
| --- | --- | --- |
| `rcp.approx{.ftz}.f32`、`sqrt.approx{.ftz}.f32`、`rsqrt.approx{.ftz}.f32` | PTX 1.4 / 所有 target architecture | 固定 `.approx`；FP32 `.ftz` optional。 |
| `rcp.rnd{.ftz}.f32`、`sqrt.rnd{.ftz}.f32` | PTX 2.0 / SM 20 | 必须有 typed `rn`、`rz`、`rm` 或 `rp`；FP32 `.ftz` optional。 |
| `rcp.rn.f64`、`sqrt.rn.f64` | PTX 1.4 / SM 13 | 固定 typed round-to-nearest；没有 `.ftz`。 |
| `rcp.{rz,rm,rp}.f64`、`sqrt.{rz,rm,rp}.f64` | PTX 2.0 / SM 20 | 必须有 directed rounding；没有 `.ftz`。 |
| `rsqrt.approx.f64` | PTX 1.4 / SM 13 | 固定 `.approx`；没有 `.ftz`。 |
| `rcp.approx.ftz.f64` | PTX 2.1 / SM 20 | 固定 `.approx` 且必须有 typed `.ftz`。 |
| `rsqrt.approx.ftz.f64` | PTX 4.0 / SM 20 | 固定 `.approx` 且必须有 typed `.ftz`。 |

每个 source 可为 floating literal 或同宽 native floating/bit register container，destination 可为匹配的 native/bit container。integer literal 和 integer container、错误宽度、sink、遗漏 mode、混合 approximate/rounding spelling，以及所有 rounded FP64 `.ftz` spelling 都会被拒绝。当前 CUDA 13.3.73 `ptxas` permissively 接受 `rcp.rn.ftz.f64`；canonical PTX syntax 排除它，因此 frontend 会拒绝它。

CUDA 13.3.73 证据使用 `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`：60 个 matrix module 有 39 个预期接受、20 个拒绝和一个已记录的 permissive FP64-FTZ acceptance；四个 focused special-FP64 immediate/container probe 都通过。PTX 1.4 前的 historical omitted-mode form 与 `.target map_f64_to_f32` behavior 仍被排除，因为它们需要 explicit model 之外的 target-profile semantics。

[浮点 transcendental](transcendental_coverage.md) 已单独建模。MIN/MAX 以及既有 ADD/SUB 审计仍不属于本文档范围。
