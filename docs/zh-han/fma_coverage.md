# FMA 覆盖情况

本文描述 frontend 已建模的完整 PTX 9.3 `fma` contract。它补充[语法覆盖矩阵](syntax_coverage.md)，
不表示所有 PTX opcode 或 simulator execution 已受支持。machine-readable source 为
`instructions/ptx_spec/arithmetic.yaml` 和 `instructions/opcode_coverage.yaml`。

规范依据是 NVIDIA 的 PTX ISA 9.3 archive：[floating FMA
§9.7.3.6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-fma)、
[half/bfloat FMA
§9.7.4.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-fma)
与 [mixed-precision FMA
§9.7.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-fma)。

下表的所有行都经过 syntax、Resolved IR 和 target-aware checking 建模；均没有
simulator execution semantics。

| Variant | 最低 PTX / target | Modifier | Operand contract |
| --- | --- | --- | --- |
| `fma_rn_f32`、`fma_directed_f32` | 2.0 / `sm_20` | 必须有 `.rn`、`.rz`、`.rm` 或 `.rp` 之一；`.ftz`、`.sat` 可独立任选 | `.f32` destination/source；每个 source 可为 floating register-or-immediate，也接受同宽 bit register |
| `fma_rn_f64`、`fma_directed_f64` | 1.4 / `sm_13` | 四种 rounding mode 之一为必需；无 flag | `.f64` destination/source；每个 source 可为 floating register-or-immediate，也接受同宽 bit register |
| `fma_f32x2` | 8.6 / `sm_100` | 四种 rounding mode 之一为必需；`.ftz` 可选 | 所有 operand 都是 exact `.b64` register |
| `fma_rn_f16`、`fma_rn_f16x2` | 4.2 / `sm_53` | `.rn` 必需；`.ftz`、`.sat` 可独立任选 | 仅 register：scalar 为 `.f16`/`.b16`，packed 为 `.f16x2`/`.b32` |
| `fma_half_relu` | 7.0 / `sm_80` | `.rn`、`.relu` 必需；`.ftz` 可选 | 仅 register 的 `.f16`/`.b16`，或 `.f16x2`/`.b32` packed value |
| `fma_half_oob`、`fma_half_oob_relu` | 8.1 / `sm_90` | `.rn`、`.oob` 必需；前者可加 `.sat`，后者必须有 `.relu`；两者都不允许 `.ftz`，也拒绝 `.sat` 与 `.relu` 同时出现 | 仅 register 的 `.f16`/`.b16`，或 `.f16x2`/`.b32` packed value |
| `fma_bf16`、`fma_bf16x2` | 7.0 / `sm_80` | `.rn` 必需；`.relu` 可选；不允许 `.ftz`、`.sat` | 仅 exact `.b16` scalar 或 `.b32` packed register |
| `fma_bf16_oob`、`fma_bf16x2_oob` | 8.1 / `sm_90` | `.rn`、`.oob` 必需；`.relu` 可选；不允许 `.ftz`、`.sat` | 仅 exact `.b16` scalar 或 `.b32` packed register |
| `fma_mixed_f32_f16`、`fma_mixed_f32_bf16` | 8.6 / `sm_100` | 四种 rounding mode 之一必需；`.sat` 可选 | destination 和 accumulator `c` 为 `.f32` 或 `.b32`；multiplicand `a`/`b` 仅可为 `.f16`/`.b16` register，bf16 form 为 exact `.b16`；`c` 可为 floating immediate |

## 有意保留的兼容性边界

half-precision prose 将 `.rn` 说成默认值，但 FMA model 要求显式写出它：NVIDIA
`ptxas` 13.3.33 会拒绝省略 `.rn` 的写法。因此，显式 rounding modifier 是接受
contract 的一部分。

PTX syntax 写作 `fma.rnd.oob.{relu}.type`，没有 `.sat`；但 `ptxas` 13.3.33 也接受
half（`.f16`/`.f16x2`）OOB form 的 `.sat`。`fma_half_oob` 特意记录了这个
assembler-compatible extension；它不推广到 bf16，也不允许与 `.relu` 同时使用。

mixed-precision 的手册示例含有重复/末尾的 `.sat`。model 只接受 variant 表达的
canonical modifier sequence：一个 rounding modifier、最多一个 `.sat`，然后是 `.f32`
和 `.f16` 或 `.bf16`。

对于 floating `.f32` 和 `.f64` source，允许 cross-width hexadecimal bit literal
（供 `.f64` 使用的 `0f`、供 `.f32` 使用的 `0d`），并从其 lexical precision 转换。
decimal literal 先按 binary64 求值再转换为 operand type；包括会缩窄为 `.f32` infinity
的有限 decimal value。
缩窄使用 round-to-nearest、ties-to-even，不受指令的 rounding 或 `.ftz` 影响。
同宽 bit literal 完整保留原有 payload；跨精度转换时，frontend 将 NaN quiet 化并保留
符号和高位 payload。这是确定性的表示策略，不是 ISA 对 NaN payload 转换的保证。

公开 C++ ID `Fma::RnF32`、`Fma::RnF64` 和 `Fma::RnF16` 继续保留。
`RnF32`、`RnF64` 的三个 source field 现在是 `WithLocs<RegOrImm>`；consumer
需要区分 register reference 与带类型的 immediate。

## 验证

[C++ resolution test](../../submod/resolved_ir/test/test_select_variant.cpp)
验证全部 70 种 canonical modifier combination（包括 half OOB `.sat` 扩展）、非法组合
及浮点字面量转换边界。
[Availability test](../../submod/resolved_ir/test/test_ptx_resolved_ir_checker.cpp)
对 16 个 variant 逐一验证最低 PTX/SM，并分别验证低于各项最低版本时的拒绝行为。
[Module test](../../submod/resolved_ir/test/test_ptx_resolved_module.cpp) 验证声明的
operand type、exact bit container 以及 mixed form 的每个 operand position。
[已安装的 C++ consumer](../../submod/resolved_ir/test/package_consumer/main.cpp)
只使用公开安装 API，对全部 16 个 variant 执行 parse、resolve 和 check。

共享 Python gate 覆盖表中的每一行：
`python/tests/code_gen/test_opcode_coverage_manifest.py` 断言精确的 16-variant/
section/selector inventory；`python/tests/code_gen/test_ptx_spec_taxonomy.py` 验证
PTX section taxonomy；`python/tests/ir/test_resolved_ir.py` 验证生成的 Resolved-IR
operand layout。installed-wheel smoke test `python/tests/code_gen/wheel_smoke.py`
会再次验证 variant inventory 及相互独立的 mixed-precision operand type。

coverage accounting 位于 official PTX 9.3 opcode-section 层级。三个已登记的 FMA section
都已覆盖，因此 inventory entry 为 complete。较早的 common-kernel corpus 与实际 opcode
coverage 不同；不会以 corpus presence 作为完成闭环的证据。
