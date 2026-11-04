# 寄存器声明兼容性

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#register-type-policy) 为唯一维护依据；本文保留技术参考与历史证据。

## Contract

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#register-type-policy--contract)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/register_type_policy.md#contract)。

## 规范来源记录

工程规范版本为 PTX ISA 9.3。本次依据
[CUDA 13.3.0 归档的 PTX ISA 9.3 手册](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)，
查阅日期为 2026-09-10：5.2.1 与 9.4/Table 26 规定基础类型兼容性，9.7.1.3/9.7.1.4
规定 wide 整数 operand，5.2.3/5.2.5 规定 alternate/packed 声明格式。
不把持续更新的无版本手册 URL 当作冻结规范版本。

## 验证边界

本节保留下述固定版本的历史审查、策略表与工具结果，不是当前 source permission 清单；当前规则见英文 [SDK 支持契约](../sdk_support.md#register-type-policy)。

审查基线为 `c6e5e2d5503b6f8a7d01dca7e7cb43e33f78719a`，三个规格文件中共有
106 处显式 `exact` 声明。本次决策如下：

| Consumer | Policy 决策 |
| --- | --- |
| `mul/mad.wide.u32`、`popc/clz`、`bfind/bfe`、普通 `mad/div/min/max` 浮点形式 | 同宽基础类型兼容，保留各 operand 原有位宽 |
| `neg.f16x2` | 两个 operand 接受 native `.f16x2` 或 `.b32`，使用 same-width compatibility |
| 原生 packed `cvt` data | 原生 `.f16x2` 或兼容 Bit 载体；converted-data widening 独立检查 |
| `cvt` 普通 f32 operand | EqualOrWider 兼容寄存器 |
| `mapa/getctarank`、`isspacep` | address shape/state-space 检查独立保留 |
| `vote/match/redux/elect/activemask`、mbarrier count/parity/hint、解码后的 cluster CTA ID | 同宽兼容；mask、数值范围与 target 检查独立保留 |
| alternate-format FMA（`bf16`、`bf16x2`、`f32x2`、mixed bf16 source） | 保留 exact bit-container 要求 |
| opaque mbarrier state 与 cluster cancellation response | 保留 exact token/response 存储及 shape 要求 |
| mbarrier opaque report value | 保留既有 exact `.b8` 模型；本次查阅手册未独立证实其声明位宽 |

`cvt` 选定的 InstructionOnly 浮点／定点格式必须使用 Bit 存储，即使 operand model
仅记录物理 Bit 宽度。原生 F16x2 允许原生或 Bit 声明。兼容 converted-data Bit 载体可
放宽到 B128，但 BF16/BF16x2 source、destination 与 TF32 destination 必须精确同宽。
Intrinsic 寄存器类型服从相同 kind／宽度检查。Scale factor 独立要求 Bit，随机 bit
辅助 operand 则保留普通 bit-compatible 类型。根据对 CVT source note 的包含式解读，
factor 允许至少 16 位的 Bit 寄存器，随机数据允许至少 32 位的兼容寄存器，均可至 B128；
只有逻辑低 16／32 位参与。Data-format 精确宽度例外仍按 data role 独立检查。这是宽泛
note 的解释，并非辅助 operand 专属句。参见
[转换契约](converage/conversion_coverage.md#逻辑格式与寄存器载体)。

回归测试链接真实 parser、resolver、兼容性 helper 和 generated checker。
独立类型矩阵与声明替换测试不从指令 YAML 推导预期结果；负向用例检查 diagnostic kind
和 operand range，descriptor 断言检查 policy 传递，不改变公共 IR 字段。

原始 round-seven evidence bundle 不是新增回归测试的前置依赖；重建用例不能被报告为
执行了不可用附件。有界工作区搜索未找到原始 bundle。最初的非交互式 PATH 查询未定位到
`ptxas`；随后检查交互式 shell，确认 `.bashrc` 提供了 `/usr/local/cuda/bin/ptxas`，
版本为 CUDA 13.1、V13.1.115，build 为 `cuda_13.1.r13.1/compiler.37061995_0`。

assembler 与当前链接的 frontend 读取了**同一批 23 个重建模块**，均使用
`.version 8.0`、`.target sm_80`，assembler 命令为 `ptxas -arch=sm_80 -O0`。
双方均接受 15 个、拒绝 8 个，接受/拒绝结果全部一致。assembler 无 warning；非法模块
报告 argument-mismatch error，退出码为 255。frontend 的拒绝来自 module resolution
传递的 generated-checker 诊断，而非 parse failure。
[完整源码、driver、退出码及诊断](../register_type_policy_ptxas.json)记录了 wide mul/mad、
位计数、普通浮点与位运算、mask、packed f16x2/bf16 控制以及 issue 的示例 kernel。

这只是 CUDA 13.1 对上述 PTX 8.0 fixture 的对照，**不是** CUDA 13.3 / PTX 9.3
工具链验证，也不代表所有 exact consumer 均已完成 conformance audit。

2026-09-10 验证环境为 GCC 15.2.0、Python 3.14.4。修改规格前，首批六组 linked
回归中五组失败，exact-bf16 对照通过。修正后，506 项 resolved-IR 测试（含九组
register-policy 测试）、九项 modern-operand code-generation 测试及 199 项 Python
model/generator 测试全部通过。执行命令：

```sh
cmake --build out/build/ci-linux-gcc-debug --target test_resolved_ir test_modern_operand_codegen -j 4
out/build/ci-linux-gcc-debug/submod/resolved_ir/test_resolved_ir --gtest_brief=1
out/build/ci-linux-gcc-debug/submod/resolved_ir/test_modern_operand_codegen --gtest_brief=1
PYTHONPATH=python .venv/bin/python -m unittest discover -s python/tests -t python -p 'test_*.py' -q
```

当前 native packed register carrier 与 exact-word source 规则见英文
[SDK 支持契约](../sdk_support.md#resolved-ir-design--scalar-packed32-source-members)
及 [packed SETP](../sdk_support.md#resolved-ir-design--packed-setp-raw-and-exact-words)。
