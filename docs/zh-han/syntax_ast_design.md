# CST 与 Syntax AST 设计

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design) 为唯一维护依据；本文保留技术参考与历史证据。

## 手写 parser 实现

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--handwritten-parser-implementation)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#handwritten-parser-implementation)。

## 指令常量表达式

指令的数值位置复用声明常量表达式语法：括号、优先级、整数一元/二元/三元运算、
`(.s64)`/`(.u64)` cast，以及同类型 f64 算术与比较、整数条件的浮点三元表达式。
单个 literal 保留原有表示。`CstConstantOperand` 与 `AstConstantOperand`
独占 operation tree 和完整/子表达式 range；复制 wrapper 时深复制整棵树。
表达式匹配既有 Immediate syntax descriptor。call input、source tuple、
predicate constant 和 address 中的常量部分复用这一语法；register-only 位置
仍受 descriptor 限制。所有表达式入口共享 depth-128 预算及通常的 recovery 规则。
symbol address 与仅用于 initializer 的运算保持各自语义。
在 bracketed address 中，顶层 `+`/`-` 仍分隔 base 与 offset；base 中包含
加减或更低优先级运算时需加括号。精确 `0f` 单精度
bit-pattern 仍可作为 literal operand，但不能参与常量表达式。

## 前端分层

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--instruction-constant-expressions)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#instruction-constant-expressions)。

## 具名数组地址语法

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--named-array-address-syntax)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#named-array-address-syntax)。

## Named memory-vector 语法

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--named-memory-vector-syntax)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#named-memory-vector-syntax)。

## CST 的所有权与表示

当前接受条件与限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--cst-ownership-and-representation)；API、算法及证据见 [英文技术参考](../us-en/syntax_ast_design.md#cst-ownership-and-representation)。

## CST 到 Syntax AST lowering

`lowerSyntaxInstruction()` 与 `lowerSyntaxModule()` 是明确的 CST→AST 边界：

```cpp
auto ast = lowerSyntaxInstruction(cst);
auto module = lowerSyntaxModule(module_cst);
```

生成的 AST 不引用 CST 的 `TokenId`，所以 CST 销毁后 AST 仍可独立使用。resolve 所需
的 leaf spelling 会连同 `SourceRange` 一起复制到 AST。

`PtxSyntaxParser` 继续作为便利 facade；`parseInstruction()` 与 `parseModule()` 分别为
fragment client 与 module client 执行 source→CST→AST，并按顺序映射 CST/lowering diagnostic。

`AstFile` 采用相同的 root 区分方式，`AstModule` 则为 module directive 与 function
提供 typed container。`AstFunction` 当前包含 function 类别、qualifier、名称，以及由
`AstVariableDeclaration`、`AstLabel`、`AstCallPrototype`、`AstCallTargets`、
`AstBranchTargets`、`AstLocDirective`、`AstPragma`、`AstBlock` 与 `AstInstruction`
组成的有序 body variant；`AstBlock` 保留有序 body item 与完整 source range。`AstCallPrototype` 保留 label、sink、formal return/input payload，以及带
range 的 PTX 9.3 `.noreturn` / ABI-preservation suffix；
返回与输入 parameter 会保留 state space、alignment、type、pointer attribute、array
形式、名称与 range。`AstConstantExpression` 结构化表示 literal/symbol、括号、cast、
一元/二元/三元表达式和 initializer operator；`AstInitializer` 则区分 scalar expression
与递归 list。symbol identity 不直接写回 AST，而由独立 `SymbolTable` 通过 source range
关联，详见 `symbol_binding_design.md`。

## 收窄后的 Syntax AST 职责

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--narrowed-syntax-ast-responsibility)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#narrowed-syntax-ast-responsibility)。

## 尚待完善的位置模型

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--remaining-location-work)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#remaining-location-work)。

## Arithmetic-negated register 语法

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--arithmetic-negated-register-syntax)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#arithmetic-negated-register-syntax)。

### 历史调试序列化

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--legacy-debug-serialization)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#legacy-debug-serialization)。

### ASCII 空白与物理位置

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#syntax-ast-design--ascii-whitespace-and-physical-positions)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/syntax_ast_design.md#ascii-whitespace-and-physical-positions)。
