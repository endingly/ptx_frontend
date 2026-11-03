# Control-flow Operand Syntax 设计

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design) 为唯一维护依据；本文保留技术参考与历史证据。

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
参数组的角色与 range。return group 必须恰有一个 identifier 或显式普通 register member；
input group 可以为空，成员为 identifier、selected member 或既有 numeric operand。

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

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--descriptor-and-resolved-ir-boundary)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#descriptor-and-resolved-ir-boundary)。

## Call-table flist array

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--call-table-flist-arrays)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#call-table-flist-arrays)。

## Return uniformity

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--return-uniformity)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#return-uniformity)。

## function-local `.callprototype` 语法

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--function-local-callprototype-syntax)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#function-local-callprototype-syntax)。

## function-local `.calltargets` 语法

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--function-local-calltargets-syntax)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#function-local-calltargets-syntax)。

## function-local `.branchtargets` 语法

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--function-local-branchtargets-syntax)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#function-local-branchtargets-syntax)。

## PTX 9.3 call parameter context

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--ptx-93-call-parameter-context)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#ptx-93-call-parameter-context)。

## 静态 recursion 边界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--static-recursion-boundary)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#static-recursion-boundary)。

## 历史 entry body input

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--historical-entry-body-inputs)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#historical-entry-body-inputs)。

### 间接函数地址 carrier

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--indirect-function-address-carriers)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#indirect-function-address-carriers)。

### call 的 selected scalar operand

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#control-flow-syntax-design--selected-scalar-call-operands)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/control_flow_syntax_design.md#selected-scalar-call-operands)。
