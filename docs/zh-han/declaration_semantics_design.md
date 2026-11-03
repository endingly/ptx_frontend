# Declaration Semantics 设计

## 共享数值求值

`numericConstantValue()` 将拥有源码树的表达式求值为整数 64-bit bits 与 signedness，
或 f64 值；带位置的错误区分 invalid expression 和没有数值的合法 symbolic/deferred
expression。指令数值位置拒绝这两类错误。原有 optional
`constantIntegerValue()` 与 `constantArrayExtent()` helper 保持兼容。
整数/浮点混合算术和相互 cast 无效。整数或浮点除零（包括负零）无效。
conditional 两个分支及 logical 两个 operand 均检查，即使值未被选中；
浮点溢出不新增统一的 finite-only 限制。

十进制浮点常量允许小数点两侧至少一侧有数字，例如 `1.`、`.5`、`1.e2`；
也允许带指数的数字序列，例如 `1e2`。指数由 `e`/`E`、可选符号及必需的数字组成。
浮点常量不带大小后缀，先解码为 binary64，再按现有规则转换到 `.f32` 或 `.f64`；
整数 destination 仍不兼容。依据是 [PTX ISA 9.3 浮点常量](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-constants)。
共享的浮点 lexical category 不扩展 `.version`：其 `major.minor` 语法仍要求小数点
两侧都有十进制数字。

二元 `&`、`|`、`^` 保持 usual arithmetic conversions：两个 signed 输入产生
signed 结果，任一 unsigned 输入产生 unsigned 结果。依据是 PTX 9.3 §4.5.5
正文：仅在 Table 5 的 `&`/`|`/`^` 行与详细二元操作规则冲突时，选择详细规则。
这是前端采用的解释，不是 NVIDIA 发布的勘误。历史 assembler 观察及其确切工具
版本见[位运算策略](bitwise_constant_policy.md)。表达式结果保留 64 位数据及符号性，
直到使用位置转换；destination 类型和规范化源符号缓存不会反向提供表达式类型。
一元 `~` 产生 unsigned，remainder 将两输入解释为 unsigned 并产生 signed。
常量表达式 shift 先将 count 求值为 64 位值，再将其数据解释为 unsigned，
仅接受 0–63。因此 `1 << (-1 + 2)` 合法；求值为 `-1`、`64`、`65` 或
`UINT64_MAX` 的 count 被拒绝，不执行 mask 或 clamp。即使 count 是 unsigned，
结果也保持左操作数的类型：signed 负数右移填充符号位，unsigned 右移填零，
count 为零时保留输入数据。

0–63 是前端采用的表达式 domain 策略。[手册的常量表达式规则](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constant-expression-evaluation)
没有为大于等于 64 的 count 定义结果；本策略不表示厂商要求一律报错，也不表示
PTX `shl`/`shr` instruction 的执行行为。具体 count 错误保留在失败 count 子树，
且不标记为 deferred，包括 conditional 或 logical 未选中子表达式中的非法 shift。
`symbol + 1` 这类合法符号重定位表达式在值未知时保持 deferred；
`symbol << 1` 和 `symbol >> 1` 是非法重定位操作，不是 deferred 数值 shift。
initializer 和 array-dimension 的
声明包装层保留原有覆盖整个表达式的 invalid-initializer/invalid-dimension 诊断；
对同一 AST 表达式调用公共 evaluator，仍得到具体 count 子树错误。MOV、call
及 address 使用位置则直接传播该 count 错误。

合法的规范化 immediate 和 storage payload 在源文本/AST 销毁、复制、赋值及
instruction clone 后，继续接受现有 owned IR 检查。非法 shift 不产生此类 payload，
因而 owned count tree 重校验不适用，也不引入 count-origin 缓存。数组 extent、
destination 类型及地址范围限制仍是独立的使用位置约束。

## 定位与 API

declaration semantics 位于 lexical binding 之后、Resolved IR 之前。公开入口为：

```cpp
auto diagnostics =
    declaration_semantics::checkDeclarations(module, binding.table);
```

该 pass 使用结构化 Syntax AST 检查单个 declaration 的 initializer/array 约束，并使用
module 级 declaration 序列检查跨声明兼容性。`resolveModule()` 会自动运行 binding 与本
pass，并在解析 instruction 前累积两者的诊断。

