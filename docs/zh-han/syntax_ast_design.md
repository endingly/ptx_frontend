# Syntax AST 设计与源码保真边界

## 当前职责

当前 `syntax_ast` 是 lexer 与 Resolved IR 之间的语法结构层。它表达 opcode、modifier、
predicate、operand 及其语法形态，并保留用于诊断的 `SourceRange`、节点 text 与部分
leading trivia。

它的目标是支持：

- 语法错误与 resolve diagnostic 的定位；
- 基于 YAML descriptor 的 variant 与 operand-layout 选择；
- `Syntax AST -> Resolved IR` 的语义解析。

## 当前不保证源码保真

`syntax_ast` **不是 lossless AST，也不是 CST**。它不能作为 formatter 或源码重写的
底档，且不保证 `parse -> print` 得到逐字节相同的 PTX。

原因是 parser 会为 address、immediate、vector pack、predicate 等组合节点重建 text；
这些节点只保留首个 token 的 leading trivia。token 间的空白和注释、逗号/分号/括号周边
的 trivia，以及完整 token 边界不会稳定地保存在 AST 中。

例如下列源码中的注释和布局不能仅由当前 AST 还原：

```ptx
add /* opcode-type */ .u32 %r1 /* before comma */, %r2, 1 /* trailing */ ;
```

`AstSyntax::text` 的用途是诊断、词法 literal 解析与尚未绑定的标识符拼写；它不是格式化
接口，也不是已解析符号名。

## 与 lexer token 的关系

`PtxToken` 本身保留其 text 和 leading trivia，EOF token 承接尾部 trivia。因此 lexer
可以产生无损 token 序列。当前 parser 以流式方式消费该序列，但不会把完整 token 序列或
token span 保存到 `AstInstruction`；这正是源码保真在 AST 边界丢失的地方。

## 未来 CST 演进

当工程需要 formatter、自动修复或源码级重写时，引入独立的 CST 层：

```text
source -> token buffer -> CST -> Syntax AST -> Resolved IR
```

推荐的最小设计为：

- `SyntaxFile` 持有不可变 source 与完整 token buffer；
- CST 节点持有 token range，覆盖 opcode、modifier、operand、标点与 delimiter；
- `SourceRange` 扩展为 source id 与 byte offset，行列仅作为展示信息；
- Syntax AST 从 CST 投影得到，保留现有的 resolution-friendly 结构；
- formatter 与源码 rewrite 在 CST/token edit 层工作，Resolved IR 不反向承担源码布局。

在该层落地前，新增 AST 字段不应声称可支持无损 round-trip 或 formatter。
