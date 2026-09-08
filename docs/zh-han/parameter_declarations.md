# 参数声明

本文描述 frontend 的 PTX ISA 9.3 声明支持边界，不代表 launch ABI、指令全集或运行时
分配模型。能 parse 不等于声明合法；`resolveModule()` 在发布 metadata 前执行声明验证。

## 覆盖矩阵

| 形式 / 上下文 | Parser | 声明验证 | Installed public metadata |
| --- | --- | --- | --- |
| Entry header scalar / 有界一维数组 | 保留 | 基础非 predicate 类型、正整数常量维度、alignment、pointer attribute、版本与总大小 | 有序 typed `parameter_declarations`，role 为 `EntryInput` |
| Device input / return scalar / 有界一维数组 | 保留 | 同类 type/extent 检查；`.param` 要求 PTX 2.0 / SM 20；现代函数至多一个返回值 | `parameter_declarations`，role 为 `DeviceInput` / `DeviceReturn` |
| 最后一个 device input `.param .b8 bytes[]` | 保留 | 仅限最后一个 input；最低 PTX 6.0 / SM 30 | 单个未知 extent，byte extent 未知，与 scalar 可区分 |
| Unsized entry、return、非末尾 input 或非 `.b8` input | 保留 | 拒绝 | 不生成 resolved module |
| Body-local scalar / 有界数组，含多维数组 | 保留 | 基础非 predicate 类型、正整数常量维度、大小溢出检查、alignment、PTX 2.0 / SM 20 | `parameter_declarations`，role 为 `BodyLocal`，保留词法 scope 与完整 shape |
| Unsized body-local `.param` | 保留 | 拒绝 | 不生成 resolved module |
| `.callprototype` scalar / array formal | 保留 | Device signature 规则；prototype 本身要求 PTX 2.1 / SM 20 | Semantic API 可生成拥有自身数据的 `declaration_semantics::FunctionSignature`；不进入声明表 |
| Header vector / 多维数组 | 明确的 unsupported parse diagnostic | 不进入 | 无 |
| Body-local vector `.param` | 保留 | 明确的 unsupported diagnostic | 无 |
| `.f16x2` / opaque `.texref`、`.samplerref`、`.surfref` | 保留 type spelling | 明确的 unsupported diagnostic | 无；不臆造 opaque size/alignment |
| `.pred`、未知或仅用于指令的 type spelling | 保留 type spelling | 在 `.param` 中拒绝 | 无 |
| Module-scope `.param` | 保留 | 拒绝 | 无 |

支持的 parameter element 为 `.s8/.s16/.s32/.s64`、`.u8/.u16/.u32/.u64`、
`.b8/.b16/.b32/.b64/.b128` 与 `.f16/.f32/.f64`。公开的
`declaration_semantics::parameterScalarType()` 对该支持集合进行分类；结果只表示类型
分类，不表示上下文或版本已通过验证。`.bf16`、`.tf32` 等 instruction alternate format
不会因为 spelling 被保留就成为合法 declaration type。`.reg` formal 继续使用既有
signature/address 路径，不属于 `.param` 声明表。

## Alignment、pointer 与大小

显式 declaration / pointee alignment 必须为正的二次幂。省略 declaration alignment 时
采用 element 的自然对齐；即使显式值恰好等于自然值，也单独保留显式指定这一事实。
Frontend 不会从 ABI 关于 1、2、4、8 或 16 字节倍数的表述推断最大 alignment。

`.ptr` 属于 entry input，要求 PTX 2.2。受支持的 scalar pointer type 为 `.u32` 与 `.u64`。
Pointed state space 可为 `.const`、`.global`、`.local` 或 `.shared`，省略时表示 generic。
Pointee alignment 省略时默认为四字节。Parameter storage alignment 与 pointee alignment
是不同字段，不推断 host pointer width 或 launch address policy。
Canonical function signature 比较有效 alignment：省略的自然对齐与显式指定的同值对齐
兼容；声明 metadata 仍保留是否显式指定的区别。Direct 与基于 metadata 的 indirect array
call 使用相同的自然对齐默认值。
Formal `.param` object 不能直接作为 call actual。应先将 input（包括 entry pointer）
加载到 register，或通过 body-local `.param` object 暂存；device formal 不会取得
entry 专属的 `.ptr` attribute。

Entry header parameter 要求 PTX 1.4。对普通非 opaque entry parameter，静态计算的总空间
包含参数之间的 alignment padding。PTX 1.4 的上限为 256 字节，PTX 1.5–8.0 为 4352 字节，
PTX 8.1 及以后为 32764 字节。Checked arithmetic 拒绝单个声明或累计大小溢出。
这些是 ISA 限制，不是 CUDA/OpenCL driver 专属限制；byte extent 不是 packed offset 或
已分配内存。缺失 version/target 时不能证明 availability 违规。
PTX 1.0–1.3 将 entry input 声明放在 body 中的旧形式不受支持；body-local metadata role
描述的是 call argument/return object，而非这些旧式 input。
Parser 会拒绝 body-local `.param` initializer；对直接构造的 AST，semantic validation
也会拒绝该形式。
没有 payload 时，call 可以省略通过验证的末尾 unsized byte input；其他必需的 input
与 return argument 仍检查准确的参数数量。

## Owned metadata 与后续范围

每个 function 按 return-list、input-list、body 词法遍历的顺序拥有
`parameter_declarations`。条目保留 symbol / lexical-scope identity、role、scalar type
enum、vector width、有序 array extent、声明字节数、有效 alignment、显式 alignment
来源与可选 pointer property。Source 与 AST 销毁后仍可检查这些值。嵌套 body 中的同名
声明具有不同 identity。这是唯一拥有自身数据的 parameter declaration table。
Consumer 按 `ParameterDeclarationRole::EntryInput` 筛选即可按源码顺序检查 entry header
input；其他 role 不是 launch slot。

原有 `ResolvedFunction::entry_parameters` 成员与 `ResolvedEntryParameter` 类型已删除。
这是破坏性的 C++ API 变更，consumer 需要迁移并重新构建，不能只重命名成员。使用
`scalar_type` 代替旧 type string，使用非 optional 的有效 `alignment`，并以
`array_extents` / `byte_extent` 代替 `is_array` / `array_extent`。空维度列表表示 scalar；
未知维度表示受支持的 unsized array。ABI layout 与 packing 仍由 consumer 负责。

Opaque entry parameter 是合法 ISA object，但需要专门的、按名称使用的 texture/surface
access contract，普通 `ld.param` 不能加载它们，其物理 layout 刻意隐藏。支持这些对象、
`.f16x2`、header vector 与更高 rank 的 header grammar 属于后续工作，需要上下文相关的
conformance evidence 及匹配的 typed metadata。当前 unsupported diagnostic 不代表这些形式
全部违反 ISA。Body-local parameterized declaration group 同样超出当前 metadata 支持边界。
本工作不增加指令族、simulator execution、argument packing 或物理分配。

## 依据与回归

规范依据为归档的 [PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)：
[parameter state space](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parameter-state-space)、
[fundamental types](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fundamental-types)、
[entry](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#kernel-and-function-directives-entry)、
[device function](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#kernel-and-function-directives-func) 与
[call prototype](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-directives-callprototype)。
Assembler 实验只补充证据，不取代规范。

回归覆盖明确的 parser 边界、semantic type/role/version/size 验证、resolved declaration
identity/lifetime，以及单独编译的 installed-package consumer。GPU execution 不属于声明
验证 gate。
