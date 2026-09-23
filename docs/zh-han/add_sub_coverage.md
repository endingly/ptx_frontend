# 浮点与 mixed `add`/`sub` 覆盖

本切片是 [PTX 9.3 §9.7.3.3/§9.7.3.4、§9.7.4.1/§9.7.4.2 与 §9.7.5.1/§9.7.5.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions) 对 [`add`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-add) 与 [`sub`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sub) 的对账。它是对既有 scalar、packed、half/bfloat 与 mixed-precision cohort 的**审计**，以 assembler 证据为准，而不是新增第二套实现。只发现一处确认的 contract gap，已在此修复。

| Form | 审计后的 contract |
| --- | --- |
| `add{.rnd}{.ftz}{.sat}.f32 d, a, b` | destination 为 register；两个 source 都接受 floating literal。`.sat` 合法，FP64 sibling 没有 `.ftz` 或 `.sat`。`.rn`/`.rz` 无 gate；`.rm`/`.rp` 为 PTX 1.0 / SM 20。 |
| `add{.rnd}{.ftz}.f32x2 d, a, b` | PTX 8.6 / SM 100。operand 必须是**精确的 `.b64`**；没有 `.sat`，不接受 immediate。 |
| `add{.rnd}.f64 d, a, b` | PTX 1.0 / SM 13。两个 source 都接受 floating literal；没有 `.ftz` 或 `.sat`。 |
| `add{.rn}{.ftz}{.sat}.{f16,f16x2} d, a, b` | PTX 4.2 / SM 53。RN-only；`.f16` 接受 `.f16` 或 `.b16`，`.f16x2` 接受 `.f16x2` 或 `.b32`。 |
| `add{.rn}.{bf16,bf16x2} d, a, b` | PTX 7.8 / SM 90。RN-only，没有 `.ftz` 也没有 `.sat`；绑定精确的 `.b16`/`.b32` bit container。 |
| `add{.rnd}{.sat}.f32.{f16,bf16} d, a, c` | PTX 8.6 / SM 100。**narrow source 位于中间位置**且仅接受 register；FP32 addend 接受 floating literal。 |

`sub` 的表与之镜像。

## 已确认的 gap 与修复

mixed-precision 的 **FP32 addend/subtrahend** 此前声明为 register-only，会拒绝 `add.f32.f16 %f0, %h1, 1.0;` 与 `sub.f32.bf16 %f0, %b1, 1.0;` 这类合法拼写。assembler 接受它们，因此这是 manual 允许 immediate 的 source 位置上的一处 false reject。该 FP32 operand 现在接受 floating literal，而 narrow source 与 destination 仍为 register，四项均在 assembler 上取得一致。

## 审计后确认原本就正确的部分

以下是疑似缺陷点，探测显示模型已与 assembler 一致。记录它们与记录修复同样重要，因为否则后续读者会重新怀疑这些部分：

- 共享的 `$binary_float_register` pattern **并非**只看宽度。BF16 拒绝 `.f16`，BF16x2 拒绝 `.f16x2`，与 assembler 的 `Arguments mismatch` 一致。
- `f32x2` 精确绑定 `.b64`。assembler 与 frontend 都拒绝 `.f64` 与 `.u64`，因此 semantic packed suffix 不会接受任意同宽 register。`.reg .f32x2` 根本不是 declaration format。
- scalar FP32/FP64 在两个 source 位置都接受 floating literal，并拒绝 literal destination。
- mixed 的 operand 顺序被强制：narrow source 必须位于中间。
- half 为 RN-only 但接受 `.ftz` 与 `.sat`；BF16 两者都不接受且为 RN-only；FP64 与 `f32x2` 不接受 saturation。

## 已安装 surface 变更

开放 addend 接受 immediate 会改变两个公共成员已安装生成 payload 的形状：`Add::MixedF32::addend` 与 `Sub::MixedF32::subtrahend` 从 register reference 变为既有的 register-or-immediate 表示。consumer 的读取方式随之改变：

```c
mixed.addend.value.spelling;                              // 之前
std::get<ResolvedRegisterRef>(mixed.addend.value).spelling;  // 之后：register spelling
std::get<ResolvedImmediate>(mixed.addend.value).bits;        // 之后：immediate payload
```

这些 public member 变更在 installed C++ package `0.1.0` 明确构成 API break；其 CMake package compatibility 为 `SameMinorVersion`。consumer 需按上文迁移到 register-or-immediate variant 的访问方式。不提供 compatibility shim，也不引入并行表示：该改动复用 scalar `add`/`sub` 与 DIV cohort 已在使用的同一条 typed register/immediate 路径。destination 与 narrow source 保持 register payload。Python wheel 版本为 `0.1.0b0`，属于 beta prerelease；installed C++ package 仍为 `0.1.0`。两者是独立的 package 版本，没有锁步要求。

## 证据

CUDA 13.3.73 `ptxas` 证据使用 `/usr/local/cuda/bin/ptxas -arch=<arch> <module>.ptx -o <temporary>.o`。49 个 module 的 matrix 有 27 个预期接受、22 个拒绝，且没有 assembler divergence。version floor 由双向确认：`add.bf16` 以 `Feature 'add.bf16' requires PTX ISA .version 7.8 or later` 拒绝 `.version 7.5`，`add.f32x2` 与 mixed form 以对应的 `requires PTX ISA .version 8.6 or later` 拒绝 `.version 8.5`。target floor 在 `sm_90` 确认，packed 与 mixed cohort 都报告 `requires .target sm_100 or higher`。

有一处 floor **无法**由 assembler 验证：该 `ptxas` 根本不把 `sm_53` 定义为 `-arch` 取值，因此 half cohort 的 PTX 4.2 / SM 53 minimum 依赖 manual 与 frontend checker test，而非 assembler 证据。这属于 toolchain 限制，不是 frontend contract 差异。

既有 MUL 与 FMA 回归——包括 mixed FMA、`.f32x2`、ReLU/OOB 及已记录的 assembler-compatibility 例外——未作改动。

Issue 142 的对账现已覆盖该 issue 划定范围内的 §9.7.1 至 §9.7.5 全部 cohort。这里不添加任何 operation 的 execution semantics。
