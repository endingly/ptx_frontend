# 参数声明

本文描述 frontend 的 PTX ISA 9.3 声明支持边界，不代表 launch ABI、指令全集或运行时
分配模型。能 parse 不等于声明合法；`resolveModule()` 在发布 metadata 前执行声明验证。

## 覆盖矩阵

| 形式 / 上下文 | Parser | 声明验证 | Installed public metadata |
| --- | --- | --- | --- |
| Entry header scalar/vector / 有界多维数组 | 保留 | 基础非 predicate 类型、正整数常量维度、alignment、pointer attribute、版本与总大小 | 有序 typed `parameter_declarations`，role 为 `EntryInput` |
| Device input / return scalar/vector / 有界多维数组 | 保留 | 同类 type/extent 检查；`.param` 要求 PTX 2.0 / SM 20；现代函数至多一个返回值 | `parameter_declarations`，role 为 `DeviceInput` / `DeviceReturn` |
| 最后一个 device input `.param .b8 bytes[]` | 保留 | 仅限最后一个 input；最低 PTX 6.0 / SM 30 | 未知外轴、保留并检查的已知内轴及未知 byte extent，与 scalar 可区分 |
| Unsized entry、return、非末尾 input 或非 `.b8` input | 保留 | 拒绝 | 不生成 resolved module |
| Body-local scalar/vector / 有界数组，含多维数组 | 保留 | 基础非 predicate 类型、正整数常量维度、大小溢出检查、alignment、PTX 2.0 / SM 20 | `parameter_declarations`，role 为 `BodyLocal`，保留词法 scope 与完整 shape |
| Unsized body-local `.param` | 保留 | 拒绝 | 不生成 resolved module |
| `.callprototype` scalar/vector / array formal | 保留 | Device signature 规则；prototype 本身要求 PTX 2.1 / SM 20 | Semantic API 可生成拥有自身数据的 `declaration_semantics::FunctionSignature`；不进入声明表 |
| Entry header 的 opaque `.texref`、`.samplerref`、`.surfref` | 保留 | 按名称使用的 entry input；检查资源种类、shape 与版本 | 按源码顺序保存的 `opaque_entry_parameters`，含绑定 symbol identity、资源种类与源码 shape；无 byte layout |
| Device formal、return 或 body-local opaque object | 保留 type spelling | 拒绝 | 无；illegal，因为 opaque object 仅可用于 module global 与 entry parameter list |
| Device `.reg .v2` / `.v4` input 或 return，含 `.callprototype` | 保留为单个 formal | 非 predicate fundamental element，总大小至多 128 bit；call 必须匹配 scalar/vector 与 width | Typed `FunctionParameterContract::vector_shape` 与 bound vector width；不产生 `.param` 声明记录 |
| Header 多维 parameter array | 保留全部轴 | 正整数有界轴、检查乘积；仅受支持的最后一个 scalar byte input 可省略外层 extent | signature 与自身声明记录保留精确轴，call ABI 使用总元素数 |
| Body-local parameterized `.param` group (`name<count>`) | 保留 | 支持的 scalar/vector parameter element、正 uint32 count、禁止 array/initializer；保留 body-local availability | 每个 declarator 一个 `BodyLocal` record 与带 count 的 owning symbol；member 使用相同 `SymbolId` 加 index |
| `.pred`、未知或仅用于指令的 type spelling | 保留 type spelling | 在 `.param` 中拒绝 | 无 |
| Module-scope `.param` | 保留 | 拒绝 | 无 |

支持的 parameter element 为 `.s8/.s16/.s32/.s64`、`.u8/.u16/.u32/.u64`、
`.b8/.b16/.b32/.b64/.b128` 与 `.f16/.f16x2/.f32/.f64`。公开的
`declaration_semantics::parameterScalarType()` 对该支持集合进行分类；结果只表示类型
分类，不表示上下文或版本已通过验证。`.bf16`、`.tf32` 等 instruction alternate format
不会因为 spelling 被保留就成为合法 declaration type。`.reg` formal 继续使用既有
signature/address 路径，不属于 `.param` 声明表。
在所属 function body 中，register-space input / return formal 会作为带类型的 register
operand resolve，并保留 lexical symbol identity。
Device `.reg` input 与 return 支持 `.v2` / `.v4` 的非 predicate fundamental element，
包括 `.f16x2`，总大小至多 128 bit。一个 vector 始终是一个 formal 与一个 named-register
call argument；scalar literal 与不同 vector width 不能满足其 signature。省略 alignment
时采用整个 vector 的大小。Direct call、`.callprototype`、`.calltargets` 与 owned
call-table signature 在 AST 销毁后仍保留同一 typed shape。
parameter-space formal 仍是 data object；
应使用符合其 parameter address 或 storage semantics 的 instruction，而不是将其作为
arithmetic register operand。

