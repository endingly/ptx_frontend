# SET 覆盖情况

Frontend 建模 [PTX ISA 9.3 §9.7.6.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-set) 的普通标量 `set`，以及 [§9.7.7.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-set) 的 half/bfloat `set`。Canonical contract 位于 `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`。Resolution 保留 typed comparison、可选 Boolean operation、可选 flush control 及 result/source type；checker 验证组合与 operand。Frontend 不求值 comparison，也不写入结果值。

## 普通标量形式

`set.CmpOp{.BoolOp}{.ftz}.dtype.stype d, a, b{, c}` 的 result `.dtype` 为 `.u32/.s32/.f32`。比较 source `a`、`b` 为 `.stype` 的 register-or-immediate value；`d` 为 `.dtype` 的 register。仅当 `.BoolOp` 为 `.and/.or/.xor` 时才需要 Boolean source `c`；它接受普通或取反的 predicate register，也接受 integer predicate constant。Integer predicate constant 遵循 PTX 的 C truth rule，包括前导 `!`。不接受 predicate special register 或 floating constant。

| Source `.stype` | Comparison suffix | `.ftz` | 最低 target |
| --- | --- | --- | --- |
| `.b16/.b32/.b64` | `.eq/.ne` | 不允许 | PTX 1.0 |
| `.s16/.s32/.s64` | `.eq/.ne/.lt/.le/.gt/.ge` | 不允许 | PTX 1.0 |
| `.u16/.u32/.u64` | signed-family suffix，加 `.lo/.ls/.hi/.hs` | 不允许 | PTX 1.0 |
| `.f32` | ordered suffix、unordered `.equ/.neu/.ltu/.leu/.gtu/.geu`、`.num/.nan` | 可选 | PTX 1.0 |
| `.f64` | 同一 floating suffix | 不允许 | PTX 1.0、`sm_13` |

选定的 `.stype` 决定 comparison domain 及 `.ftz` 可用性，与 `.dtype` 无关。每个 source family 分别有无 Boolean 与需要 Boolean 的 generated variant。[普通 SET C++ 测试](../../submod/resolved_ir/test/test_set_completeness.cpp) 覆盖 family、modifier/operand negative、target gate 与被修改的 typed IR。

## Half 与 bfloat 形式

Half/bfloat form 继续使用 typed comparison 与可选 Boolean source，但 destination 与 source contract 分开建模。`set.CmpOp{.BoolOp}{.ftz}.dtype.stype d, a, b{, {!}c}` 写入 `.f16`、`.bf16`、`.f16x2`、`.bf16x2` 或允许的整数 result。可选的 `c` 与 ordinary `set` 使用相同 predicate-source contract。30 个 generated alternative 对 15 个 result/source cohort 分别建模有无 Boolean 组合。

| Result `.dtype` | Source `.stype` | 最低 PTX / SM | `.ftz` |
| --- | --- | --- | --- |
| `.f16` | `.b16/.b32/.b64`、16/32/64 位整数、`.f16/.f32/.f64` | 4.2 / 53 | 仅 `.f16/.f32` source |
| `.bf16` | 相同 source 列表 | 7.8 / 90 | 不允许 |
| `.u16/.s16/.u32/.s32` | `.f16` | 6.5 / 53 | 可选 |
| `.u16/.s16/.u32/.s32` | `.bf16` | 7.8 / 90 | 不允许 |
| `.f16x2` | `.f16x2` | 4.2 / 53 | 可选 |
| `.u32/.s32` | `.f16x2` | 6.5 / 53 | 可选 |
| `.bf16x2/.u32/.s32` | `.bf16x2` | 7.8 / 90 | 不允许 |

Bit source 只允许 `.eq/.ne`；有符号及无符号整数 source 允许 `.eq/.ne/.lt/.le/.gt/.ge`；浮点与 packed source 还允许 `.equ/.neu/.ltu/.leu/.gtu/.geu/.num/.nan`。Ordinary unsigned 的 `.lo/.ls/.hi/.hs` 别名不适用于这些 form。`ptxas` 13.3 对 bit、整数和 `.f64` source 的 `.ftz` 报错，包括 `.f16` result；因此对应 generated alternative 不允许 `.ftz`。

标量 `.f16` source 接受 `.f16` 或兼容的 `.b16` register；标量 `.bf16` source 要求 exact `.b16`。Packed `.f16x2` source 接受 `.f16x2` 或兼容的 `.b32`，`.bf16x2` source 则要求 exact `.b32`。Native `.f16x2` result 接受 `.f16x2` 或 `.b32`；native `.bf16x2` result 要求 `.b32`。整数 result 使用宽度兼容的 bit 或整数 register container；packed-source 整数 result 还接受同宽度 `.f16x2` 物理 register。Half/bfloat source 不接受 immediate；普通 bit、整数、`.f32` 和 `.f64` source cohort 在合法场景下保留 register-or-immediate operand。

[Half/bfloat C++ 测试](../../submod/resolved_ir/test/test_set_half_completeness.cpp) 覆盖全部 generated alternative、target 边界、非法 control/container、数值 immediate、owned IR 与变异的 typed 字段。[Python spec 测试](../../python/tests/spec/test_set_completeness.py) 检查普通与 half/bfloat descriptor domain。安装后的 [consumer](../../examples/conversion_consumer/main.cpp) 使用 public generated representation。

C++ package 0.2.0 将原 generated `Set::EqU32U32` 与 `Set::LtAndF32S32` 分支替换为 typed ordinary family：`Bit`、`Signed`、`Unsigned`、`Float`、`FloatF64`，以及相应的 `Boolean` 分支。Consumer 应选择新分支，并读取 `dtype`/`stype` 字段判断具体 type。这是 source 与 binary API 变更；installed C++ consumer 需用新 header 和 library 重新构建。
