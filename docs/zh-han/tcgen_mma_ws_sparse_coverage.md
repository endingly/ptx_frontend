# 稀疏 weight-stationary TCGEN MMA

固定的 [CUDA 13.3 PTX 9.3 TCGEN MMA 规范](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) 定义 f16、tf32、f8f6f4、i8 的 `.mma.ws.sp.cta_group::1.kind`。四个精确来源类各保留 shared 或 Tensor Memory A 以及可选末尾 64 位标量零列寄存器：`D, A, B, [metadata], idesc, enableD[, zeroColumnDesc]`。带括号的 Tensor Memory 元数据地址必须位于 B 和 `idesc` 之间。B0–B3 collector 有四种书写操作或规范省略；省略仅在操作报告中推导 B0/discard。这里不接受 `.mma.sp.ws`、CTA group 2、A collector、ashift、block scale、lane mask 或 D-scale。[安装消费示例](../../examples/tcgen_mma_ws_sparse_consumer/)检查语法树销毁后的持有来源及条件性报告。

Table 42 行的 M 为 32/64/128，N 为 64/128；f16/tf32/f8f6f4/i8 的逻辑 K 分别为 32/16/64/64。稀疏 A 保存 K/2 个元素，B 保留逻辑 K。共用 WS 路径数据选择 M32/G1×4、M64/E2×2 或 M128/D4×1，并要求独立已知的 A（处于 Tensor Memory 时）、D 和元数据半 lane 均为 0。M32 元数据子列映射没有已确立的固定图，故即使调用方提供半 lane 断言，报告仍保留 `MetadataLayoutRule` 义务；规范 M32 来源不会因此被拒绝。

复用现有 Table 45 稀疏 selector 及逐 kind 元数据 nibble 域。tf32 的 1:2 元数据索引为 E/4；f16、f8f6f4、i8 的 2:4 索引为 4/8/C/9/D/6/E。调用方提供的 nibble 可与该域矛盾，但不能由此证明运行时元数据内容或 50% 稀疏性。稠密 WS 的 Table 43/48 shared 与零列检查、低位 packing 义务、逐 kind 目标目录和逐 B buffer 条件历史规则继续适用。先前有效性 false 与 use/lastuse 矛盾，true 也不能证明先前 fill。完成前 A/B 来源仍可能被重读。描述符真实性、Tensor Memory 分配、collector 顺序、同步和 GPU 执行仍是运行时义务。[稠密 WS](tcgen_mma_ws_dense_coverage.md)具有同样的 B/零列来源控制，但没有稀疏元数据。