## Alignment、pointer 与大小

显式 declaration / pointee alignment 必须为正的二次幂。省略 declaration alignment 时
采用 scalar 大小或整个 vector 大小，与 array count 无关；即使显式值恰好等于自然值，也单独保留显式指定这一事实。
Frontend 不会从 ABI 关于 1、2、4、8 或 16 字节倍数的表述推断最大 alignment。

`.ptr` 属于 entry input，要求 PTX 2.2。受支持的 scalar pointer type 为 `.b32`、`.b64`、`.s32`、`.s64`、`.u32` 与 `.u64`。
声明的 integer 或 bit type 会保留；两种宽度均可用于 `.address_size 32` 或
`.address_size 64`。
Pointed state space 可为 `.const`、`.global`、`.local` 或 `.shared`，省略时表示 generic。
Pointee alignment 省略时默认为四字节。Parameter storage alignment 与 pointee alignment
是不同字段，不推断 host pointer width 或 launch address policy。
PTX 2.2 的 pointer 引入门槛从当前 owned entry-input metadata 检查，即使缺少 target
context 也不会跳过已知旧版本的拒绝。未知版本在 available-context 验证中延后，
strict 验证仍要求完整 context。带源码验证采用 supplied source version，AST-free 验证
采用所选 owned source region 的版本；这不会把 pointer container 宽度与 address size 等同。
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

两个 `validateModule` overload 都按各 function 的 `declaration_scope`，将有序 return
与 input signature 同 binding formal 比较。Scalar `.param` formal 必须匹配对应
declaration record 的 role、identity、type、shape、有效 alignment、checked byte extent
与 pointer property。`.reg` formal 关联 binding type、vector shape 和 alignment，不进入 parameter
table。Opaque entry input 保留独立的 name-only record 和完整 input signature 中的位置。
Body-local record 单独检查，不能变为 formal。兼容 prototype 和 definition 共享 function
identity，但各自拥有 declaration scope。因此，即使已销毁源码和 AST，仅将 signature
中的 `.u32` 改为 `.f64` 也会检查失败。

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
与 body 的 opaque declaration 继续拒绝。每个 function 按源码顺序持有
`opaque_entry_parameters`，在 AST 销毁后仍保留绑定 identity、种类、alignment、array
shape 与源码位置。

Fundamental `.f16x2` 与合法 `.param .v2/.v4` object 在声明、call ABI 和 owned
layout 检查中保留 native type/shape。Numeric packed literal representation 的独立
reference gap 见下文。

Body-local scalar/vector `.param` compact group 复用普通 group binding：`value<2>` 拥有一个
count 为 2 的 symbol，`value0` 与 `value1` 分别以同一 `SymbolId` 加 index 0/1 引用。
每个 declarator 只有一个 `BodyLocal` parameter record，不产生 storage declaration record。
`byte_extent` 是单个 member object 的大小，不是 count 乘以 allocation size；count 由
symbol 持有。lexical shadowing、member bounds 与 call staging 使用 typed member identity。
owned validation 拒绝缺失或越界的 member index，以及 ordinary symbol 上多余的 index。
一致的范围内 member-index edit 是合法 semantic change；owned validation 不重建原始拼写。
提供 AST 时，source association 独立保留 compact count 与 written member reference。
不生成展开的 source declaration 或逐 member symbol。

Header 的轴按从外到内排列；entry 的有界输入，以及 device/prototype 的有界输入与返回值，均可使用多维数组。
仅受支持的最后一个 scalar `.b8` device/prototype input 可省略最外轴，例如 `bytes[][2][3]`。
即使总字节数未知，仍检查已知内层乘积。Call ABI 比较 element type、vector shape、
总元素数与 effective alignment，不要求调用实参 rank 相同，也不新增 inner product 整除要求；
`[2][3]` 与 `[6]` ABI 兼容。各函数自身 signature 与声明记录仍须逐轴精确一致，
source association 仍区分 ABI 总数相同的书写 shape。