Module `.reg` 与 `.local` 声明根据 source target 的 ABI 假设进行检查：
PTX ISA 3.0 及以上且当前源码 target 为已识别的 SM 20 及以上时，这些变量必须在函数内声明。
Frontend 不建模由调用者选择的禁用 ABI 编译开关。较早 PTX 版本与已识别的 SM 1.x target
保留历史 module placement。Target 按源码顺序生效；后续未知或空 target 会清除先前的 placement context。

声明语义检查每个实际 module occurrence，包括未使用的变量与冲突重声明，并在完整声明范围处
报告 `InvalidDeclarationPlacement`。自有校验把每个 variable identity 及保留的 occurrence
关联到其 source region，包括没有 storage record 的寄存器，并报告 `RuleViolation`。
两个 validation overload 在源码与 AST 销毁后或使用替换源码 context 时仍重新检查 placement。
`AvailableContext` 在 version 或已识别的当前 target 缺失时推迟此规则；严格校验在受影响的声明处
报告 `MissingValidationContext`。其他 header 与 identity diagnostic 保持独立。
函数内声明、nested block 与合法 register formal 沿用现有契约。依据为固定的 PTX 9.3
[PTX 1.x 变更](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#changes-from-ptx-isa-version-1-x)、
[寄存器](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#register-state-space)、
[local](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#local-state-space) 与
[变量声明](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#variable-declarations) 规则。

## Array 与 initializer

寄存器声明不能使用方括号数组维度。此规则也适用于未使用的声明，并在第一个方括号维度处
报告 `InvalidRegisterDeclarationShape`，不依赖类型或向量诊断。紧凑寄存器组（`r<2>`）
仍是独立形式。自有形状与校验契约见[寄存器声明兼容性](register_type_policy.md)。

整数字面量与指令立即数共享十进制、前导零八进制、二进制和十六进制解码规则。例如，
`010` 的初始化值为 8，用作数组维度时也为 8；无符号后缀不改变基数。

无法在 64 位源码数值域内解码的整数字面量属于非法表达式，而非待求值的合法值。
声明检查在字面量自身的 range 报告 `InvalidIntegerLiteral`，`resolveModule()`
保留该类别和位置。即使三元表达式的两个分支相同，或字面量位于未选中的分支，
折叠也不能掩盖解码失败。合法的待求值表达式仍被单独处理：尚未求值的合法比较
作为条件时，等值分支仍可折叠为常量。

array dimension 必须能求值为正整数 constant。求值器以带 `.s64/.u64` signedness 的
64-bit bit pattern 保存每个整数子表达式，支持负数中间值、cast、usual arithmetic
conversion，以及一元/二元/三元运算；因此 `-1 + 2` 等合法表达式不会在中间阶段被
误判。`WARP_SZ` 同样在此阶段求值；symbol address 不能作为 dimension。
只有第一维可以省略：有 initializer 时由最外层 list 推导其长度；external storage
declaration 也可在没有 initializer 时保留未知的首维。

initializer 的 brace nesting 必须与 array 维数一致；vector declaration 额外形成长度为
2 或 4 的最内层 aggregate。每一维允许少于声明长度，剩余元素按 PTX 规则补零；只有
超出该维长度才产生元素数量诊断。

scalar leaf 区分 integer、floating 和 symbol address expression。整数与浮点 expression
必须进入相应类型类别，symbol address 只能初始化 `.u32/.u64`；initializer symbol 必须
指向 function 或非 opaque 的 `.global/.const` variable。`.texref`、`.samplerref` 与
`.surfref` identity 不能成为 initializer address，即使经由 `generic()`、byte-mask 或算术
wrapper 也不例外。`generic()` 与 mask operator 作为 initializer operator 处理，而不是普通
function call；该限制不影响允许的 opaque handle `mov` retrieval。

## Unified UUID attribute

对于已建模的 `.unified(upper, lower)` attribute，declaration semantics 将两个 direct
integer token 都解码为精确的 unsigned 64-bit UUID half。十进制、前导零八进制、二进制、十六进制及
支持的 unsigned suffix 沿用 integer-literal rule；overflow 会指向出错 token，而不会截断。
该检查同样适用于合法的 global variable 与 device function definition/prototype，并发生在
storage metadata 或未来 function metadata backend 消费该值之前。placement、PTX-version 与
target check 仍是独立的 declaration rule。

## Redeclaration

Recursion legality 在 module validation 中、binding/visibility/signature/table join 后
检查 current typed call。`resolveModuleOnly` 仍是 transport，成功本身不验证 recursion
support。已识别 direct/finite-candidate cycle 要求 PTX 2.0 / SM 20；open call 不被认证为
acyclic。参见 [control flow](control_flow_syntax_design.md)。

module scope 的同名 item 先由 binding 合并到稳定的 `SymbolId`，再由本 pass 判断是否合法：

- 多个签名相同的 `.extern` variable declaration 合法；
- 签名相同的 `.func` prototype 可以与至多一个 definition 合并；
- variable 的 state space、alignment、vector/base type、parameterized count 与 array
  shape 必须兼容；
- function kind、`.noreturn`、return/input parameter interface 与 linkage 必须兼容；
- symbol kind 冲突、linkage 冲突、签名变化和多个 definition 均产生带 previous range 的
  诊断；
- `.extern .func` 只能是 prototype，不能带 body。

`.weak` 在所有 target 上要求 PTX 3.1。Source pass 在每个 typed variable/function
occurrence 的 weak qualifier 处检查，包含未使用 prototype。Binding 保留首个 canonical
linkage；成功 module owning handoff 前，兼容 redeclaration 必须具有相同 linkage 与
signature。Mixed weak/nonweak declaration 仍不兼容。来自已拒绝 mixed source 的
binding-only table 不保留每个历史 qualifier；不新增 occurrence-history API 或 weak linker 行为。

redeclaration 的 alignment 按 effective value 比较，但每个 declaration occurrence
仍保留 source provenance 与 range。对于已建模的 fundamental storage，省略的
alignment 是 scalar byte size（scalar array 也相同），或完整 `.v2`/`.v4` element
width（vector array 也相同）。显式且有效的 alignment 按解码后的整数值比较，
因此等值的八进制与十进制拼写相匹配。unsupported layout 与 invalid alignment syntax
不会被猜测出 default。parameterized count 和 ABI-preserve count 也按解码后的整数值比较。

验证注记：CUDA 13.1 `ptxas` V13.1.115 接受 `.version 8.0` / `.target sm_80` /
`.address_size 64` fixture 中 scalar、scalar-array、byte-array、`.v2 .u32` 与 `.v4
.u32` external declaration 的 matching implicit/explicit form，且两种 declaration
order 都成功。对于 `g` scalar fixture，whole-program compilation 精确给出警告
`ptxas warning : Unresolved extern variable 'g' in whole program compilation, ignoring
extern qualifier`；因此该比较是带此警告的成功，而非 warning-free。相同 assembler 以
`Alignment must be a power of two` 拒绝 `.align 3`。

function prototype 与 definition 各自仍拥有 lexical scope。function symbol 的
`owned_scope` 优先指向 definition scope，从而使后续 module resolution 使用 definition
中的 parameter/local declaration。

## Control-flow metadata

同一 pass 还检查 function-local 的 indirect-control metadata。`.calltargets` member 必须是
此前已声明的 device `.func`；重复 member 会以两个 member range 诊断，所有有效 member 必须有
相同的 canonical `FunctionSignature`。`.branchtargets` member 必须是所属 function 中的 label，
允许 forward label。`N<5>` 这样的 compact entry 会基于已有 local label 检查，不创建 synthetic
symbol；缺失 label 使用 compact-entry range 诊断。分支表是有序的索引序列：允许显式目标
重复、compact 与显式目标交叠，以及 compact entry 之间交叠，不对这些目标去重。

`.callprototype` 拒绝同时出现 return parameter 与 `.noreturn`，对 formal 使用既有
alignment/array-extent 检查，并要求 array formal 使用 `.param`。duplicate declaration label 仍由
binding 负责。module resolution 会把有效 prototype 转为与 function 相同的 canonical signature，
并复用已验证的首个 `.calltargets` member signature 进行 indirect-call ABI checking。
已建模 ABI suffix availability 由 module validation 检查；仍受[当前边界](converage/coverage_status.md)限制。

普通 storage 仅在作为 indirect-call flist 消费时检查 call-table 适用性。module 层复用
owned sparse storage relocation 以及本 pass 完整的 `FunctionSignature` equality，保留
精确 token 的 prior-function 规则。允许表 target 重复和隐式 zero tail，刻意区别于
`.calltargets` 的 duplicate policy。未被消费的 mixed function/numeric 或 mixed-signature
data 仍是普通合法 storage。

## 参数声明

参数另有按上下文区分的[覆盖与验证契约](parameter_declarations.md)，包括支持的类型、
unsized array 位置、pointer attribute，以及可静态判定的 ISA version/target 与 entry
大小限制。该契约适用于 entry/device header、call-prototype formal 与 body-local `.param`
声明；保留 type spelling 或 array syntax 本身不表示声明合法。

Parameter element classifier 复用 fundamental metadata 并排除 predicate，
保留 native scalar F16x2，不展开其 packed component。已验证 vector lane 参与
checked object bytes、自然 alignment 与 padded entry total；未使用的 prototype
signature 同样检查 shape、alignment 和 role。

## Entry resource constraint

对已支持的 entry-header `.maxnreg`、`.maxntid`、`.reqntid` 与 `.minnctapersm`，本 pass
以已声明的 module `.version` 检查最低 PTX 版本（依次为 1.3、1.3、2.1 与 2.0）。四者均支持所有 SM，
故此处不额外检查 `.target`。同一 entry 中 `.reqntid` 与 `.maxntid` 互斥；诊断指向后出现的
directive，并以先出现的 range 为上下文。单独的 `.minnctapersm` 在 PTX 中是 warning 而非 error；
warning severity、backend resource feasibility 与数值上限仍不属于本 pass。

## Debug metadata 边界

`.file`/`.loc` 与 `.debug_str` 的 identity table 由 binding 负责：重复的规范化 file index 一律诊断，并保留当前/首次 range，
`.loc` file reference 必须解析，`function_name` 只能标识 `.debug_str` 自身或其中 raw label。
本 declaration pass 不增加 DWARF payload expression、source attachment 或 resource
feasibility semantic。

## 当前边界

declaration 检查之后，module resolution 将已支持的 storage form 投影到
[拥有自身数据的存储元信息](storage_declarations.md)，包含 checked byte extent 与 typed
initializer value/reference。即使 declaration checker 接受某个 expression category，
超出可表示范围的 form 仍可能在这一步产生 diagnostic。declaration category 与关联 range
会通过 `ResolveDiagnostic` 保留。

该 pass 不负责 opcode-specific instruction type checking，也不实现 link-time 的跨 module
symbol 选择。integer constant expression 当前覆盖已有 AST grammar，并按 PTX 的
`.s64/.u64` 类型传播规则求值；后续若增加新的 constant operator，需要同时扩展分类、
signedness 传播与求值逻辑。

二元位运算遵循 PTX 正文中的通常算术转换规则；汇总表的不同表述、可复现汇编
证据和工具版本限制见[符号性决策说明](bitwise_constant_policy.md)。

`FunctionParameterContract::array_extents` 是 formal 轴唯一的 normalized 表示。
合法数组按检查后的总元素数比较 ABI，rank 本身不是额外限制；无效 structural 轴与
溢出仍无效并保留逐轴 identity。未知外轴仍保留并检查已知内轴。
派生 `CallArgumentProperties::array_size` 表示总数，不是第一轴缓存。
精确 source identity 独立保留，见[parameter 合同与 member migration](parameter_declarations.md)。

Register-state device/call-prototype formal 使用独立的 default-ABI 下限：已知
PTX >=2.0 且当前可识别 target 为 SM20+ 时，整个 scalar/vector 值必须至少32bit，
不是每个 lane 都要求32bit；source type/shape 保持不变。未使用的输入和返回 formal
同样检查。Source-aware retarget 使用所提供 AST 的 context，AST 销毁后使用 owned
region；已知 legacy context 不应用该下限，缺失 context 在 partial checking 中延后。
普通小宽度/predicate register 与传给匹配 `.param` formal 的小 actual 仍合法，
完整边界见[parameter 声明](parameter_declarations.md)。

按[上下文 keyword 解释](lexer_design.md)，instruction root 拼写在显式声明 name role
中仍可作为普通 identifier。官方 `bar` object/function 示例不支持无条件名称排除；
这是规范解释与 regression 澄清，不是补加 missing predicate 的代码修复。Source
validation 检查 name leaf，AST-free owned validation 通过共享字符 grammar 检查
actual bound symbol name；diagnostic reference spelling 不重建权威 identity。
独立的声明 role、type、ABI 与先行 visibility 限制保持不变。

现有 `ptx_declaration_semantics.hpp` 的公共
`declaration_semantics::languageCode(std::string_view)` 同步借用一个原始 operand token，
返回 `optional<uint64_t>` 且不保留 view。source item 检查、ordered redeclaration
compatibility 与 owned function integrity 统一调用该 decoder。quoted name
`unknown`、`ptx`、`nvvm`、`cuda c++`、`cuda c++ tile`、`tile ir`、`python-cutile`、
`fortran`、`optix` 分别映射 0、3、4、5、6、7、8、9、10，以确定性 ASCII folding 比较。
完整 unsigned integer token 0..10（含 Reserved 1/2）支持既有 radix 与 U suffix。
不 trim、求值 expression、将 quoted number 转为 code 或接受 unquoted name alias。
只移除外层 quote，不解码 inner escape bytes 来扩展名称 alias；这是 frontend decoder
边界，不是一般 PTX escape 禁令。directive 省略/default `ptx` 由 caller 处理，empty
token 返回无 code。raw list 的顺序、quote 与 case 仍作为 owned data 保存，不新增 decoded cache。


## 历史 entry body input

PTX 1.0–1.3 entry body `.param` 在唯一 `parameter_declarations` 表中保留
`LegacyEntryInput` role，以及 scope/group/array identity。Signature 仍只描述语法
header ABI，空 header 不等于没有 host input；lexical metadata 顺序不分配 launch
地址或 packing。只有 entry body role 歧义才要求 version 并报告
`MissingValidationContext`，不按 SM 猜 role。已 typed module 缺 context 时保留 role。

`ResolvedSymbolRef::parameter_role` 从 actual record 同步派生，structural kind 仍为
`CallParameter`。Address/MOV/tensor 共用 input/return/call-slot 方向映射。两个
validator 都在 context skip 前重连当前 record/cache，检查 readonly、entry frame、
direct call-actual 排除、shape/scope 和 logical capacity。Legacy LD 不要求 call
return staging，MOV 保持 `.param`；现代 BodyLocal staging 不变。Checked logical
总量使用 per-member bytes 乘 compact count，检查32764-byte已知 global ceiling，
不重复乘 axes/vector，也不套用1.4的256-byte header cap。
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` 对精确历史 physical launch packing
仍 qualified。详见[parameter contract](parameter_declarations.md)的窗口与边界。


### 已弃用的最大 CTA 提示

Entry header 从 PTX ISA 1.3 起接受 `.maxnctapersm ncta`，适用于所有已识别
source target，包含 PTX 2.0 及后续版本；弃用不表示版本上界。独立的 AST/owned
`MaxNctaPerSm` kind 表达优化提示：仍可执行额外 CTA，因此不是 runtime occupancy
硬上限。`.maxntid` 可省略，`.minnctapersm` 可作为独立 resource 同时存在。

完整 unsigned integer token 经检查后进入现有 `uint32_t` resource value，允许
零及 `UINT32_MAX`，溢出不截断。该范围仅为 frontend 已建模容量；公开语法未指定
此位宽，检查不证明硬件 occupancy 或 driver 可执行性。CST/AST 存活时保留书写的
radix、suffix 和 range；owned contract 仅保留 kind、normalized values、explicit
count 和 range，不增加平行 raw spelling 字段。

两个 validator 都检查当前 payload shape、entry placement 和已知 range containment。
已知 PTX 版本低于 1.3 时，即使 target 不完整也拒绝；`AvailableContext` 缺少版本时
只延迟 availability，intrinsic 检查继续。Source-aware 路径采用 supplied effective
version。当前 numeric value 的一致修改合法；替换 AST 中书写的 resource kind 或
value 仍受现有 source association 规则检查。

源码依据：[PTX ISA 最大 CTA 指令](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#performance-tuning-directives-maxnctapersm)。
