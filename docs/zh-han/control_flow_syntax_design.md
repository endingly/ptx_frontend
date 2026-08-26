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
literal 在后续 call-signature pass 给出类型之前保持 untyped。

本切片只解析 direct named-function call。`.reg` target 或第四个 `CallTargetSet` operand 会给出
明确拒绝：indirect call 需要尚未建模的 `.calltargets/.callprototype` metadata。direct function
signature/ABI 对比也暂缓；现有 declaration pass 没有可复用的 call-contract 表示。