这是 C++ source-breaking member migration：`CstFunctionParameter` 删除
`left_bracket`、`array_size`、`right_bracket`，替换为
`std::vector<CstArrayDimension> array_dimensions`；`AstFunctionParameter` 删除
`is_array`、`array_size`，替换为 `std::vector<AstArrayDimension> array_dimensions`。
`FunctionParameterContract` 删除 `is_array`、`array_extent`，替换为
`std::vector<std::optional<NormalizedNumericValue>> array_extents`；
`ResolvedOpaqueEntryParameter` 对应替换为
`std::vector<std::optional<uint64_t>> array_extents`。空向量表示 scalar。
Numeric `ResolvedParameterDeclaration::array_extents` 不变；binding `Symbol::is_array`
和 `CallArgumentProperties::is_array/array_size` 仍是派生 category/ABI 总数，
不是精确轴的权威。无效轴与溢出乘积不会退化为普通 unknown extent；opaque 轴不推导物理字节数。
本变更不增加多 bracket memory operand、运行时 allocation 或 bounds 语义。

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

## Fundamental vector 与 packed element

公开 classifier 接纳 17 个非 predicate fundamental element，包括 scalar `.f16x2`。
它的两个 half component 合为一个四字节 packed scalar，不是隐式 `.v2` formal。
显式 `.v2/.v4` 包含两个/四个这样的 element；每个 vector 至多 16 字节，因此分别有
16/12 个合法 element 组合。Header/body `.param`、device/prototype input/return
保留该 shape。Sized parameter vector array 受支持；自然 alignment 是整个 vector
大小，byte extent 再乘各维。Compact count 不乘入单个 member object 的 extent。
Register vector array、vector pointer、predicate、opaque 与 instruction-only element
仍被拒绝，末尾 unsized byte input 仍只允许 scalar。

Source 与 AST-free owned validation 重算相同的 padded entry byte limit 与 checked
extent。Native typed F16x2 parameter/register actual 保留精确 dtype 和显式 shape；
call 不采用 instruction-style bit compatibility。Packed storage 使用既有 b32 memory
container；named array index 仍按声明 scalar element 缩放（F16x2 为四字节），不是
vector 大小。Header 多维 parameter array 保留完整轴；call ABI 使用总元素数，自身记录逐轴核对。

Numeric one-to-pair F16x2 call-literal constructor 仍是 reference gap：整数、
zero、decimal 与 F64 expression 不定义 splat 或 lane placement。Exact-word 契约要求
同宽、裸 unsigned 直接 `0f` binary32 字面量为其余条件合法的 scalar `.f16x2`
Reg 或 Param input 保留完整 32 位 word。Direct call、`.callprototype`、
`.calltargets` 与 storage function-list 路径复用同一 formal-driven 转换。这不允许
scalar literal 满足 vector formal 或 return list，也不把 `.bf16x2` 分类为 call formal。
该 exact-word 路径不同于普通 scalar F16 numeric conversion。

已知 opaque entry input 仍为 name-only：numeric aggregate 遍历跳过它们，不分配
byte 或 padding，同时继续检查后续 numeric/vector input。

完整 module 的 direct-call target 必须在 use 前声明/定义实际绑定的 function 或 alias
identity；兼容的先行 prototype 足够，whole-module lookup 找到 later declaration
不代表已可见。此 source/owned 检查独立于精确 parameter ABI 与 staging 规则。

Default stack ABI 下，只有在已知 PTX >=2.0 且当前可识别 target architecture >=20
时，device/call-prototype 的 register-state 输入和返回 formal 才要求**整个值**至少
32 bit。宽度是 scalar storage width 乘显式 vector lane：`.v2.u8`、scalar `.f16/.pred`
不满足；`.v4.u8`、`.v2.f16` 与 native scalar `.f16x2` 满足。既有 fundamental/vector
资格及最大宽度仍独立检查，source type/shape 不隐式 promote。已知旧版本或旧 target
使该下限不生效；缺失 context 在 available-context validation 中延后判断，strict
validation 仍报告既有 missing-context 错误。每个 function 使用自身 active target region；
source-aware validation 使用所提供 AST，AST-free validation 使用 owned region。
未使用 prototype 同样检查。该规则仅针对 formal：普通小宽度/predicate register 声明、
小宽度 `.param` formal，以及传给匹配 `.param` 的小 register actual，仍合法。

其余条件合法的 scalar F16 Reg 与 Param input 共用 numeric literal 转换，覆盖 direct 及 metadata-backed indirect call。浮点 1.5 或等值 F64 expression 生成 F16 word 0x3e00；原 lexical／转换前 source 保留用于 owned 重算。现代 Register-formal minimum width 仍独立拒绝 scalar Reg F16；已建立的 nonstack legacy call 与 partial AvailableContext 遵循既有 gate。Strict missing context 及 scalar-to-vector no-splat 保留；不增加 packed F16x2 构造。

