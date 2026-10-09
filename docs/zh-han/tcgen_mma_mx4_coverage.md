# 稠密 MX4 块缩放 TCGEN MMA

固定的 [CUDA 13.3 PTX 9.3 手册](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)规定两种稠密、非 WS 的 `.kind::mxf4.block_scale` 源码布局：共享内存 A 或 Tensor Memory A。两者均使用 Tensor Memory D、共享内存 B、32 位指令描述符寄存器、Tensor Memory 缩放 A/B 地址和谓词源。规范形式没有 lane mask 或 D-scale。生成的精确类 `Tcgen05MmaMxf4` 持有强类型 selector 及源码位置；`tcgen_mma_mx4_view` 只借用该类的角色。AST 销毁后，模块检查仍重验这些角色。

selector 分别保留 `Absent`、`Vec2X`、`Block32`。此 kind 省略时默认 block32，但源码仍记为 `Absent` 并使用基础 MMA 目标门槛。显式 `.scale_vec::2X` 要求精确 `sm_100a`；显式 `.block32` 要求 PTX 8.8 且启用 `sm_100f` family feature，或 PTX 9.0 且启用 `sm_110f` family feature。源码合法的指令仍带有不透明的实时 `idesc`，不能仅凭拼写证明 K。

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)规定稠密 K64：group 1 M128，group 2 M128/256；group 2 M256 还可在精确 `sm_103a` 上使用 K96。group 1 的 N 为 8..256、步长 8；group 2 为 16..256、步长 16。D 为 F32，A/B 为 E2M1，缩放类型为 UE8M0。[Table 47](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)独占指令字编码，包括 K 选择位及 A/B 缩放 ID 0 或 2。已知指令字查询独立于源码寄存器检查这些值。特别是已知 K96 加显式 Vec2X 不存在同时满足 K96 精确 `sm_103a` 与 Vec2X 精确 `sm_100a` 的目标，因此报告目标冲突；`sm_100a` 上的 Vec2X 源码在 K 未知时仍合法。

[Tables 59/60](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)给 Vec2X 及 K64 block32 分配两个缩放因子。K96 block32 有独立的三因子 `Mx3`/`3xN` 行，要求 4 字节对齐；K64 行为 `Mx2`/`2xN`，要求 2 字节对齐。调用方布局与对齐断言只是条件性事实；实时缩放放置、内容及向 Tensor Memory 全部 32 个 lane partition 复制仍未证明。查询在行策略允许时接受更强的二次幂对齐保证。K96 block32 继续执行 Table 47 的编码 ID 0/2。后续 K96 布局图也画出 ID 1/3；这里记录手册内部冲突，不扩宽已定义指令字的域。Table 59 将 Vec2X 列为适用于所有支持的 K，但实体布局图只覆盖 K64/K128；假设性的已知 K96 Vec2X 查询即使收到调用方事实，也保留明确的布局规则义务。

手册规定 Tensor Memory A 与共享 A/B 的两个 4 位元素装入一个 8 位容器，共享内存形式无填充。查询暴露强类型的必需打包格式并核对调用方断言，同时保留实时内容义务。[已安装消费者](../../examples/tcgen_mma_mx4_consumer/)检验公开源码及已知值契约。独立的[稠密 MX NV 形式](tcgen_mma_mxnv_coverage.md)也已覆盖；稀疏 MX4 有独立覆盖，[MX A collector](tcgen_mma_mx_a_collector_coverage.md)现已支持。ashift、WS、运行时分配历史及 GPU 结果不在此来源契约内。
