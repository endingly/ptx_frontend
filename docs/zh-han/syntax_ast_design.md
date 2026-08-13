# CST 与 Syntax AST 设计

## 前端分层

当前前端已经把具体源码表示与供 resolve 使用的语法模型分开：

```text
source -> lexer token buffer -> CST -> Syntax AST -> symbol binding -> Resolved IR
```

- CST 负责源码保真：token、标点、delimiter、注释、空白、原始拼写与 token range；
- Syntax AST 负责 instruction matching 与 resolve 所需的规范化 grammar shape；
- symbol binding 建立 module/function scope，并把 identifier reference 关联到声明；
- Resolved IR 负责选中的 variant、带类型的 modifier/operand、语义值和目标检查元数据。

## CST 的所有权与表示

公共 CST 头文件位于 `include/ptx_ir/cst`。`syntax_cst::CstFile` 持有完整
`PtxToken` buffer；其 `CstRoot` 区分独立 instruction fragment 与 `CstModule`。节点通过
`TokenId` 引用 file buffer，组合节点另外保存半开区间 `CstTokenRange`。

`CstModule`、`CstModuleDirective` 与 `CstFunction` 已建立 module-level 所有权，且不会
重复持有 token buffer。`parseModule()` 当前支持 `.version`、`.target`、
`.address_size`，以及函数体由现有 instruction parser 可处理指令构成的 `.entry/.func`
definition、`.func` prototype、结构化 formal parameter、通用 variable declaration 与
label。function
body 中的 instruction 由现有 instruction parser 处理。variable declaration 会结构化
保留 linkage qualifier、state space、可选 alignment/vector type、base type、逗号分隔的
名称、parameterized-name `<count>` 语法、多维 array declarator、可选等号与 initializer。
array dimension 与 scalar initializer 使用结构化 constant-expression tree；brace
initializer 则递归保留每一层花括号、元素和逗号。CST 同时保留 function
qualifier 与完整 header token sequence，并显式标记 entry/function 类别和函数名。

CST 明确保留逗号、分号、方括号、花括号、正负号、predicate 与 vector selector
token。每个 `PtxToken` 持有 leading trivia，EOF token 持有文件尾 trivia，因此
`CstFile::sourceText()` 可以逐字节还原已解析输入：

```cpp
PtxCstParser parser(source);
auto cst = parser.parseInstruction();
if (cst)
  assert(cst->sourceText() == source);
```

`parseInstruction()` 只接受一条完整 instruction fragment，`parseModule()` 则要求
module root。当前 module grammar 尚不接受 debug directive、
嵌套 statement scope、错误恢复节点、missing-token 插入或 token edit API。initializer
目前只做 grammar shape 和 state-space/linkage 约束；类型、array 维度及元素数量要在未来
的绑定/语义阶段校验。这些未实现部分不会被静默当成 instruction 解析。

## CST 到 Syntax AST lowering

`lowerSyntaxInstruction()` 与 `lowerSyntaxModule()` 是明确的 CST→AST 边界：

```cpp
auto ast = lowerSyntaxInstruction(cst);
auto module = lowerSyntaxModule(module_cst);
```

生成的 AST 不引用 CST 的 `TokenId`，所以 CST 销毁后 AST 仍可独立使用。resolve 所需
的 leaf spelling 会连同 `SourceRange` 一起复制到 AST。

`PtxSyntaxParser` 继续作为便利 facade；`parseInstruction()` 与 `parseModule()` 分别为
fragment client 与 module client 执行 source→CST→AST。

`AstFile` 采用相同的 root 区分方式，`AstModule` 则为 module directive 与 function
提供 typed container。`AstFunction` 当前包含 function 类别、qualifier、名称，以及由
`AstVariableDeclaration`、`AstLabel` 和 `AstInstruction` 组成的有序 body variant；
返回与输入 parameter 会保留 state space、alignment、type、pointer attribute、array
形式、名称与 range。`AstConstantExpression` 结构化表示 literal/symbol、括号、cast、
一元/二元/三元表达式和 initializer operator；`AstInitializer` 则区分 scalar expression
与递归 list。symbol identity 不直接写回 AST，而由独立 `SymbolTable` 通过 source range
关联，详见 `symbol_binding_design.md`。

## 收窄后的 Syntax AST 职责

Syntax AST 不再保存 trivia、标点 token 或组合 operand 的重建文本，只保留：

- opcode、modifier、identifier、literal 与 selector 的 spelling；
- immediate 的词法类别；
- predicate 是否取反；
- address base、offset operation 与是否有括号这一 grammar form；
- vector member/vector pack 结构；
- declaration array dimension、constant expression 与递归 initializer 结构；
- diagnostic 所需的 source range；
- generated layout descriptor 需要的 operand grammar alternative。

Syntax AST 不负责把 identifier 分类为 register/symbol/label/function，不应在选定 scalar
type 之前解码 literal，也不负责选择 instruction variant 或检查 PTX/SM availability；
这些仍属于 resolve/checker。

formatter、源码保真 rewrite 与未来自动修复必须工作在 CST/token buffer 上，不能从
Syntax AST 或 Resolved IR 反向恢复源码布局。

## 尚待完善的位置模型

`SourceRange` 目前只有行列信息。未来支持多文件 CST 与可靠 edit 时，应增加 source
identity 与 byte offset；这不需要重新扩大 Syntax AST 的职责。
