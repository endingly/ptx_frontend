# 非 WS 激活收集器与 ashift

固定 [CUDA 13.3 PTX 9.3 TCGEN MMA 语法](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)允许普通非 WS 稠密及稀疏 f16、tf32、f8f6f4、i8 形式在 kind 后依次书写 `.ashift` 和 `.collector::a::{fill|use|lastuse|discard}`。这两个可选修饰符扩展现有八种来源身份，继承原来的 A 放置、mask、D-scale、元数据、形状和目标规则；不接受 collector 位于 ashift 前的别名。共享 A 可以使用 A collector，但不能使用 ashift。ashift 要求 Tensor Memory A，可单独出现或与 lastuse/discard 配对，不能与 fill/use 配对。

来源修饰符解析为 `TcgenCollectorControl`，分别保存强类型 buffer 和 operation。双 Unspecified 值与空位置代表省略；显式 `.collector::a::discard` 保留不同的值和来源范围。非 WS 操作语义可从省略状态导出有效 A/discard，但不改写拥有的来源。通用修饰符检查比较整个强类型值，所选来源检查拒绝不完整的字段组合、非法枚举值、错误 buffer、缺失或多余位置及非法 ashift 配对。`tcgen_mma_a_collector_view` 仅从匹配的具体 final 类借用控制信息；语法树销毁后完整模块验证仍可工作。

独立提供的 M 可以检查 ashift 形状规则：M128 或 M256 合法，M64 矛盾，未知 M 保留义务。调用方声称 A collector 无效时，use/lastuse 与之矛盾；未知有效性仍为缺失事实。即使声称有效，也不能证明此前 fill 或指令顺序。collector 操作可能重读共享内存或 Tensor Memory 来源，因此 A/B 来源在完成前必须保持稳定。前端不推断实时描述符、collector 内容、分配、同步历史或 GPU 执行。[使用示例](../../examples/tcgen_mma_a_collector_consumer/)覆盖公开来源及条件性查询合同。
