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
descriptor model 和 C++ backend domain 使用相同 bit。现有 YAML opcode 都是 `Flat`
layout，`call/bra` 尚未作为不完整的 flat opcode 加入生成数据库。

下一阶段需要先引入能描述 call group/可变参数的 layout algorithm，并为 execution
predicate、function/label symbol target 和 call parameter 增加 binding-aware Resolved IR，
再把 control-flow opcode 接入统一 dispatch/checker。
