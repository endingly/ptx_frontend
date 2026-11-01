# Control-flow Operand Syntax 设计

## 为什么不是普通 operand list

`bra`、`brx.idx` 与 `call` 的 operand 不是通用 comma-separated flat list。最新版 PTX 中，direct
`bra` 只有一个 label target；`brx.idx` 有 register index 和 target-list declaration；`call` 则由可选 return parameter group、callee、可选 input
parameter group，以及 indirect call 可用的 target-set/prototype symbol 组成。

因此 frontend 不再把 call 的括号组伪装成 vector pack，也不把 branch label 保留为无角色
的 identifier。CST/AST 提供以下专用节点：

- `CstCallParameterList` / `AstCallParameterList`，并区分 `Return` 与 `Input`；
- `CstCallTarget` / `AstCallTarget`；
- `CstCallTargetSet` / `AstCallTargetSet`；
- `CstBranchTarget` / `AstBranchTarget`。
- `CstBranchTargetSet` / `AstBranchTargetSet`。

CST 保留括号、组内 comma 和 operand 间 comma；Syntax AST 删除 punctuation，但保留每个
参数组的角色与 range。return group 必须恰有一个 identifier；input group 可以为空，成员
当前为 identifier 或 immediate。

## Binding

专用节点映射到独立 `ReferenceKind`：call target、return parameter、argument、target set 和
branch target。binding 会检查当前已可判定的 symbol kind：

- direct callee 必须是 function，indirect callee 可以是 `.reg` function pointer；
- call return/input identifier 必须是 `.reg` 或 `.param` variable/parameter；
- direct branch target 必须是当前 function scope 的 label。
- `brx.idx` target list 必须是当前 function scope 的 `.branchtargets` declaration。

indirect-call 的 target-set operand 指向 function-local `.callprototype`、
`.calltargets` declaration，或受支持的词法可见 call-table array。它们的 label，以及 `.branchtargets` label，现在都有稳定的
function-scope symbol。declaration semantics 会检查 metadata member 与 target-set signature；
generated instruction layout 与 normal module metadata use 现已通过各自 descriptor resolve。

## Descriptor 与 Resolved IR 边界

`OperandSyntaxShape` 已提供 `Group`、`CallTarget`、`CallTargetSet`、`BranchTarget` 与 `BranchTargetSet`，Python
descriptor model 和 C++ backend domain 使用相同 bit。`bra` 只有一个 direct label target，
因此合法地使用现有 `Flat` layout，并已进入 YAML database、统一 dispatch 与 checker。
`ResolvedBranchTarget` 在 module resolution 中保存当前 function label 的稳定 `SymbolId`；
standalone resolution 只保存源码 spelling。`.uni` 和 execution predicate 也分别作为
generated modifier field 与 opcode 公共字段保留。

`brx.idx{.uni} index, tlist` 是独立的 PTX 6.0 / SM 30 opcode。其 index 是 `.u32`
register，`tlist` resolve 为 `ResolvedBranchTargetSet`，保留当前 function `.branchtargets`
的 `SymbolId`；standalone resolution 仅保留 spelling。它不会展开 target entry 或构建 CFG；
在 module 中 `.branchtargets` declaration 必须在同一 function 的 lexical traversal 内早于
其 use；其中 member label 仍可 forward reference。direct `bra` target 仍允许 forward
reference，且 `bra` 仍只支持 direct form。

`call` 现在使用非 `Flat` 的 `Call` layout algorithm。一个 generated direct variant 固定有
三种 payload layout：仅 target、target 加可变 input group、return group 加 target 加 input
group。layout 选择时会检查 group role，因此 return group 不会匹配 input 位置。
`ResolvedFunctionRef` 保存已绑定的 direct target，`ResolvedCallParameterRef` 保存每个
`.reg/.param` 的 identity、type 与 state space，`ResolvedCallArguments` 保存逐项 range。
standalone instruction resolution 没有 callee declaration context，因此其中的 literal 保持
untyped。

indirect form 使用独立的 `IndirectCall` layout，而不复用 direct `Call` 或 `Flat`：target 加
metadata、target 加 input group 加 metadata、return group 加 target 加 input group 加 metadata。
register target 与最终 metadata operand 都 resolve 为 `ResolvedIndirectCallee`；layout slot shape 则
区分 `CallTarget` 与 `CallTargetSet`。这些 layout 要求 PTX 2.1 与 SM 20。为保持兼容，公开
modifier variant 仍名为 `call_direct`。

