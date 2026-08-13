# PTX 语法覆盖情况

## 用途

本矩阵描述 parser 已实现的行为，并不表示已经完整支持 PTX ISA。语法基准为 NVIDIA
[PTX ISA 文档](https://docs.nvidia.com/cuda/parallel-thread-execution/)。

| 范围 | 状态 | 当前实现子集 |
| --- | --- | --- |
| Token 与 trivia | 部分支持 | identifier、dot identifier、literal、标点、注释、空白与部分稳定 directive |
| 单 instruction fragment | 部分支持 | predicate guard、opcode/modifier、普通 operand、address、vector member 与 vector pack |
| Module header | 支持子集 | `.version`、`.target`、`.address_size` |
| Function | 支持子集 | `.entry/.func` definition、`.func` prototype、visibility/linkage qualifier、返回与输入参数列表、`.noreturn` |
| Formal parameter | 支持子集 | `.reg/.param`、alignment、scalar type、pointer space/alignment、有长度与无长度 array |
| Variable declaration | 支持子集 | module/function scope、linkage qualifier、`.reg/.param/.local/.shared/.global/.const`、alignment、vector/base type、register bank 与多维 array |
| Function body | 支持子集 | variable declaration、label 与当前 instruction grammar |
| Declaration 扩展 | 尚未支持 | initializer、fixed address 与完整解析的 array constant expression |
| 其他 directive | 尚未支持 | debug、section、pragma、module variable 与结构化 kernel-tuning directive |
| 结构化控制语法 | 尚未支持 | nested scope 与由 directive 驱动的 control-flow metadata |
| 恢复与编辑 | 尚未支持 | missing token、recovery node、多错误解析与 token edit |
| Resolved opcode | 部分支持 | 仅支持 YAML database 中存在的 opcode；当前为 `add`、`sub`、`bar` |

Lexer 能切分矩阵以外的源码，Syntax AST 也可能以文本形式保留未知 opcode；这两种情况
都不表示该结构能够 lower 到 Resolved IR。

## 近期实现顺序

1. 增加 declaration initializer、fixed address 与 constant expression；
2. 增加 call 与 branch 的专用 operand grammar；
3. 表示 kernel tuning 与其余 module directive；
4. 等 declaration 与 parameter 语法稳定后建立 symbol table；
5. PTX module grammar 与 YAML instruction coverage 分别独立扩展。
