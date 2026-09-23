# 浮点 `min`/`max` 覆盖

生成模型补齐了 [PTX 9.3 §9.7.3.11–§9.7.3.12](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions) 的 [minimum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-min) 与 [maximum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-max) form，以及 §9.7.4.7 与 §9.7.4.8 的 half/bfloat cohort。sign/magnitude 控制是 typed：`.xorsign.abs` 与 `.abs` 是带独立 slot identity（`xorsign_abs` 与 `abs`）的两个不同 token，因此 `.xorsign` 无法脱离 `.abs` 拼写，consumer 也不需要从一个读作 false 的 `abs` field 推断某个 paired operation 其实是 absolute-magnitude operation。frontend 建模 parsing、owned resolution 与 target validation，不执行 comparison 或 selection。

| Form | Minimum | Contract |
| --- | --- | --- |
| `min{.ftz}{.NaN}{.xorsign.abs}.f32 d, a, b` | base PTX 1.0 / 所有 target architecture；`.ftz` PTX 1.4；`.NaN` PTX 7.0 / SM 80；`.xorsign.abs` PTX 7.2 / SM 86 | paired sign/magnitude。destination 为 register；两个 source 都接受 floating literal 或同宽 register。 |
| `min{.ftz}{.NaN}{.abs}.f32 d, a, b, c` | PTX 8.8 / SM 100 | 三个 source；只接受 `.abs`，禁止 `.xorsign`。destination 为 register；三个 source 都接受 floating literal 或同宽 register。 |
| `min.f64 d, a, b` | PTX 1.0 / SM 13 | 没有 FP32-only flag。destination 为 register；两个 source 都接受 floating literal 或同宽 register。 |
| `min{.ftz}{.NaN}{.xorsign.abs}.{f16,f16x2} d, a, b` | PTX 7.0 / SM 80 | `.ftz` optional；仅 register。`.f16` 接受 `.f16` 或 `.b16`，`.f16x2` 接受 `.f16x2` 或 `.b32`。 |
| `min{.NaN}{.xorsign.abs}.{bf16,bf16x2} d, a, b` | PTX 7.0 / SM 80 | 没有 `.ftz`；仅 register，且 BF16 只绑定 `.b16`/`.b32` bit container。 |

`max` 的表相同。two-source 与 three-source FP32 form 是**同一 variant 的两个 operand layout**，只按 operand 个数选择，且每个 layout 声明自己拒绝的 modifier slot。因此 `min.xorsign.abs.f32` 在三操作数下被接受、四操作数下被拒绝，而 `min.abs.f32` 恰好相反：coupled spelling 属于 two-source topology，three-source cohort 只接受 `.abs`。layout selection 只看 shape，所以这一合法性由 checker 强制，而不是由 variant selection 决定。variant exclusivity 始终保守地基于 resolver 实际匹配的完整 syntactic modifier language，因为 `select_variant_name` 不查阅 layout 约束。

只有 scalar FP32 与 FP64 cohort 接受 literal，且只在 source 位置；half 与 bfloat cohort 仍仅接受 register。integer literal 与 integer register container、错误宽度、sink、literal destination、任何 BF16 cohort 上的 `.ftz`、FP64 或任何非 FP32 cohort 上的 `.abs`，以及 FP32 cohort 三或四以外的所有 arity 都会被拒绝。

