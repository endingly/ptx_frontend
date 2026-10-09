# TCGEN 普通稀疏 MMA 覆盖范围

固定 [CUDA 13.3 PTX 9.3 手册](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)定义非 WS 的 f16、tf32、f8f6f4 和 i8 `.mma.sp.cta_group.kind` 来源形式。每种形式都要求在 B 之后、`idesc` 之前放置带方括号的 Tensor Memory 元数据地址。A 可在 shared 或 Tensor Memory；支持 CTA group 1 和 2。f16/tf32 保留普通形式的可选 lane mask 和 D-scale；f8f6f4/i8 允许 mask 而禁止 D-scale。group 1 的 mask 含四个 32 位标量寄存器，group 2 含八个。`Tcgen05MmaSp*` final 类拥有这些角色，`tcgen_mma_sparse_view` 仅在具体类与 layout 匹配时借用。模块检查在语法树销毁后仍检查绑定与来源位置。

生成的 [Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) 稀疏行对 f16/tf32/f8f6f4/i8 分别使用逻辑 K 32/16/64/64。压缩 A 的 K 是一半，B 保持逻辑 K。独立的调用方已知值查询检查形状、类型、目标、PTX 版本，以及来源 kind 与调用方提供的指令字及其 sparse 位是否一致。唯一的 [Table 45](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) 描述符目录负责编码字段和 sparsity selector。f16/tf32 的已知 selector 可为 0–3；f8f6f4/i8 要求零。查询不读取实时 `idesc` 寄存器。

固定元数据编码对 tf32 是 1:2（nibble `E` 或 `4`），其余三种是 2:4（`4,8,C,9,D,6,E`）。查询逐个检查调用方给出的 nibble；实时元数据及压缩 A 内容仍是义务。F（group 1，M64）和 C（group 2，M128）半 lane 路径允许 0 或 16，并比较所有适用且独立已知的 Tensor Memory A、D 和元数据标识。D（group 1，M128）和 A（group 2，M256）完整路径要求每个适用的已知标识为零。shared A 仅免除 A 的比较，已知 D 和元数据仍须满足路径规则。这些数值表示半 lane，而不是字节地址对齐。缺少的事实保留为明确义务，不被假定一致。f8f6f4 的 E2M1/E2M3/E3M2 输入另保留低位 packing 规则义务，因为源码和已知字都不能证明实时布局内容。

来源形式与已知值报告不证明运行时 Tensor Memory 分配、稀疏内容、描述符真实性、同步或 GPU 行为。稀疏 MX、collector 和 WS 各有独立契约。[安装后使用示例](../../examples/tcgen_mma_sparse_consumer/) 演示公开的稀疏来源、借用元数据和条件性查询 API。
