# TCGEN 稠密 tf32 MMA 覆盖范围

本页说明稠密、非权重驻留、非卷积 `.kind::tf32` 源码形式及调用方已知值查询。
规范依据是固定的 [PTX 9.3 `tcgen05.mma` 条款](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)、
已接受的描述符目录。有限的 CUDA 13.3 完整模块汇编检查
佐证了源码拼写；汇编本身不能证明 GPU 行为或运行时描述符寄存器中的值。

规范拼写是 `tcgen05.mma.cta_group::1/2.kind::tf32`。两个 group
都可选择共享描述符或 Tensor Memory A、可选输出通道掩码及可选 D 缩放。
这构成八种结构性操作数布局和两个有类型的 group 值，共十六种源码拓扑。
检查点在 `sm_100a`/PTX9.3 上接受了全部十六种。确切的相邻书写顺序别名
`.mma.kind::tf32.cta_group::1/2` 也已获准映射到同一个有类型的选中形式。
group 1 别名有汇编实测；把同一相邻交换应用到 group 2 是 Authority 的语法推断，
不是额外的汇编结果。其他排列及重复修饰符仍无效。

A/B 共享描述符使用 General 标量 B64/U64/S64 **寄存器**，指令描述符使用
General 标量 B32/U32/S32 寄存器。汇编器在三个描述符位置均接受字面量零，
但固定源码仍把它们称为寄存器操作数；工具接受不扩大前端角色。
`enable-input-d` 可为允许书写取反的标量谓词寄存器，也可为按零/非零取真值的
整数常量，包括 2；特殊谓词寄存器仍不在范围内。掩码可以省略；出现时 group 1
恰有四个、group 2 恰有八个 General 标量 32 位寄存器项。
B32/U32/S32/F32 声明按**掩码比特**兼容；这里的 F32 不要求浮点转换。
显式空向量无效。可选 D 缩放立即数的**原始**求值整数须在 0..15，
更宽整数的低位别名不在此范围内。带源码位置的持有负载须在语法树释放后继续
保存并重新检查这些角色。

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
确定 D=F32、A=B=TF32。稠密 group 1 的 M=64/128，N 从 8 到 256、步长 8；
group 2 的 M=128/256，N 从 16 到 256、步长 16。K=8 唯一确定，
不是独立的源码操作数。已接受的 Table 45 描述符目录独占 D/A/B 类型编码字段。
操作查询先调用目录的已定义字段检查，再分别报告完成的已知事实检查、
已证明的违规以及仍缺失的事实。运行时源码寄存器保持不透明；调用方提供的描述符
字值是独立声明，不能认证这些寄存器。

对于调用方已知的共享描述符字值，[Table 57](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
要求转置的 32 位 A 或 B 矩阵使用**32 字节原子单位的 128 字节 swizzle**。
A、B 两种角色分别检查；普通 K-major 共享操作数使用已接受的非转置 swizzle
域。A 在 Tensor Memory 时没有 A 共享字值义务。非权重驻留的 F/D/B/A 路径由
group/M 决定，依次为 (1,64)、(1,128)、(2,128)、(2,256)。F 半通道路径上，
若 A 在 Tensor Memory，且调用方独立提供 A/D 的通道半部身份，查询可比较二者。
地址拼写本身不能推导这种身份或证明分配历史。

源码和目标检查点支持 PTX8.6 起的精确 `sm_100a`、PTX8.8 起包含
`sm_103a/f` 的 `sm_100f` 系列，以及 PTX9.0 起包含 `sm_110a/f` 的
`sm_110f` 系列上的普通 tf32。可选 D 缩放只采用更窄的精确 100a/100f
系列目标门槛；`sm_110a/f` 拒绝了缩放形式。通用 `sm_100`、`sm_110` 和
`sm_120a/f` 不支持此形式。低于版本门槛且无 MMA 的 `.target` 对照模块也失败的
汇编案例受指令前置条件混淆，不能独立证明 MMA 的版本下限。

本切片之外仍有其他稠密 kind、稀疏及权重驻留拼写、块缩放、卷积，以及运行时描述符和内存有效性。tf32 的选中视图从精确
`Tcgen05MmaTf32` 形式借用负载，并保留现有 f16 视图与生成身份。模块 CTA group
一致性检查仍只适用于 TCGEN 指令。TMA 的省略/group-1/group-2 形式可在同一
函数体共存，不应继承 TCGEN 专属的一致性规则。

当前普通非 WS 稠密与稀疏形式还支持[强类型 A collector 与 ashift 控制](tcgen_mma_a_collector_coverage.md)；调用方已知历史检查仍是条件性的。
