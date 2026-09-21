# `abs` 与 `neg` 浮点覆盖

此切片通过 generated YAML、typed Resolved IR 与 target-aware checker 建模浮点 [PTX 9.3 `abs`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-abs)、[half/bfloat `abs`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-abs)、[PTX 9.3 `neg`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-neg) 及 [half/bfloat `neg`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-neg)。frontend 不执行这些运算。

| Form | Minimum | source 与 storage contract |
| --- | --- | --- |
| `abs{.ftz}.f32`、`neg{.ftz}.f32` | PTX 1.0 / 所有 target architecture；`.ftz` 为 PTX 1.4 | source 可为 floating literal 或同宽 `.f32`/`.b32` register；destination 为同宽 `.f32`/`.b32` register。 |
| `abs.f64`、`neg.f64` | PTX 1.0 / SM 13 | source 可为 floating literal 或同宽 `.f64`/`.b64` register；destination 为同宽 `.f64`/`.b64` register。 |
| `abs{.ftz}.f16`、`abs{.ftz}.f16x2` | PTX 6.5 / SM 53 | 仅 register。scalar `.f16` 接受 `.f16` 或 `.b16`；packed `abs.f16x2` 接受 `.f16x2` 或 `.b32`。 |
| `neg{.ftz}.f16`、`neg{.ftz}.f16x2` | PTX 6.0 / SM 53 | 仅 register。scalar `.f16` 接受 `.f16` 或 `.b16`；packed `neg.f16x2` 两个 operand 都要求 `.b32`。 |
| `abs`/`neg` `.bf16`、`.bf16x2` | PTX 7.0 / SM 80 | 仅 register 且没有 `.ftz`；scalar 要求 exact `.b16`，packed 要求 exact `.b32`。 |

integer literal、错误宽度或 integer register container、未支持 modifier、额外 operand 及 destination sink 都会被拒绝。checker 保持 FP32 base instruction 的 PTX 1.0 minimum，只在 optional `.ftz` value 出现时要求 PTX 1.4。CUDA 13.3.73 的 `ptxas` 在 `.version 1.3` 直接报告 `.ftz` 需要 PTX 1.4；该 assembler 无法证明历史 SM minimum，后者由 frontend target test 检查。

记录的 CUDA 13.3.73 `sm_90` probe command 是 `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`。146 个 module 覆盖 modern positive form、FP literal、physical container、forbidden modifier、source/destination mismatch 及 sink rejection。即使当前 assembler 对 native `.f16x2` container 较宽松，模型仍以 manual 对 `neg.f16x2` 的 exact `.b32` contract 为准。

[explicit 浮点 MAD](mad_coverage.md) 已单独建模。Issue 142 剩余工作会覆盖 DIV、reciprocal 与 square-root、transcendental、MIN/MAX，以及既有 ADD/SUB 与 mixed-family contract 的审计。这里不添加这些 operation 的 execution semantics。
