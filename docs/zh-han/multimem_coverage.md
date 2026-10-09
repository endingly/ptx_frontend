# Multimem 前端覆盖范围（PTX ISA 9.3）

前端识别固定版 [CUDA 13.3 PTX ISA 9.3 手册](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
§§9.7.9.13、9.7.9.15、9.7.9.26.4.4–5 和 9.7.14.8 的七个 `multimem`
源码族。它们共用一个 canonical `multimem` opcode，解析为精确的、自有的类型化指令类。
公共 `ResolvedAddress` 保留原地址表达式、源码位置，以及可用的声明绑定。
全局地址或符号**不能证明**运行时地址是 multimem 映射。

| 源码族 | 检查的操作数与后缀 | PTX / 目标下限 | 完成机制 |
| --- | --- | --- | --- |
| `multimem.ld_reduce` | 寄存器目标、全局/泛型 multimem 地址；省略或显式 `.weak`，或带 scope 的 `.relaxed/.acquire`；整数/浮点操作、类型、向量和可选累加精度受规范表约束 | 8.1 / SM 90；`.acc::f32` 为 8.2；FP8 和 `.acc::f16` 为 8.6 且限列出的架构族 | 无 |
| `multimem.st` | 全局/泛型 multimem 目标和标量或寄存器向量源；省略或显式 `.weak`，或带 scope 的 `.relaxed/.release` | 8.1 / SM 90；FP8 为 8.6 且限列出的架构族 | 无 |
| `multimem.red` | 全局/泛型 multimem 目标和标量或寄存器向量源；`.relaxed/.release` 与 `.cta/.cluster/.gpu/.sys` 分别可省略 | 8.1 / SM 90 | 无 |
| `multimem.st.async` | 标量源，必须有 `.release.{gpu,sys}`，可选 `.global`；类型为 `b/u/s8–64`、`f32/f64` | 9.3 / SM 100 | 无 |
| `multimem.red.async` | 标量源，必须有 `.release.{gpu,sys}`，可选 `.global`，只支持 `.add.{u32,s32,u64}` | 9.3 / SM 100 | 无 |
| `multimem.cp.async.bulk` | 仅 `.global.shared::cta.bulk_group`；32 位字节数，可选 16 位 `.cp_mask` 操作数；默认 weak，显式 `.weak` 或 `.relaxed.scope...b128` | 9.1 / SM 90；mask 需 SM 100；显式排序需 9.3 及 SM 90a、SM 100f/110f 架构族 | Bulk group |
| `multimem.cp.reduce.async.bulk` | 相同方向和字节数合同；操作/类型规范表，包括半精度/bfloat add 必须带 `.noftz`；可选成对的 `.relaxed.scope` | 9.1 / SM 90；显式排序需 9.3 | Bulk group |

普通整数 reduction 表将 `add` 限于 `u32/u64/s32`，`min/max` 限于
`u32/u64/s32/s64`，`and/or/xor` 限于 `b32/b64`。浮点 `multimem.red`
只允许 `add`。浮点 `ld_reduce` 可用 `add/min/max`，但 `f32/f64` 只可与
`add` 组合；`.acc::f32` 适用于 half/bfloat，`.acc::f16` 适用于 FP8。
标量/v2/v4/v8 类型集合取手册操作、向量和累加精度表的交集，不做无限制笛卡尔积。
Bulk reduction 的 `add` 类型为 `u32/s32/u64/f32/f64/f16/bf16`，
`min/max` 类型为 `u32/s32/u64/s64/f16/bf16`，`inc/dec` 只接受 `u32`，
位操作只接受 `b32/b64`。

检查器复用通用类型化操作数、modifier 和引用遍历。对已知声明检查全局与共享地址空间、
标量或向量输入类型、字节地址对齐，以及绑定元数据一致性。整数/位类型的标量 store 和
reduction 输入接受寄存器或在使用处窄转换的整数字面量；`f32/f64` 标量输入接受现有的
类型化浮点字面量。对标量 FP8 `e4m3x4/e5m2x4` store，整数字面量提供经窄转换的
原始 32 位模式，`0f` 字面量直接复制原始 32 位模式；拒绝十进制浮点与 `0d` 字面量。
这个有界 packed 字面量兼容范围依据 CUDA 13.3 `ptxas` 探针，而非手册中明确的
multimem 字面量规则。标量 `f16x2/bf16x2` 和所有向量输入仍限寄存器，load 目标
也仍限寄存器。Bulk 目标与源均要求
16 字节对齐；立即数字节数须能被 16 整除。寄存器字节数仍是运行时义务。两个 release-async 族要求寄存器基址（可带立即数偏移）；
即使符号声明为 global，也不接受仅符号基址。
写出 `.cp_mask` 必须提供最后的 byte-mask 操作数。Release async store/reduce
**没有**具名完成机制；bulk copy/reduce 使用 `BulkGroup`，其 commit/wait
由已有 bulk-group 指令提供。前端不证明完成顺序、内存范围界限、运行时 multimem 映射、
跨设备可见性或实际 GPU 效果。

固定版手册有三处示例与 Syntax/合法表冲突。前端依规范表：拒绝
`multimem.red...max.f64`，因为浮点 `red` 只允许 `add`；也拒绝省略
`.shared::cta` 且多出 `[mbar]` 的 bulk reduction 示例，因为规定形式恰有
三个操作数并以 `bulk_group` 完成。手册中的 copy 示例
`multimem.cp.async.bulk.relaxed.cta.global.bulk_group.b128` 也漏掉必需的
`.shared::cta` 源空间限定符，因此被拒绝。对于 `multimem.red`，手册分别将省略的
semantics 默认为 `.relaxed`、省略的 scope 默认为 `.sys`；拥有的源码表示仍以没有
源码范围的 `Omitted`/`None` 类型值保留省略状态，因此只写一个限定符的源码也被前端接受。
另行记录的 `ptxas` 13.3.73 探针拒绝这类只写一个限定符的形式；
汇编器行为不覆盖此处的前端源码合同。

Canonical 清单和负例边界测试见
[`test_multimem_contract.py`](../../python/tests/spec/test_multimem_contract.py) 与
[`test_multimem_coverage.cpp`](../../submod/resolved_ir/test/test_multimem_coverage.cpp)。
[`multimem_consumer`](../../examples/multimem_consumer/main.cpp) 验证安装后的公共头。
这些测试检验前端接受性与源码生命周期，不在 GPU 上执行 multimem 指令。
