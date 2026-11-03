# Multimem 前端覆盖范围（PTX ISA 9.3）

前端识别固定版 [CUDA 13.3 PTX ISA 9.3 手册](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
§§9.7.9.13、9.7.9.15、9.7.9.26.4.4–5 和 9.7.14.8 的七个 `multimem`
源码族。它们共用一个 canonical `multimem` opcode，解析为精确的、自有的类型化指令类。
公共 `ResolvedAddress` 保留原地址表达式、源码位置，以及可用的声明绑定。
全局地址或符号**不能证明**运行时地址是 multimem 映射。

| 源码族 | 检查的操作数与后缀 | PTX / 目标下限 | 完成机制 |
| --- | --- | --- | --- |
| `multimem.ld_reduce` | 寄存器目标、全局/泛型 multimem 地址；省略 semantics 或显式 `.weak` 时 scope 可选，`.relaxed/.acquire` 时 scope 必需；整数/浮点操作、类型、向量和可选累加精度受规范表约束 | 8.1 / SM 90；`.acc::f32` 为 8.2；FP8 和 `.acc::f16` 为 8.6 且限列出的架构族 | 无 |
| `multimem.st` | 全局/泛型 multimem 目标和标量或寄存器向量源；省略 semantics 或显式 `.weak` 时 scope 可选，`.relaxed/.release` 时 scope 必需 | 8.1 / SM 90；FP8 为 8.6 且限列出的架构族 | 无 |
| `multimem.red` | 全局/泛型 multimem 目标和标量或寄存器向量源；`.relaxed/.release` 与 `.cta/.cluster/.gpu/.sys` 分别可省略 | 8.1 / SM 90 | 无 |
| `multimem.st.async` | 标量源，必须有 `.release.{gpu,sys}`，可选 `.global`；类型为 `b/u/s8–64`、`f32/f64` | 9.3 / SM 100 | 无 |
| `multimem.red.async` | 标量源，必须有 `.release.{gpu,sys}`，可选 `.global`，只支持 `.add.{u32,s32,u64}` | 9.3 / SM 100 | 无 |
| `multimem.cp.async.bulk` | 仅 `.global.shared::cta.bulk_group`；32 位字节数，可选 16 位 `.cp_mask` 操作数；默认 weak，显式 `.weak` 或 `.relaxed.scope...b128` | 9.1 / SM 90；mask 需 SM 100；显式排序需 9.3 及 SM 90a、SM 100f/110f 架构族 | Bulk group |
| `multimem.cp.reduce.async.bulk` | 相同方向和字节数合同；操作/类型规范表，包括半精度/bfloat add 必须带 `.noftz`；可选成对的 `.relaxed.scope` | 9.1 / SM 90；显式排序需 9.3 | Bulk group |

普通 `ld_reduce` 与 `st` 在省略 semantics（有效语义为 weak）或显式 `.weak` 时，
可选 `.cta/.cluster/.gpu/.sys` scope。Scope 位于 semantics 之后、state space、
operation 和 type 之前；例如接受 `multimem.ld_reduce.gpu.add.u32` 与
`multimem.st.weak.sys.u32`。每个 weak/default 公共 form 都暴露
`WithLocs<MemoryScope> scope`。省略 semantics 仍表示为 `MemoryConsistency::Omitted`，
省略 scope 仍表示为 `MemoryScope::None`，两者均无源码范围；显式 `.weak` 与 scope
保留其值和源码范围。强排序仍要求显式 scope；`red`、release-async 与 bulk 族的
默认规则相互独立。

普通整数 reduction 表将 `add` 限于 `u32/u64/s32`，`min/max` 限于
`u32/u64/s32/s64`，`and/or/xor` 限于 `b32/b64`。浮点 `multimem.red`
只允许 `add`。浮点 `ld_reduce` 可用 `add/min/max`，但 `f32/f64` 只可与
`add` 组合；`.acc::f32` 适用于 half/bfloat，`.acc::f16` 适用于 FP8。
标量/v2/v4/v8 类型集合取手册操作、向量和累加精度表的交集，不做无限制笛卡尔积。
Bulk reduction 的 `add` 类型为 `u32/s32/u64/f32/f64/f16/bf16`，
`min/max` 类型为 `u32/s32/u64/s64/f16/bf16`，`inc/dec` 只接受 `u32`，
位操作只接受 `b32/b64`。

普通及 release-async multimem 的 data 寄存器宽度必须等于指令的每个 data element；
vector 逐 lane 遵守该规则。整数 signedness 可不同，同大小 Bit 载体仍合法。原生 F16x2
允许自身或 B32，非 fundamental BF16/FP8 格式则要求匹配大小的 Bit 存储；浮点 kind
保留各自兼容规则。更宽 B64/B128 载体不能充当更窄 data element。适用固定参考的默认
类型一致规则：独立的 `multimem` 指令不因 suffix 名称而继承 `ld`/`st`/`cvt` 的 wider-register
例外。Address 载体、bulk byte-size 输入和 byte mask 保留各自独立契约。同宽 data 检查
不证明运行时 mapping、completion protocol 或 GPU 效果。

检查器复用通用类型化操作数、modifier 和引用遍历。对已知声明检查全局与共享地址空间、
标量或向量输入类型、字节地址对齐，以及绑定元数据一致性。整数/位类型的标量 store 和
reduction 输入接受寄存器或在使用处窄转换的整数字面量；`f32/f64` 标量输入接受现有的
类型化浮点字面量。对现有 weak/default 与 ordered 标量 FP8
`e4m3x4/e5m2x4` store，拒绝整数字面量及求值得到的整数结果。把整数窄转换为
32 bit，不会使它成为逻辑浮点操作数的 Bit 来源。显式 `mov.b32` 准备后使用 B32
寄存器仍合法；前端不会隐式插入这条 move。

无符号、无包装的直接 `0f` 字面量保留完整 32-bit 字，包括编码的 sign 与 NaN
模式，不做浮点转换、splat 或 quieting。当前 packed immediate 必须不超过 32 bit，
没有 `integer_source_bits`（optional 值为零也属于存在），且 `is_negative=false`。
没有整数来源的当前 word 可以合法修改；检查针对当前 typed data，不认证已经擦除的
源码拼写。带符号、包装或 operator 的 exact-word 来源保留既有约束。十进制、`0d`
及浮点表达式 packed constructor 仍不支持：一个数到四个值的映射仍是 reference-qualified
边界，并非手册全面禁止浮点字面量。更广 packed source 权限与此四分支 origin 修复分开。
六个相邻标量 `f16x2/bf16x2` source 分支也接受既有裸 unsigned 直接 `0f` whole-word producer；九个所选浮点 vector source 字段
遵循下述 value-vector 契约。Load 目标仍限寄存器。

Direct 与 owned 检查保留 SameWidth、global address 和 target/flavor 约束。在适用
instruction check 时，有效的 supplied AST 不能掩盖当前无效 origin。不完整上下文下
`AvailableContext` 保留既有的整条指令检查延后行为；成功不证明被延后的 operand invariant。
Bulk 目标与源均要求
16 字节对齐；立即数字节数须能被 16 整除。寄存器字节数仍是运行时义务。两个 release-async 族要求寄存器基址（可带立即数偏移）；
即使符号声明为 global，也不接受仅符号基址。
写出 `.cp_mask` 必须提供最后的 byte-mask 操作数。Release async store/reduce
**没有**具名完成机制；bulk copy/reduce 使用 `BulkGroup`，其 commit/wait
由已有 bulk-group 指令提供。前端不证明完成顺序、内存范围界限、运行时 multimem 映射、
跨设备可见性或实际 GPU 效果。

固定版手册有三处示例与 Syntax/合法表冲突。前端依规范表：拒绝
`multimem.red...max.f64`，因为浮点 `red` 只允许 `add`；也拒绝省略
`.shared::cta` 且多出 `[mbar]` 的 bulk reduction 示例，因为规定形式恰有
三个操作数并以 `bulk_group` 完成。手册中的 copy 示例
`multimem.cp.async.bulk.relaxed.cta.global.bulk_group.b128` 也漏掉必需的
`.shared::cta` 源空间限定符，因此被拒绝。对于 `multimem.red`，手册分别将省略的
semantics 默认为 `.relaxed`、省略的 scope 默认为 `.sys`；拥有的源码表示仍以没有
源码范围的 `Omitted`/`None` 类型值保留省略状态，因此只写一个限定符的源码也被前端接受。
另行记录的 `ptxas` 13.3.73 探针拒绝这类只写一个限定符的形式；
汇编器行为不覆盖此处的前端源码合同。

Canonical 清单和负例边界测试见
[`test_multimem_contract.py`](../../../python/tests/spec/test_multimem_contract.py) 与
[`test_multimem_coverage.cpp`](../../../submod/resolved_ir/test/test_multimem_coverage.cpp)。
[`multimem_consumer`](../../../examples/multimem_consumer/main.cpp) 验证安装后的公共头。
这些测试检验前端接受性与源码生命周期，不在 GPU 上执行 multimem 指令。

选定的 v2/v4 data register-vector role 接受普通 named 声明与 brace list。Instruction-only logical format 使用兼容的 fundamental Bit carrier。V8 仍仅接受 brace；普通声明总宽不得超过 128 bit。Named form 不改变逐 element 的 SameWidth 契约。

### 浮点 value-vector source 契约

恰有九个既有 `src` 字段采用 `WithLocs<ResolvedValueVector>`：v2/v4/v8 的 weak、ordered 浮点 store 与浮点 add reduction。Class、selector/layout identity、element type domain、arity、default、availability 和 address 均保持不变。公共 field/factory/observer payload 从 `ResolvedRegisterVector` 迁移，consumer 必须使用匹配的生成头与库重建。每个既有 register lane 包装为 `RegOrImm`，保留 location 与 `ResolvedVectorSource`，并用 `std::get_if` 区分 alternative。原有 null register element 不是 immediate，也不会获得 sink 权限。

全部64个所选 type 分支具有 typed value carrier。其中32个有单数值 numeric producer（15个 F16/F32 与17个 BF16/E4M3/E5M2），20个有裸 unsigned 直接 `0f` 的完整32位 exact-word producer（F16x2/BF16x2/E4M3x4/E5M2x4）。12个 E4M3x2/E5M2x2 分支没有 exact16 lexical producer，也没有已采用的 numeric packing constructor：literal-source 与相应 source-lifetime positive 保持 Source_N_A。其真实 register control 与独立构造的 canonical low16 typed word 仍可验证；源码 producer 缺席不是 typed-data 禁令。Numeric scalar 转换复用既有 binary64 单次舍入策略；packed32 原样保留所有 written word bits，不 splat、量化或 quiet。Integer-valued source expression 仍不兼容 logical Float lane。

Mixed brace 可同时含 register 与 immediate。50个 v2/v4 named 分支保留真实 General whole-base declaration identity、精确 NamedProjection lane、element SameWidth 及16字节 declaration 上限。14个 v8 分支仍仅接受 brace；既有128-element projection buffer 与256-bit generic capacity 不变，本 cohort 至多需要8 lane／128 bit。Immediate projection 不是 user sink。同步 observer 接收实际 payload 与全部 lane range（包括第8 lane），不得保留 callback reference。Copy／assignment／clone 必须从自身 owner 重新取得 view。

普通 typed word 要求所选 logical type、位宽内 bits，且没有 integer-origin／negative flag；一致的当前字修改不重放已擦除的浮点 source history。当前 payload／pointer／range／provenance 检查与保留的 source identity 检查彼此独立。Address alignment、所选 type／modifier／target gate 及不完整上下文延期策略仍适用。Vector 契约独立于 scalar packed source permission，不扩展 atomic input、packed numeric constructor 或 load destination，也不声称 runtime multimem mapping 或 GPU 执行。记录 delivery 完成前仍需 current generation、Debug、fresh installed public consumer 与独立 actual-diff 证据。

### 标量 packed32 源成员

既有类 `MultimemStFloatPackedNoneScalarDefaultWeak`、
`MultimemStFloatPackedNoneScalarDefaultOrdered` 与
`MultimemRedFloatPackedAddScalarDefaultRed` 的 `src` 使用
`WithLocs<RegOrImm>`。Consumer 须以匹配的 installed header/library 重建，并对
`.value` 使用 `std::get_if<ResolvedRegisterRef>` 或 `std::get_if<ResolvedImmediate>`。
仅有三个源字段变化；各自保留既有 `modifier(type)` 的 F16x2/BF16x2 domain 与
SameWidth。Variant identity、成员名、control、address/global/alignment、target
契约保持不变，先前九个 ValueVector 源字段的 API 也保持不变。

六个分支均通过既有 shared helper 接受裸 unsigned 直接 `0f` packed32 word。
全部 32 位都是数据，包括 encoded sign、signaling/quiet NaN-looking pattern；
不量化、splat 或 quiet NaN。F16x2 保留普通 B32/native F16x2 register carrier，
BF16x2 保留 B32。既有 scalar component provenance 规则仍适用，不授权整体向量
或新 tuple source。Integer-origin literal/result、unary sign、括号、operator、
decimal/F64 packed constructor、numeric pair 和隐式 packing 仍被排除；不增加
`0d` 低位截断或新 lexical format。

源码 converter 产生选中的逻辑 packed 类型。Canonical current-word 正例使用该类型、
最多 32 位、没有 integer origin（optional zero 仍是 present）且 `is_negative=false`。
既有 current Bit-carrier 兼容性保持不变；这些示例不收窄继承的 `SameWidth` 策略，
也不制定新的 B32 规则。Canonical current-word 修改不认证已擦除的 producer history。Copy/assignment/clone 与 AST-free 检查保留实际成员、范围及
provenance；supplied AST association 独立核对 retained syntax identity。同步
observer 仅在 traversal 中借用真实 owner。前端不执行 multimem mapping、store、
reduction、ordering 或 GPU 行为。

Scalar packed 检查针对选定逻辑 F16x2/BF16x2 的 canonical word。Bit 类型 B32 **current** word 的兼容性是独立的继承策略问题，此处仍未证明；标量 F16 Bit16 控制不能决定该问题。本限定不增加 source literal 或 constructor 权限。
