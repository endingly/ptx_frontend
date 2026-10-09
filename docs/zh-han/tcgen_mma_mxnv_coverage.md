# 稠密 MX NV 四位块缩放 TCGEN MMA

固定的 [CUDA 13.3 PTX 9.3 手册](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)给出两种稠密非 WS `.kind::mxf4nvf4.block_scale` 来源形态：A 位于共享内存或 Tensor Memory。两种形态均使用 Tensor Memory D、共享内存 B、不透明的指令描述符寄存器、Tensor Memory scale A/B 地址及 input-D 谓词。规范形态没有 lane mask 或 D-scale。`Tcgen05MmaMxf4nvf4` 拥有书写的 selector 与来源位置；`tcgen_mma_mxnv_view` 只从该精确类借用角色。语法树销毁后，owned module 验证仍重新检查这些信息。

缩放 selector **必须显式书写**：`.scale_vec::2X`、`.scale_vec::4X`、`.block32` 或 `.block16`；不存在省略时的默认值。显式向量 selector 要求 PTX 8.7 与精确 `sm_100a`；block selector 要求 PTX 8.8 且启用 `sm_100f` family feature，或 PTX 9.0 且启用 `sm_110f` family feature。来源合法并不认证不透明实时 `idesc` 的 K 或缩放内容。

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)允许 group 1 M128 和 group 2 M128/256 使用稠密 K64；group 2 M256 还可在 PTX 8.8 起、精确 `sm_103a` 上使用 K96。group 1 的 N 为 8–256、步长 8；group 2 为 16–256、步长 16。D 为 F32，A/B 为 E2M1。唯一的 [Table 47](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 描述符目录检查已知字的 K 选择、UE8M0/UE4M3 缩放类型以及 0 或 2 的缩放 ID。Vec2X/block32 要求 UE8M0；Vec4X/block16 允许 UE8M0 或 UE4M3。已知 K96 与任一向量 selector 的精确 `sm_100a` 门不存在目标交集，因此已知值查询报告目标违例；当 K 未知时，`sm_100a` 上的来源仍合法。

[Tables 59/60](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)规定 K64 Vec2X/block32 有两个因子、选中子列需 2 字节对齐；K64 Vec4X/block16 有四个因子、需 4 字节对齐。K96 block32 有三个因子，K96 block16 有六个；两者均需 4 字节对齐且采用独立的布局定义放置策略。调用方提供更强的正数 2 的幂对齐保证时，查询按相应 row 接受。K96 block 仍强制 Table 47 的 ID 域 0/2；后续 K96 布局图也画出 ID 1/3，这是手册内部冲突，前端不扩大 defined-word 域。Table 59 给出 K96 Vec2X/Vec4X 的因子数，但给定图示未建立其物理布局规则；即使提供调用方布局事实，查询仍保留布局规则义务以及目标违例。

手册规定 Tensor Memory A 和共享内存 A/B 把两个四位元素装入一个八位容器，共享内存形式没有填充。查询检查强类型 packing 规则与调用方断言，仍不证明实时内容、选中的缩放放置、全部 32 个 lane partition 上的复制、分配历史或 GPU 结果。[已安装消费者](../../examples/tcgen_mma_mxnv_consumer/)覆盖公开来源与已知值契约。[稀疏 MX NV](tcgen_mma_sparse_mx_coverage.md)、collector、ashift 和 WS 各有独立契约。
