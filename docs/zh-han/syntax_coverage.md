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
| Kernel resource directive | 支持子集 | entry header 的 `.maxnreg n`、`.maxntid nx[,ny[,nz]]`、`.reqntid nx[,ny[,nz]]`、`.minnctapersm ncta`、`.reqnctapercluster nx[,ny[,nz]]`、零参数 `.explicitcluster` 与 `.maxclusterrank n` 进入专用 CST/AST；declaration semantics 检查 source `.version` 最低版本，并拒绝同一 entry 同时使用 `.maxntid` 与 `.reqntid`、`.reqnctapercluster` 与 `.maxclusterrank`；target/launch-time rule 尚未检查 |
| Function | 支持子集 | `.entry/.func` definition、`.func` prototype、visibility/linkage qualifier、返回/输入参数列表、`.noreturn`、`.func` ABI suffix、`.language` 与 entry `.blocksareclusters` |
| Formal parameter | 支持子集 | `.reg/.param`、alignment、scalar type、pointer space/alignment，以及由结构化 constant expression 指定长度的 array |
| Variable declaration | 支持子集 | module/function scope、linkage qualifier、`.reg/.param/.local/.shared/.global/.const`、窄 `.attribute(.managed/.unified)`、alignment、vector/base type、parameterized name、多维 array，以及 `.global/.const` initializer |
| Function body | 支持子集 | variable declaration、label、当前 instruction grammar，以及递归绑定的 nested block；resolution 会按源码顺序递归平铺 instruction，call staging 限于各 lexical block |
| Constant expression | 支持子集 | literal/symbol、括号、`.s64/.u64` cast、一元/二元/三元运算、`generic(symbol)` 与 mask initializer operator |
| Initializer | 支持子集 | scalar expression、递归 brace list、未定长首维；拒绝 `.extern`、parameterized name 及非 `.global/.const` initializer |
| Symbol binding | 支持子集 | module/function/nested-block scope、变量/参数/函数/label、lexical shadowing、parameterized member、instruction/initializer/dimension/control-flow reference，以及隔离的 debug file/string metadata identity；label 与 control-flow metadata 保持 function-local |
| Declaration 语义 | 支持子集 | 正整数 array extent、未定长首维推导、initializer type/brace shape/元素上限、symbol address、module linkage-compatible redeclaration，以及已支持 entry resource 的 version/conflict 规则 |
| 其他 directive | 部分支持 | 同 module `.alias` 会 canonicalize direct-call ABI lookup；仅建模 typed `.managed/.unified` attribute 与列出的 header directive。linker/backend、UUID class、ld/st attribute effect 仍不支持 |
| 结构化控制语法 | 支持子集 | `.callprototype`、`.calltargets` 与 `.branchtargets` 均有专用 function-local CST/AST grammar；binding 与 declaration semantics 检查其 label/member/contract。generated `IndirectCall` layout 可在 PTX 2.1 / SM 20 解析 `.reg` target 加已绑定 prototype/target-set metadata，module resolution 会应用共享的 call ABI contract。`brx.idx` 可在 PTX 6.0 / SM 30 解析 `.u32` index 加当前 function `.branchtargets` identity；不会展开 target entry 或构建 CFG |
| 恢复与编辑 | 支持子集 | `parseModule()` 产生有序 diagnostic 和 inserted/skipped/error CST recovery node，并在有界结构/module anchor 处继续；partial nested block 会保留其合法 body，但没有 closing-brace token。standalone instruction parsing 保持 fail-fast。recovered module 只 lower 合法相邻 node；recovery marker 保持 CST-only，parser diagnostic 只一次、按 source order 返回。installed consumer 覆盖合法 PTX 9.3 directive text、semantic directive failure 与 recovered unknown directive。round-trip serialization 使用原始 token buffer 而非 recovery marker。可选 Clang lexer/CST libFuzzer target 有 GTest seed smoke，但尚未加入 ASan/UBSan 或 CI matrix |
| Resolved opcode | 部分支持 | machine-readable manifest 是已建模 opcode slice 与 deferred 范围的权威来源。M12 common-kernel corpus 在 `sm_80`、`sm_90a`、`sm_100` 上对 60 个冻结 form 执行 parse、resolve 与 target-aware check；其中 `setmaxnreg.inc.sync.aligned.u32` 只出现在 `sm_90a` corpus fixture。该 corpus presence 不等同于 checker availability：模型接受 PTX 8.0 的 `sm_90a`、PTX 8.6 的 exact `sm_100a`、PTX 8.8 的 enabled `sm_100f` family（包含已建模的 `sm_100f` 与 `sm_103a`/`sm_103f`），以及 PTX 8.8 的 `sm_120f`。未 catalog 的官方 spelling 会报告 `UnknownTarget`，且不推断 translation compatibility。inventory 条目对已实现冻结 slice 保持 partial，其 residual variant 在 M12 后 deferred；simulator execution 仍 unsupported。 |

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
| `.alias` | G | T | T | Y | I | C | 仅同 module device-function alias；不做 linker/backend alias |
| `.abi_preserve` | G | T | T | — | — | C | `.callprototype` 与 `.func` suffix；检查 PTX 9.0 source version，不查 target |
| `.abi_preserve_control` | G | T | T | — | — | C | `.callprototype` 与 `.func` suffix；检查 PTX 9.0 source version，不查 target |
| `.align` | D | E | E | Y | Y | C | declaration/parameter alignment |
| `.attribute` | G | T | T | — | — | C | 仅 `.managed` 与 `.unified(id,id)` 的 placement/version 子集 |
| `.branchtargets` | D | T | T | Y | I | C / I | declaration rule 直接检查；`brx.idx` consumer 要求 PTX 6.0 / SM 30 |
| `.callprototype` | D | T | T | Y | I | C / I | declaration rule 直接检查；indirect-call availability 由 consumer 驱动 |
| `.calltargets` | D | T | T | Y | I | C / I | declaration rule 直接检查；indirect-call availability 由 consumer 驱动 |
| `.common` | G | R | — | — | — | — | 未建模 declaration directive |
| `.const` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.entry` | D | E | E | Y | Y | C | 既有 function node |
| `.explicitcluster` | D | T | T | — | — | C | 仅 entry、零参数、PTX 7.8 source-version minimum；target/launch rule 留后续 |
| `.extern` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.file` | D | T | T | Y | — | C | decimal/hex uint64 identity；重复 ID 幂等，overflow 诊断 |
| `.func` | D | E | E | Y | Y | C | 既有 function node |
| `.global` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.local` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.loc` | D | T | T | Y | — | C | decimal/hex file ID 与 `.debug_str` function-name identity；不做 attachment |
| `.maxclusterrank` | D | T | T | — | — | C | 仅 entry、一个参数、PTX 7.8 source-version minimum；与 `.reqnctapercluster` 冲突 |
| `.maxnctapersm` | G | R | — | — | — | — | 未建模 deprecated resource directive |
| `.maxnreg` | D | T | T | — | — | C | 仅 entry；检查 source-version minimum |
| `.maxntid` | D | T | T | — | — | C | 仅 entry；与 `.reqntid` 冲突 |
| `.minnctapersm` | D | T | T | — | — | C | warning/device feasibility 留后续 |
| `.noreturn` | D | E | E | — | — | C | device `.func`/`.callprototype`；检查 return-parameter conflict 与 PTX 6.4 source version；target rule 留后续 |
| `.param` | D | E | E | Y | Y | C | 既有 variable/formal/call-parameter declaration |
| `.pragma` | D | T | T | — | — | — | backend string interpretation 刻意未实现 |
| `.reg` | D | E | E | Y | Y | C | 既有 variable/formal declaration |
| `.reqnctapercluster` | D | T | T | — | — | C | 仅 entry、一至三个参数、PTX 7.8 source-version minimum；与 `.maxclusterrank` 冲突 |
| `.reqntid` | D | T | T | — | — | C | 仅 entry；与 `.maxntid` 冲突 |
| `.section` | D | T | T | Y | — | C | 仅 `.debug_str` 及 raw `name:` label binding；payload 保持 raw |
| `.shared` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.sreg` | G | R | — | — | — | — | 未建模 special-register declaration |
| `.target` | D | T | T | — | — | — | 保留 module syntax；不是 checker context |
| `.tex` | G | R | — | — | — | — | 未建模 declaration directive |
| `.version` | D | T | T | — | — | S | 为已支持 resource 提供 source-version check |
| `.visible` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.weak` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.blocksareclusters` | G | T | T | — | — | C | 零参数 entry marker、PTX 9.0；需要 `.reqntid` + `.reqnctapercluster`；target/launch rule 留后续 |
| `.language` | G | T | T | — | — | C | 非空 official string/integer list、PTX 9.3；只保留 syntax |

## 实现优先级

[项目 roadmap](../../.agents/project_roadmap.v2.md) 是实现状态、依赖和优先级的唯一权威来源。
本矩阵只记录能力边界，刻意不重复该排序。

PTX ISA 的 variable declaration 概述提到 optional fixed address，但当前规范没有给出独立
语法、约束或示例。frontend 不会据此发明语法；只有获得规范性 grammar 或可验证的
`ptxas` 行为后才会增加对应节点。