`ResolvedIndirectCallee` 表示一个 indirect-call component：non-predicate `.reg` target、
function-local metadata label，或下文的 call-table reference。module 中 metadata label 保留 `SymbolId`，并区分 `.callprototype` 与
`.calltargets`；standalone resolution 只保留 spelling。它不携带 signature 或 member list。module
resolution 将每个 function-local metadata `SymbolId` 索引到 canonical signature：`.callprototype`
转换自身的 return/input contract（包括 `.noreturn`），`.calltargets` 则复用 declaration semantics
已经验证的首个 member signature。direct 与 indirect call 随后共享同一 arity、literal typing 与
argument-compatibility 检查；后者的 diagnostic 会指出 metadata label。未匹配任何 descriptor
的 malformed metadata-bearing call syntax 现由通用 descriptor layout diagnostic 处理。

## Call-table flist array

既有 indirect-call layout 也接受最终 storage 名称，例如
`call %fp, (1+2), table;`。独立的 `ResolvedCallTableRef` branch 保存真实 variable
`SymbolId`、书写名称和 range，不将 storage 改标为 metadata。module 为每个被消费的表
拥有一个 `ResolvedCallTableContract`，以其公共完整 function signature 复用正常
return/input ABI 以及 formal-driven literal/expression 检查。

支持 module 或词法可见 local scope 的已初始化一维 scalar `.u32/.u64`
`.global/.const` array；允许 inferred extent，element width 无须等于 function-pointer
width。显式 entry 必须是完整、无 mask、zero-addend 的 device-function relocation。
保留顺序与重复 target；省略的尾部是 zero-fill，不是 callable slot，且至少需要一个
callable entry。numeric/mixed、masked/addended、nonfunction、vector、多维、parameter
和 extern-only 表不支持作为 flist，但不因此使无关普通 data 非法。

表 declaration 必须先于 call。函数引用继续使用精确 token 的 prior-declaration 规则，
包括 alias occurrence 和兼容 prototype/definition identity。所有 target 的完整 normalized
ABI 与 `.noreturn` 必须一致。不证明 loaded-pointer membership 或 mutable runtime contents。
已保存的 ptxas 13.3.33 compile-only 证据接受 partial/duplicate 表，但 mixed function/numeric
call control 会导致 assembler crash；frontend 对该 unsupported subset 正常诊断，不声称
assembler parity 或 GPU execution。

## Return uniformity

`ret{.uni};` 在 device function 和 entry 中均支持无 predicate、`@p` 与 `@!p`
形式，沿用 PTX 1.0 / 全架构的 availability。规范 variant `ret_bare` 与生成的
`RetBare` 类保持稳定。其 `WithLocs<bool> uni` 字段默认是 `false` 且没有 location；
显式 `.uni` 将它设为 `true`，并保留 modifier 的精确 range。execution predicate
独立保留其 negation 和绑定的 register identity；两者在 AST 释放后仍保留，并支持
owned-module validation。`.uni` 记录程序员对 uniform return behavior 的保证，
frontend 不证明运行时 divergence 或 convergence。未知或重复的 modifier 及意外
operand 仍会被拒绝。

## function-local `.callprototype` 语法

Parser 现在将 PTX 9.3 的 `.callprototype` declaration 保留为专用的 function-body
CST/AST node，而不是 label 加 instruction。支持四种 signature form：`_`、`_ (params)`、
`(return) _` 和 `(return) _ (params)`。CST 保留 label、colon、sink、parameter-list
punctuation、`.noreturn`、`.abi_preserve N` 和 `.abi_preserve_control N`；AST 保留相应的
semantic spelling 与 source range。declaration semantics 会拒绝 return parameter 与 `.noreturn`
同时出现，并检查 array formal；module scope 会明确拒绝该 grammar。binding 负责 local label；I06
保留其 bound identity；当前 indirect-call layout 消费该 prototype identity，
并复用公共 call ABI contract。entry point 与边界见 [Resolved IR](resolved_ir_design.md)。

current owned 与 supplied prototype contract 独立检查 base PTX 2.1 / SM 20、
`.noreturn` PTX 6.4 / SM 30，以及两个 ABI suffix 的 PTX 9.0 / SM 80 minima，
包括各自独立已知的 partial context。nested 与 unused prototype 也参与检查；
不因此扩大普通 instruction availability 或 runtime ABI behavior 的保证。

## function-local `.calltargets` 语法

PTX 9.3 `.calltargets` 同样使用专用的 function-body CST/AST node。它保留 label、colon、
directive、非空且有序的 function-name list、comma、semicolon，以及整体/member source range。
Parser 会拒绝 empty list、trailing comma、缺少 local label 或 module scope 中的使用。declaration
semantics 要求每个 member 是此前声明的 device `.func`，以逐项 range 诊断 duplicate，并要求
canonical signature 相同。

`.branchtargets` 与 `.calltargets` 均要求 PTX 2.1 和 SM 20，包含 nested block
中未使用的 declaration。AST-free validation 使用 function 选中的 owned region；
source-aware validation 采用 supplied active context，分别检查 current record 与
supplied typed declaration。已知 version 与可识别 profile 的下限在 full-context 门槛前
独立检查；只有未知 prerequisite 在 `AvailableContext` 中延后。Strict context、unknown-target
诊断、nonneutral payload identity、member binding、signature 与 declaration-order
检查均保留既有契约。

