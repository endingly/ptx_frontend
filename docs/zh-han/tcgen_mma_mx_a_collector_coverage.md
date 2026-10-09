# MX 激活 collector

固定 [CUDA 13.3 PTX 9.3 TCGEN MMA 语法](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)允许在已有稠密及稀疏 `mxf8f6f4`、`mxf4`、`mxf4nvf4` 来源身份的可选或必需缩放 selector 后书写 `.collector::a::{fill|use|lastuse|discard}`。六个身份保留两种 A 放置方式、原有 scale-A/B 操作数、稀疏元数据位置、形状表、打包、selector 省略规则和目标交集。MX NV 仍必须书写 selector。这些 MX 形式均不接受 ashift、lane mask、D-scale、B collector 或权重驻留语法。

该限定符复用强类型 `TcgenCollectorControl` 缓冲区／操作对。全 Unspecified 值与空来源范围保留省略状态；显式 A/discard 则有独立值和来源范围。来源检查核对完整强类型对及其位置，`tcgen_mma_mx_a_collector_view` 只在既有精确稠密／稀疏 MX 布局视图成功后借用控制字段。`TcgenMxACollectorKnownFacts` 仅包含 collector 和可选的先前有效性断言；其查询复用 A 缓冲区历史规则，不开放 ashift 或 M 约束。未知历史保留为义务；无效断言与 use/lastuse 矛盾，有效断言也不能证明先前 fill 或操作顺序。完成前可能重新读取来源，因此 A/B 来源稳定性仍是义务。

既有独立 MX 操作查询继续条件性检查调用方已知的描述符字、形状、缩放 ID／布局、打包、元数据及目标条件。collector 的存在不会削弱或证明这些条件，也不推断实时描述符、collector 内容、分配历史或 GPU 执行。[已安装消费者](../../examples/tcgen_mma_mx_a_collector_consumer/)检验稀疏来源、AST 销毁后拥有型模块的借用视图和条件性历史查询。
