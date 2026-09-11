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
| Entry header 的 opaque `.texref`、`.samplerref`、`.surfref` | 保留 type spelling | 明确的 unsupported diagnostic | 无；opaque entry object 合法，但需要 identity-only metadata 和专用 texture/surface 使用方式，不能臆造 byte layout |
| Device formal、return 或 body-local opaque object | 保留 type spelling | 拒绝 | 无；illegal，因为 opaque object 仅可用于 module global 与 entry parameter list |
| Entry header、device formal/return 或 body-local 中的 `.f16x2` | 保留 type spelling | 明确的 unsupported diagnostic | 无；`.f16x2` 是 fundamental type，因此这是保留的 legal-but-unsupported 边界，不是 alternate-format rejection |
| Header `.v2` / `.v4` parameter | 明确的 unsupported parse diagnostic | 不进入 | 无；保留为 legal-but-unsupported，等待上下文相关的 vector shape、size 与 ABI metadata |
| Header 多维 parameter array | 明确的 unsupported parse diagnostic | 不进入 | 无；header array grammar 仍为 unresolved，不据此宣称 illegal |
| Body-local vector `.param` | 保留 | 明确的 unsupported diagnostic | 无；保留为 legal-but-unsupported，等待 vector shape 与 call-staging metadata |
| Body-local parameterized `.param` group (`name<count>`) | 保留 | 明确的 unsupported diagnostic | 无；合法 declaration shorthand 需要逐个展开名称的 identity 与 declaration-order metadata |
| `.callprototype` 中的 vector 或 array `.param` | prototype grammar 接受时保留 | 明确的 unsupported diagnostic | 不声明 signature；不能从 spelling 推断 vector/array ABI shape |
| `.pred`、未知或仅用于指令的 type spelling | 保留 type spelling | 在 `.param` 中拒绝 | 无 |
| Module-scope `.param` | 保留 | 拒绝 | 无 |

支持的 parameter element 为 `.s8/.s16/.s32/.s64`、`.u8/.u16/.u32/.u64`、
`.b8/.b16/.b32/.b64/.b128` 与 `.f16/.f32/.f64`。公开的
`declaration_semantics::parameterScalarType()` 对该支持集合进行分类；结果只表示类型
分类，不表示上下文或版本已通过验证。`.bf16`、`.tf32` 等 instruction alternate format
不会因为 spelling 被保留就成为合法 declaration type。`.reg` formal 继续使用既有
signature/address 路径，不属于 `.param` 声明表。
在所属 function body 中，register-space input / return formal 会作为带类型的 register
operand resolve，并保留 lexical symbol identity。parameter-space formal 仍是 data object；
应使用符合其 parameter address 或 storage semantics 的 instruction，而不是将其作为
arithmetic register operand。

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
PTX 1.0–1.3 将 entry input 声明放在 body 中的旧形式仍被排除。PTX 9.3 说明 PTX 1.x
拥有 kernel `.param` object，而 device `.param` formal 到 PTX 2.0 才出现；但它没有给出
把旧式 body input 映射到当前 declaration table 所需的 grammar 与 identity rule。因此
body-local metadata role 绝不会将这类 object 重新解释成 entry input。这是 unresolved 的
legacy grammar，而不是声称历史形式 ISA-illegal。
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

### 保留边界与规范分类

归档规范的分类在本文固定为四种：表中的形式为 **legal supported**；
**legal but unsupported** 会继续明确诊断，直到实现其 typed metadata 与 access rule；
**illegal** 继续拒绝；**unresolved** 不能仅因 parser 行为而被提升到任一种分类。

Opaque entry parameter 是合法 ISA object：`.entry` directive 允许它们，而 opaque type
一节将声明位置限制为 module global 与 entry parameter list。它们是按名称使用的
texture/surface object，普通 `ld.param` 不能加载，物理 layout 也被刻意隐藏。因此 device
与 body 的 opaque declaration 继续拒绝。

`.f16x2` 是 fundamental type，不同于 alternate packed format。parameter-passing rule
讨论 base-type scalar 与 vector `.param` formal，因此 frontend 将 `.f16x2` 记录为
legal but unsupported，而不会把它并入 instruction-only type diagnostic。支持它还需要一致的
declaration、direct-call ABI、literal 与 resolved-memory 行为；现有 `ScalarType::F16x2`
值本身并不构成该 contract。

`.v2`/`.v4` parameter shape 与 local parameterized group 也采用相同区分。variable rule
允许 non-predicate fundamental type 的二/四 lane vector，并允许任意 state space 的
fundamental type 使用 parameterized name。当前 grammar 已保留的 source form 会继续保留，
但 frontend 不会臆造展开 declaration、vector byte/alignment、call staging 或 public metadata。
通用 array rule 只确立 constant dimension，未解决不同的 header 多维 parameter grammar；
该行仍为 unresolved。

本工作不增加 instruction family、simulator execution、argument packing 或物理分配。

## 依据与回归

规范依据为归档的 [PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)：
[parameter state space](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parameter-state-space)、
[fundamental types](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fundamental-types)、
[entry](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#kernel-and-function-directives-entry)、
[device function](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#kernel-and-function-directives-func) 与
[call prototype](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-directives-callprototype)。
Assembler 实验只补充证据，不取代规范。

作为非硬件的补充观察，使用 PTX 9.1 / `sm_80` 运行 `ptxas` 13.1：independent texture
mode 的 opaque entry list 与 body-local parameterized group 可汇编；孤立 `.f16x2`、vector
`.param` 在 allocation 时被拒绝，header 多维 array 被该 parser 拒绝。该工具版本结果不会
覆盖上述 PTX 9.3 分类，也不是 execution test。

回归覆盖明确的 parser 边界、semantic type/role/version/size 验证、resolved declaration
identity/lifetime，以及单独编译的 installed-package consumer。GPU execution 不属于声明
验证 gate。
