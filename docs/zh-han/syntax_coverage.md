# PTX 语法覆盖情况

## 用途

本矩阵描述 parser 已实现的行为，并不表示已经完整支持 PTX ISA。语法基准为 NVIDIA
[PTX ISA 文档](https://docs.nvidia.com/cuda/parallel-thread-execution/)。
[核心 opcode 完整性审计](core_opcode_completeness_audit.md) 将此 frontend 边界与
archived PTX 9.3 及固定 simulator execution 对 11 个常用 operation name 的证据分开记录。

| 范围 | 状态 | 当前实现子集 |
| --- | --- | --- |
| Token 与 trivia | 部分支持 | identifier、dot identifier、literal、标点、注释、空白与部分稳定 directive；未修改的 `CstFile::sourceText()` 会从 token buffer 逐字节 round-trip |
| 单 instruction fragment | 部分支持 | predicate guard、opcode/modifier、普通 operand、address、vector member/vector pack，以及 call/branch 专用 operand shape |
| Module header | 支持子集 | `.version`、`.target`、`.address_size` 会 lower 为有序且 independent 于 AST 的 source-configuration region。每个 region 拥有 effective version、target-option spelling、address width 与 explicit/defaulted provenance；省略 address size 时拥有 PTX 的 32-bit default，而非 host property。已识别的 region 会提供 target-aware module validation，但不构成 hardware configuration 或 execution contract。 |
| Debug file directive | 支持子集 | outermost `.file file_index "filename"` 与可选且成对的 `, timestamp, file_size`；decimal/octal/hex uint64 ID 在 debug-only namespace 中 binding，重复 ID 幂等且 overflow 会诊断 |
| Debug location directive | 支持子集 | function/nested-block `.loc file line column` 的 decimal/octal/hex file ID 与成对 PTX 7.2 `function_name`/`inlined_at` payload 会验证已绑定 file ID 与 `.debug_str` section/label identity；不附着到 instruction，也不进入 Resolved IR |
| Debug section directive | 支持子集 | outermost `.section name { ... }` 的匹配 brace 与有序 raw DWARF payload token 会保留；`.debug_str` 与 raw `name:` label 会绑定为 debug identity，payload width、relocation 和 offset semantic 仍未支持 |
| Backend pragma directive | 支持子集 | module、`.entry` header 与 function/nested-block statement 的 `.pragma` 保留非空 comma-separated string list 到 CST/AST；pragma 不进入 binding 或 Resolved IR |
| Kernel resource directive | 支持子集 | entry header 的 `.maxnreg n`、`.maxntid nx[,ny[,nz]]`、`.reqntid nx[,ny[,nz]]`、`.minnctapersm ncta`、`.reqnctapercluster nx[,ny[,nz]]`、零参数 `.explicitcluster` 与 `.maxclusterrank n` 进入专用 CST/AST，并以 normalized function-contract value 自主保存。declaration semantics 拒绝同一 entry 同时使用 `.maxntid` 与 `.reqntid`、`.reqnctapercluster` 与 `.maxclusterrank`；module validation 检查已建模 PTX/target minimum。launch feasibility、occupancy 与 physical resource allocation 仍不检查。 |
| Function | 支持子集 | `.entry/.func` definition、`.func` prototype、visibility/linkage qualifier、返回/输入参数列表、`.noreturn`、`.func` ABI suffix、`.language` 与 entry `.blocksareclusters`。Resolved function 会 independent 于 AST 地拥有 normalized signature、linkage/canonical identity、已支持 attribute、ABI suffix、resource，以及 language/cluster marker。 |
| Formal parameter | 支持子集 | `.reg/.param`、alignment、scalar type、pointer space/alignment，以及由结构化 constant expression 指定长度的 array |
| Variable declaration | 支持子集 | module/function scope、linkage qualifier、`.reg/.param/.local/.shared/.global/.const`、窄 `.attribute(.managed/.unified)`、alignment、vector/base type、parameterized name、多维 array，以及 `.global/.const` initializer |
| Function body | 支持子集 | variable declaration、label、当前 instruction grammar，以及递归绑定的 nested block；resolution 会按源码顺序递归平铺 instruction，call staging 限于各 lexical block |
| Constant expression | 支持子集 | literal/symbol、括号、`.s64/.u64` cast、一元/二元/三元运算、`generic(symbol)` 与 mask initializer operator |
| Initializer | 支持子集 | scalar expression、递归 brace list、未定长首维；拒绝 `.extern`、parameterized name 及非 `.global/.const` initializer |
| Symbol binding | 支持子集 | module/function/nested-block scope、变量/参数/函数/label、lexical shadowing、parameterized member、instruction/initializer/dimension/control-flow reference，以及隔离的 debug file/string metadata identity；label 与 control-flow metadata 保持 function-local |
| Declaration 语义 | 支持子集 | 正整数 array extent、未定长首维推导、initializer type/brace shape/元素上限、symbol address、module linkage-compatible redeclaration，以及已支持 entry resource 的 version/conflict 规则 |
| Resolved 存储声明 | 支持子集 | 拥有自身数据的 global/constant/shared/local declaration metadata，包含 identity/scope、typed shape、checked byte extent、alignment、linkage 与 initializer constant/relocation；external 未定长 shared data 保持 dynamic、size-unknown。规范化边界见[存储契约](storage_declarations.md)；不分配内存或生成 runtime instance |
| 其他 directive | 部分支持 | 同 module `.alias` 会 canonicalize direct-call ABI lookup，并作为 owned alias contract 保存。typed `.managed/.unified` attribute 会作为 typed function/storage contract 保存（`.unified` 拥有两个 numeric UUID half）；列出的 header directive 亦由 model 自主保存。LD/ST 检查 unified-address 与只读约束；linker/backend 行为及 runtime allocation 仍不支持。 |
| 结构化控制语法 | 支持子集 | `.callprototype`、`.calltargets` 与 `.branchtargets` 均有专用 function-local CST/AST grammar；binding 与 declaration semantics 检查其 label/member/contract。Resolved module contract 会保留 bound metadata-label identity、scope、canonical call signature、ordered call target 及 expanded logical branch-target entry。generated `IndirectCall` layout 可在 PTX 2.1 / SM 20 解析 `.reg` target 加已绑定 prototype/target-set metadata，module resolution 会应用共享 call ABI contract。`brx.idx` 可在 PTX 6.0 / SM 30 解析 `.u32` index 加当前 function `.branchtargets` identity；不会构建 CFG 或证明 dynamic control flow。 |
| 恢复与编辑 | 支持子集 | `parseModule()` 产生有序 diagnostic 和 inserted/skipped/error CST recovery node，并在有界结构/module anchor 处继续；partial nested block 会保留其合法 body，但没有 closing-brace token。standalone instruction parsing 保持 fail-fast。recovered module 只 lower 合法相邻 node；recovery marker 保持 CST-only，parser diagnostic 只一次、按 source order 返回。installed consumer 覆盖合法 PTX 9.3 directive text、semantic directive failure 与 recovered unknown directive。round-trip serialization 使用原始 token buffer 而非 recovery marker。可选 Clang lexer/CST libFuzzer target 有 GTest seed smoke，但尚未加入 ASan/UBSan 或 CI matrix |
| Resolved opcode | 部分支持 | 已文档化的 supported form 及其 parser/resolver/checker 测试定义当前 opcode 边界；不存在 exhaustive 的手工 ISA ledger。M12 common-kernel corpus 在 `sm_80`、`sm_90a`、`sm_100` 上对 60 个冻结 form 执行 parse、resolve 与 target-aware check；其中 `setmaxnreg.inc.sync.aligned.u32` 只出现在 `sm_90a` corpus fixture。该 corpus presence 不等同于 checker availability，也不表示完整 ISA coverage：模型接受 PTX 8.0 的 `sm_90a`、PTX 8.6 的 exact `sm_100a`、PTX 8.8 的 enabled `sm_100f` family（包含已建模的 `sm_100f` 与 `sm_103a`/`sm_103f`），以及 PTX 8.8 的 `sm_120f`。未 catalog 的官方 spelling 会报告 `UnknownTarget`，且不推断 translation compatibility。已实现冻结 slice 仍为 partial，其 residual variant 在 M12 后 deferred；simulator execution 仍 unsupported。 |

| 冻结的 M10 memory/atomic 子集 | 部分支持 | PTX 7.4 / SM 70 的 L1 eviction 与 PTX 7.4 / SM 80 的 L2 cache-hint `ld`/`st`；历史上的 `ldu.global.u32` 与 `prefetch.global.L1` seed（现已在下文扩展）；以及冻结的 `membar`、`fence` 和 global relaxed-CTA scalar `atom`/`red` form。它们复用既有 memory-consistency/scope domain；其余 qualifier、operation、space 和 type 不在这个冻结子集内。 |
| PTX 9.3 `atom` / `red` | 支持子集 | 同步标量与向量操作/类型组合，语义与 scope 可独立省略；向量形式要求 global 内存及 PTX 8.1 / SM 90。`red.async` 有独立的 shared-completion（PTX 8.1 / SM 90）和 global-release（PTX 8.7 / SM 100）组合。具体组合与契约详见[原子与归约覆盖范围](atomic_reduction_coverage.md)。 |
| PTX 9.3 非 tensor bulk async | 支持子集 | 按方向区分的 `cp.async.bulk` copy、`cp.reduce.async.bulk`、L2 prefetch、bulk-group 完成、shared/global `st.async` 及 `st.bulk` 清零，包括 PTX 9.3 限定符目标门槛。详见 [bulk async 覆盖范围](bulk_async_coverage.md)。Tensor 与 multimem 形式有独立契约。 |
| PTX 9.3 `ldu` | 支持 | 支持 generic 或显式 `.global` 寻址的标量、v2/v4 uniform global load，以及文档规定的类型集合、目标与操作数检查。参见 [`ldu` 覆盖范围](ldu_coverage.md)。 |
| PTX 9.3 `prefetch` / `prefetchu` | 支持 | 支持普通的 generic/global/local L1/L2、global L2 eviction priority、generic/const/param tensor-map 形式，以及 uniform-cache L1。参见 [预取覆盖范围](prefetch_coverage.md)。 |
| PTX 9.3 `applypriority` / `discard` | 支持 | 支持 generic 与显式 global 的 L2 形式，并检查固定的 128 字节范围、对齐及目标条件。参见 [缓存范围覆盖范围](applypriority_discard_coverage.md)。 |
| PTX 9.3 `createpolicy` | 支持 | 支持 fractional、range 与 access-property 转换形式，并检查类型化 priority、fraction、size 及目标条件。参见 [`createpolicy` 覆盖范围](createpolicy_coverage.md)。 |
| 冻结的 M10 warp/async/matrix seed | 部分支持 | `activemask`（PTX 6.2 / SM 30）、`vote.sync.{all,any,uni}.pred`、`vote.sync.ballot.b32` 与 `shfl.sync.{up,down,bfly,idx}.b32`（PTX 6.0 / SM 30；见[同步 warp 覆盖范围](warp_sync_coverage.md)）、原有的 `cp.async.ca.shared.global` seed（现扩展于[非 bulk 复制覆盖范围](cp_async_coverage.md)），以及原有的 `ldmatrix.sync.aligned.m8n8.x2.shared.b16`、`mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32` seed。Warp-matrix 扩展在下文单列。 |
| PTX 9.3 warp-level matrix | 已建模，待验证 | 现行 `ldmatrix`、`stmatrix`、`movmatrix`、dense `mma`、sparse `mma.sp`/`mma.sp::ordered_metadata` 及 `wmma.load`/`wmma.store`/`wmma.mma` 的类型化 shape、element、layout、fragment、sparse metadata 与 scale 契约已进入 canonical spec。详见[具体边界](#ptx-93-warp-level-matrix)；generated C++ 验证和独立 core 验收尚待完成。 |
| PTX 9.3 logic/shift | 支持 | `and`/`or`/`xor`/`not` 覆盖 `.pred/.b16/.b32/.b64`；`cnot` 覆盖 `.b16/.b32/.b64`；`lop3` 覆盖 base 与带 predicate-result 的 `.and/.or` layout，并检查 `.u8` LUT；`shf` 覆盖所有 `.l/.r` × `.clamp/.wrap` form；`shl` 覆盖 bit width；`shr` 覆盖 bit、unsigned 与 signed width。详见 [logic/shift 覆盖](logic_shift_coverage.md)。 |
| PTX 9.3 整数位操作 | 支持 | `popc`、`clz`、`brev`、`bfind`、`bfe` 与 `bfi` 覆盖文档定义的 PTX 2.0 / `sm_20` width、sign、`.shiftamt` 与 control-operand form。详见 [位操作覆盖](bit_operations_coverage.md)。 |
| PTX 9.3 整数算术 | 支持 | 已通过 parsing、resolution、operand/type check 与 target availability 建模 §9.7.1 全部文档 syntax form。详见 [整数算术覆盖](integer_arithmetic_coverage.md)。 |
| conversion 与 address-query form | 支持子集 | 已建模的 `isspacep`、`cvta`、`cvt`、`cvt.pack`、`prmt`、`mapa` 与 `getctarank` syntax、operand layout、typed modifier boundary 及每个 form 的 PTX/target minimum 见 [conversion coverage](conversion_coverage.md)。这是 frontend 的 source acceptance/validation boundary，不表示 conversion execution，也不表示完整 PTX conversion family。 |
| 已建模的 `mul` | 支持 | 完整 PTX 9.3 integer、floating、half 与 bfloat MUL form、其 modifier/operand contract 和 availability 见 [MUL 覆盖矩阵](mul_coverage.md)；simulator execution 仍不支持 |
| 已建模的 `setp` | 支持 | 普通与 half/bfloat 比较、Boolean predicate source、destination shape 及目标边界见 [SETP 覆盖](setp_coverage.md)；不执行比较运算 |
| 已建模的 `set` | 支持 | 普通与 half/bfloat result/source type、comparison 与 Boolean domain、`.ftz`、operand container 及 target 下限见 [SET 覆盖](set_coverage.md)；不执行比较运算 |
| Ordinary `selp` | 支持 | PTX 9.3 全部 ordinary scalar type、predicate selection operand 与 `.f64` target 边界见 [SELP 覆盖](selp_coverage.md)；不执行选择运算 |
| 已建模的 `slct` | 支持 | PTX 9.3 全部 ordinary data type、numeric selector、`.ftz`、operand container 与 `.f64` target 边界见 [SLCT 覆盖](slct_coverage.md)；不执行选择运算 |
| 已建模的 `ld`/`st` | 支持 | Scalar/vector、shared 子空间、cache-control 组合、有序语义、NC load 和 unified-address 检查见 [LD 覆盖](ld_coverage.md) 与 [ST 覆盖](st_coverage.md)；内存执行与分配不属于 frontend |
| 扩展精度整数 | 支持 | §9.7.2 全部文档化的 `add`/`addc`/`sub`/`subc`/`mad`/`madc` type、mode 与 CC-effect 组合均提供 typed carry/borrow effect 和目标检查，见 [carry 覆盖](carry_coverage.md)；运行时 CC 状态仍不属于 frontend |
| 已建模的 `mad` | 支持子集 | 保留 integer 与 carry form；explicit-rounding FP32/FP64 form、operand、target minimum 及排除的 legacy profile 见 [MAD 覆盖](mad_coverage.md)。 |
| 已建模的 `fma` | 支持 | 16 个 PTX 9.3 FMA variant、其 modifier/operand contract 与 availability 见 [FMA 覆盖矩阵](fma_coverage.md)；simulator execution 仍不支持 |
| 已建模的 `div` | 支持子集 | frozen integer `div.u32`（PTX 1.0 / SM 0）与 explicit FP32/FP64 form；见 [DIV 覆盖](div_coverage.md)。zero divisor 保持接受，行为由 PTX 指定为 unspecified。 |

## PTX 9.3 warp-level matrix

Matrix 规格固定采用 [PTX ISA 9.3 §9.7.15](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions)。当前 canonical YAML 已包含下表形式；generated C++ 验证和独立 core 验收尚待完成。Frontend 契约包括解析、owned resolution、operand 与 fragment 检查及 target-aware validation；不执行 GPU 运算，也不证明所有 warp lane 满足 collective protocol。每个 form 的 PTX 版本和 generic、架构专属或 family-specific target 下限由 instruction model 提供。

当前规格源码列出 994 个逻辑 form：82 个 matrix movement（54 `ldmatrix`、27 `stmatrix`、1 `movmatrix`）、175 个 dense `mma`、185 个 sparse `mma.sp`，以及 552 个 WMMA（352 load、104 store、96 compute）。这些数量表示互不相同的 canonical instruction contract，并非 C++ storage alternative 的数量；统一验证尚待完成。紧凑的 generated representation 让物理 operand 存储相同的 form 共用 alternative。逻辑 form identity 与其 owned modifier 值决定准确契约；validation 先检查 identity 是否属于实际存储的 alternative，再选择契约并检查当前 modifier 与 operand。因此，仅凭 C++ type 不能确定操作。既有两个具体且 modifier 静态的 seed API 继续保留。

| Family | 已建模的 PTX 9.3 形式 |
| --- | --- |
| Matrix domain | 类型化 M/N/K shape；与 register scalar type/packing 分开的逻辑 element/accumulator type；row/col layout、fragment cardinality、sparse metadata order/selector，以及 block-scale kind、scale type、vector size、data 和 ID。原始 matrix move 使用 K=0；WMMA load/store 保留完整计算 shape。Fragment 数量从 operand check 共用的规格数据推导，包括超过普通 256-bit vector 限额的 matrix fragment。 |
| Matrix movement | `ldmatrix` 的 `.m8n8`/`.m16n16`/`.m8n16` count、transpose、`.b16`/`.b8` 与压缩 source format；`stmatrix` 的 `.m8n8`/`.m16n8` count、transpose 与 type；`movmatrix.sync.aligned.m8n8.trans.b16`。Generic/shared 与 `.shared::cta` address qualifier 遵循各指令的 version/target gate；现代 shape 要求对应的架构专属或 family-specific target。 |
| Dense `mma.sync.aligned` | 经典 `.f16`/`.bf16`/`.tf32`/`.f64` shape；`.s8`/`.u8`、`.s4`/`.u4`、`.b1` 形式及合法的 `.satfinite` 或 bit operation 位置；FP8 `.e4m3`/`.e5m2`、新式 `.kind::f8f6f4` 低位类型，以及 `.kind::mxf4`/`.kind::mxf4nvf4`/`.kind::mxf8f6f4` block-scale 组合。逐 form 检查 shape、layout、accumulator、register tuple、packing、scale operand 与 target gate。 |
| Sparse `mma.sp` | 经典 `.f16`/`.bf16`/`.tf32`、integer、FP8 形式及 `mma.sp::ordered_metadata`；新式 ordered low-bit 与 block-scale 组合分别约束 type、shape、metadata、selector、scale 与 target。Ordered-metadata 标志保留源码承诺；静态检查不能证明运行时 metadata bit 已排序，也不能证明动态 scale selector 的值。 |
| WMMA compatibility | `wmma.load.{a,b,c}`、`wmma.store.d`、`wmma.mma` 的现行 `.f16`、`.bf16`、`.tf32`、`.f64`、integer、sub-byte 与 single-bit topology。检查 layout、shape、register fragment、generic/global/shared address form、可选 stride 与各 cohort 的 availability。 |

归档 grammar 中 dense FP64 的 `.m8n84` 排版按 fragment 章节与示例解释为 `.m8n8k4`。WMMA FP64 C/D fragment 按 `wmma.mma` 和 `wmma.store` 示例使用两个 `.f64` 寄存器。`stmatrix.m16n8.x4` 示例省略了 `.trans`；规范描述要求该限定符。已移除的浮点 WMMA `.satfinite` 与 PTX 6.3 前隐式 `.aligned` 的历史形式不在本次扩展内。WGMMA、TCGEN05、GPU 数值结果和动态 collective 行为另有边界。

离线 assembler 抽样使用 CUDA `ptxas` V13.3.73，并为每个完整 PTX module 指定 `.version` 和 `.target`。WMMA FP64 C/D 双寄存器、dense FP64 `.m8n8k4`、代表性的 movement、混合符号与 `.b1` MMA、经典 sparse MMA，以及 `sm_120a` 上的 dense 和 ordered-sparse block-scale 形式均被接受；对应的错误 fragment、已移除 modifier、必需 `.trans`、scale type 和 target 反例被拒绝。这是 assembler 抽样证据，不代表 994 个 variant 均已验证，也不代表 frontend 测试已通过。`ptxas` 接受 WMMA 立即数 stride 17，但 PTX memory-layout 契约下该布局行为未定义；frontend 保留静态 stride 约束。

稀疏 FP8 `.m16n8k32` 的 shape 已在规范中引入，但规范未直接列出 metadata selector 范围。Frontend 根据 2:4 稀疏格式和 fragment 规模所隐含的双线程 metadata 拓扑接受 0 和 1。`ptxas` V13.3.73 对 native 和 ordered metadata 的完整 `.m16n8k32` FP8 module 均在 instruction-type 检查阶段拒绝 selector 0 和 1，而 `.m16n8k64` FP8 对照能够汇编；selector 2 的诊断另外指出预期范围为 0 或 1。因此，此 assembler 尚未证实 selector 1 的正例。Frontend 保留此规范解释；完整的 core 验收仍待完成。

conversion family 的 inventory 已移至独立的 [conversion coverage](conversion_coverage.md)。
该文档列出已建模 form 与有意保留的边界，但不重建已退役的 manual opcode ledger。

Lexer 能切分矩阵以外的源码，Syntax AST 也可能以文本形式保留未知 opcode；这两种情况
都不表示该结构能够 lower 到 Resolved IR。

## PTX 9.3 directive registry

此表逐 spelling 覆盖 PTX ISA Table 1 的 35 个 directive，以及该表遗漏的五项：5.4.8 的
`.attribute`、11.4 的 `.abi_preserve`、`.abi_preserve_control` 和 11.8 的
`.blocksareclusters`、`.language`。它回答 coverage matrix 的六个 pipeline 问题。legacy
非 dot spelling `@@dwarf` 与 `.ptr` 等 attribute 刻意不属于这份 dot-directive registry。

图例：`D` = 专用 lexer token；`G` = 通用 `DotIdent`（仍可 tokenize，但不表示 CST
支持）。`T` = typed directive CST/AST；`E` = 进入既有 declaration/function node；`R` =
parser 明确拒绝。`Y` = 作为 owned binding/Resolved-IR contract 保存；`I` = consuming
instruction 保留/check 已绑定 identity；`C` = direct binding/declaration semantic check；
`V` = 当前 AST-free `validateModule` traversal 会执行 target-aware validation。仅被保留不等于
`V`：prototype ABI/`.noreturn` availability 与 storage-declaration attribute availability 仍走
AST/declaration-validation path。`V` 只验证已建模 source-profile requirement，不建立 launch
feasibility、simulator execution 或 hardware behavior；`—` = 该阶段无支持。

| Directive | Token | CST | Syntax AST | Binding | Resolved IR | Target / semantic | 明确边界 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `.address_size` | D | T | T | — | Y | C | 拥有带 explicit/defaulted provenance 的 effective 32/64-bit source-header width；作为 header integrity revalidate，不用于构建 `checker::TargetInfo`，也不表示 host-address 或 allocation |
| `.alias` | G | T | T | Y | Y / I | C / V | owned 同 module device-function alias；不做 linker/backend alias |
| `.abi_preserve` | G | T | T | — | Y | C / V（仅 `.func`） | 两种 form 均拥有 ABI metadata。PTX 9.0 availability 对 `.func` 为 AST-free；`.callprototype` availability 仍为 AST-backed。不进行 physical register assignment。 |
| `.abi_preserve_control` | G | T | T | — | Y | C / V（仅 `.func`） | 两种 form 均拥有 ABI metadata。PTX 9.0 availability 对 `.func` 为 AST-free；`.callprototype` availability 仍为 AST-backed。不进行 physical register assignment。 |
| `.align` | D | E | E | Y | Y | C | declaration/parameter alignment |
| `.attribute` | G | T | T | — | Y | C / V（仅 function） | 仅 typed `.managed` 与 `.unified(id,id)` placement/version subset。AST-free target availability 覆盖 function attribute；已保留 storage attribute 当前仍通过 AST/declaration validation 检查。不进行 runtime allocation 或 host-address interpretation。 |
| `.branchtargets` | D | T | T | Y | Y / I | C / I | 拥有 bound、expanded logical target；`brx.idx` consumer 为 PTX 6.0 / SM 30，不提供 CFG/protocol proof |
| `.callprototype` | D | T | T | Y | Y / I | C / I | 拥有 normalized signature 与已支持 ABI metadata。AST-free validation 会 recheck identity、scope 与 signature，不检查 prototype ABI/`.noreturn` availability；indirect-call availability 仍由 consumer 决定。 |
| `.calltargets` | D | T | T | Y | Y / I | C / I | 拥有 ordered bound/canonical function target 及 shared signature；indirect-call availability 仍由 consumer 决定 |
| `.common` | G | R | — | — | — | — | 未建模 declaration directive |
| `.const` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.entry` | D | E | E | Y | Y | C | 既有 function node |
| `.explicitcluster` | D | T | T | — | Y | C / V | entry-only、零参数、PTX 7.8 modeled availability；target launch feasibility deferred |
| `.extern` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.file` | D | T | T | Y | — | C | decimal/octal/hex uint64 identity；重复 ID 幂等，overflow 诊断 |
| `.func` | D | E | E | Y | Y | C | 既有 function node |
| `.global` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.local` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.loc` | D | T | T | Y | — | C | decimal/octal/hex file ID 与 `.debug_str` function-name identity；不做 attachment |
| `.maxclusterrank` | D | T | T | — | Y | C / V | entry-only normalized resource、PTX 7.8 modeled availability；与 `.reqnctapercluster` 冲突 |
| `.maxnctapersm` | G | R | — | — | — | — | 未建模 deprecated resource directive |
| `.maxnreg` | D | T | T | — | Y | C / V | entry-only normalized resource，含 modeled availability；occupancy implication deferred |
| `.maxntid` | D | T | T | — | Y | C / V | entry-only normalized resource；与 `.reqntid` 冲突；launch feasibility deferred |
| `.minnctapersm` | D | T | T | — | Y | C / V | entry-only normalized resource；warning/device feasibility deferred |
| `.noreturn` | D | E | E | — | Y | C / V（仅 `.func`） | device `.func`/`.callprototype`；检查 return-parameter conflict，但 AST-free PTX 6.4 availability 当前只对 `.func` recheck；prototype availability 仍为 AST-backed。 |
| `.param` | D | E | E | Y | Y | C | 既有 variable/formal/call-parameter declaration |
| `.pragma` | D | T | T | — | — | — | backend string interpretation 刻意未实现 |
| `.reg` | D | E | E | Y | Y | C | 既有 variable/formal declaration |
| `.reqnctapercluster` | D | T | T | — | Y | C / V | entry-only normalized resource、PTX 7.8 modeled availability；与 `.maxclusterrank` 冲突 |
| `.reqntid` | D | T | T | — | Y | C / V | entry-only normalized resource；与 `.maxntid` 冲突；launch feasibility deferred |
| `.section` | D | T | T | Y | — | C | 仅 `.debug_str` 及 raw `name:` label binding；payload 保持 raw |
| `.shared` | D | E | E | Y | Y | C | 既有 variable declaration |
| `.sreg` | G | R | — | — | — | — | 未建模 special-register declaration |
| `.target` | D | T | T | — | Y | V | owned source-target option 会为 recognized profile 构建 `checker::TargetInfo`；不表示 physical target selection 或 translation guarantee |
| `.tex` | G | R | — | — | — | — | 未建模 declaration directive |
| `.version` | D | T | T | — | Y | V | owned source version 参与 target-aware module validation |
| `.visible` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.weak` | D | E | E | Y | Y | C | 既有 linkage qualifier |
| `.blocksareclusters` | G | T | T | — | Y | C / V | owned 零参数 entry marker；PTX 9.0 modeled availability，且要求 `.reqntid` + `.reqnctapercluster`；launch rule deferred |
| `.language` | G | T | T | — | Y | C / V | owned 非空 official string/integer list；PTX 9.3 modeled availability，不实现 backend-language behavior |

## 实现优先级

[项目 roadmap](../../.agents/project_roadmap.v2.md) 是实现状态、依赖和优先级的唯一权威来源。
本矩阵只记录能力边界，刻意不重复该排序。

PTX ISA 的 variable declaration 概述提到 optional fixed address，但当前规范没有给出独立
语法、约束或示例。frontend 不会据此发明语法；只有获得规范性 grammar 或可验证的
`ptxas` 行为后才会增加对应节点。
