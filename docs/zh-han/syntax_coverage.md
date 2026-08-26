# PTX 语法覆盖情况

## 用途

本矩阵描述 parser 已实现的行为，并不表示已经完整支持 PTX ISA。语法基准为 NVIDIA
[PTX ISA 文档](https://docs.nvidia.com/cuda/parallel-thread-execution/)。

| 范围 | 状态 | 当前实现子集 |
| --- | --- | --- |
| Token 与 trivia | 部分支持 | identifier、dot identifier、literal、标点、注释、空白与部分稳定 directive |
| 单 instruction fragment | 部分支持 | predicate guard、opcode/modifier、普通 operand、address、vector member/vector pack，以及 call/branch 专用 operand shape |
| Module header | 支持子集 | `.version`、`.target`、`.address_size` |
| Function | 支持子集 | `.entry/.func` definition、`.func` prototype、visibility/linkage qualifier、返回与输入参数列表、`.noreturn` |
| Formal parameter | 支持子集 | `.reg/.param`、alignment、scalar type、pointer space/alignment，以及由结构化 constant expression 指定长度的 array |
| Variable declaration | 支持子集 | module/function scope、linkage qualifier、`.reg/.param/.local/.shared/.global/.const`、alignment、vector/base type、parameterized name、多维 array，以及 `.global/.const` initializer |
| Function body | 支持子集 | variable declaration、label 与当前 instruction grammar |
| Constant expression | 支持子集 | literal/symbol、括号、`.s64/.u64` cast、一元/二元/三元运算、`generic(symbol)` 与 mask initializer operator |
| Initializer | 支持子集 | scalar expression、递归 brace list、未定长首维；拒绝 `.extern`、parameterized name 及非 `.global/.const` initializer |
| Symbol binding | 支持子集 | module/function scope、变量/参数/函数/label、局部遮蔽、parameterized member、instruction/initializer/dimension/control-flow reference |
| Declaration 语义 | 支持子集 | 正整数 array extent、未定长首维推导、initializer type/brace shape/元素上限、symbol address，以及 module linkage-compatible redeclaration |
| 其他 directive | 尚未支持（直接拒绝） | debug、section、pragma、module variable 与结构化 kernel-tuning directive；未建模 function-header token 不会静默进入 AST |
| 结构化控制语法 | 尚未支持 | nested scope 与由 directive 驱动的 control-flow metadata |
| 恢复与编辑 | 尚未支持 | missing token、recovery node、多错误解析与 token edit |
| Resolved opcode | 部分支持 | 仅支持 YAML database 中存在的 opcode；当前为 `add`、`sub`、`bar`、`bra`、direct named-function `call`、部分 scalar/vector `mov`，以及 `.b8/.b16/.b32/.b64`、`.u8/.u16/.u32/.u64`、`.s8/.s16/.s32/.s64`、`.f32/.f64` 的 generic/basic-explicit scalar 与 braced-vector `ld`/`st`；direct `call` 保留可选 return/input group 与 untyped literal，但暂缓 signature ABI check，并拒绝 indirect target-list/prototype form。PTX 8.8/SM 100 modern vector 只接受 `.v8` × 32-bit 或 `.v4` × 64-bit 的 256-bit payload，地址已知时要求 global，并可部分使用 `_` sink；legacy vector 仍最多 128 bit 且拒绝 sink。静态 natural alignment 会检查 bound data symbol 的常量 byte offset 和 absolute immediate，register/standalone unresolved address 保持 unknown。`ld/st` 支持 omission/显式 `.weak`、`.volatile`、带 scope 的 relaxed/acquire/release，以及 PTX 8.2 scalar `.mmio.relaxed.sys`，并由生成的 cross-modifier checker 约束；generic load 接受已知 `.const/.global/.local/.shared` space（`.const` 要求 PTX 3.1），generic store 接受 `.global/.local/.shared`，explicit form 要求精确匹配 runtime modifier；绑定的 `.param` load 要求 input parameter，store 要求 return parameter，并按 function context 检查 PTX/SM；load destination/store source register 以及 vector element 可在 bit/integer/float kind rule 下使用更宽声明，其余 typed operand 仍要求 same-width，未知 address identity 不推断 |

Lexer 能切分矩阵以外的源码，Syntax AST 也可能以文本形式保留未知 opcode；这两种情况
都不表示该结构能够 lower 到 Resolved IR。

## 近期实现顺序

1. 完成 `ld/st` 其余跨 modifier
   规则；`.b128` 不属于当前 scalar family；
   function-local call-argument `.param`、`::entry`/`::func` 以及 call
   adjacency/predication 留到后续 call-context 工作；
2. 增加 call signature/ABI 对比及 indirect `.calltargets/.callprototype` metadata；
3. 表示 `.branchtargets` 及其余 module/function directive；
4. PTX module grammar 与 YAML instruction coverage 分别独立扩展。

PTX ISA 的 variable declaration 概述提到 optional fixed address，但当前规范没有给出独立
语法、约束或示例。frontend 不会据此发明语法；只有获得规范性 grammar 或可验证的
`ptxas` 行为后才会增加对应节点。
