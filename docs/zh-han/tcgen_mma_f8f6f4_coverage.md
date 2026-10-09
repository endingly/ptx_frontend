# TCGEN 密集无缩放 f8f6f4 MMA 覆盖范围

本文规定 `.kind::f8f6f4` 密集、非 WS、非稀疏、非卷积前端切片。
规范依据是固定版本的
[CUDA 13.3 PTX 9.3 `tcgen05.mma` 章节](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)。
已接受的表 43/45 描述符目录独占字段编码规则；已知操作查询不读取活描述符寄存器。

规范源码顺序为
`tcgen05.mma.cta_group::{1,2}.kind::f8f6f4 [d-tmem], a, b-desc,
idesc, {disable-output-lane}, enable-input-d`。A 可以是共享描述符寄存器或带方括号的
Tensor Memory 地址。可选掩码按组 1/2 分别含四/八个 32 位位寄存器。
四种 A 位置／掩码布局乘以两个带源码位置的组值，形成八种类型化源码拓扑。
普通形式没有源码 `scale-input-d`、`.satfinite` 或块缩放操作数。
前端接受印出的规范 modifier 顺序；相邻的 `.mma.kind::f8f6f4.cta_group` 拼写被排除。

A/B 共享描述符必须为标量 General B64/U64/S64 寄存器，`idesc` 必须为标量
General B32/U32/S32 寄存器，字面值零不是源码描述符角色。谓词保留普通寄存器身份和
写出的否定，或保留整数常量的零／非零真值。F32 声明也可作掩码位载体。
掩码缺席、显式空向量、长度错误及类型错误彼此有别。持有式结果必须在语法树销毁后
仍可检查源码范围、声明和绑定。

表 42 规定密集 K32、D 为 F16/F32，A/B 各自可取 E4M3、E5M2、E2M3、E3M2、
E2M1；配对域允许全部 25 种已定义组合。组 1 的 M 为 64/128，N 为 8..256、步长 8；
组 2 的 M 为 128/256，N 为 16..256、步长 16。四个封闭行覆盖两个组和两种 D 类型。
K 是隐含值，源码没有额外 K 操作数。F/D/B/A 数据路径分别对应
（组，M）=（1,64）／（1,128）／（2,128）／（2,256）。F 半路径只能根据调用者
独立提供的 A/D 0/16 通道半区事实判断对齐。

表 45 中 D 编码 0/1 分别为 F16/F32；A/B 各自的编码 0/1/3/4/5 分别为
E4M3/E5M2/E2M3/E3M2/E2M1。饱和位 3 必须为零，转置和取负位受到支持。
稀疏位虽为已定义字段，但对本密集操作是独立违例。查询分别报告字段错误、操作违例
和缺失事实。调用者给出的已知字是独立声明，不能认证不透明的源码寄存器。

每个共享内存角色仍须独立检查表 43 已定义字段与上下文；非转置 A/B 使用已接受的
major/swizzle 规则。E4M3/E5M2 为 8 位，E2M3/E3M2 为 6 位，E2M1 为 4 位。
表 54 允许转置，但表 57 没有 4/6 位转置行，所以转置的低位共享 A 或 B 须分别
报告 `ATransposeLayoutRule` / `BTransposeLayoutRule` **Missing**。
通用的 128 字节、32 字节原子粒度 swizzle 禁止规则仍是 A/B 硬违例。
8 位转置共享角色采用已确立的逐角色 major/swizzle 规则。表 55 的 B 转置 N 限制
**仅在 B 为 8 位时**适用：组 1 为 N16..256、步长 16，组 2 为 N32..256、步长 32；
它不取决于 A 的位宽。Tensor Memory A 无共享 A 字／表 57 义务，但仍有位置和路径事实。

调用者已知的每个 4/6 位 A/B 角色还分别携带
`ALowBitPackingRule` / `BLowBitPackingRule` **Missing**，即使 A 在 Tensor Memory
或该角色未转置。手册低位 packing 图明确标为 **mxf8f6f4**，本无缩放切片不复制其
填充或格式规则。D 为 F16 时，每个 32 位 Tensor Memory 字的低 16 位存储一个元素；
但已知查询不能检查真实矩阵字节、分配、完成或 GPU 数值行为。已提供事实没有违例时，
仍可能存在未决义务。

当前目标目录使用四个普通无缩放分支：PTX 8.6 起的精确 `sm_100a`、8.8 起启用的
`sm_100f` 家族、9.0 起的精确 `sm_110a` 和启用的 `sm_110f` 家族。
目标判断基于显式身份与 feature 交集，不按 SM 数值大小推断。手册中的历史
`sm_101a`／`sm_101f` 不在当前目录中，不能暗中映射到 `sm_110`。
i8 的精确目标排除规则以及 f16/tf32 的 D 缩放门槛不适用于本切片。

这是有限的源码与已知事实表示。缩放 MX kind、稀疏和 WS 形式、
卷积、物理低位 packing 证明、活描述符位、Tensor Memory 生命周期及 GPU 执行均在
范围之外。TMA 与 MMA 可以共存于同一个模块，但本切片不证明执行或跨指令完成。

当前普通非 WS 稠密与稀疏形式还支持[强类型 A collector 与 ashift 控制](tcgen_mma_a_collector_coverage.md)；调用方已知历史检查仍是条件性的。