## function-local `.branchtargets` 语法

`.branchtargets` 现在有自己的 function-body CST/AST node。其非空且有序的 list 会保留
普通 label 与 `N<5>` 这样的 compact entry；后者保留 name、count、angle punctuation 和单项
range，不会展开为 synthetic label。declaration semantics 在不增加 symbol 的前提下检查 local label
membership 与 count validity。重复目标（包括交叠的 compact entry）保留原有顺序及重复次数；
展开后的每次出现均占据独立索引槽。`brx.idx` 以 stable local identity 使用该
declaration，但不会展开其中 entry。

对于 module 中的 direct named call，resolution 会查找 callee 的 canonical
prototype/definition signature，按顺序比较 return/input 的数量；随后复用 call-argument
compatibility contract 检查 `.reg/.param` type 与 vector shape、`.param .b8` array 的
extent/alignment，以及 pointer state-space/alignment。每个 input literal 都按对应 formal 定型，
literal kind 或 overflow 在该 literal 位置诊断。该检查属于 module resolution，不属于 generated
single-instruction checker。

## PTX 9.3 call parameter context

`ld` 接受 `.param`、`.param::entry` 与 `.param::func`；`st` 接受 `.param` 与
`.param::func`。`::entry` 只能访问 entry 的 formal input；`::func` 只能访问 device-function
parameter 或 function-local call parameter。未限定的拼写在 entry 中默认访问 entry parameter，
在 device function 中默认访问 function parameter，并可在两种上下文中访问 function-local call
parameter。

只有 function-local `.param` variable 是 call staging。其 `st.param` input store 必须不带
predicate，并与使用同一 variable 的 call 形成紧邻的连续块；其 `ld.param` return load 也必须不带
predicate，并紧跟在返回同一 variable 的 call 后。非执行的 declaration、label、metadata 和词法
花括号对 instruction 邻接透明；中间的可执行 instruction 会打断该块。binding/scope 规则仍要求
引用同一个可见的 parameter object/member。这是局部顺序检查，不推断运行时路径上的初始化。
`call` 自身可以带 predicate。`.uni` 仅保留为程序员的 uniformity assertion，frontend 不尝试不可
证明的 static uniformity analysis，也不会因 predicated direct `call.uni` 而拒绝它。

Register-state device/call-prototype formal 使用独立的 default-ABI 下限：已知
PTX >=2.0 且当前可识别 target 为 SM20+ 时，整个 scalar/vector 值必须至少32bit，
不是每个 lane 都要求32bit；source type/shape 保持不变。未使用的输入和返回 formal
同样检查。Source-aware retarget 使用所提供 AST 的 context，AST 销毁后使用 owned
region；已知 legacy context 不应用该下限，缺失 context 在 partial checking 中延后。
普通小宽度/predicate register 与传给匹配 `.param` formal 的小 actual 仍合法，
完整边界见[parameter 声明](parameter_declarations.md)。

局部 `ResolvedCallPrototypeContract` 描述 device-call interface，因此 owned
`signature.is_entry` 必须保持 false。AST-free module validation 即使在 prototype
未使用时也检查该 role，包括 type/width 其他条件均合法的 return-only interface。
Module/function copy 与 instruction clone 保留 prototype identity。该 metadata-integrity
规则独立于 formal ABI width、feature availability 与 supplied-source association；
不重建 source spelling/history 来检查该 flag。

## 静态 recursion 边界

已知 local call cycle 要求 PTX 2.0 与 SM 20；acyclic device function 在支持的旧 context
仍合法。两个 module validator 均重算私有 current typed may-call graph，按 canonical local
device definition 建图，包含 unused definition。实际 direct call 是 exact edge；只有实际
consumed 且验证过的 finite `.calltargets`/call-table alternative 才是 candidate edge。
Unused metadata 与 implicit-zero table tail 不建边。依赖 finite alternative 的 cycle 是
statically possible recursion，不证明会执行。Predicated call 仍是 may edge；不做 CFG、
predicate feasibility、stack execution 或 dataflow analysis。

参与 caller 使用 selected region，source-aware API 则使用 supplied active context。
已知旧 PTX 或可识别低 SM 独立拒绝，不能被另一缺失事实遮蔽；只有未知 prerequisite 可在
`AvailableContext` 中延后。Strict context 与 unknown-target 行为不变。Prototype-only、external
callee 和 dynamic signature-only call 是 open boundary；不伪造返边，也不因 strict 而一律拒绝。
成功只表示 modeled check 通过，不是跨这些 open edge 的 whole-program acyclic 证明。
