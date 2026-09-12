# MUL 覆盖情况

本文记录 frontend 建模的完整 PTX 9.3 `mul` contract。它补充[语法覆盖矩阵](syntax_coverage.md)，
不表示 simulator execution 或 physical GPU 行为。唯一的 machine-readable source 是
`python/code_gen/resources/ptx_spec/arithmetic.yaml`。

规范依据为 NVIDIA PTX ISA 9.3 archive：[integer MUL
§9.7.1.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-mul)、
[floating MUL
§9.7.3.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mul)
和 [half/bfloat MUL
§9.7.4.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-mul)。

下表每行都经过 syntax、Resolved IR 与 target-aware checking 建模；均只定义 frontend
contract，不提供 simulator semantics。

| Form | 最低 PTX / target | Modifier | Operand contract |
| --- | --- | --- | --- |
| `.u16/.u32/.u64/.s16/.s32/.s64` 的 integer `.lo` 与 `.hi` | 1.0 / 全部 target | 恰有 `.lo` 或 `.hi` 之一 | destination/source 使用所选 width；source 可为 register-or-immediate，也接受同宽 bit register |
| `.u16/.s16` 的 integer `.wide` | 1.0 / 全部 target | `.wide` | 32-bit destination；16-bit register-or-immediate source |
| `.u32/.s32` 的 integer `.wide` | 1.0 / 全部 target | `.wide` | 64-bit destination；32-bit register-or-immediate source |
| `.f32` | 1.0 / 全部 target；`.rm/.rp` 要求 `sm_20` | 省略 rounding 时默认 `.rn`；`.rn/.rz` 全 target 可用；`.rm/.rp`、`.ftz`、`.sat` 均按合法 form 建模 | floating register-or-immediate source，且接受同宽 bit-register compatibility |
| `.f32x2` | 8.6 / `sm_100` | 省略 rounding 时默认 `.rn`；四种 rounding 与 `.ftz` 合法；不允许 `.sat` | 所有 operand 都是 exact `.b64` register |
| `.f64` | 1.0 / `sm_13` | 省略 rounding 时默认 `.rn`；四种 rounding 合法；不允许 `.ftz`、`.sat` | floating register-or-immediate source，且接受同宽 bit-register compatibility |
| `.f16` | 4.2 / `sm_53` | 省略 rounding 时默认 `.rn`；显式 `.rn`、`.ftz`、`.sat` 可独立出现 | 仅 register 的 `.f16` 或同宽 `.b16` value |
| `.f16x2` | 4.2 / `sm_53` | 省略 rounding 时默认 `.rn`；显式 `.rn`、`.ftz`、`.sat` 可独立出现 | 所有 operand 都是 exact `.b32` register |
| `.bf16` | 7.8 / `sm_90` | 省略 rounding 时默认 `.rn`；仅显式 `.rn` 合法；不允许 `.ftz`、`.sat` | 所有 operand 都是 exact `.b16` register |
| `.bf16x2` | 7.8 / `sm_90` | 省略 rounding 时默认 `.rn`；仅显式 `.rn` 合法；不允许 `.ftz`、`.sat` | 所有 operand 都是 exact `.b32` register |

## 有意保留的边界

archive 只允许 16-bit 与 32-bit integer type 使用 `.wide`。因此 `.wide.u64` 和
`.wide.s64` 会被拒绝，不能从同宽 `.hi/.lo` family 推断为合法。

floating、half 与 bfloat syntax 允许省略 rounding，默认值是 `.rn`。YAML 记录该默认值，
而非要求冗余 `.rn`。它仍拒绝 directed half/bfloat rounding、`.f32x2` 与 bfloat packed
form 的 `.sat`，以及 bfloat `.ftz/.sat`。packed form 保留文档给出的 exact bit-container
width，因此即使看似同宽但 container 错误也会被 checker 拒绝。

既有 public Resolved-IR 名称 `Mul::RnF32`、`Mul::LoU32`、`Mul::HiU32`、
`Mul::WideU32` 与 `Mul::WideS32` 仍可用。本覆盖只增加 form，不建立独立 public
support API。

## 验证来源

[专用 C++ MUL 测试](../../submod/resolved_ir/test/test_mul_completeness.cpp)
覆盖所有 family 的 parse、resolve、declared-operand check、target minimum 和非法
modifier/container。[已安装 consumer MUL 测试](../../submod/resolved_ir/test/package_consumer/mul_completeness.cpp)
只用 installed public header 验证新的 integer semantic form 与错误 packed container 的
checker rejection。focused Python database test 验证 canonical section、variant、modifier
domain、availability 和 operand layout。这些来源仅定义 frontend 边界；不验证 simulator
或 hardware execution result。
