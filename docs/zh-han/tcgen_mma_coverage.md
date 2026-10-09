# TCGEN 稠密 f16 MMA 覆盖范围

本页描述固定 [PTX 9.3 `tcgen05.mma`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
四种拼写中的首个源码切片。
首个切片是非权重驻留、非块缩放的稠密 f16，覆盖两个 CTA group 和 A 的两种来源。
可选输出通道掩码及可选 D 缩放是源码拓扑选择；其来源契约已由固定手册、完整模块
汇编证据和独立 Authority 检查点确定。独立的
[稠密 tf32 形式](tcgen_mma_tf32_coverage.md)及独立的
[稠密 i8 形式](tcgen_mma_i8_coverage.md)和规范顺序的
[无缩放 f8f6f4 形式](tcgen_mma_f8f6f4_coverage.md)及
[稠密 MX8 块缩放形式](tcgen_mma_mx8_coverage.md)及
[稠密 MX4 块缩放形式](tcgen_mma_mx4_coverage.md)及
[稠密 MX NV 四位形式](tcgen_mma_mxnv_coverage.md)及
[普通稀疏 f16/tf32/f8f6f4/i8 形式](tcgen_mma_sparse_coverage.md)及
[稀疏 MX 形式](tcgen_mma_sparse_mx_coverage.md)也已覆盖。普通非 WS
f16/tf32/f8f6f4/i8 形式还包含[强类型 A collector 与 ashift 控制](tcgen_mma_a_collector_coverage.md)。
六种稠密／稀疏 MX 形式也包含[强类型 A collector 控制](tcgen_mma_mx_a_collector_coverage.md)。独立的[稠密权重驻留形式](tcgen_mma_ws_dense_coverage.md)已覆盖；稀疏权重驻留及其余卷积形式仍未完成。

A/B 共享描述符只能使用 General 标量 B64/U64/S64 **寄存器**；指令描述符只能使用
General B32/U32/S32 寄存器。汇编器也接受了这些位置的字面量零，但前端仍遵循文档的
寄存器角色。`enable-input-d` 可以是标量谓词寄存器（允许书写取反），或采用零/非零
真值的整数谓词常量，包括 2。已接受的持有表示保存真值、源码位置及适用时的书写取反，
但不保存原始整数大小。首个切片不接受特殊谓词寄存器。

输出通道掩码可以省略；出现时 group 1 恰有四个、group 2 恰有八个 General 标量
32 位寄存器项，显式空向量无效。B32/U32/S32/F32 声明均按 32 位**掩码比特**兼容；
这里的 F32 不表示浮点运算或转换。字面量向量项及错误宽度、向量或谓词寄存器项无效。
可选 D 缩放须为原始求值整数 0..15 的立即数，不接受低位别名。汇编器接受了 2^32
作为零的别名，但固定源码范围更严格。缩放形式仅使用更窄的 exact100a/family100f
目标系列。观察到的 `.mma.kind.cta_group` 单一顺序是规范
`.mma.cta_group.kind` 的别名；不放开其他修饰符排列。

操作规则目录按 Table 42 记录：group 1 的 M 为 64/128，N 从 8 到 256、
步长 8，K 为 16；group 2 的 M 为 128/256，N 从 16 到 256、步长 16，K 为 16。
D=f16 要求 A/B 均为 f16；D=f32 时，A/B 各自可处于文档所列 f16/bf16 域。
合并的表格没有单独确定 f16×bf16 混用关系，因此该关系保留为待核实义务，
既不凭空拒绝，也不宣称完全合法。K 可唯一确定，不在指令描述符中另行指定。

对于调用方提供的共享描述符事实，Table 57 按 A、B 两种角色分别检查：
普通 K-major 16 位矩阵可使用已接受的 swizzle 域；转置后的 MN-major 排除
32 字节原子单位的 128 字节 swizzle。既有描述符目录独占 Table 43 和
Tables 45–48 的编码字段；操作查询须先调用其已定义字段检查。
生成的操作查询先调用既有已定义字段检查，再分别报告已完成检查、操作违规
和缺失事实，并安全处理无效枚举。运行时描述符寄存器保持不透明；调用方已知字值
不能证明寄存器内容或共享内存布局。

F/D/B/A 路径来自 **§9.7.17.10.5**，与 Table 58 的共享 swizzle atom 不同。
A 位于 Tensor Memory 时，F 半路径可比较调用方分别提供的 A/D 通道半区事实。
缺失的地址或布局事实保留为义务；源码和汇编都不能证明分配历史、完成事件或
实际通道一致性。已接受的 TCGEN group 一致性仅针对单个函数中的 TCGEN 指令；
TMA 的省略/1/2 混用属于另一种信号路由契约，不参加这一检查。

生成的精确 `Tcgen05MmaF16` 类直接持有类型化操作数，用八种结构化布局表示十六种
group、A 来源、掩码有无及缩放有无的组合。`tcgen_mma_f16_view` 从
`const Instruction&` 借用选中的角色；对其他形式或布局标签不符返回空值。
检查器在 AST 销毁后重新检查持有的寄存器、谓词、掩码、
立即数和地址元数据。独立的完整模块汇编检查点为源码拼写提供证据，但不证明运行时
描述符内容或 GPU 执行结果。
