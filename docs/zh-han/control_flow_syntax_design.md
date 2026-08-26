# Control-flow Operand Syntax 设计

## 为什么不是普通 operand list

`bra` 与 `call` 的 operand 不是通用 comma-separated flat list。最新版 PTX 中，direct
`bra` 只有一个 label target；`call` 则由可选 return parameter group、callee、可选 input
parameter group，以及 indirect call 可用的 target-set/prototype symbol 组成。

因此 frontend 不再把 call 的括号组伪装成 vector pack，也不把 branch label 保留为无角色
的 identifier。CST/AST 提供以下专用节点：

- `CstCallParameterList` / `AstCallParameterList`，并区分 `Return` 与 `Input`；
- `CstCallTarget` / `AstCallTarget`；
- `CstCallTargetSet` / `AstCallTargetSet`；
- `CstBranchTarget` / `AstBranchTarget`。

CST 保留括号、组内 comma 和 operand 间 comma；Syntax AST 删除 punctuation，但保留每个
参数组的角色与 range。return group 必须恰有一个 identifier；input group 可以为空，成员
当前为 identifier 或 immediate。

## Binding

专用节点映射到独立 `ReferenceKind`：call target、return parameter、argument、target set 和
branch target。binding 会检查当前已可判定的 symbol kind：

- direct callee 必须是 function，indirect callee 可以是 `.reg` function pointer；
- call return/input identifier 必须是 `.reg` 或 `.param` variable/parameter；
- direct branch target 必须是当前 function scope 的 label。

target-set/prototype 的 symbol kind 要等 `.calltargets/.callprototype` directive 进入 AST 与
symbol table 后才能完整校验；当前仍会保留 reference，并在没有 declaration 时给出明确的
unresolved target-set diagnostic。

## Descriptor 与 Resolved IR 边界

`OperandSyntaxShape` 已提供 `Group`、`CallTarget`、`CallTargetSet` 与 `BranchTarget`，Python
descriptor model 和 C++ backend domain 使用相同 bit。`bra` 只有一个 direct label target，
因此合法地使用现有 `Flat` layout，并已进入 YAML database、统一 dispatch 与 checker。
`ResolvedBranchTarget` 在 module resolution 中保存当前 function label 的稳定 `SymbolId`；
standalone resolution 只保存源码 spelling。`.uni` 和 execution predicate 也分别作为
generated modifier field 与 opcode 公共字段保留。

`call` 现在使用非 `Flat` 的 `Call` layout algorithm。一个 generated direct variant 固定有
三种 payload layout：仅 target、target 加可变 input group、return group 加 target 加 input
group。layout 选择时会检查 group role，因此 return group 不会匹配 input 位置。
`ResolvedFunctionRef` 保存已绑定的 direct target，`ResolvedCallParameterRef` 保存每个
`.reg/.param` 的 identity、type 与 state space，`ResolvedCallArguments` 保存逐项 range。
standalone instruction resolution 没有 callee declaration context，因此其中的 literal 保持
untyped。

本切片只解析 direct named-function call。`.reg` target 或第四个 `CallTargetSet` operand 会给出
明确拒绝：indirect call 仍需要尚未建模的 `.calltargets/.callprototype` metadata。

## function-local `.callprototype` 语法

Parser 现在将 PTX 9.3 的 `.callprototype` declaration 保留为专用的 function-body
CST/AST node，而不是 label 加 instruction。支持四种 signature form：`_`、`_ (params)`、
`(return) _` 和 `(return) _ (params)`。CST 保留 label、colon、sink、parameter-list
punctuation、`.noreturn`、`.abi_preserve N` 和 `.abi_preserve_control N`；AST 保留相应的
semantic spelling 与 source range。return parameter 是否与 `.noreturn` 冲突不在 parser 判断，
留给 declaration semantics。module scope 会明确拒绝该 grammar；本 issue 尚不 binding 或
resolve prototype label。

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
