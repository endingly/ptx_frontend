# Ordinary SET 覆盖情况

Frontend 建模 [PTX ISA 9.3 §9.7.6.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-set) 的 ordinary scalar `set` form。Canonical contract 位于 `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`。Resolution 保留 typed comparison、可选 Boolean operation、可选 flush control 及 result/source type；checker 验证组合与 operand。Frontend 不求值 comparison，也不写入结果值。

`set.CmpOp{.BoolOp}{.ftz}.dtype.stype d, a, b{, c}` 的 result `.dtype` 为 `.u32/.s32/.f32`。比较 source `a`、`b` 为 `.stype` 的 register-or-immediate value；`d` 为 `.dtype` 的 register。仅当 `.BoolOp` 为 `.and/.or/.xor` 时才需要 Boolean source `c`；它接受普通或取反的 predicate register，也接受 integer predicate constant。Integer predicate constant 遵循 PTX 的 C truth rule，包括前导 `!`。不接受 predicate special register 或 floating constant。

| Source `.stype` | Comparison suffix | `.ftz` | 最低 target |
| --- | --- | --- | --- |
| `.b16/.b32/.b64` | `.eq/.ne` | 不允许 | PTX 1.0 |
| `.s16/.s32/.s64` | `.eq/.ne/.lt/.le/.gt/.ge` | 不允许 | PTX 1.0 |
| `.u16/.u32/.u64` | signed-family suffix，加 `.lo/.ls/.hi/.hs` | 不允许 | PTX 1.0 |
| `.f32` | ordered suffix、unordered `.equ/.neu/.ltu/.leu/.gtu/.geu`、`.num/.nan` | 可选 | PTX 1.0 |
| `.f64` | 同一 floating suffix | 不允许 | PTX 1.0、`sm_13` |

选定的 `.stype` 决定 comparison domain 及 `.ftz` 可用性，与 `.dtype` 无关。每个 source family 分别有无 Boolean 与需要 Boolean 的 generated variant。[C++ 测试](../../submod/resolved_ir/test/test_set_completeness.cpp) 覆盖 family、modifier/operand negative、target gate 与被修改的 typed IR。[Python spec 测试](../../python/tests/spec/test_set_completeness.py) 检查不重叠的 domain 与 operand descriptor。§9.7.7.1 的 half/bfloat `set` 不属于此 ordinary slice。

C++ package 0.2.0 将原 generated `Set::EqU32U32` 与 `Set::LtAndF32S32` 分支替换为 typed ordinary family：`Bit`、`Signed`、`Unsigned`、`Float`、`FloatF64`，以及相应的 `Boolean` 分支。Consumer 应选择新分支，并读取 `dtype`/`stype` 字段判断具体 type。这是 source 与 binary API 变更；installed C++ consumer 需用新 header 和 library 重新构建。