CUDA 13.3.73 `ptxas` 证据使用 `/usr/local/cuda/bin/ptxas -arch=<arch> <module>.ptx -o <temporary>.o`。58 个 module 的 matrix 有 35 个预期接受、23 个拒绝，且没有 assembler divergence。拒绝项由 assembler 自身诊断印证，其措辞与模型约束一致：bare two-source spelling 得到 `Modifier '.abs' requires modifier '.xorsign'`，bare fragment 得到 `Modifier '.xorsign' requires modifier '.abs'`，three-source form 得到 `Illegal modifier '.xorsign' for instruction 'min'`，BF16 cohort 得到 `Illegal modifier '.ftz' for instruction 'min'`。literal 可接受性由双向确认：`min.f32 %f0, 1.0, %f1;`、`min.f32 %f0, %f1, 0f00000000;` 与 `min.f64 %d0, %d1, 1.0;` 被接受，而 `min.f32 1.0, %f1, %f2;` 以 `Result register required for instruction 'min'` 失败，`min.f32 %f0, 1, %f1;` 以 `Arguments mismatch` 失败，所有 half/bfloat literal spelling 也以 `Arguments mismatch` 失败。在工具链允许处 floor 由双向确认：`.target sm_80` 接受 `.NaN` 而在 `sm_75` 拒绝（`Modifier '.NaN' requires .target sm_80 or higher`），`sm_86` 接受 `.xorsign.abs` 而在 `sm_80` 拒绝，three-source form 需要 `sm_100`（`Feature 'min.f32 with 3 input operands' requires .target sm_100 or higher`）。配对的 `.version` probe 将 version floor 定在 7.0 与 7.2（`Feature '.abs' requires PTX ISA .version 7.2 or later`，在 7.1 被拒绝），three-source form 为 8.8。

本次审计还记录了一个 assembler 较宽松的 container 结果，模型选择遵循而非与之相悖：`min.f16x2` 同时接受 `.f16x2` 与 `.b32` operand，因此 packed half cohort 绑定 same-width container，而不是 exact bit container。既有的 `mul_half_x2` 对同一 operand shape 绑定 exact `.b32` container，该 probe 提示这属于过度限制；此处未改动它。

## 已安装 surface 变更

两个生成的公共类型改变了形状，这是刻意为之，且仅限本切片重写的 opcode：

- `Min::NanF32` / `Max::NanF32` 变为 `Min::F32` / `Max::F32`，因为 frozen `.NaN` seed 被并入通用 FP32 cohort，而不是重复建模。
- `Min::F32` 与 `Max::F32` 现在带 `Operands = std::variant<BinaryOperands, TernaryOperands>`，`ResolvedOperandLayoutTag{0}` 选择 two-source layout，`{1}` 选择 three-source layout。

consumer 需将 `Min::NanF32` / `Max::NanF32` 引用迁移到 `Min::F32` / `Max::F32`。假设有一个名为 `value` 的 `Min::F32` 值，应按 operand 数量选择对应的 variant alternative：

```cpp
const auto& binary_operands = std::get<Min::F32::BinaryOperands>(value.operands);  // two-source
// Or, when the instruction has three sources:
const auto& ternary_operands = std::get<Min::F32::TernaryOperands>(value.operands);
```

这些 public shape change 在 installed C++ package `0.1.0` 明确构成 API break；其 CMake package compatibility 为 `SameMinorVersion`。不提供 compatibility shim 或并行的 legacy representation，因此 consumer 需要迁移到重命名后的 cohort 和 operand variant。Python wheel 版本为 `0.1.0b0`，属于 beta prerelease；installed C++ package 仍为 `0.1.0`。两者是独立的 package 版本，没有锁步要求。仓库没有专用 migration 或 changelog 文档，因此迁移说明保留在此处。

共享 checker 结构的新增成员一律**追加**而非插入，因此既有 aggregate initializer 仍可编译：`checker::ModifierValueView` 的 `slot` 是最后一个成员，`checker::OperandLayoutDescriptor` 新增 `forbidden_modifiers`，同时新增 `CheckDiagnosticKind::ModifierNotAllowedForLayout`。该 modifier 诊断在 provenance 仍保留时报告 offending modifier 自身的 source range，否则回落到 context range。

[浮点与 mixed ADD/SUB](add_sub_coverage.md) 完成 §9.7.3–§9.7.5 其余 cohort 的对账；这里不添加它们的 execution semantics。
