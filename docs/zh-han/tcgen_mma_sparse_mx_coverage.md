# TCGEN 稀疏块缩放 MMA 覆盖范围

固定 [CUDA 13.3 PTX 9.3 手册](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)给出三种非 WS 稀疏块缩放来源 kind：`mxf8f6f4`、`mxf4` 和 `mxf4nvf4`。每个 `tcgen05.mma.sp` 形式均有两种 A 放置方式；方括号内的 Tensor Memory 元数据地址位于 B 之后、`idesc` 之前，Tensor Memory scale-A/B 地址位于 `idesc` 之后。这些形式没有 lane mask、D-scale、collector、ashift 或 WS 后缀。具体 final 类拥有强类型操作数及书写的 selector 来源位置。`tcgen_mma_sparse_mx_view` 仅在具体类和 layout 匹配时借用元数据与缩放角色；语法树销毁后，完整模块检查仍验证来源和绑定。

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)规定稀疏 MX8 的逻辑 K64、稀疏 MX4/MX NV 的逻辑 K128。group 1 使用 M128、N8–256 且步长为八；group 2 **只使用 M256**、N16–256 且步长为十六。稠密 group-2 M128 和稠密 K96 都不是稀疏行。调用方已知 MX4/MX NV 字的 Table 47 K-choice 位必须选稀疏兼容的 K128 状态。唯一的描述符目录负责包括 sparse 位在内的编码字段；独立查询比较已知 kind 和 sparse 位与来源身份。两者均不读取实时 `idesc` 内容。

稀疏 MX8 可省略 selector（有效含义为 `scale_vec::1X`），MX4 可省略（有效含义为 `block32`），MX NV 则必须书写 selector。省略来源与显式后缀保持区别。稀疏 MX8 有一个缩放因子、要求一字节 sub-column 对齐，合法 ID 为 0–3。稀疏 MX4 的 `scale_vec::2X`/`block32` 有两个因子、两字节对齐，ID 为 0/2。稀疏 MX NV 的 `scale_vec::2X`/`block32` 有两个 UE8M0 因子，ID 为 0/2；`scale_vec::4X`/`block16` 有四个 UE8M0 或 UE4M3 因子、四字节对齐，ID 为 0。生成的 [Tables 59/60](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) 行是显式数据，因子数并非以 K 除块长推断。调用方已知对齐保证必须是满足该行要求的正二次幂，字节槽 ID 还须与保证一致。实时选中的缩放放置和数值仍是义务。

稀疏 MX4/MX NV 的基础目标身份只能是固定 PTX 下的精确 `sm_100a`、`sm_110a` 或 `sm_103a`；显式向量 selector 另要求精确 `sm_100a`，显式块 selector 则要求 `sm_100f`/`sm_110f` 家族特性和 PTX 8.8/9.0。稀疏 MX NV kind 在 `sm_100a` 上始于 PTX 8.7。来源及已知操作查询分别求这些门槛的交集，不把基础门槛扩大成整个家族。A/B 的低位 packing 规则沿用相应稠密 kind，作为条件性的调用方已知值检查。

稀疏 MX8 元数据采用 2:4 分组；稀疏 MX4/MX NV 采用 **pairwise 4:8**：每个双元素 chunk 要么全零，要么全非零。它们都使用有意义的 nibble 码 `4,8,C,9,D,6,E`，相同的码不表示分组规则相同。可以检查已知索引和完整路径 A/D/元数据 lane 零标识；实时元数据、压缩 A、缩放内容、分配与同步仍未证明。[安装后使用示例](../../examples/tcgen_mma_sparse_mx_consumer/)演示公开来源和生成的条件性查询。
