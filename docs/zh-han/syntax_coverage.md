# PTX 语法覆盖情况

## 用途

本矩阵描述 parser 已实现的行为，并不表示已经完整支持 PTX ISA。语法基准为 NVIDIA
[PTX ISA 文档](https://docs.nvidia.com/cuda/parallel-thread-execution/)。

| 范围 | 状态 | 当前实现子集 |
| --- | --- | --- |
| Token 与 trivia | 部分支持 | identifier、dot identifier、literal、标点、注释、空白与部分稳定 directive；未修改的 `CstFile::sourceText()` 会从 token buffer 逐字节 round-trip |
| 单 instruction fragment | 部分支持 | predicate guard、opcode/modifier、普通 operand、address、vector member/vector pack，以及 call/branch 专用 operand shape |
| Module header | 支持子集 | `.version`、`.target`、`.address_size` |
| Debug file directive | 支持子集 | outermost `.file file_index "filename"` 与可选且成对的 `, timestamp, file_size`；decimal/hex uint64 ID 在 debug-only namespace 中 binding，重复 ID 幂等且 overflow 会诊断 |
| Debug location directive | 支持子集 | function/nested-block `.loc file line column` 的 decimal/hex file ID 与成对 PTX 7.2 `function_name`/`inlined_at` payload 会验证已绑定 file ID 与 `.debug_str` section/label identity；不附着到 instruction，也不进入 Resolved IR |
| Debug section directive | 支持子集 | outermost `.section name { ... }` 的匹配 brace 与有序 raw DWARF payload token 会保留；`.debug_str` 与 raw `name:` label 会绑定为 debug identity，payload width、relocation 和 offset semantic 仍未支持 |
| Backend pragma directive | 支持子集 | module、`.entry` header 与 function/nested-block statement 的 `.pragma` 保留非空 comma-separated string list 到 CST/AST；pragma 不进入 binding 或 Resolved IR |
| Kernel resource directive | 支持子集 | entry header 的 `.maxnreg n`、`.maxntid nx[,ny[,nz]]`、`.reqntid nx[,ny[,nz]]` 与 `.minnctapersm ncta` 进入专用 CST/AST；declaration semantics 检查 source `.version` 最低版本，并拒绝同一 entry 同时使用 `.maxntid` 与 `.reqntid` |
| Function | 支持子集 | `.entry/.func` definition、`.func` prototype、visibility/linkage qualifier、返回与输入参数列表、`.noreturn` |
| Formal parameter | 支持子集 | `.reg/.param`、alignment、scalar type、pointer space/alignment，以及由结构化 constant expression 指定长度的 array |
| Variable declaration | 支持子集 | module/function scope、linkage qualifier、`.reg/.param/.local/.shared/.global/.const`、alignment、vector/base type、parameterized name、多维 array，以及 `.global/.const` initializer |
| Function body | 支持子集 | variable declaration、label、当前 instruction grammar，以及递归绑定的 nested block；resolution 会按源码顺序递归平铺 instruction，call staging 限于各 lexical block |
| Constant expression | 支持子集 | literal/symbol、括号、`.s64/.u64` cast、一元/二元/三元运算、`generic(symbol)` 与 mask initializer operator |
| Initializer | 支持子集 | scalar expression、递归 brace list、未定长首维；拒绝 `.extern`、parameterized name 及非 `.global/.const` initializer |
| Symbol binding | 支持子集 | module/function/nested-block scope、变量/参数/函数/label、lexical shadowing、parameterized member、instruction/initializer/dimension/control-flow reference，以及隔离的 debug file/string metadata identity；label 与 control-flow metadata 保持 function-local |
| Declaration 语义 | 支持子集 | 正整数 array extent、未定长首维推导、initializer type/brace shape/元素上限、symbol address、module linkage-compatible redeclaration，以及已支持 entry resource 的 version/conflict 规则 |
| 其他 directive | 尚未支持（直接拒绝） | module variable 与其他结构化 kernel-tuning directive，包括官方但未建模的 `.language`，会产生 recovery diagnostic，而不会静默进入 AST |
| 结构化控制语法 | 支持子集 | `.callprototype`、`.calltargets` 与 `.branchtargets` 均有专用 function-local CST/AST grammar；binding 与 declaration semantics 检查其 label/member/contract。generated `IndirectCall` layout 可在 PTX 2.1 / SM 20 解析 `.reg` target 加已绑定 prototype/target-set metadata，module resolution 会应用共享的 call ABI contract。`brx.idx` 可在 PTX 6.0 / SM 30 解析 `.u32` index 加当前 function `.branchtargets` identity；不会展开 target entry 或构建 CFG |
| 恢复与编辑 | 支持子集 | `parseModule()` 产生有序 diagnostic 和 inserted/skipped/error CST recovery node，并在有界结构/module anchor 处继续；partial nested block 会保留其合法 body，但没有 closing-brace token。standalone instruction parsing 保持 fail-fast。recovered module 只 lower 合法相邻 node；recovery marker 保持 CST-only，parser diagnostic 只一次、按 source order 返回。installed consumer 覆盖合法 PTX 9.3 directive text、semantic directive failure 与 recovered unknown directive。round-trip serialization 使用原始 token buffer 而非 recovery marker。可选 Clang lexer/CST libFuzzer target 有 GTest seed smoke，但尚未加入 ASan/UBSan 或 CI matrix |
| Resolved opcode | 部分支持 | 仅支持 YAML database 中存在的 opcode；当前为 bare `ret`、`exit` 与 `trap`（PTX 1.0 / all SM，无 modifier 或 operand；接受普通 predicate guard）、固定 `and.b32`/`or.b32`/`xor.b32` 和 `not.b32`（register 或 immediate source）、fixed `shl.b32` / `shr.u32`（32-bit count，接受同宽 bit/integer register 声明），以及冻结的单 predicate destination `setp.lt.u32` / `setp.lt.and.u32`（u32 register-or-immediate source，combine 可为 predicate 或 negated predicate）与 fixed `selp.u32`（u32 register-or-immediate data source 和一个不可 negated 的 predicate），以及 register-only `cvt.s32.u32`（两端接受 equal-or-wider register declaration）/ `cvt.rn.f32.f64`（PTX 1.0 / SM 13）、`add`、`sub`、`bar`、direct `bra`、indexed `brx.idx`、direct 与 descriptor-backed indirect `call`、部分 scalar/vector `mov`，以及 `.b8/.b16/.b32/.b64`、`.u8/.u16/.u32/.u64`、`.s8/.s16/.s32/.s64`、`.f32/.f64` 的 generic/basic-explicit scalar 与 braced-vector `ld`/`st`；module-resolved direct 与 indirect `call` 共享 canonical return/input ABI 检查，包括数量/顺序、`.reg/.param` type/shape、`.param .b8` array、pointer 和按 formal 定型的 literal。indirect signature 来自 local prototype 或已验证的 target set，且仍不支持 call-table。`brx.idx` 保留 target-list identity，不展开 metadata entry。PTX 8.8/SM 100 modern vector 只接受 `.v8` × 32-bit 或 `.v4` × 64-bit 的 256-bit payload，地址已知时要求 global，并可部分使用 `_` sink；legacy vector 仍最多 128 bit 且拒绝 sink。静态 natural alignment 会检查 bound data symbol 的常量 byte offset 和 absolute immediate，register/standalone unresolved address 保持 unknown。`ld/st` 支持 omission/显式 `.weak`、`.volatile`、带 scope 的 relaxed/acquire/release，以及 PTX 8.2 scalar `.mmio.relaxed.sys`，并由生成的 cross-modifier checker 约束；generic load 接受已知 `.const/.global/.local/.shared` space（`.const` 要求 PTX 3.1），generic store 接受 `.global/.local/.shared`，explicit form 要求精确匹配 runtime modifier；绑定的 `.param` load 要求 input parameter，store 要求 return parameter，并按 function context 检查 PTX/SM；load destination/store source register 以及 vector element 可在 bit/integer/float kind rule 下使用更宽声明，其余 typed operand 仍 same-width，未知 address identity 不推断 |

| 冻结的 M10 memory/atomic 子集 | 部分支持 | PTX 7.4 / SM 70 的 L1 eviction 与 PTX 7.4 / SM 80 的 L2 cache-hint `ld`/`st`；冻结的 `ldu`、`prefetch`、`membar`、`fence` 和 global relaxed-CTA scalar `atom`/`red` form。它们复用既有 memory-consistency/scope domain；其余 qualifier、operation、space 和 type 不在该子集内。 |
| 冻结的 M10 warp/async/matrix 子集 | 部分支持 | `activemask`（PTX 6.2 / SM 30）、`vote.sync.ballot.b32` 与 `shfl.sync.idx.b32`（PTX 6.0 / SM 30）、`cp.async.ca.shared.global` 及 commit/wait form（PTX 7.0 / SM 80）、`ldmatrix.sync.aligned.m8n8.x2.shared.b16`（PTX 9.3 §9.7.15.5.15；PTX 6.5 / SM 75；destination 2×b32）以及 `mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32`（PTX 9.3 §9.7.15.5.14；PTX 6.5 / SM 75；D/C 4×f32、A 2×f16x2、B 1×f16x2）。只 resolve/check 这些 form；没有 execution semantics 或 simulator support。 |
| 冻结的 mixed `cvt` | 支持 | register-only `cvt.rn.f32.u32` 与 `cvt.rzi.u32.f32`（两端接受 equal-or-wider register declaration；PTX 1.0 / SM 0） |
| 冻结的 `cvta` | 支持 | register-only `cvta.global.u64` 与 `cvta.to.global.u64`（PTX 2.0 / SM 20）；不支持 variable address 或 provenance inference |
| 冻结的 integer `mul` | 支持 | register-or-immediate source `mul.lo.u32`（PTX 1.0 / SM 0） |
| 冻结的 floating `mul` | 支持 | register-only `mul.rn.f32`（PTX 1.0 / SM 0） |
| 冻结的 integer `mad` | 支持 | register-or-immediate source `mad.lo.u32`（PTX 1.0 / SM 0） |
| 冻结的 `fma` | 支持 | register-only `fma.rn.f32`（PTX 2.0 / SM 20） |
| 冻结的 integer `div` | 支持 | register-or-immediate source `div.u32`（PTX 1.0 / SM 0）；zero divisor 保持接受，行为由 PTX 指定为 unspecified |

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
| `.file` | D | T | T | Y | — | C | decimal/hex uint64 identity；重复 ID 幂等，overflow 诊断 |
| `.func` | D | E | E | Y | Y | C | 既有 function node |
| `.global` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.local` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.loc` | D | T | T | Y | — | C | decimal/hex file ID 与 `.debug_str` function-name identity；不做 attachment |
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
| `.section` | D | T | T | Y | — | C | 仅 `.debug_str` 及 raw `name:` label binding；payload 保持 raw |
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

[项目 roadmap](../../.agents/project_roadmap.v2.md) 是实现状态、依赖和优先级的唯一权威来源。
本矩阵只记录能力边界，刻意不重复该排序。

PTX ISA 的 variable declaration 概述提到 optional fixed address，但当前规范没有给出独立
语法、约束或示例。frontend 不会据此发明语法；只有获得规范性 grammar 或可验证的
`ptxas` 行为后才会增加对应节点。
