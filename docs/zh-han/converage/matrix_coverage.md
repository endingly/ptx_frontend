# 矩阵片段覆盖

### 命名矩阵片段

符合条件的 MMA 片段、WGMMA 目标及寄存器 A 片段、WMMA MMA 的 A/B/C
片段，在具体角色恰好包含两个或四个物理寄存器时，接受普通 `.reg.v2` 或
`.reg.v4` 声明的整体标识符。Element 策略逐项检查物理 lane 类型；逻辑矩阵元素
类型与既有 cardinality 保持独立。整个声明不得超过 128 位。这不增加命名
v1/v8/v128 语法，也不使 `.v4.f64` 声明合法。

拥有所有权的 IR 保留整体声明 SymbolId、按顺序排列的 NamedProjection 及其
源码范围。释放源码与 AST、复制、赋值或 clone 后，当前状态验证仍检查这些
事实；permission 标志不能代替 provenance。既有合法的大型花括号片段继续支持。

当 WGMMA 寄存器 A 与共享描述符形式具有相同的标识符语法时，私有 resolver
根据实际词法声明形状及配对的类型化 descriptor 选择：整体向量进入已授权片段
路径，标量寄存器进入描述符路径。错误的向量数量或类型仍然报错；未知声明
不能证明其为标量。公共 syntax-only selector 继续保持无上下文契约，可以报告
歧义。选择不依赖目标可用性或试探性解析。WMMA load/store 操作数及 WMMA MMA 目标、
地址与描述符、scale ID、稀疏 metadata 等排除角色保持既有花括号、标量或元组
规则。GPU 调度与运行时资源有效性仍不属于前端验证范围。

规范 loader 仅在每一对 layout 组合均能证明分离时接受声明域可分的重叠。
新增证明要求交集仅为 IdentifierRef、显式 named Element permission、既有精确
二或四 lane 数量，以及标量 SharedMatrixDescriptor 竞争项。事实缺失、Aggregate
策略、通用标量角色或其他共有语法均不能提供该证明。目标与默认值不能消除
重叠；此验证证明保持公共 selector 在指令范围上报告的精确歧义诊断。
