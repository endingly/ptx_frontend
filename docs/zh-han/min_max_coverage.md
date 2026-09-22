# 浮点 `min`/`max` 覆盖

生成模型补齐了 [PTX 9.3 §9.7.3.11–§9.7.3.12](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions) 的 [minimum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-min) 与 [maximum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-max) form，以及 §9.7.4.7 与 §9.7.4.8 的 half/bfloat cohort。sign/magnitude 控制是 typed：`.xorsign.abs` 是单一 coupled token，而不是两个独立 flag，因此 manual 的 coupling 无法被拼写绕过。frontend 建模 parsing、owned resolution 与 target validation，不执行 comparison 或 selection。

| Form | Minimum | Contract |
| --- | --- | --- |
| `min{.ftz}{.NaN}{.xorsign.abs}.f32 d, a, b` | base PTX 1.0 / 所有 target architecture；`.ftz` PTX 1.4；`.NaN` PTX 7.0 / SM 80；`.xorsign.abs` PTX 7.2 / SM 86 | paired sign/magnitude；只接受 register operand。 |
| `min{.ftz}{.NaN}{.abs}.f32 d, a, b, c` | PTX 8.8 / SM 100 | 三个 source；只接受 `.abs`，禁止 `.xorsign`。 |
| `min.f64 d, a, b` | PTX 1.0 / SM 13 | 没有 FP32-only flag。 |
| `min{.ftz}{.NaN}{.xorsign.abs}.{f16,f16x2} d, a, b` | PTX 7.0 / SM 80 | `.ftz` optional；`.f16` 接受 `.f16` 或 `.b16`，`.f16x2` 接受 `.f16x2` 或 `.b32`。 |
| `min{.NaN}{.xorsign.abs}.{bf16,bf16x2} d, a, b` | PTX 7.0 / SM 80 | 没有 `.ftz`；BF16 只绑定 `.b16`/`.b32` bit container。 |

`max` 的表相同。two-source 与 three-source FP32 form 是**同一 variant 的两个 operand layout**，只按 operand 个数选择，且每个 layout 声明自己拒绝的 modifier slot。因此 `min.xorsign.abs.f32` 在三操作数下被接受、四操作数下被拒绝，而 `min.abs.f32` 恰好相反：coupled spelling 属于 two-source topology，three-source cohort 只接受 `.abs`。layout selection 只看 shape，所以这一合法性由 checker 强制，而不是由 variant selection 决定。

`.xorsign` 不带 `.abs` 根本无法拼写，且没有任何 FP32-only flag 进入 FP64。integer literal 与 integer register container、错误宽度、sink、任何 BF16 cohort 上的 `.ftz`、FP64 或任何非 FP32 cohort 上的 `.abs`，以及 FP32 cohort 三或四以外的所有 arity 都会被拒绝。

CUDA 13.3.73 `ptxas` 证据使用 `/usr/local/cuda/bin/ptxas -arch=<arch> <module>.ptx -o <temporary>.o`。45 个 module 的 matrix 有 27 个预期接受、18 个拒绝，且没有 assembler divergence。拒绝项由 assembler 自身诊断印证，其措辞与模型约束一致：bare two-source spelling 得到 `Modifier '.abs' requires modifier '.xorsign'`，bare fragment 得到 `Modifier '.xorsign' requires modifier '.abs'`，three-source form 得到 `Illegal modifier '.xorsign' for instruction 'min'`，BF16 cohort 得到 `Illegal modifier '.ftz' for instruction 'min'`。在工具链允许处 floor 由双向确认：`.target sm_80` 接受 `.NaN` 而在 `sm_75` 拒绝（`Modifier '.NaN' requires .target sm_80 or higher`），`sm_86` 接受 `.xorsign.abs` 而在 `sm_80` 拒绝，three-source form 需要 `sm_100`（`Feature 'min.f32 with 3 input operands' requires .target sm_100 or higher`）。配对的 `.version` probe 将 version floor 定在 7.0 与 7.2（`Feature '.abs' requires PTX ISA .version 7.2 or later`，在 7.1 被拒绝），three-source form 为 8.8。

本次审计还记录了一个 assembler 较宽松的 container 结果，模型选择遵循而非与之相悖：`min.f16x2` 同时接受 `.f16x2` 与 `.b32` operand，因此 packed half cohort 绑定 same-width container，而不是 exact bit container。

Issue 142 剩余工作是对既有 ADD/SUB 与 mixed-precision contract 的审计；这里不添加它们的 execution semantics。