Parameter address 验证还在 context 可跳过之前重新关联当前 symbol ID、lexical
scope、compact member 和 declaration record。已知 Parameter 对象的 effective
space 必须属于非空 operand domain，并与关联的当前 state-space modifier 一致。
entry input 的 readonly 检查仅使用显式 Write/ReadWrite operand 要求或既有
Return parameter constraint，不把 DeviceInput 一概视为 readonly。诊断仅合并
局部完全相同的 address cause，保留其他失败；此检查不增加 source form 权限，
也不替代完整 target-aware 验证。


## 历史 entry body input 与 typed role

PTX 1.0–1.3 的 entry body `.param` 是只读 kernel input，role 为
`ParameterDeclarationRole::LegacyEntryInput`。唯一 owning handoff 是
`ResolvedFunction::parameter_declarations`，按 lexical depth-first/declarator
顺序保留，包括 nested scope 与同名 shadow 的独立 identity。Compact group 每组
一个 record，`byte_extent` 是每个 member 的大小，count 保留在 bound symbol；array
保留所有轴。这个顺序只规定 metadata 遍历，不承诺 host launch slot、相对地址、
padding 或 allocated offset。

`FunctionSignature::parameters` 与 returns 只描述语法 header ABI；legacy entry
可以有空 header signature 和非空 body input。Consumer 分别枚举 header 的
`EntryInput` 与 body 的 `LegacyEntryInput`，opaque header input 仍独立保存。
Legacy body input 必须是 fully-sized fundamental object，保留已有 type-specific
availability，不新增旧版本 opaque、`.ptr`、unsized 或 initializer 权限。
PTX 1.4–1.x 拒绝 entry body `.param`；2.0+ 选择 `BodyLocal`，独立要求已知
SM >=20。Device body object 始终是 `BodyLocal`。

只有含 body `.param` 且无法选择 role 的 entry，fresh `resolveModuleOnly` /
`resolveModule` 才要求 PTX version；即便已知 SM 1.x 也报告精准
`MissingValidationContext`，不按 target 猜 role，也不为 typing 要求 target。
普通 fragment、无 body parameter 的 entry、header input 和 device body object
保留原 context 行为。已 typed module 在缺 context 时保留当前 role；
AvailableContext 只延迟未知 window，strict 仍要求完整 context。

`ResolvedSymbolRef::parameter_role` 同步从 actual record 派生，structural binding
仍为 `CallParameter`。已知 legacy direct reference 必须有匹配 cache；所有 engaged
cache 都与 actual parameter record 重连，错误/缺失 legacy cache、伪造 kind 或
role mismatch 均拒绝。Address、MOV、tensor 共用方向映射：entry/device/legacy input
为 `Input`，device return 为 `Return`，body call slot 为 `CallArgument`。
Legacy LD 是普通输入读取，不要求 call 后 return-load staging；MOV 保持 `.param`
地址，不物化为 device-local 地址。Actual record 独立保证只读与 entry frame，
禁止直接将 legacy input identity 作为 call actual/return；先 load 到 register 的
合法 register ABI call 保留。现代 body call-store/call/load adjacency 不变。
两个 validator 都在缺 target skip 前检查当前 record/cache/shape/scope/access/
logical capacity；原合法 AST 不豁免当前损坏 payload。替换 context 检查当前 role，
不自动重解释，也不引入 producer history。

Legacy input 的已知 global ceiling 为 32764 bytes。Checked logical sum 对每个
record 的 per-member `byte_extent` 乘 compact count（普通对象为1），遍历全部
unused/nested input。Axes/vector lanes 已包含在 extent 中，不重复相乘。
Overflow 或总量超32764在 fresh resolution 和两个 validator 中均拒绝，缺 context
也不延迟这个 intrinsic 事实。不把1.4 header 的256-byte限制套给 legacy body input，
已有较新 header 限制保持不变。
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` 仍是 reference-boundary qualified：
通过已知 logical ceiling 不证明 physical ABI packing、更低的历史阈值，或全部
legacy driver/hardware launch ABI 都接受该布局。


共用 current operand-domain pass 将 actual `EntryInput` 与 `LegacyEntryInput`
record 都识别为 readonly entry object，在 context skip 前使用当前 typed
layout/field requirement，覆盖非 LD/ST consumer。MOV 读取地址仍合法；明确要求
writable operand 时拒绝。Legacy readonly 保留原 label 与 instruction range，
仅合并相同 local cause，不按 opcode 推测 writer，也不新增历史指令权限。
