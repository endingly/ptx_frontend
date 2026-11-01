# FMA 覆盖情况

本文描述 frontend 已建模的完整 PTX 9.3 `fma` contract。它补充[语法覆盖矩阵](syntax_coverage.md)，
不表示所有 PTX opcode 或 simulator execution 已受支持。machine-readable instruction
specification 为 `python/src/ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml`。

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
| `fma_rn_f16`、`fma_rn_f16x2` | 4.2 / `sm_53` | `.rn` 可省略（默认 RN）；`.ftz`、`.sat` 可独立任选 | 仅 register：scalar 为 `.f16`/`.b16`，packed 为 `.f16x2`/`.b32` |
| `fma_half_relu` | 7.0 / `sm_80` | `.rn` 可省略（默认 RN）；`.relu` 必需；`.ftz` 可选 | 仅 register 的 `.f16`/`.b16`，或 `.f16x2`/`.b32` packed value |
| `fma_half_oob`、`fma_half_oob_relu` | 8.1 / `sm_90` | `.rn` 可省略（默认 RN）；`.oob` 必需；后者必须有 `.relu`；两者都不允许 `.ftz` 或 `.sat` | 仅 register 的 `.f16`/`.b16`，或 `.f16x2`/`.b32` packed value |
| `fma_bf16`、`fma_bf16x2` | 7.0 / `sm_80` | `.rn` 可省略（默认 RN）；`.relu` 可选；不允许 `.ftz`、`.sat` | 仅 exact `.b16` scalar 或 `.b32` packed register |
| `fma_bf16_oob`、`fma_bf16x2_oob` | 8.1 / `sm_90` | `.rn` 可省略（默认 RN）；`.oob` 必需；`.relu` 可选；不允许 `.ftz`、`.sat` | 仅 exact `.b16` scalar 或 `.b32` packed register |
| `fma_mixed_f32_f16`、`fma_mixed_f32_bf16` | 8.6 / `sm_100` | 四种 rounding mode 之一必需；`.sat` 可选 | destination 和 accumulator `c` 为 `.f32` 或 `.b32`；F16 multiplicand `a`/`b` 接受 `.f16`/`.b16` register 或 numeric half constant；当前 bf16 form 保留 exact `.b16` register；`c` 可为 floating immediate |

## 有意保留的兼容性边界

half/bfloat 章节将 RN 定义为默认值，也适用于 OOB form。frontend 接受省略或显式
`.rn`，仍拒绝其他 rounding mode。OOB syntax 允许可选 `.relu`，不允许 `.sat` 或 `.ftz`。
历史上 `ptxas` 13.3.33 拒绝省略 `.rn`、接受 half OOB `.sat`，仅作为补充工具差异，
不决定 frontend grammar。

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

公开 C++ ID `FmaRnF32`、`FmaRnF64` 和 `FmaRnF16` 继续保留。
`RnF32`、`RnF64` 的三个 source field 现在是 `WithLocs<RegOrImm>`；consumer
需要区分 register reference 与带类型的 immediate。

九个 half/bfloat class（`FmaRnF16`、`FmaRnF16x2`、`FmaHalfRelu`、
`FmaHalfOob`、`FmaHalfOobRelu`、`FmaBf16`、`FmaBf16x2`、`FmaBf16Oob`、
`FmaBf16x2Oob`）现提供 instance `WithLocs<RoundingMode> rounding`，替代 static
rounding constant。读取 `value.rounding.value` 与 `.locs`：省略 RN 时值为 `Rn`、
locations 为空；显式 RN 保留 token location。当前 RN presence 的一致修改仍合法；
supplied AST association 独立检查 retained syntax identity。
`FmaHalfOob::saturate` 已删除，不提供 SAT field 或隐含 SAT permission。
这些 generated C++ layout 变化要求重编译 consumer。26 个 register-only source role
以及全部 carrier/type/availability contract 不变；modifier validation 不执行 FMA 算术、
FTZ、saturation、ReLU 或 OOB 运行语义。

## 验证

[C++ resolution test](../../../submod/resolved_ir/test/test_select_variant_fma.cpp)
验证全部 92 种 canonical modifier combination：48 种 half/bfloat product 和
44 种 ordinary/mixed product，并保留非法组合及浮点字面量转换边界。
[Availability test](../../../submod/resolved_ir/test/test_select_variant_fma.cpp)
对 16 个 variant 逐一验证最低 PTX/SM，并分别验证低于各项最低版本时的拒绝行为。
[Module test](../../../submod/resolved_ir/test/test_resolved_module_instruction_types.cpp) 验证声明的
operand type、exact bit container 以及 mixed form 的每个 operand position。
[Owned-control test](../../../submod/resolved_ir/test/test_half_fma_modifiers.cpp)
验证全部 48 种 half product、默认 presence、非法 mode、source association、availability，
以及 AST 销毁后的独立 copy/clone。
[安装 consumer](../../../examples/resolved_ir_consumer/main.cpp) 读取全部九个 instance
rounding field，并在无 source storage 时验证 copy/clone。

共享 Python 测试通过 `python/tests/code_gen/test_ptx_spec_taxonomy.py` 验证
PTX section taxonomy，通过 `python/tests/ir/test_resolved_ir.py` 验证生成的
Resolved-IR operand layout。installed-wheel smoke test
`python/tests/code_gen/wheel_smoke.py` 验证 16 个 variant 及相互独立的
mixed-precision operand type。

本文的 coverage 由三个 official PTX 9.3 FMA section 及上述实现和测试界定，不依赖手工
coverage ledger。较早的 common-kernel corpus 与实际 opcode coverage 不同；不会以
corpus presence 作为完成闭环的证据。

Mixed F32/F16 multiplicand 现使用 RegOrImm，接受只舍入一次为 binary16 的 numeric floating constant。F32 accumulator 保留既有 numeric 路径。显式 half-arithmetic register-only form 与当前 BF16 mixed-source representation 保持独立；literal 转换不执行 FMA rounding/saturation 的运行结果。
