# 稠密 weight-stationary TCGEN MMA

固定的 [CUDA 13.3 PTX 9.3 TCGEN MMA 规范](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) 定义 f16、tf32、f8f6f4、i8 四种稠密 `.mma.ws.cta_group::1.kind` 来源。前端分别保留为精确的最终类。每种有 shared 或 Tensor Memory A，及可选的末尾 64 位标量寄存器零列描述符：`D, A, B, idesc, enableD[, zeroColumnDesc]`。可选的 B0–B3 `fill`、`use`、`lastuse`、`discard` collector 位于 kind 之后。该形式不接受 CTA group 2、A collector、block scale、ashift、lane mask 或 D-scale。写出的 collector 是带来源范围的强类型 buffer/action 对；省略时保存全 Unspecified 值和空范围，仅在操作解释时推导有效 B0/discard。[安装消费示例](../../examples/tcgen_mma_ws_dense_consumer/)在语法树销毁后检查公开来源、借用的 `tcgen_mma_ws_view` 和已知值查询。

Table 42 的已知形状行为 M32、M64 或 M128，N64、N128 或 N256；K 对 f16 为 16，对 tf32 为 8，对 f8f6f4/i8 为 32。所选路径为 M32/G1×4、M64/E2×2、M128/D4×1，半 lane 仅允许 0。shared 描述符的 major/swizzle 与低位转置约束仍独立按角色检查；适用的 f8 输入低位 packing 继续作为显式义务。仅当来源写出可选寄存器时才检查调用方独立提供的 Table 48 零列字；此时未提供字属于缺失事实。该字或寄存器来源都不能认证运行时实际位值。

查询分别保存各 B buffer 的有效性断言。false 与 `use`/`lastuse` 矛盾；未知仍为缺失义务；true 也不能证明先前 fill 或顺序。collector 可能重读 A/B，因此完成前来源稳定性仍是义务。来源接受或调用方已知字均不证明描述符运行时值、Tensor Memory 分配、collector 内容、指令顺序、同步或 GPU 执行。[稀疏 WS](tcgen_mma_ws_sparse_coverage.md)另加入强制元数据来源与条件稀疏规则。
