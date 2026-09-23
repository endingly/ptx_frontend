# 浮点 `div` 覆盖

生成模型保留既有 integer 与 round-to-nearest floating division form，并加入 [PTX 9.3 §9.7.3.8](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-div) 的 explicit mode。每种 mode 都是 typed Resolved IR variant，因此 `.approx`、`.full` 与 `.rnd` 不会成为可任意组合的 unchecked spelling flag。frontend 建模 parsing、owned resolution 与 target validation，不执行 division。

| Form | Minimum | Contract |
| --- | --- | --- |
| `div.approx{.ftz}.f32`、`div.full{.ftz}.f32` | PTX 1.4 / 所有 target architecture | 固定 typed `.approx` 或 `.full`；`.ftz` optional；没有 rounding modifier。 |
| `div.{rn,rz,rm,rp}{.ftz}.f32` | PTX 1.4 / SM 20 | 必须有 typed rounding；`.ftz` optional。 |
| `div.rn.f64` | PTX 1.4 / SM 13 | 固定 round-to-nearest；没有 `.ftz`。 |
| `div.{rz,rm,rp}.f64` | PTX 1.4 / SM 20 | 必须有 directed rounding；没有 `.ftz`。 |

每个 floating source 可为 floating literal 或同宽 native floating/bit register container；destination 可为相应 native 或 bit register container。integer literal 和 integer register container、错误宽度、sink、遗漏 explicit mode、混合 `.approx`/`.full`/rounding form，以及 FP64 `.ftz` 都会被拒绝。

CUDA 13.3.73 证据使用 `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`。60 个 module 的 matrix 有 39 个预期接受、20 个拒绝，以及一个 permissive assembler acceptance：`rcp.rn.ftz.f64`。该不符合 canonical syntax 的 form 仍由 frontend 拒绝。DIV row 验证四个 rounded direction、两个 source position、literal 与 bit container。PTX 1.0–1.3 的 historical omitted-mode DIV 与 `.target map_f64_to_f32` behavior 刻意排除，因为它们需要这些 explicit form 之外的 target-profile contract。

[explicit reciprocal 与 square-root form](unary_float_coverage.md)、[浮点 transcendental](transcendental_coverage.md) 与 [浮点 MIN/MAX](min_max_coverage.md) 已单独建模。既有 ADD/SUB 审计仍不属于本文档范围。
