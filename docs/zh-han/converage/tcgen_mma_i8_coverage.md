# TCGEN 密集 i8 MMA 覆盖范围

本文说明 `.kind::i8` 密集、非权重驻留、非卷积前端切片。打包语法、持有形式、操作目录与借用视图均不推断活描述符寄存器的内容。
规范依据是固定版本的
[CUDA 13.3 PTX 9.3 `tcgen05.mma` 章节](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)；
已接受的表 43/45 描述符目录仍是编码规则的唯一权威。

规范印出的顺序为
`tcgen05.mma.cta_group::{1,2}.kind::i8 [d-tmem], a, b-desc, idesc,
{disable-output-lane}, enable-input-d`。`a` 可以是共享内存描述符寄存器，也可以是带方括号的 Tensor Memory 地址。
可选掩码在组 1 含四个寄存器，在组 2 含八个寄存器。四种结构操作数布局乘以两个带类型的组值，共八种源码拓扑。
i8 语法**没有** `scale-input-d` 操作数，也没有 `.satfinite` 源码限定符。
CUDA 13.3 汇编器的独立四模块检查在两个组上均接受规范顺序及相邻的 `.mma.kind::i8.cta_group` 顺序。
Authority 已批准恰好这个相邻别名，它选择同一个带类型的指令形式并保留写出的组位置。
其他顺序和重复限定符仍无效。两种获准顺序选择同一个类型化持有形式。

共享 A/B 描述符必须是标量 General B64/U64/S64 **寄存器**，`idesc` 必须是标量 General B32/U32/S32 寄存器。
活寄存器的内容保持不透明。`enable-input-d` 可为普通标量谓词寄存器（可保留写出的否定），
也可为按零/非零解释的整数谓词常量；特殊谓词寄存器不在此切片中。
可选输出通道掩码恰有四/八个标量 General 32 位寄存器。B32/U32/S32/F32 声明均可作为**位载体**，
F32 不表示浮点运算。掩码缺席与显式空向量不同。拥有式解析结果必须在语法树销毁后保留并检查源码位置、声明及寄存器身份。

[表 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
规定乘数为 S8/U8，累加及输出为 S32，密集 K 唯一确定为 32。组 1 的 M 为 64/128，N 为 8、16、24、32，
之后从 48 到 256 每次增加 16；组 2 的 M 为 128/256，N 从 32 到 256 每次增加 32。K 不是单独的源码操作数。
表 45 分别编码 A/B 的有符号性，但固定版本的 TCGEN 文字未明确裁定混合 S8×U8 或 U8×S8 配对。
调用者已知字查询必须把该配对规则列为**未决义务**，不得把它当作拒绝或已经完成的验证。
同类型配对可以完成该项检查。WGMMA 的同类型条款不能移植到 TCGEN。

表 45 的位 3 是 i8 饱和字段：0（不饱和）和 1（饱和）都属于已定义取值。
饱和仅为调用者已知的编码属性，不是源码修饰符，也不是 GPU 数值结果的证据。
D 类型 S32 的编码为 2；A/B 的 U8、S8 编码分别为 0、1。
i8 禁止 A/B 取负位，允许转置位。已知字查询先检查已定义字段，随后分别报告字段错误、已证实的运算违例和缺失事实。
调用者提供的已知字是独立声明，不能认证活描述符寄存器的内容。

对于共享内存操作数，[表 57](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
禁止转置的 8 位 A **和** B 使用 128 字节、32 字节原子粒度的 swizzle；两侧需要分别检查。
[表 55](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
还单独限制 B 转置时的 N：组 1 为 16..256、步长 16，组 2 为 32..256、步长 32。
因此组 1 的 N8、N24 虽是合法密集形状，但 B 转置时无效；其他事实合规时 N16、N32、N48 可用。
A 位于 Tensor Memory 时没有 A 共享描述符字的义务。非 WS 的 F/D/B/A 路径依次由
（组，M）=（1,64）、（1,128）、（2,128）、（2,256）选择。
在 F 半路径上，仅当 A 位于 Tensor Memory 且调用方独立提供 A/D 的 0/16 通道半区身份时才能比较。
完整 D/B/A 路径要求零；即使 A 来自 shared，也检查已知 D 半区。源码地址自身无法证明这些声明或分配历史。

当前显式目标目录把 i8 源码门槛投影为 PTX 8.6 起的精确 `sm_100a`，或 PTX 9.0 起的精确 `sm_110a`。
家族 `f` 目标不继承 i8 MMA，`sm_103a` 的其他家族特性也不能赋予它此运算能力。
手册所述历史 `sm_101a` 拼写不在当前目录中，不能把 PTX 9.0 之前的它暗中映射到 `sm_110a`。
表 43 对 103a 的字节步长描述符能力不代表 i8 运算可在 103a 上执行。

其他固定稠密、稀疏与 WS 来源形式见[全族矩阵](tcgen_mma_family_coverage.md)。
实时描述符内容、分配、完成与 GPU 行为仍是义务。TMA 的省略/group-1/group-2
组选择不继承 TCGEN 专属的统一 group 规则。

当前普通非 WS 稠密与稀疏形式还支持[强类型 A collector 与 ashift 控制](tcgen_mma_a_collector_coverage.md)；调用方已知历史检查仍是条件性的。
