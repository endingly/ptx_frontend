# 寄存器声明兼容性

## Contract

`register_width: same_width` 同时检查位宽和基础类型兼容性，而非枚举 identity：
bit-size 寄存器可承载同宽兼容的整数或浮点 operand，同宽有符号与无符号整数声明可互换。
整数与浮点之间的替换仍然非法。`mul.wide.u32` 和 `mad.wide.u32` 仍要求 32 位乘数、
64 位结果；`mad` 的加数也必须为 64 位。本次不引入更宽寄存器放宽。

`exact` 保留枚举 identity 语义，适用于显式声明格式要求，不能仅因为普通算术 operand
位宽固定就使用它。`equal_or_wider` 和立即数转换不变。resolution 仍构造相同 IR，
generated checker 按 descriptor policy 检查已绑定寄存器声明；仅 resolve 成功不表示
整个模块的指令均通过 checking。

declaration semantics 还会验证每个 `.reg` 的 type token，即使该寄存器从未被使用。
允许的基础 declaration type 为 `.s8/.s16/.s32/.s64`、`.u8/.u16/.u32/.u64`、
`.b8/.b16/.b32/.b64/.b128`、`.f16`、`.f16x2`、`.f32`、`.f64` 以及 scalar `.pred`。
已识别的 packed 或 alternate instruction format（例如 `.bf16`、`.tf32` 和已建模的
packed 拼写）会报告为 instruction-only；未识别的拼写会报告为 unknown。CST 仍保持
permissive。non-predicate `.v2`/`.v4` 寄存器最大为 128 bit，predicate 寄存器必须
是 scalar。这些 declaration rule 保留合法 `.reg` function formal 及 parameterized
register group。

## 规范来源记录

工程规范版本为 PTX ISA 9.3。本次依据
[CUDA 13.3.0 归档的 PTX ISA 9.3 手册](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)，
查阅日期为 2026-09-10：5.2.1 与 9.4/Table 26 规定基础类型兼容性，9.7.1.3/9.7.1.4
规定 wide 整数 operand，5.2.3/5.2.5 规定 alternate/packed 声明格式。
不把持续更新的无版本手册 URL 当作冻结规范版本。

## 验证边界

审查基线为 `c6e5e2d5503b6f8a7d01dca7e7cb43e33f78719a`，三个规格文件中共有
106 处显式 `exact` 声明。本次决策如下：

| Consumer | Policy 决策 |
| --- | --- |
| `mul/mad.wide.u32`、`popc/clz`、`bfind/bfe`、普通 `mad/div/min/max` 浮点形式 | 同宽基础类型兼容，保留各 operand 原有位宽 |
| `neg.f16x2`、packed `cvt` 的 destination | expected type 使用 `f16x2` 加 `same_width`，接受 `.f16x2`/`.b32`，不接受任意 32 位存储 |
| `cvt` 普通 f32 operand、`mapa/getctarank`、`isspacep` | 同宽兼容；address shape/state-space 检查独立保留 |
| `vote/match/redux/elect/activemask`、mbarrier count/parity/hint、解码后的 cluster CTA ID | 同宽兼容；mask、数值范围与 target 检查独立保留 |
| alternate-format FMA（`bf16`、`bf16x2`、`f32x2`、mixed bf16 source） | 保留 exact bit-container 要求 |
| opaque mbarrier state 与 cluster cancellation response | 保留 exact token/response 存储及 shape 要求 |
| mbarrier opaque report value | 保留既有 exact `.b8` 模型；本次查阅手册未独立证实其声明位宽 |

本次不是更宽寄存器 `cvt` 行为的完整审查。受影响的 f32 operand 现在接受兼容的**同宽**
声明；PTX 9.4.1 节与 `cvt` 指令规则中的进一步 widening 不属于此次修正。

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
