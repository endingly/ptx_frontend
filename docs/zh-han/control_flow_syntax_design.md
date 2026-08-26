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

indirect-call 的 target-set operand 必须指向 function-local `.callprototype` 或
`.calltargets` declaration。它们的 label，以及 `.branchtargets` label，现在都有稳定的
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
`bra` 仍只支持 direct form。

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

`ResolvedIndirectCallee` 现在表示一个 indirect-call component：non-predicate `.reg` target，或
function-local metadata label。module 中后者保留 `SymbolId`，并区分 `.callprototype` 与
`.calltargets`；standalone resolution 只保留 spelling。它不携带 signature 或 member list。module
resolution 将每个 function-local metadata `SymbolId` 索引到 canonical signature：`.callprototype`
转换自身的 return/input contract（包括 `.noreturn`），`.calltargets` 则复用 declaration semantics
已经验证的首个 member signature。direct 与 indirect call 随后共享同一 arity、literal typing 与
argument-compatibility 检查；后者的 diagnostic 会指出 metadata label。C03 的临时 fallback 只保留给
未匹配任何 descriptor 的 malformed metadata-bearing call syntax。

## function-local `.callprototype` 语法

Parser 现在将 PTX 9.3 的 `.callprototype` declaration 保留为专用的 function-body
CST/AST node，而不是 label 加 instruction。支持四种 signature form：`_`、`_ (params)`、
`(return) _` 和 `(return) _ (params)`。CST 保留 label、colon、sink、parameter-list
punctuation、`.noreturn`、`.abi_preserve N` 和 `.abi_preserve_control N`；AST 保留相应的
semantic spelling 与 source range。declaration semantics 会拒绝 return parameter 与 `.noreturn`
同时出现，并检查 array formal；module scope 会明确拒绝该 grammar。binding 负责 local label；I06
可以保留其 resolved identity，但 instruction layout 与 ABI use 仍留给后续工作。

## function-local `.calltargets` 语法

PTX 9.3 `.calltargets` 同样使用专用的 function-body CST/AST node。它保留 label、colon、
directive、非空且有序的 function-name list、comma、semicolon，以及整体/member source range。
Parser 会拒绝 empty list、trailing comma、缺少 local label 或 module scope 中的使用。declaration
semantics 要求每个 member 是此前声明的 device `.func`，以逐项 range 诊断 duplicate，并要求
canonical signature 相同。

## function-local `.branchtargets` 语法

`.branchtargets` 现在有自己的 function-body CST/AST node。其非空且有序的 list 会保留
普通 label 与 `N<5>` 这样的 compact entry；后者保留 name、count、angle punctuation 和单项
range，不会展开为 synthetic label。declaration semantics 在不增加 symbol 的前提下检查 local label
membership、compact overlap 与 count validity。`brx.idx` 以 stable local identity 使用该
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
predicate，并紧跟在返回同一 variable 的 call 后。label、declaration 或其他 instruction 会打断该块。
`call` 自身可以带 predicate。`.uni` 仅保留为程序员的 uniformity assertion，frontend 不尝试不可
证明的 static uniformity analysis，也不会因 predicated direct `call.uni` 而拒绝它。
