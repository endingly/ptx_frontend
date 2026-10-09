# TCGEN MX8 稠密 MMA 覆盖范围

本页覆盖固定 [CUDA 13.3 / PTX 9.3 §9.7.17.10.9.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) 中非 WS 的稠密 `.kind::mxf8f6f4.block_scale` 源码形式。规范语句为 `tcgen05.mma.cta_group::{1|2}.kind::mxf8f6f4.block_scale{.scale_vec::1X|.block32} [d], a, b-desc, idesc, [scale-A], [scale-B], enable-input-d`。A 可为 64 位标量共享描述符寄存器或带括号的 Tensor Memory 地址；D、scale A、scale B 均为带括号的 Tensor Memory 地址，指令描述符为 32 位标量寄存器。块缩放语法只有 A 来源对应的**两种**布局，不含输出通道掩码或 D 缩放操作数。谓词可为标量谓词寄存器（含书写取反）或整数真值常量。

Tensor Memory 地址使用 General 类标量 32 位通道/列载体；前端接受 B32/U32/S32 声明，拒绝 F32、谓词、向量及 64 位声明。限定范围的 CUDA 13.3 `ptxas` 角色宽度探针在 D/A/scale-A/scale-B 上拒绝 U32/S32，但固定 §9.7.17.1.1 只规定地址宽度为 32 位，并未要求声明必须恰为 `.b32`。此工具差异不改变前端既有的宽度兼容契约，也不能证明运行时地址值。

selector 是携带源码位置的强类型值，分别保留 `Absent`、`Vec1X`、`Vec2X`、`Vec4X`、`Block16`、`Block32` 身份。当前 MX8 仅允许显式 `.scale_vec::1X` 与 `.block32`。省略仍在持有 IR 中记为 `Absent`；[Tables 59/60](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 对 MX8 唯一确定其有效缩放布局与 1X 相同。查询派生有效布局，但不改写源码来源，也不提前合并以后随 K 改变的块格式。

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 的 MX8 行规定 K32、D F32。group 1 为 M128、N8..256 且步长 8；group 2 为 M128/256、N16..256 且步长 16。A、B 各自可选 E4M3、E5M2、E2M3、E3M2、E2M1，缩放因子类型为 UE8M0。[Table 46](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 定义指令字中的 A/B 缩放数据 ID 和缩放类型，既有描述符目录独占这些编码字段。生成的操作规则分别给出强类型 A `Mx1` 与 B `1xN` 布局：每逻辑块一个因子、子列要求至少按 1 字节对齐、ID 范围 0..3。ID 选择 Tensor Memory 字内部的子列，**不是**加到缩放基地址上的字节偏移。调用方提供的布局及对齐保证按 A/B 角色分别检查；与所选 ID 一致的更强二次幂对齐保证可以通过。未知事实保持 Missing。

既有 Table 43 定义字段及 Table 54/55/57 的共享 A/B 规则按角色适用。8 位 B 转置时，group 1/2 的 N 步长分别为 16/32。§9.7.17.10.4.3–.4 已规定 4/6 位打包：Tensor Memory A 的每个元素占 8 位容器，共享 A/B 使用带填充的 4 位或 6 位容器。查询暴露强类型的必需打包格式并拒绝矛盾的调用方事实，同时保留实时内容尚未验证的义务。固定手册没有给出可直接完整验证的 4/6 位转置共享打包行，查询另保留该义务。实时 `idesc`、A/B 描述符及缩放内容均为不透明的寄存器或内存值；调用方已知字只是条件性断言，不能证明实时值、分配历史、排序或 GPU 结果。

基础源码形式在当前目标目录中采用 PTX 8.6 起的精确 `sm_100a`、8.8 起启用 `sm_100f` family、9.0 起精确 `sm_110a` 及启用 `sm_110f` family 分支。显式 `.scale_vec::1X` 要求精确 `sm_100a`，`.block32` 要求 PTX 8.8 起的 family feature 分支；省略 selector 使用基础门槛。CUDA 13.3 `ptxas` 还接受 `sm_110a` 上显式 1X，但固定 MMA 目标说明及 Table 63 给出更窄的显式 selector 要求，前端遵循该要求。源码解析和调用方已知目标查询使用同一门槛。

生成的精确类 `Tcgen05MmaMxf8f6f4` 持有 selector、缩放地址及源码范围；`tcgen_mma_mx8_view` 仅借用匹配的选中布局。AST 销毁后，模块检查仍重验操作数类型、绑定及 selector/缩放源码位置。[已安装消费者](../../examples/tcgen_mma_mx8_consumer/)检验这些源码与已知值 API。稀疏及其他 MX kind 有独立覆盖；[MX A collector](tcgen_mma_mx_a_collector_coverage.md)现已支持。ashift、WS、物理低位打包证明及执行不在此来源契约内。
