# PTX 语法覆盖情况

## 用途

本矩阵描述 parser 已实现的行为，并不表示已经完整支持 PTX ISA。语法基准为 NVIDIA
[PTX ISA 文档](https://docs.nvidia.com/cuda/parallel-thread-execution/)。

| 范围 | 状态 | 当前实现子集 |
| --- | --- | --- |
| Token 与 trivia | 部分支持 | identifier、dot identifier、literal、标点、注释、空白与部分稳定 directive |
| 单 instruction fragment | 部分支持 | predicate guard、opcode/modifier、普通 operand、address、vector member/vector pack，以及 call/branch 专用 operand shape |
| Module header | 支持子集 | `.version`、`.target`、`.address_size` |
| Debug file directive | 支持子集 | outermost `.file file_index "filename"` 与可选且成对的 `, timestamp, file_size`；CST 无损、AST 保留 field range，duplicate index table 仍未支持 |
| Debug location directive | 支持子集 | function/nested-block `.loc file line column` 与成对的 PTX 7.2 `function_name`/`inlined_at` payload 进入无损 CST 和结构化 AST；`.file`/DWARF 验证及附着到 instruction/label 尚未支持 |
| Debug section directive | 支持子集 | outermost `.section name { ... }` 的匹配 brace 与有序 raw DWARF payload token 进入无损 CST 和带 range 的 AST；payload width、private label、relocation、`.target debug` 与 `.loc` offset 验证尚未支持 |
| Backend pragma directive | 支持子集 | module、`.entry` header 与 function/nested-block statement 的 `.pragma` 保留非空 comma-separated string list 到 CST/AST；pragma 不进入 binding 或 Resolved IR |
| Kernel resource directive | 支持子集 | entry header 的 `.maxnreg n`、`.maxntid nx[,ny[,nz]]`、`.reqntid nx[,ny[,nz]]` 与 `.minnctapersm ncta` 进入专用 CST/AST；declaration semantics 检查 source `.version` 最低版本，并拒绝同一 entry 同时使用 `.maxntid` 与 `.reqntid` |
| Function | 支持子集 | `.entry/.func` definition、`.func` prototype、visibility/linkage qualifier、返回与输入参数列表、`.noreturn` |
| Formal parameter | 支持子集 | `.reg/.param`、alignment、scalar type、pointer space/alignment，以及由结构化 constant expression 指定长度的 array |
| Variable declaration | 支持子集 | module/function scope、linkage qualifier、`.reg/.param/.local/.shared/.global/.const`、alignment、vector/base type、parameterized name、多维 array，以及 `.global/.const` initializer |
| Function body | 支持子集 | variable declaration、label、当前 instruction grammar，以及递归绑定的 nested block；resolution 会按源码顺序递归平铺 instruction，call staging 限于各 lexical block |
| Constant expression | 支持子集 | literal/symbol、括号、`.s64/.u64` cast、一元/二元/三元运算、`generic(symbol)` 与 mask initializer operator |
| Initializer | 支持子集 | scalar expression、递归 brace list、未定长首维；拒绝 `.extern`、parameterized name 及非 `.global/.const` initializer |
| Symbol binding | 支持子集 | module/function/nested-block scope、变量/参数/函数/label、lexical shadowing、parameterized member、instruction/initializer/dimension/control-flow reference；label 与 control-flow metadata 保持 function-local |
| Declaration 语义 | 支持子集 | 正整数 array extent、未定长首维推导、initializer type/brace shape/元素上限、symbol address、module linkage-compatible redeclaration，以及已支持 entry resource 的 version/conflict 规则 |
| 其他 directive | 尚未支持（直接拒绝） | module variable 与其他结构化 kernel-tuning directive；未建模 function-header token 不会静默进入 AST |
| 结构化控制语法 | 支持子集 | `.callprototype`、`.calltargets` 与 `.branchtargets` 均有专用 function-local CST/AST grammar；binding 与 declaration semantics 检查其 label/member/contract。generated `IndirectCall` layout 可在 PTX 2.1 / SM 20 解析 `.reg` target 加已绑定 prototype/target-set metadata，module resolution 会应用共享的 call ABI contract。`brx.idx` 可在 PTX 6.0 / SM 30 解析 `.u32` index 加当前 function `.branchtargets` identity；不会展开 target entry 或构建 CFG |
| 恢复与编辑 | 仅基础设施 | 公开 parser/lowering result 已携带有序 `DiagnosticCollection`；CST 已建模 inserted/skipped/error recovery node，但 parser 尚不产生它们。在 synchronization、多错误解析与 token edit 到来前，parser 仍 fail-fast 且只报告一条 diagnostic |
| Resolved opcode | 部分支持 | 仅支持 YAML database 中存在的 opcode；当前为 `add`、`sub`、`bar`、direct `bra`、indexed `brx.idx`、direct 与 descriptor-backed indirect `call`、部分 scalar/vector `mov`，以及 `.b8/.b16/.b32/.b64`、`.u8/.u16/.u32/.u64`、`.s8/.s16/.s32/.s64`、`.f32/.f64` 的 generic/basic-explicit scalar 与 braced-vector `ld`/`st`；module-resolved direct 与 indirect `call` 共享 canonical return/input ABI 检查，包括数量/顺序、`.reg/.param` type/shape、`.param .b8` array、pointer 和按 formal 定型的 literal。indirect signature 来自 local prototype 或已验证的 target set，且仍不支持 call-table。`brx.idx` 保留 target-list identity，不展开 metadata entry。PTX 8.8/SM 100 modern vector 只接受 `.v8` × 32-bit 或 `.v4` × 64-bit 的 256-bit payload，地址已知时要求 global，并可部分使用 `_` sink；legacy vector 仍最多 128 bit 且拒绝 sink。静态 natural alignment 会检查 bound data symbol 的常量 byte offset 和 absolute immediate，register/standalone unresolved address 保持 unknown。`ld/st` 支持 omission/显式 `.weak`、`.volatile`、带 scope 的 relaxed/acquire/release，以及 PTX 8.2 scalar `.mmio.relaxed.sys`，并由生成的 cross-modifier checker 约束；generic load 接受已知 `.const/.global/.local/.shared` space（`.const` 要求 PTX 3.1），generic store 接受 `.global/.local/.shared`，explicit form 要求精确匹配 runtime modifier；绑定的 `.param` load 要求 input parameter，store 要求 return parameter，并按 function context 检查 PTX/SM；load destination/store source register 以及 vector element 可在 bit/integer/float kind rule 下使用更宽声明，其余 typed operand 仍要求 same-width，未知 address identity 不推断 |

Lexer 能切分矩阵以外的源码，Syntax AST 也可能以文本形式保留未知 opcode；这两种情况
都不表示该结构能够 lower 到 Resolved IR。

## PTX 9.3 directive registry

此表逐 spelling 覆盖 PTX ISA Table 1 的 35 个 directive，以及该表遗漏的五项：5.4.8 的
`.attribute`、11.4 的 `.abi_preserve`、`.abi_preserve_control` 和 11.8 的
`.blocksareclusters`、`.language`。它回答 coverage matrix 的六个 pipeline 问题。legacy
非 dot spelling `@@dwarf` 与 `.ptr` 等 attribute 刻意不属于这份 dot-directive registry。

图例：`D` = 专用 lexer token；`G` = 通用 `DotIdent`（仍可 tokenize，但不表示 CST
支持）。`T` = typed directive CST/AST；`E` = 进入既有 declaration/function node；`R` =
parser 明确拒绝。`Y` = 在 binding/Resolved IR 阶段直接保留或使用；`I` = 仅 consumer
instruction 间接保留/check 该 identity；`C` = 直接 binding/declaration semantic check；
`S` = 向当前 semantic check 提供 source `.version`，而非 `checker::TargetInfo`；`—` =
该阶段无支持。

| Directive | Token | CST | Syntax AST | Binding | Resolved IR | Target / semantic | 明确边界 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `.address_size` | D | T | T | — | — | — | 仅保留 module syntax |
| `.alias` | G | R | — | — | — | — | 未建模 module directive |
| `.abi_preserve` | G | T | T | — | — | — | 仅 `.callprototype` suffix；`.func` header form 与 PTX 9.0 / SM 80 availability 尚未支持 |
| `.abi_preserve_control` | G | T | T | — | — | — | 仅 `.callprototype` suffix；`.func` header form 与 PTX 9.0 / SM 80 availability 尚未支持 |
| `.align` | D | E | E | Y | Y | C | declaration/parameter alignment |
| `.attribute` | G | R | — | — | — | — | 未建模 variable/function directive |
| `.branchtargets` | D | T | T | Y | I | C / I | declaration rule 直接检查；`brx.idx` consumer 要求 PTX 6.0 / SM 30 |
| `.callprototype` | D | T | T | Y | I | C / I | declaration rule 直接检查；indirect-call availability 由 consumer 驱动 |
| `.calltargets` | D | T | T | Y | I | C / I | declaration rule 直接检查；indirect-call availability 由 consumer 驱动 |
| `.common` | G | R | — | — | — | — | 未建模 declaration directive |
| `.const` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.entry` | D | E | E | Y | Y | C | 既有 function node |
| `.explicitcluster` | G | R | — | — | — | — | 未建模 entry-header directive |
| `.extern` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.file` | D | T | T | — | — | — | file table semantics 留给 C02 |
| `.func` | D | E | E | Y | Y | C | 既有 function node |
| `.global` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.local` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.loc` | D | T | T | — | — | — | attachment/file validation 留给 C02 |
| `.maxclusterrank` | G | R | — | — | — | — | 未建模 entry-header directive |
| `.maxnctapersm` | G | R | — | — | — | — | 未建模 deprecated resource directive |
| `.maxnreg` | D | T | T | — | — | C | 仅 entry；检查 source-version minimum |
| `.maxntid` | D | T | T | — | — | C | 仅 entry；与 `.reqntid` 冲突 |
| `.minnctapersm` | D | T | T | — | — | C | warning/device feasibility 留后续 |
| `.noreturn` | D | E | E | — | — | C | 支持 `.func`/`.callprototype`；检查 return-parameter conflict；PTX 6.4 / SM 30 availability 未查 |
| `.param` | D | E | E | Y | Y | C | 既有 variable/formal/call-parameter declaration |
| `.pragma` | D | T | T | — | — | — | backend string interpretation 刻意未实现 |
| `.reg` | D | E | E | Y | Y | C | 既有 variable/formal declaration |
| `.reqnctapercluster` | G | R | — | — | — | — | 未建模 entry-header directive |
| `.reqntid` | D | T | T | — | — | C | 仅 entry；与 `.maxntid` 冲突 |
| `.section` | D | T | T | — | — | — | 仅 raw DWARF payload；C02 负责 semantics |
| `.shared` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.sreg` | G | R | — | — | — | — | 未建模 special-register declaration |
| `.target` | D | T | T | — | — | — | 保留 module syntax；不是 checker context |
| `.tex` | G | R | — | — | — | — | 未建模 declaration directive |
| `.version` | D | T | T | — | — | S | 为已支持 resource 提供 source-version check |
| `.visible` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.weak` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.blocksareclusters` | G | R | — | — | — | — | 表外 PTX 9.0 entry / SM 90 directive |
| `.language` | G | R | — | — | — | — | 表外 PTX 9.3 entry/function directive |

## 实现优先级

[项目 roadmap](../../.agents/project_roadmap.md) 是实现状态、依赖和优先级的唯一权威来源。
本矩阵只记录能力边界，刻意不重复该排序。

PTX ISA 的 variable declaration 概述提到 optional fixed address，但当前规范没有给出独立
语法、约束或示例。frontend 不会据此发明语法；只有获得规范性 grammar 或可验证的
`ptxas` 行为后才会增加对应节点。
