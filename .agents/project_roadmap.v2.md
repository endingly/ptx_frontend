# PTX Frontend 项目计划与 Roadmap

> 文档名称：`.agents/project_roadmap.v2.md`
>
> 规划日期：2026-08-28
>
> 项目阶段：pre-1.0
>
> Roadmap 决策：**选择 B——1.0 必须代表现代 PTX frontend，明确覆盖 Hopper 与 Blackwell 方向。**
>
> 状态范围：**只记录本文所在当前分支可验证的代码、测试与文档事实。**
>
> - M0～M10 的功能状态为完成；
> - M8-I14 与 M9-C03 保持暂停；
> - M10 后续的 PTX ISA 9.3 §9.7 YAML taxonomy 规范化已经完成；
> - M11 已完成；M12～M19 尚未开始。
>
> ISA 规划基线：
>
> - [NVIDIA PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/)；
> - [PTX ISA 9.3 Contents](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/contents.html)；
> - PTX ISA 9.3 文档发布日期：2026-06-25。
>
> 本文不记录托管平台、外部分支、代码评审、流水线运行或合入状态，也不据此推断功能状态。

---

# 0. 本轮全量 Review 结论

## 0.1 结论

旧版 roadmap 对 M0～M10 的前端分层和逐 issue 闭环原则基本正确，但 M10 之后直接进入
公共 API 与 1.0，会使 1.0 缺少现代 PTX 的核心结构：

- cluster 维度、cluster special register 和 cluster synchronization；
- `mbarrier` 生命周期、phase、transaction count 和 wait token；
- async proxy 与 bulk/tensor copy completion；
- tensor map、tensor coordinate 和 TMA 指令；
- matrix shape/type/layout/fragment 的公共表示；
- Hopper `wgmma` descriptor、warpgroup 和 async group；
- Blackwell Tensor Memory、`tcgen05` descriptor、CTA pair、MMA kind、scale factor 和
  specialized synchronization。

因此，旧 M11 不再直接承担 1.0。M10 之后重建为 M11～M19，其中：

```text
M11  PTX 9.3 基线、完整 coverage ledger 和 target capability
M12  真实 compiler kernel 的 common scalar/data-movement 闭环
M13  cluster、proxy 与 mbarrier
M14  tensor map、TMA 与 bulk/tensor async copy
M15  warp-level matrix、sparse MMA 与 WMMA compatibility
M16  Hopper WGMMA
M17  Blackwell Tensor Memory 与 tcgen05 data movement
M18  Blackwell tcgen05 MMA 与同步
M19  稳定公共 API、simulator adapter 和 1.0 gate
```

## 0.2 已确认的文档漂移

### D-01：M9 状态

M9 的 frontend coverage 已经完成。`M9-C03` 是 simulator functional execution，而不是
frontend coverage 的必要出口。按当前项目决策：

- M9 milestone 标记为 ✅；
- `M9-C03` 标记为 ⏸；
- 它在 M19 的 adapter integration 阶段解除暂停；
- 不允许它继续把 M9 错误地显示为“正在实现 opcode”。

### D-02：M10 当前状态

当前分支已经完成旧版 M10-I01～I17 和 M10-C01～C03。该完成状态代表冻结 slice 的：

```text
source
  -> parse
  -> resolve
  -> target-aware check
  -> positive/negative corpus
```

它不代表完整 opcode coverage，也不代表 simulator execution。M10 功能状态为 ✅。

### D-03：旧 M10 的名字过度承诺

旧 M10 名为“Memory、atomic、warp、async-copy 和 matrix 扩展”，容易被误解为这些 family
已经整体完成。新版将其明确改名为：

> **Modern instruction seed slices**

它是现代指令建模的种子和 schema 验证，不是 Hopper/Blackwell coverage 的终点。

### D-04：coverage manifest 不是 exhaustive ledger

当前 `instructions/opcode_coverage.yaml` 只列出已经碰到的 opcode，而且所有条目均是
`partial`。没有被列出的官方 opcode 无法区分：

- 未发现；
- 已发现但未计划；
- 明确推迟；
- 明确不属于 1.0；
- schema 暂时无法表达；
- 等待 consumer evidence。

M11 必须把它升级为覆盖 instruction、directive、special register 的 exhaustive ledger。

### D-05：target 不能只按 SM 数字比较

现代 PTX 同时存在 generic、architecture-specific 与 family-specific target spelling。
`sm_90`、`sm_90a`、`sm_100a`、`sm_100f` 不能被简化为单一整数全序。新版要求：

```text
TargetProfile
  = numeric architecture
  + target flavor
  + explicit enabled family-specific source-feature set
  + explicit capability set
  + preserved source spelling
```

checker 必须按 capability 判断 WGMMA、TMA、TCGEN05 等特性，不得仅以 `sm >= N` 代替。

### D-06：现代指令缺少公共 operand domain

WGMMA 和 TCGEN05 不能继续只依靠 generic register/vector/address 拼接。必须先建立：

- matrix shape/type/layout/fragment；
- shared-memory matrix descriptor；
- tensor-map reference 与 tensor coordinate tuple；
- mbarrier state token；
- tensor-memory address；
- TCGEN05 instruction descriptor；
- zero-column-mask descriptor；
- CTA group / CTA pair / peer CTA；
- scale factor 与 sparsity metadata。

### D-07：时序协议不属于 instruction-local checker

`mbarrier`、WGMMA 和 TCGEN05 存在跨指令、跨线程甚至跨 CTA 的 protocol obligation。
1.0 frontend 负责：

- 精确保存 instruction-local semantic；
- 检查 operand、modifier、shape、descriptor、target 和 module-local identity；
- 把 completion/proxy/protocol metadata 暴露给 consumer。

1.0 frontend **不承诺**在没有 CFG/abstract interpretation 的情况下验证完整动态序列合法性。
该能力进入 post-1.0 static-analysis layer，不允许以 opcode-specific ad-hoc state machine 污染
resolver/checker。

---

# 1. 文档定位

本文是 `ptx_frontend` 的长期权威 roadmap，回答：

1. 项目边界是什么；
2. 当前实现事实是什么；
3. 每个官方 PTX family 在 1.0 前如何处置；
4. Hopper/Blackwell 支持需要哪些公共表示；
5. 每个 milestone 的独立 issue、耦合 issue 和出口是什么；
6. 哪些能力明确推迟到 post-1.0；
7. 何时可以冻结公共 API 并发布 1.0。

本文不是 release note，也不维护仓库外的交付状态。

## 1.1 功能状态

| 标记 | 含义 |
| --- | --- |
| ✅ | 功能已经在当前分支实现并完成相应验证 |
| 🚧 | 已经开始，但 milestone 仍有未完成 issue |
| ⬜ | 尚未开始 |
| ⏸ | 暂停；需要规范、consumer、toolchain 或前置架构证据 |
| ⚠️ | 已实现但存在技术债、验证缺口或文档漂移 |

## 1.2 状态判定范围

本文只维护当前分支的功能状态：

- ✅ 必须能由当前分支中的代码、测试与文档直接验证；
- 🚧、⬜、⏸ 与 ⚠️ 只描述当前分支，不映射任何外部交付流程；
- 不保存 commit hash、托管平台编号、外部分支名称或流水线 run 结果；
- 当前分支无法验证的状态一律不写入本文。

## 1.3 Issue 类型

- `M<n>-I<n>`：独立 issue，可以单独实现、测试、review、bisect 和回滚；
- `M<n>-C<n>`：耦合 issue，只允许出现在 milestone 尾部，用于连接此前已完成能力。

硬约束：

1. 所有 `I` issue 位于 `C` issue 之前；
2. 一个 issue 只属于一个 milestone；
3. 一个 instruction issue 只能覆盖一个 opcode 或一个边界明确的 variant slice；
4. descriptor/domain 基础必须在使用它的 opcode issue 之前独立建立；
5. 耦合 issue 不得临时发明新的公共表示；
6. 当前 milestone 外的“顺便实现”必须拒绝。

---

# 2. 1.0 的产品定义：Modern PTX Frontend

## 2.1 选择 B 的含义

1.0 不要求覆盖 PTX 9.3 的完整 cross-product，但必须覆盖三个验证 profile：

| Profile | 目标 | 1.0 最低能力 |
| --- | --- | --- |
| `core-sm80` | 普通 simulator/compiler kernel | module、call、control、common scalar、memory、warp、`cp.async`、基础 `mma` |
| `hopper-sm90a` | Hopper modern kernel | cluster、`mbarrier`、TMA、bulk/tensor async copy、WGMMA |
| `blackwell-sm100` | Blackwell modern kernel | Tensor Memory、TCGEN05 data movement、TCGEN05 MMA 与同步 |

`blackwell-sm100` profile 的具体 target spelling 必须区分 `sm_100a`、`sm_100f` 及规范允许的
后续 family spelling；不能把它们视为同义字符串。

## 2.2 1.0 的 coverage 语义

对进入 1.0 profile 的每个 instruction family：

- 每一种不同 operand topology 至少有一个完整 slice；
- family 的公共 shape/type/layout/descriptor domain 已稳定；
- 支持的 slice 有完整 parse/resolve/check/corpus；
- 邻接但未支持的 variant 有明确、稳定的 diagnostic；
- coverage ledger 明确记录 `partial`，不得伪装为 complete；
- 后续同 topology 的 variant 应主要通过 YAML/data 扩展，而不是新增 C++ special case。

## 2.3 1.0 不等于完整 PTX 9.3

1.0 可以明确推迟：

- Fabric/CFT；
- texture/surface；
- video instructions；
- 完整 multimem；
- 完整 stack manipulation；
- 所有 legacy/deprecated variant；
- 全部 transcendental、extended-precision 与 packed cross-product；
- WGMMA/TCGEN05 的 simulator execution；
- 跨 CFG、跨线程的 async protocol proof。

但这些 family 必须在 exhaustive ledger 中明确标为 `deferred` 或 `out_of_scope_1_0`，不能消失。

---

# 3. 项目架构与职责边界

## 3.1 核心流水线

```text
PTX source
    |
    v
lexer
    |
    v
lossless CST
    |
    v
typed Syntax AST
    |
    v
lexical binding
    |
    v
declaration semantics
    |
    v
Resolved IR
    |
    v
target-aware checker
    |
    +--> public visitor / serialization / adapter
```

## 3.2 数据驱动主线

```text
PTX facts
+ C++ backend spelling
+ schemas
+ target capability catalog
+ exhaustive coverage ledger
        |
        v
Python load / normalize / validate
        |
        v
generated public types
generated descriptors
generated lookup/dispatch
generated checker data
generated coverage reports
```

## 3.3 新增的现代公共 domain

M11～M18 必须逐步建立以下 target-independent identity：

```text
TargetArchitecture
TargetFlavor
TargetCapability
TargetProfile

AsyncProxyKind
AsyncCompletionKind
MBarrierPhaseKind
ResolvedMBarrierStateToken

TensorAccessMode
TensorFormat
TensorMapRef
TensorCoordinateTuple

MatrixShape
MatrixElementType
MatrixLayout
MatrixFragmentShape
ResolvedMatrixFragment

SharedMatrixDescriptor
TensorMemoryAddress
TcgenInstructionDescriptor
ZeroColumnMaskDescriptor

WarpGroup
CtaGroup
CtaPairRole
SparseMetadata
ScaleFactorDescriptor
```

公共类型不得包含某个具体 YAML variant 的私有 class name，也不得以 target-dependent mutation
改变已经 resolve 的 identity。

## 3.4 Frontend 与 simulator 边界

| 能力 | Frontend | Simulator |
| --- | --- | --- |
| source/CST/AST | 是 | 否 |
| symbol 与 descriptor identity | 是 | 否 |
| instruction variant selection | 是 | 否 |
| PTX/target legality | 是 | 否 |
| shape/type/layout constraint | 是 | 否 |
| protocol metadata 暴露 | 是 | 消费 |
| register/memory state 更新 | 否 | 是 |
| async group runtime state | 否 | 是 |
| mbarrier runtime phase | 否 | 是 |
| warp/CTA scheduling | 否 | 是 |
| cycle/latency | 否 | 是 |

---

# 4. 模块边界硬规则

1. Lexer 不枚举全部 opcode、modifier、shape 或 target spelling。
2. 新 opcode 不得要求修改 lexer，除非引入真正新的 lexical form。
3. CST 只保存结构和源码，不执行 instruction legality。
4. Syntax AST target-independent。
5. Binding 只解析 identity，不判断 opcode legality。
6. Declaration semantics 与 instruction semantics 分离。
7. 已建模 instruction fact 优先写入 YAML。
8. C++ spelling 与 PTX fact 分离。
9. Generated file 不手工修改。
10. 未知信息保持未知。
11. standalone unresolved address 不猜测 state space。
12. Resolved IR 不因 checker target 改写 identity。
13. target compatibility 由 checker 临时评估。
14. 新 semantic 必须保存 SourceRange。
15. public API 变更必须有 installed consumer test。
16. temporary opcode-specific fallback 必须记录删除 milestone。
17. matrix/tensor descriptor 不得退化为无类型 `u64` 后让 consumer 自行猜测。
18. shape cardinality 不得散落在多个 C++ switch。
19. generic target、architecture-specific target 和 family-specific target 不得只按数值比较。
20. temporal protocol validation 不得伪装成 instruction-local checker。
21. unsupported official item 必须进入 coverage ledger。
22. 文档不得把 representative slice 描述为完整 family coverage。

---

# 5. 规范、证据与 coverage 治理

## 5.1 证据优先级

冲突时采用：

1. NVIDIA PTX ISA 9.3 的规范性 grammar、instruction section 和 target note；
2. 可复现的 `ptxas` 正反例；
3. checked-in corpus 的原始 provenance；
4. `instructions/schemas`；
5. `instructions/ptx_spec`；
6. C++ backend spec；
7. 双语设计文档；
8. C++/Python tests；
9. README 和历史日志。

## 5.2 Coverage 状态

exhaustive ledger 必须支持：

| 状态 | 含义 |
| --- | --- |
| `unsupported` | 已知官方 item，当前完全不支持 |
| `syntax_only` | 可无损进入 CST/AST，但不能 resolve/check |
| `partial` | 有明确的 variant slice |
| `complete` | 对 ledger 中冻结的 family scope 完整，不等于整部 PTX |
| `deferred` | 已知且明确推迟，有理由和目标 milestone |
| `out_of_scope_1_0` | 不进入 1.0，但仍保留正式记录 |
| `paused` | 等待规范/toolchain/consumer evidence |

每条 record 至少包含：

```text
official section
family
opcode/directive/sreg spelling
variant slice
syntax status
resolved status
checker status
simulator status
minimum PTX
target capability
planned milestone
disposition reason
evidence links
```

## 5.3 1.0 family disposition

| PTX 9.3 family | 1.0 处置 | Milestone |
| --- | --- | --- |
| Integer arithmetic | common compiler-kernel subset；剩余 cross-product 延后 | M12 |
| Extended-precision integer | ledger 完整；非 corpus 必需项延后 | post-1.0 |
| Floating / half / mixed precision | common subset；transcendental 完整度延后 | M12 / post-1.0 |
| Comparison / selection | common integer/float/predicate topology | M12 |
| Logic / shift | common compiler-kernel subset | M12 |
| Ordinary data movement / conversion | M4/M9/M10 基础，M12 补 common gaps | M12 |
| Non-bulk `cp.async` | frozen slice 已完成 | M10 |
| Bulk/tensor async copy 与 tensor map | 进入 1.0 | M14 |
| Fabric/CFT | 明确不进入 1.0 | post-1.0 |
| Texture/surface | 明确不进入 1.0 | post-1.0 |
| Control flow / call metadata | 已完成核心 contract | M2/M6/M7/M9 |
| Cluster / mbarrier / proxy | 进入 1.0 | M13 |
| Warp-level matrix / sparse MMA | 进入 1.0 | M15 |
| WMMA compatibility | representative slice 进入 1.0 | M15 |
| Hopper WGMMA | 进入 1.0 | M16 |
| Blackwell TCGEN05 | 进入 1.0 | M17/M18 |
| Stack manipulation | ledger 完整；默认延后 | post-1.0 |
| Video instructions | 明确不进入 1.0 | post-1.0 |
| Misc instructions | `trap` 已完成；`setmaxnreg` 进入 common profile；其他延后 | M9/M12 |
| Multimem | base identity 可预留；完整 instruction family 延后 | post-1.0 |
| Special registers | cluster/smem/graph modern subset进入 1.0 | M11 |
| Directives | cluster 与现代 ABI/pragma subset进入 1.0 | M11 |

---

# 6. Issue 的统一闭环标准

除纯文档、纯 schema 或纯构建 issue 外，每个功能 issue 默认必须满足：

1. 指向精确 PTX ISA section；
2. 冻结支持的 syntax/variant 列表；
3. 列出明确排除的邻接 variant；
4. 标明 minimum PTX 与 target capability；
5. schema 能表达并拒绝非法配置；
6. Python loader/normalizer 有正反测试；
7. Python typed model 有测试；
8. generator 输出完整；
9. standalone resolver 有成功与失败测试；
10. module-aware resolver 有成功与失败测试；
11. checker 有 operand/modifier/layout/target 测试；
12. diagnostics 包含准确 SourceRange；
13. coverage ledger 同步；
14. 双语 coverage 同步；
15. public capability 变化时同步 README；
16. `git diff --check` 通过；
17. Debug build/test 通过；
18. Release build/test 通过；
19. installed consumer 不被破坏；
20. generated output 未被手工修改；
21. issue 对应独立 commit；
22. milestone 最后一个 commit 进行 code review 与 document-drift review。

只有 lexer/parser 能接受源码，不算支持 instruction。

---

# 7. Roadmap 总览

| Milestone | 功能状态 | 目标 |
| --- | --- | --- |
| M0 | ✅ | source-faithful lexer、CST 与 Syntax AST |
| M1 | ✅ | YAML 驱动 Resolved IR/checker 平台 |
| M2 | ✅ | binding、declaration semantics 与 direct branch |
| M3 | ✅ | typed value、address、special register 与 `mov` |
| M4 | ✅ | basic scalar/vector `ld/st` 子集 |
| M5 | ✅ | package、consumer test 与 CI workflow |
| M6 | ✅ | direct-call signature、ABI 与 call context |
| M7 | ✅ | indirect call 与 control-flow metadata |
| M8 | ✅ | module grammar、nested scope 与 recovery；I14 暂停 |
| M9 | ✅ | simulator MVP frontend opcode coverage；C03 暂停 |
| M10 | ✅ | modern instruction seed slices |
| M11 | ✅ | PTX 9.3 基线、exhaustive ledger 与 target capability |
| M12 | ⬜ | common compiler-generated scalar/data-movement closure |
| M13 | ⬜ | cluster、proxy 与 mbarrier |
| M14 | ⬜ | tensor map、TMA 与 bulk/tensor async copy |
| M15 | ⬜ | warp-level matrix、sparse MMA 与 WMMA compatibility |
| M16 | ⬜ | Hopper WGMMA |
| M17 | ⬜ | Blackwell Tensor Memory 与 TCGEN05 data movement |
| M18 | ⬜ | Blackwell TCGEN05 MMA 与同步 |
| M19 | ⬜ | 稳定 API、adapter、conformance 与 1.0 gate |

---

# 8. 已完成 milestone ledger

## M0：Source-faithful 语法前端

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M0-I01 | ✅ | C++23 与 SourceRange 基础 | `SourcePos`、`SourceRange` 与 location wrapper |
| M0-I02 | ✅ | 可重入 Flex lexer | scanner 独立；token 自持 text/range/trivia |
| M0-I03 | ✅ | lossless CST | 已支持 grammar 的 token、trivia、顺序不丢失 |
| M0-I04 | ✅ | typed Syntax AST | CST 可降为 target-independent AST |
| M0-C01 | ✅ | source → CST → AST | facade 与显式 pipeline 一致 |
| M0-C02 | ✅ | 语法回归与 coverage | tokenize/CST/AST 边界分别记录 |

## M1：数据驱动 Resolved IR 平台

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M1-I01 | ✅ | instruction schema | variant/modifier/operand/layout/availability 可校验 |
| M1-I02 | ✅ | C++ backend schema | enum/class/field spelling 集中 |
| M1-I03 | ✅ | normalized Python model | reference/value set/default/expression 统一展开 |
| M1-I04 | ✅ | generated public Resolved IR | payload/modifier/location 可生成 |
| M1-I05 | ✅ | 三类 descriptor | syntax/resolved/checker descriptor 贯通 |
| M1-I06 | ✅ | lookup 与 dispatch | opcode/suffix/resolve/check 数据驱动 |
| M1-I07 | ✅ | 多 operand layout | layout tag/payload/checker 一致 |
| M1-C01 | ✅ | codegen ownership | output topology 由 resolved_ir CMake 管理 |
| M1-C02 | ✅ | 首批 opcode | `add`、`sub`、`bar` 等贯通 |

## M2：Binding、声明语义与 direct branch

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M2-I01 | ✅ | lexical SymbolTable | module/function scope 与 shadowing |
| M2-I02 | ✅ | reference classification | declared/external/special/unresolved 分离 |
| M2-I03 | ✅ | declaration semantics | array/initializer/redeclaration/prototype |
| M2-I04 | ✅ | constant evaluator | 64-bit、cast、unary/binary/conditional/shift |
| M2-I05 | ✅ | call/branch syntax node | call group 不与 vector 共用 |
| M2-I06 | ✅ | execution predicate binding | SymbolId/type/negation/range |
| M2-I07 | ✅ | direct `bra` | stable label identity 与 function scope |
| M2-C01 | ✅ | `resolveModule()` | binding/declaration/instruction diagnostic 汇总 |
| M2-C02 | ✅ | module diagnostic regression | duplicate/unresolved/wrong-kind 可区分 |

## M3：值、地址与 `mov`

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M3-I01 | ✅ | special-register catalog | stable identity/type/shape/availability |
| M3-I02 | ✅ | resolved operand 基础类型 | register/predicate/immediate/symbol/address/function |
| M3-I03 | ✅ | `ResolvedMovSource` | binding 后按真实语义分类 |
| M3-I04 | ✅ | formal-parameter address | entry/device、input/return、effective space |
| M3-I05 | ✅ | function address | `.func/.entry` identity |
| M3-I06 | ✅ | scalar `mov` | 16/32/64-bit scalar/predicate |
| M3-I07 | ✅ | historical sreg width | checker data 表达 compatibility |
| M3-I08 | ✅ | vector `mov` | `.b16/.b32/.b64/.b128` pack/unpack |
| M3-C01 | ✅ | identity 与 checker | checker 不修改 identity |
| M3-C02 | ✅ | standalone/module 统一 | descriptor 共享，未知 identity 不猜测 |

## M4：Basic `ld/st`

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M4-I01 | ✅ | generic/basic explicit scalar `ld/st` | state-space policy 分离 |
| M4-I02 | ✅ | `.param` direction/context | entry/device input/return |
| M4-I03 | ✅ | 14 种 scalar memory type | `.b/.u/.s` 与 `.f32/.f64` |
| M4-I04 | ✅ | legacy cache operator | load/store 集合分离 |
| M4-I05 | ✅ | wider-register policy | memory equal-or-wider |
| M4-I06 | ✅ | legacy `.v2/.v4` | 最大 128-bit |
| M4-I07 | ✅ | memory consistency slice | weak/volatile/scoped/mmio |
| M4-I08 | ✅ | PTX 8.8 modern vector | 256-bit、SM100、partial sink |
| M4-C01 | ✅ | static alignment | symbol+offset/absolute 与 total width |
| M4-C02 | ✅ | memory diagnostics | space/direction/consistency/vector/alignment |

## M5：工程化、安装包和 CI

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M5-I01 | ✅ | CMake target 拆分 | dependency 明确 |
| M5-I02 | ✅ | codegen ownership | 顶层不直接拥有 generated source |
| M5-I03 | ✅ | CMake package | config/version/namespace/components |
| M5-I04 | ✅ | installed consumer | 外部 configure/build/run |
| M5-I05 | ✅ | Debug/Release preset | 本地与 CI 一致 |
| M5-I06 | ✅ | Ubuntu 26.04 CI workflow | Debug/Release matrix |
| M5-I07 | ✅ | README 与双语文档 | 边界、构建、package |
| M5-C01 | ✅ | generated header 安装 | consumer 不依赖 private source path |
| M5-C02 | ✅ | 工程门禁 | Python/C++/install/consumer |

## M6：Direct call

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M6-I01 | ✅ | call CST/AST/binding shape | return/input/callee |
| M6-I02 | ✅ | direct-call Resolved IR | 三种 call layout |
| M6-I03 | ✅ | direct-call 文档基线 | README/coverage/design |
| M6-I04 | ✅ | canonical FunctionSignature | prototype/definition 统一 |
| M6-I05 | ✅ | call literal typing | formal-driven typing |
| M6-I06 | ✅ | argument compatibility | type/shape/space/alignment/pointer |
| M6-I07 | ✅ | function-local call `.param` | scope/lifetime/direction/address |
| M6-I08 | ✅ | call-context semantics | `::entry/::func`、adjacency、predication |
| M6-C01 | ✅ | ABI checker | arity/order/type/shape/literal |
| M6-C02 | ✅ | direct-call regression | prototype/extern/recursive/error |
| M6-C03 | ✅ | docs/package regression | installed consumer 闭环 |

## M7：Indirect call 与 control-flow metadata

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M7-I01 | ✅ | `.callprototype` | grammar/signature/scope/range |
| M7-I02 | ✅ | `.calltargets` | ordered target list |
| M7-I03 | ✅ | `.branchtargets` | function-local identity |
| M7-I04 | ✅ | metadata symbol kind | stable SymbolId |
| M7-I05 | ✅ | metadata declaration semantics | order/member/scope/duplicate |
| M7-I06 | ✅ | indirect callee value | register/prototype/target-set |
| M7-I07 | ✅ | indirect-call layout | target/metadata shape |
| M7-C01 | ✅ | indirect ABI checker | canonical signature reuse |
| M7-C02 | ✅ | `brx.idx` integration | index/target-set/current function |
| M7-C03 | ✅ | 删除 temporary special case | generic layout diagnostic |
| M7-C04 | ✅ | indirect-control-flow corpus | installed consumer |

## M8：Module grammar、nested scope 与 recovery

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M8-I01 | ✅ | nested block CST/AST | 独立 node/range |
| M8-I02 | ✅ | nested lexical scope | shadowing/visibility/recursive pipeline |
| M8-I03 | ✅ | `.file` | lossless payload |
| M8-I04 | ✅ | `.loc` | basic/extended payload |
| M8-I05 | ✅ | `.section` | matched brace/raw DWARF payload |
| M8-I06 | ✅ | `.pragma` | module/header/body placement |
| M8-I07 | ✅ | kernel-resource directives | maxnreg/maxntid/reqntid/minnctapersm |
| M8-I08 | ✅ | directive registry | PTX 9.3 directive boundary |
| M8-I09 | ✅ | DiagnosticCollection | optional value + ordered diagnostics |
| M8-I10 | ✅ | recovery node | inserted/skipped/error |
| M8-I11 | ✅ | synchronization point | bounded module recovery |
| M8-I12 | ✅ | CST round-trip | byte-faithful sourceText |
| M8-I13 | ✅ | lexer/CST fuzz harness | arbitrary bytes/malformed nesting |
| M8-I14 | ⏸ | optional fixed-address evidence | 等待规范或可复现 `ptxas` |
| M8-C01 | ✅ | recovered CST → AST | recovery node CST-only |
| M8-C02 | ✅ | directive binding/semantic | file/debug identity |
| M8-C03 | ✅ | real PTX module corpus | no silent drop |

## M9：Simulator MVP frontend coverage

M9 milestone 已完成；`M9-C03` 单独暂停，不再把 milestone 标为进行中。

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M9-I01 | ✅ | opcode coverage manifest | syntax/resolved/checker/simulator 状态 |
| M9-I02 | ✅ | simulator MVP corpus | 固定最小 kernel/opcode |
| M9-I03 | ✅ | `ret` | frozen slice |
| M9-I04 | ✅ | `exit` | context/availability |
| M9-I05 | ✅ | `trap` | PTX 1.0 / target |
| M9-I06 | ✅ | `and.b32` | 全链条 |
| M9-I07 | ✅ | `or.b32` | 全链条 |
| M9-I08 | ✅ | `xor.b32` | 全链条 |
| M9-I09 | ✅ | `not.b32` | 全链条 |
| M9-I10 | ✅ | `shl.b32` | width/count |
| M9-I11 | ✅ | `shr.u32` | width/count |
| M9-I12 | ✅ | comparison domain | canonical enum |
| M9-I13 | ✅ | boolean domain | predicate combine |
| M9-I14 | ✅ | `setp` | `lt.u32` / `lt.and.u32` |
| M9-I15 | ✅ | `selp.u32` | predicate select |
| M9-I16 | ✅ | integer `cvt` | `cvt.s32.u32` |
| M9-I17 | ✅ | float `cvt` | `cvt.rn.f32.f64` |
| M9-I18 | ✅ | mixed `cvt` | int↔float |
| M9-I19 | ✅ | `cvta` | to/from global |
| M9-I20 | ✅ | integer `mul` | `mul.lo.u32` |
| M9-I21 | ✅ | float `mul` | `mul.rn.f32` |
| M9-I22 | ✅ | integer `mad` | `mad.lo.u32` |
| M9-I23 | ✅ | `fma` | `fma.rn.f32` |
| M9-I24 | ✅ | integer `div` | `div.u32` |
| M9-C01 | ✅ | domain/diagnostic integration | canonical generated domain |
| M9-C02 | ✅ | MVP corpus parse/check | PTX 9.3 / SM80 |
| M9-C03 | ⏸ | MVP functional execution | M19 恢复；adapter 驱动 simulator |

## M10：Modern instruction seed slices

当前分支中所有 M10 issue 的功能状态均为 ✅。

| ID | 状态 | Issue | 闭环摘要 |
| --- | --- | --- | --- |
| M10-I01 | ✅ | `ld/st` extension gap manifest | machine-readable gaps |
| M10-I02 | ✅ | cache-hint/eviction slice | L1 eviction + L2 hint |
| M10-I03 | ✅ | `ldu` | global scalar slice |
| M10-I04 | ✅ | `prefetch` | global/L1 slice |
| M10-I05 | ✅ | `membar` | scope/availability |
| M10-I06 | ✅ | `fence` | semantics/scope |
| M10-I07 | ✅ | `atom` | global relaxed CTA scalar |
| M10-I08 | ✅ | `red` | global relaxed CTA scalar |
| M10-I09 | ✅ | `activemask` | `.b32` |
| M10-I10 | ✅ | `vote.sync` | ballot `.b32` |
| M10-I11 | ✅ | `shfl.sync` | idx `.b32` |
| M10-I12 | ✅ | `cp.async` | `ca.shared.global` |
| M10-I13 | ✅ | `cp.async.commit_group` | group-state syntax |
| M10-I14 | ✅ | `cp.async.wait_group` | unsigned immediate |
| M10-I15 | ✅ | `cp.async.wait_all` | standalone wait |
| M10-I16 | ✅ | `ldmatrix` seed | `m8n8.x2.shared.b16` |
| M10-I17 | ✅ | `mma` seed | `m16n8k8.f32.f16.f16.f32` |
| M10-C01 | ✅ | memory-ordering domain | ld/st/fence/atom/red reuse |
| M10-C02 | ✅ | warp/matrix seed integration | generic descriptor/vector/availability |
| M10-C03 | ✅ | advanced-kernel corpus | positive + inline negative boundaries |

M10 的出口只声明以上 frozen slice，不声明 family complete。

M10 完成后的文档审查还规范了 `instructions/ptx_spec` 的 PTX ISA 9.3 §9.7 taxonomy：
§9.7.1～§9.7.5 合并为 `arithmetic.yaml`，其余现有指令按官方 family 分文件；该维护修正
不扩大 M10 的 frozen slice。

---

# 9. M11：PTX 9.3 基线、exhaustive ledger 与 target capability

## 目标

在继续扩展 opcode 前，先保证：

- PTX 9.3 的每个 instruction/directive/sreg 都被记录；
- target identity 能表达 generic、architecture-specific 和 family-specific profile；
- modern descriptor/fragment/tensor operand 有 schema 基础；
- 未支持 item 不再因为“不在 YAML 中”而不可见。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M11-I01 | ✅ | 独立 | 冻结 PTX ISA 9.3 规范基线 | 记录版本、发布日期、章节 URL 与更新策略；不得无审查追随 latest |
| M11-I02 | ✅ | 独立 | 建立 exhaustive instruction registry | PTX 9.3 §9.7 每个 family/opcode/sub-opcode 均有 record |
| M11-I03 | ✅ | 独立 | 建立 exhaustive directive registry | Chapter 11 与已知 pragma string 全部有 record |
| M11-I04 | ✅ | 独立 | 建立 exhaustive special-register registry | Chapter 10 每个 spelling、shape、type、availability 有 record |
| M11-I05 | ✅ | 独立 | 将 coverage 提升到 variant-slice 粒度 | 同一 opcode 的不同 topology/type/shape 可独立标记 |
| M11-I06 | ✅ | 独立 | 建立 disposition/reason schema | planned/deferred/out-of-scope/paused 均要求理由和 milestone |
| M11-I07 | ✅ | 独立 | 建立 `TargetArchitecture` 与 `TargetFlavor` | 保留 source spelling；numeric、`a`、`f` 不混淆 |
| M11-I08 | ✅ | 独立 | 扩展 availability expression | 支持 min PTX、numeric SM、exact target、feature capability 与 AND/OR |
| M11-I09 | ✅ | 独立 | 建立 validation `TargetProfile` catalog | 至少覆盖 sm80、sm90/sm90a、sm100/sm100a/sm100f；未知 target 不猜测 |
| M11-I10 | ✅ | 独立 | 支持 cluster dimension directives | `.reqnctapercluster/.explicitcluster/.maxclusterrank` CST/AST/semantic |
| M11-I11 | ✅ | 独立 | 补齐 1.0 modern directive subset | `.alias/.attribute/.noreturn/.abi_preserve/.abi_preserve_control/.blocksareclusters/.language` 与 `mma_throughput` provenance |
| M11-I12 | ✅ | 独立 | 支持 cluster special-register family | `%is_explicit_cluster/%clusterid/%nclusterid/%cluster_ctaid/%cluster_nctaid/%cluster_ctarank/%cluster_nctarank` |
| M11-I13 | ✅ | 独立 | 支持 modern smem/graph special-register family | reserved/aggr/dynamic/total smem 与 `%current_graph_exec` 的 type/availability |
| M11-I14 | ✅ | 独立 | 建立 complex modifier/shape lexical corpus | `::`、numeric-leading shape、comma-bearing form、collector/layout/kind round-trip |
| M11-I15 | ✅ | 独立 | 扩展 modern operand schema primitive | opaque descriptor、tensor coordinate、variable fragment cardinality、typed token |
| M11-I16 | ✅ | 独立 | 定义 real-PTX corpus provenance contract | fixture 记录生成器/toolkit/target/source/license/hash，禁止无来源 blob |
| M11-C01 | ✅ | 耦合 | 统一 instruction/directive/sreg availability | 三类 checker 共用 `TargetProfile` 与 diagnostic protocol |
| M11-C02 | ✅ | 耦合 | 建立 no-unaccounted-item CI gate | PTX 9.3 官方 inventory 新增/删除时 CI 给出结构化 diff |
| M11-C03 | ✅ | 耦合 | 建立 SM80/SM90a/SM100 multi-generation corpus | 支持项通过，未支持项明确 diagnostic，无 silent drop |

### 闭环证据（持续可验证）

- PTX ISA 9.3 证据固定到 CUDA 13.3.0 archive；digest 明确 `contents.html` subject 和
  `raw_response_body`。baseline 与 instruction/directive/special-register registry 的
  `source_url`/`evidence_url` 由 cross-artifact equality 测试分别对齐 archive
  `root`/`contents`，scheduled/manual workflow 可重算远端原始 bytes。
- instruction、directive 与 special-register registry 均通过 official inventory accounting
  join；每个官方 item 只有一个可解释的 support outcome，新增、缺失、重复或冲突均产生
  structured diff。
- generic、architecture-specific exact identity、`family` 与 capability 在同一
  `TargetProfile` 路径中分别验证。`family` 是最低 family-specific 源特性 target，只查
  `enabled_family_features`：`sm_100`/`sm_103` 为无，`sm_100f`/`sm_100a` 为 `sm_100f`，
  `sm_103f`/`sm_103a` 为 `sm_100f`+`sm_103f`，`sm_120f` 仅为 `sm_120f`。此集合不由数值或
  suffix 推导，也不表示当前未建模的 PTX-to-physical-GPU translation compatibility；numeric
  target/SM 受 `uint32_t` 范围约束。
- modern operand pack 的 cardinality、element shape/type 与 layout specificity 均进入运行时
  contract；normalization 在生成前拒绝不可比较的 overlap，typed element 检查覆盖至第 64 项。
- synthetic modern-operand outputs 以 `gen_all.py --list-outputs` 为权威清单；spec 内容变更通过
  `CMAKE_CONFIGURE_DEPENDS` 自动重配置 output/source topology。topology regression 每次运行都在
  唯一的 nested temporary fixture/build 中执行，repo source 与主 build 均只读不变，因此可并发；
  它验证 category 切换、编译与无修改 no-op。incremental build 对缺失 header 自愈，并由 backend
  schema 变更正确触发生成输出失效与重建。
- corpus provenance 为每个 fixture 保存有序 `targets`；multi-target corpus 保留完整 directive
  sequence，删除、重排或追加 target 都会触发 provenance mismatch。

M11 的完成状态以前述离线测试以及最终 Debug/Release workflow、installed consumer、online
archive digest verifier 和 `git diff --check` 全部通过为前置条件；这些是持续门禁，不记录
一次性运行数量。

### 出口

```text
PTX 9.3 official inventory
        |
        v
exhaustive machine-readable ledger
        |
        +--> target capability catalog
        +--> generated coverage report
        +--> CI no-unaccounted-item gate
```

---

# 10. M12：Common compiler-generated scalar/data-movement closure

## 目标

现代 matrix kernel 仍依赖大量普通 scalar/control/address 指令。M12 以固定的
SM80/SM90a/SM100 compiler corpus 为准，补齐常见 scaffolding，避免“能识别 WGMMA，却在
前一条 `lop3` 或 `prmt` 上失败”。

每个 opcode issue 只实现固定 corpus 所需的明确 slice；不追求完整 historical cross-product。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M12-I01 | ⬜ | 独立 | 建立 common-kernel gap manifest | 按 corpus 统计 opcode/variant frequency、first blocker 与 profile |
| M12-I02 | ⬜ | 独立 | 支持 `set` common slice | integer/float result topology 与 compare/boolean modifier 冻结 |
| M12-I03 | ⬜ | 独立 | 扩展 `setp` common slice | dual-predicate output 与 common compare operators |
| M12-I04 | ⬜ | 独立 | 支持 `slct` common slice | predicate/value/result type 约束 |
| M12-I05 | ⬜ | 独立 | 扩展 `add` common slice | u32/s32/u64/f32 的 corpus variants |
| M12-I06 | ⬜ | 独立 | 扩展 `sub` common slice | u32/s32/u64/f32 的 corpus variants |
| M12-I07 | ⬜ | 独立 | 扩展 `mul` common slice | integer high/wide 与 common float variant 中 corpus 所需部分 |
| M12-I08 | ⬜ | 独立 | 扩展 `mad` common slice | corpus 所需 integer/float topology |
| M12-I09 | ⬜ | 独立 | 扩展 `fma` common slice | f16/f32/f64 中 corpus 所需 variant |
| M12-I10 | ⬜ | 独立 | 扩展 `div` common slice | integer/float rounding 与 target rule |
| M12-I11 | ⬜ | 独立 | 支持 `rem` common slice | signed/unsigned type 与 zero-divisor boundary |
| M12-I12 | ⬜ | 独立 | 支持 `min` common slice | integer/float type 与 NaN modifier boundary |
| M12-I13 | ⬜ | 独立 | 支持 `max` common slice | integer/float type 与 NaN modifier boundary |
| M12-I14 | ⬜ | 独立 | 支持 `abs` common slice | integer/float type 与 saturation boundary |
| M12-I15 | ⬜ | 独立 | 支持 `neg` common slice | integer/float/packed boundary |
| M12-I16 | ⬜ | 独立 | 支持 `lop3` common slice | truth-table immediate 与 type/width |
| M12-I17 | ⬜ | 独立 | 支持 `shf` common slice | direction/mode/type/shift-count |
| M12-I18 | ⬜ | 独立 | 支持 `prmt` common slice | selector immediate、source/result width |
| M12-I19 | ⬜ | 独立 | 支持 `popc` common slice | source/result type |
| M12-I20 | ⬜ | 独立 | 支持 `clz` common slice | signedness/width/result |
| M12-I21 | ⬜ | 独立 | 支持 `bfind` common slice | shift amount、type、availability |
| M12-I22 | ⬜ | 独立 | 支持 `bfe` common slice | offset/width operands 与 type |
| M12-I23 | ⬜ | 独立 | 支持 `bfi` common slice | insert/base/offset/width |
| M12-I24 | ⬜ | 独立 | 支持 `brev` common slice | fixed bit width |
| M12-I25 | ⬜ | 独立 | 扩展 `cvt` common slice | corpus 中整数/浮点/packed/rounding variants |
| M12-I26 | ⬜ | 独立 | 支持 `cvt.pack` common slice | pack topology、sat、destination layout |
| M12-I27 | ⬜ | 独立 | 支持 `isspacep` common slice | address identity、state space、result predicate |
| M12-I28 | ⬜ | 独立 | 支持 `ld.global.nc` common slice | type/cache/target rule |
| M12-I29 | ⬜ | 独立 | 支持 `prefetchu` common slice | uniform address、level、availability |
| M12-I30 | ⬜ | 独立 | 支持 `createpolicy` common slice | policy operand/result 与 cache domain |
| M12-I31 | ⬜ | 独立 | 支持 `applypriority` common slice | address/range/policy semantics |
| M12-I32 | ⬜ | 独立 | 支持 `discard` common slice | address/size/target constraints |
| M12-I33 | ⬜ | 独立 | 支持 `setmaxnreg` common slice | action、immediate、warpgroup/target rule |
| M12-C01 | ⬜ | 耦合 | 统一 common scalar domain | compare/rounding/saturation/width/type diagnostic 无重复实现 |
| M12-C02 | ⬜ | 耦合 | 打通 common compiler-kernel corpus | 三个 target profile 的普通 scaffolding 均可 parse/resolve/check |
| M12-C03 | ⬜ | 耦合 | 回写 exhaustive ledger | corpus frequency、support status 与 deferred reason 同步 |

### 出口

M12 结束后，Hopper/Blackwell kernel 不应在进入 modern instruction 之前被常见 scalar
scaffolding 阻断。

---

# 11. M13：Cluster、proxy 与 mbarrier

## 目标

建立 Hopper/Blackwell async instruction 共用的同步基础。M13 只建模 frontend identity 与
instruction-local legality，不模拟 runtime barrier phase。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M13-I01 | ⬜ | 独立 | 支持 `bar.warp.sync` | member mask、barrier semantics、target |
| M13-I02 | ⬜ | 独立 | 支持 `barrier.cluster` | arrive/wait slice、cluster capability |
| M13-I03 | ⬜ | 独立 | 支持 `match.sync` | mask/value/result topology |
| M13-I04 | ⬜ | 独立 | 支持 `redux.sync` | operation/type/mask/result |
| M13-I05 | ⬜ | 独立 | 支持 `elect.sync` | leader predicate 与 optional lane result |
| M13-I06 | ⬜ | 独立 | 支持 `mapa` | peer CTA rank、shared address、cluster scope |
| M13-I07 | ⬜ | 独立 | 支持 `getctarank` | shared address 与 rank result |
| M13-I08 | ⬜ | 独立 | 支持 `griddepcontrol` | launch/wait topology 与 target |
| M13-I09 | ⬜ | 独立 | 建立 mbarrier operand domain | object address、state token、phase、tx-count、layout |
| M13-I10 | ⬜ | 独立 | 支持 `mbarrier.init` | address/count/alignment/space |
| M13-I11 | ⬜ | 独立 | 支持 `mbarrier.inval` | object lifecycle boundary |
| M13-I12 | ⬜ | 独立 | 支持 `mbarrier.expect_tx` | expected transaction bytes/count |
| M13-I13 | ⬜ | 独立 | 支持 `mbarrier.complete_tx` | complete count/space/scope |
| M13-I14 | ⬜ | 独立 | 支持 `mbarrier.arrive` | state token output、count、semantics |
| M13-I15 | ⬜ | 独立 | 支持 `mbarrier.arrive_drop` | drop count 与 token |
| M13-I16 | ⬜ | 独立 | 支持 `cp.async.mbarrier.arrive` | legacy cp.async completion bridge |
| M13-I17 | ⬜ | 独立 | 支持 `mbarrier.test_wait` basic slice | token/phase/predicate |
| M13-I18 | ⬜ | 独立 | 支持 `mbarrier.try_wait` basic slice | token/phase/suspend hint |
| M13-I19 | ⬜ | 独立 | 支持 PTX 9.3 wait extension | `.phase_type::*`、reportPredicate、reportValue |
| M13-I20 | ⬜ | 独立 | 支持 `mbarrier.pending_count` | state token 与 count result |
| M13-I21 | ⬜ | 独立 | 支持 `mbarrier.check_layout` | `.layout` qualifier 与 report result |
| M13-I22 | ⬜ | 独立 | 建立 proxy-kind 与 `fence.proxy` modern slice | async/tensormap 与 source/destination proxy identity |
| M13-I23 | ⬜ | 独立 | 支持 `clusterlaunchcontrol.try_cancel` | cancel token/status topology |
| M13-I24 | ⬜ | 独立 | 支持 `clusterlaunchcontrol.query_cancel` | query token/status topology |
| M13-C01 | ⬜ | 耦合 | 统一 mbarrier lifecycle domain | init/arrive/wait/tx/layout 共用唯一 token/phase 表示 |
| M13-C02 | ⬜ | 耦合 | 统一 cluster capability constraints | directive、sreg、address、scope、instruction 共同验证 |
| M13-C03 | ⬜ | 耦合 | 建立 synchronization corpus | CTA/cluster/mbarrier/proxy 正反例在 sm90a/sm100 profile 通过 |

### 出口

M14、M16、M17、M18 不得各自重新定义 barrier token、phase 或 async proxy。

---

# 12. M14：Tensor map、TMA 与 bulk/tensor async copy

## 目标

建模 PTX Tensor（Chapter 5.5）、tensor map、bulk/tensor copy 与 completion group，为真实 Hopper
TMA kernel 提供 parse/resolve/check。

完整 multimem 与 Fabric 不属于本 milestone。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M14-I01 | ⬜ | 独立 | 建立 Tensor dimension/format/access-mode domain | rank、element format、tile/im2col、swizzle/interleave 可表达 |
| M14-I02 | ⬜ | 独立 | 建立 `TensorMapRef` | descriptor identity、space、alignment、source range |
| M14-I03 | ⬜ | 独立 | 建立 tensor coordinate tuple | rank-dependent arity、signedness、register/immediate policy |
| M14-I04 | ⬜ | 独立 | 支持 `tensormap.replace` | replace field/value/type/target constraints |
| M14-I05 | ⬜ | 独立 | 支持 `tensormap.cp_fenceproxy` | tensor-map proxy 与 fence semantics |
| M14-I06 | ⬜ | 独立 | 支持 `cp.async.bulk` global→shared::cluster slice | mbarrier completion、size、space、target |
| M14-I07 | ⬜ | 独立 | 支持 `cp.async.bulk` shared::cta→global slice | bulk-group completion、size、space |
| M14-I08 | ⬜ | 独立 | 支持 PTX 9.3 bulk `.sem/.scope` | exact qualifier matrix 与 availability |
| M14-I09 | ⬜ | 独立 | 支持 `cp.reduce.async.bulk` first slice | reduction op/type/direction/completion |
| M14-I10 | ⬜ | 独立 | 支持 `cp.async.bulk.prefetch` | level/address/size/target |
| M14-I11 | ⬜ | 独立 | 支持 `cp.async.bulk.tensor` tiled global→shared | tensor map、coords、mbarrier |
| M14-I12 | ⬜ | 独立 | 支持 `cp.async.bulk.tensor` tiled shared→global | tensor map、coords、bulk group |
| M14-I13 | ⬜ | 独立 | 支持 `cp.reduce.async.bulk.tensor` first slice | tensor reduction topology |
| M14-I14 | ⬜ | 独立 | 支持 `cp.async.bulk.prefetch.tensor` tiled slice | tensor map/coords/cache |
| M14-I15 | ⬜ | 独立 | 支持 tensor copy `im2col` mode | bbox/traversal stride/coords |
| M14-I16 | ⬜ | 独立 | 支持 `im2col::w` mode | wHalo/wOffset/target restriction |
| M14-I17 | ⬜ | 独立 | 支持 `.tile::scatter4/.tile::gather4` mode | coordinate topology 与 boundary |
| M14-I18 | ⬜ | 独立 | 支持 `cp.async.bulk.commit_group` | bulk-group identity |
| M14-I19 | ⬜ | 独立 | 支持 `cp.async.bulk.wait_group` | immediate group count |
| M14-I20 | ⬜ | 独立 | 支持 `st.async` first slice | completion/space/type/target |
| M14-I21 | ⬜ | 独立 | 支持 `st.bulk` first slice | size/space/target |
| M14-I22 | ⬜ | 独立 | 支持 `red.async` first slice | reduction/completion/space |
| M14-C01 | ⬜ | 耦合 | 统一 async completion domain | async-group、bulk-group、mbarrier 使用同一 completion model |
| M14-C02 | ⬜ | 耦合 | 统一 tensor constraints | map rank、mode、coords、direction、space、swizzle 集中验证 |
| M14-C03 | ⬜ | 耦合 | 建立 TMA corpus | sm90a/sm100 tiled、im2col、bulk 正例与邻接负例 |

### 出口

真实 TMA source 可以无损进入 AST，形成 typed tensor-map/coordinate Resolved IR，并完成
instruction-local target check。

---

# 13. M15：Warp-level matrix、sparse MMA 与 WMMA compatibility

## 目标

把 M10 的单一 `ldmatrix`/`mma` seed 提升为可复用 matrix domain，覆盖 Ampere 之后常见
warp-level matrix topology，并为 WGMMA/TCGEN05 提供公共 shape/type/layout 基础。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M15-I01 | ⬜ | 独立 | 建立 `MatrixShape` | M/N/K 与 instruction family identity |
| M15-I02 | ⬜ | 独立 | 建立 matrix element/accumulator type domain | f16/bf16/tf32/f32/f64/int/fp8/packed |
| M15-I03 | ⬜ | 独立 | 建立 matrix layout 与 fragment cardinality | row/col、register tuple 数量由 data 生成 |
| M15-I04 | ⬜ | 独立 | 扩展 `ldmatrix` x1/x4 slice | destination cardinality/shape |
| M15-I05 | ⬜ | 独立 | 扩展 `ldmatrix` trans/modern type slice | transpose/type/target |
| M15-I06 | ⬜ | 独立 | 支持 `stmatrix` first slice | source fragment/address/layout |
| M15-I07 | ⬜ | 独立 | 支持 `movmatrix` first slice | source/destination fragment/transpose |
| M15-I08 | ⬜ | 独立 | 扩展 `mma` f16/bf16 slice | common m16n8 shape 与 fragment |
| M15-I09 | ⬜ | 独立 | 支持 `mma` tf32 slice | type/shape/layout/availability |
| M15-I10 | ⬜ | 独立 | 支持 `mma` f64 slice | fragment/cardinality/target |
| M15-I11 | ⬜ | 独立 | 支持 `mma` integer slice | signedness/satfinite/shape |
| M15-I12 | ⬜ | 独立 | 支持 `mma` fp8 slice | e4m3/e5m2 input/accumulator |
| M15-I13 | ⬜ | 独立 | 支持 `mma.sync` block-scaling first slice | scale vectors/IDs/type/shape |
| M15-I14 | ⬜ | 独立 | 建立 sparse metadata domain | metadata register/selector/ordering |
| M15-I15 | ⬜ | 独立 | 支持 `mma.sp` f16/bf16 first slice | sparse A、metadata、fragment |
| M15-I16 | ⬜ | 独立 | 支持 `mma.sp` tf32 slice | shape/type/metadata |
| M15-I17 | ⬜ | 独立 | 支持 `mma.sp` integer/fp8 slice | type/shape/selector |
| M15-I18 | ⬜ | 独立 | 支持 `mma.sp::ordered_metadata` slice | metadata order contract |
| M15-I19 | ⬜ | 独立 | 支持 `wmma.load` compatibility slice | fragment/layout/address/target |
| M15-I20 | ⬜ | 独立 | 支持 `wmma.store` compatibility slice | fragment/layout/address/target |
| M15-I21 | ⬜ | 独立 | 支持 `wmma.mma` compatibility slice | A/B/C/D fragment contract |
| M15-C01 | ⬜ | 耦合 | 统一 matrix fragment constraint | shape/type/layout/cardinality 单一数据源 |
| M15-C02 | ⬜ | 耦合 | 统一 sparse metadata constraint | mma.sp 与后续 WGMMA/TCGEN05 可复用基础 |
| M15-C03 | ⬜ | 耦合 | 建立 warp-matrix corpus | sm75/sm80/sm90 matrix 正反例共同通过 |

### 出口

WGMMA 与 TCGEN05 milestone 不得再次用硬编码 tuple 数量表示 matrix fragment。

---

# 14. M16：Hopper WGMMA

## 目标

完整建模 WGMMA 的 operand topology、shared-memory descriptor、warpgroup 和 async proxy，
并为每种主要 topology 提供 representative slice。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M16-I01 | ⬜ | 独立 | 建立 `WarpGroup` participation domain | 128-thread group identity 与 target capability |
| M16-I02 | ⬜ | 独立 | 建立 WGMMA shared-memory descriptor | bitfield/source identity/alignment/layout |
| M16-I03 | ⬜ | 独立 | 建立 WGMMA register-fragment cardinality | m64nNk* shape 驱动 destination tuple |
| M16-I04 | ⬜ | 独立 | 建立 WGMMA shape/type/layout domain | N/K/type/major/source-placement 可表达 |
| M16-I05 | ⬜ | 独立 | 建立 WGMMA async-proxy metadata | issue/completion/fence obligation 暴露给 consumer |
| M16-I06 | ⬜ | 独立 | 支持 `wgmma.mma_async` f16 first slice | descriptor/register source topology |
| M16-I07 | ⬜ | 独立 | 支持 `wgmma.mma_async` bf16 first slice | shape/type/accumulator |
| M16-I08 | ⬜ | 独立 | 支持 `wgmma.mma_async` tf32 first slice | source descriptor/layout |
| M16-I09 | ⬜ | 独立 | 支持 `wgmma.mma_async` fp8 first slice | e4m3/e5m2 combination |
| M16-I10 | ⬜ | 独立 | 支持 `wgmma.mma_async` integer first slice | s8/u8 type/satfinite |
| M16-I11 | ⬜ | 独立 | 支持 WGMMA accumulator scale/transpose slice | scale-d/scale-a/scale-b/transpose rule |
| M16-I12 | ⬜ | 独立 | 建立 WGMMA sparse metadata specialization | metadata/selector 与 sparse shape |
| M16-I13 | ⬜ | 独立 | 支持 `wgmma.mma_async.sp` first slice | sparse topology 全链条 |
| M16-I14 | ⬜ | 独立 | 支持 `wgmma.fence` | proxy/fence syntax 与 target |
| M16-I15 | ⬜ | 独立 | 支持 `wgmma.commit_group` | async group commit |
| M16-I16 | ⬜ | 独立 | 支持 `wgmma.wait_group` | immediate group count/result visibility |
| M16-C01 | ⬜ | 耦合 | 统一 WGMMA descriptor/fragment/shape | 所有 slice 复用 M15 matrix domain |
| M16-C02 | ⬜ | 耦合 | 暴露 WGMMA protocol obligation | Resolved IR 明确 fence/commit/wait metadata，不做 CFG proof |
| M16-C03 | ⬜ | 耦合 | 建立 Hopper WGMMA corpus | real/handwritten sm90a positive 与 target/shape negative |

### 出口

Hopper WGMMA family 的主要 operand topology 均有 typed representation；未支持 shape/type
组合由 data-driven checker 明确拒绝。

---

# 15. M17：Blackwell Tensor Memory 与 TCGEN05 data movement

## 目标

先建模 Tensor Memory 本身，再接入 TCGEN05 allocation、load/store、copy、shift 和 wait。
M17 不实现 TCGEN05 MMA。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M17-I01 | ⬜ | 独立 | 建立 `TensorMemoryAddress` | 与 generic/global/shared address 分离，保留 typed identity |
| M17-I02 | ⬜ | 独立 | 建立 CTA group/pair/peer-CTA domain | `.cta_group::1/2`、pair role、issue granularity |
| M17-I03 | ⬜ | 独立 | 建立 tensor-memory allocation permit domain | column count、allocator CTA、result address |
| M17-I04 | ⬜ | 独立 | 建立 TCGEN data-movement shape domain | `.32x32b/.16x64b/.16x128b/.16x256b/.16x32bx2` |
| M17-I05 | ⬜ | 独立 | 建立 TCGEN shared-memory descriptor | major/stride/swizzle/source identity |
| M17-I06 | ⬜ | 独立 | 建立 TCGEN instruction descriptor | typed descriptor 与合法 bitfield/value |
| M17-I07 | ⬜ | 独立 | 建立 zero-column-mask descriptor | mask shape/source/target |
| M17-I08 | ⬜ | 独立 | 建立 canonical TCGEN stride/layout domain | relative/absolute LD、stride、swizzle、alignment |
| M17-I09 | ⬜ | 独立 | 支持 `tcgen05.alloc` | CTA group、column count、result tmem address |
| M17-I10 | ⬜ | 独立 | 支持 `tcgen05.dealloc` | address/column count/CTA group |
| M17-I11 | ⬜ | 独立 | 支持 `tcgen05.relinquish_alloc_permit` | permit ownership/target |
| M17-I12 | ⬜ | 独立 | 支持 `tcgen05.ld` `.32x32b` slice | tmem→register fragment |
| M17-I13 | ⬜ | 独立 | 支持 `tcgen05.ld` `.16x64b` slice | fragment cardinality |
| M17-I14 | ⬜ | 独立 | 支持 `tcgen05.ld` `.16x128b` slice | fragment cardinality |
| M17-I15 | ⬜ | 独立 | 支持 `tcgen05.ld` `.16x256b` slice | fragment cardinality |
| M17-I16 | ⬜ | 独立 | 支持 `tcgen05.ld` `.16x32bx2` slice | dual-block fragment |
| M17-I17 | ⬜ | 独立 | 支持 `tcgen05.st` first slice | register→tmem topology |
| M17-I18 | ⬜ | 独立 | 支持 `tcgen05.wait` | wait-kind/target/completion metadata |
| M17-I19 | ⬜ | 独立 | 支持 `tcgen05.cp` basic slice | shared/tmem descriptor、shape、direction |
| M17-I20 | ⬜ | 独立 | 支持 `tcgen05.cp` 4-bit decompression slice | source packing/destination type |
| M17-I21 | ⬜ | 独立 | 支持 `tcgen05.cp` 6-bit decompression slice | source packing/destination type |
| M17-I22 | ⬜ | 独立 | 支持 `tcgen05.shift` | tmem address/column shift/target |
| M17-C01 | ⬜ | 耦合 | 统一 Tensor Memory allocation constraints | alloc/dealloc/permit/CTA group 共用 domain |
| M17-C02 | ⬜ | 耦合 | 统一 TCGEN data movement constraints | shape/layout/descriptor/fragment/cardinality 集中 |
| M17-C03 | ⬜ | 耦合 | 建立 Blackwell tmem corpus | allocation、ld/st/cp/shift/wait 正反例 |

### 出口

TCGEN05 MMA milestone 可以直接消费 typed TensorMemoryAddress、CTA group、descriptor 和
data-movement shape，而不再创建第二套表示。

---

# 16. M18：Blackwell TCGEN05 MMA 与同步

## 目标

覆盖 TCGEN05 dense/sparse/WS MMA 的主要 operand topology，并接入 specialized fence 与
mbarrier-based commit completion。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M18-I01 | ⬜ | 独立 | 建立 TCGEN MMA kind domain | f16/tf32/f8f6f4/mxf8f6f4/i8/mxf4/mxf4nvf4 |
| M18-I02 | ⬜ | 独立 | 建立 TCGEN MMA shape/descriptor constraint | M/N/K、CTA group、A/B/D placement |
| M18-I03 | ⬜ | 独立 | 建立 major/stride/swizzle valid-combination table | 规范表 machine-readable |
| M18-I04 | ⬜ | 独立 | 建立 TCGEN data-path layout A～G | layout identity 与 shape/CTA/WS 条件 |
| M18-I05 | ⬜ | 独立 | 建立 block-scaling domain | scale vector size、A/B ID、block size、K |
| M18-I06 | ⬜ | 独立 | 建立 TCGEN sparsity metadata domain | selector、metadata layout、alignment |
| M18-I07 | ⬜ | 独立 | 支持 `tcgen05.mma` f16 dense slice | descriptor/tmem/accumulate topology |
| M18-I08 | ⬜ | 独立 | 支持 `tcgen05.mma` tf32 dense slice | kind/shape/layout |
| M18-I09 | ⬜ | 独立 | 支持 `tcgen05.mma` f8f6f4/mxf8f6f4 dense slice | packing/kind/descriptor |
| M18-I10 | ⬜ | 独立 | 支持 `tcgen05.mma` i8 dense slice | signedness/accumulator |
| M18-I11 | ⬜ | 独立 | 支持 `tcgen05.mma` mxf4/mxf4nvf4 dense slice | packing/scale/target |
| M18-I12 | ⬜ | 独立 | 支持 `tcgen05.mma` block-scaling slice | scale-factor descriptors |
| M18-I13 | ⬜ | 独立 | 支持 `tcgen05.mma.sp` first slice | sparse metadata/selector |
| M18-I14 | ⬜ | 独立 | 扩展 `tcgen05.mma.sp` modern low-bit slice | kind/packing/alignment |
| M18-I15 | ⬜ | 独立 | 支持 `tcgen05.mma.ws` first slice | WS mode、data-path layout、collector |
| M18-I16 | ⬜ | 独立 | 支持 `tcgen05.mma.ws.sp` first slice | WS+sparse topology |
| M18-I17 | ⬜ | 独立 | 支持 `tcgen05.fence` | specialized synchronization target/operand |
| M18-I18 | ⬜ | 独立 | 支持 `tcgen05.commit` | mbarrier completion、group/target |
| M18-I19 | ⬜ | 独立 | 统一 TCGEN completion metadata | implicit pipeline、mbarrier、`tcgen05.wait` identity |
| M18-C01 | ⬜ | 耦合 | 统一 TCGEN descriptor/shape/kind constraints | dense/sparse/WS 共用单一生成数据 |
| M18-C02 | ⬜ | 耦合 | 暴露 TCGEN synchronization obligation | thread/CTA/proxy obligation 可供 analyzer/simulator 消费 |
| M18-C03 | ⬜ | 耦合 | 建立 Blackwell MMA corpus | sm100a/sm100f profile 的 dense/sparse/WS 正反例 |

### 出口

1.0 frontend 对 TCGEN05 family 的主要 topology 有稳定 typed representation；完整 type/shape
cross-product 可以继续作为 data-only expansion，不要求破坏 public contract。

---

# 17. M19：稳定 API、adapter、conformance 与 1.0

## 目标

在 modern operand/domain 稳定后冻结 public contract，并完成最低限度的 simulator integration。
现代 async/matrix 指令在 1.0 只要求 frontend parse/resolve/check；simulator execution 可返回稳定的
`unsupported` capability，不得 silent no-op。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M19-I01 | ⬜ | 独立 | 建立 diagnostic code/severity | error/warning/note 与稳定 code |
| M19-I02 | ⬜ | 独立 | 提供统一 frontend session API | source→parse→bind→semantic→resolve→check |
| M19-I03 | ⬜ | 独立 | 定义 unsupported-feature contract | syntax-preservation、resolver diagnostic、checker diagnostic 边界稳定 |
| M19-I04 | ⬜ | 独立 | 提供稳定只读 Resolved IR visitor | consumer 不依赖 private generated type/variant index |
| M19-I05 | ⬜ | 独立 | 提供 modern descriptor/tensor/matrix public views | opaque但typed，可查询 identity/shape/layout/source range |
| M19-I06 | ⬜ | 独立 | 定义 Resolved IR serialization | schema/version、golden、unknown-field policy |
| M19-I07 | ⬜ | 独立 | 定义 public API version policy | 1.x compatibility/deprecation/migration |
| M19-I08 | ⬜ | 独立 | 建立 generated-output reproducibility | 相同输入 byte-identical |
| M19-I09 | ⬜ | 独立 | 冻结 deterministic diagnostic ordering | parser/binding/semantic/resolver/checker 跨阶段稳定 |
| M19-I10 | ⬜ | 独立 | 扩展 CI matrix | 受支持的触发路径、GCC、Clang、ASan、UBSan、fuzz smoke |
| M19-I11 | ⬜ | 独立 | 建立 optional `ptxas` conformance harness | tool 可用时运行；checked-in evidence 可离线复核 |
| M19-I12 | ⬜ | 独立 | 定义 `ptxsim` adapter contract | visitor/value/address/control/unsupported behavior |
| M19-I13 | ⬜ | 独立 | 建立 downstream compatibility suite | installed public API 构建独立 adapter |
| M19-I14 | ⬜ | 独立 | 建立 installed modern-profile consumer | SM80/SM90a/SM100 source 只依赖安装包 |
| M19-I15 | ⬜ | 独立 | 完成 release 文档与 migration guide | README、coverage、roadmap、API docs 一致 |
| M19-C01 | ⬜ | 耦合 | 解除并完成 `M9-C03` | core-sm80 kernel text→ResolvedModule→functional simulator |
| M19-C02 | ⬜ | 耦合 | 建立 modern 1.0 RC gate | 三 profile parse/resolve/check，unsupported 均稳定可见 |
| M19-C03 | ⬜ | 耦合 | 发布 1.0 contract | package/API/diagnostic/serialization/coverage schema 冻结 |

### 1.0 最低完成标准

- M0～M19 的必要 issue 完成；
- M8-I14 可以继续暂停，但必须在 ledger 中明确排除；
- M10 frozen slices 与其 PTX ISA 9.3 taxonomy 维护修正保持完整；
- PTX 9.3 instruction/directive/sreg 无 unaccounted item；
- `core-sm80` 能完成最小 functional execution；
- `hopper-sm90a` 能完成 cluster/mbarrier/TMA/WGMMA parse/resolve/check；
- `blackwell-sm100` 能完成 Tensor Memory/TCGEN05 parse/resolve/check；
- 未支持 variant 有稳定 diagnostic；
- target generic/`a`/`f` capability 判定正确；
- modern descriptor/tensor/matrix public view 稳定；
- GCC/Clang、Debug/Release、ASan/UBSan、fuzz、package、consumer 全通过；
- generated output 可复现；
- README、双语 coverage、roadmap、manifest 与代码一致。

---

# 18. 关键依赖关系

## 18.1 Frontend coverage 主关键路径

```text
M10 seed slices
      |
      v
M11 exhaustive ledger + target capability
      |
      +-----------------------------+
      |                             |
      v                             v
M12 common kernel closure       M15 matrix foundation
      |                             |
      v                             v
M13 cluster/mbarrier            M16 WGMMA
      |
      +--------------+
      |              |
      v              v
M14 TMA         M17 Tensor Memory
                     |
                     v
                 M18 TCGEN05 MMA
                     |
                     v
                 M19 1.0 freeze
```

更精确地说：

- M14 依赖 M13 的 mbarrier/proxy；
- M16 依赖 M13 的 proxy 与 M15 的 matrix domain；
- M17 依赖 M11 target capability、M13 cluster/mbarrier 和 M15 descriptor基础；
- M18 依赖 M17 的 Tensor Memory/descriptor 与 M13 completion；
- M19 的 API infrastructure 可提前做，但 public type freeze 必须等待 M18。

## 18.2 可并行工作

M11 完成后可并行：

```text
M12 common scalar slices
M13 cluster/mbarrier independent opcode
M15 matrix domain independent issue
M19 diagnostics/CI/reproducibility infrastructure
```

并行约束：

- 不同时修改同一 backend enum/domain；
- 不在不同并行任务中各自定义 descriptor；
- 不同时重构 `ResolvedFieldValue`；
- 不同时改变 checker diagnostic protocol；
- 最终由 milestone 尾部耦合 issue 集成。

## 18.3 不得提前冻结

以下工作不得在 M18 前冻结：

- public visitor 对 matrix/tensor descriptor 的最终形态；
- serialization schema；
- stable enum numeric value；
- target capability public ABI；
- WGMMA/TCGEN05 fragment cardinality contract。

---

# 19. 当前推荐实施顺序

1. 以当前分支已完成的 M10 frozen slices 与 PTX ISA 9.3 taxonomy 维护修正为起点，不重复实现。
2. 实现 M11-I01～I09，先冻结 PTX 9.3 inventory、coverage schema 和 target capability。
3. 完成 M11 cluster directive/sreg、modern lexical corpus 和 no-unaccounted-item gate。
4. M12、M13 和 M15 的独立基础 issue 并行推进。
5. M13 完成后进入 M14 TMA。
6. M13 + M15 完成后进入 M16 WGMMA。
7. M11 + M13 + M15 完成后进入 M17 Tensor Memory。
8. M17 完成后进入 M18 TCGEN05 MMA。
9. M19 的 diagnostic/CI/reproducibility 可提前推进，但 API/serialization freeze 最后执行。
10. `M8-I14` 与 `M9-C03` 保持暂停，分别在取得证据和 M19 adapter 条件成熟后处理。

---

# 20. Roadmap 维护规则

1. 本文只维护当前分支的功能状态，不维护交付状态。
2. 功能状态只引用当前分支可验证的代码、测试与文档事实。
3. 不记录 commit hash、外部分支、托管平台或流水线 run 状态。
4. 一个 issue 只属于一个 milestone。
5. 独立 issue 位于耦合 issue 之前。
6. 每个 milestone 必须有明确出口。
7. instruction issue 必须是单 opcode 或单 variant slice。
8. descriptor/domain 必须先于使用它的 opcode。
9. public capability 变化同步 README。
10. syntax/semantic 边界变化同步双语 coverage。
11. YAML/schema 变化同时有 Python 与 C++ 测试。
12. generated file 不能代替 generator 修改。
13. consumer 需求通过 adapter/public view 进入 frontend，不把 simulator state 放入 IR。
14. future ISA item 必须先进入 exhaustive ledger。
15. official inventory 变化由 CI 报告，不允许人工遗忘。
16. unsupported item 不得 silent drop。
17. `partial` 不得在 prose 中写成 complete。
18. target family capability 不得退化为 SM 数值比较。
19. cross-instruction protocol proof 不得塞入 opcode resolver。
20. temporary fallback 必须记录删除 milestone。
21. milestone 最后一个 commit 必须同时 review code、coverage、README、双语 docs 与 roadmap。
22. `docs/deprecated/next_step.md` 保持冻结历史。
23. 本文与当前分支事实冲突时，以当前代码和可重复的本地验证为准并立即修本文。

---

# 21. Post-1.0 明确 backlog

以下内容已知但不阻塞 1.0：

| Family | Post-1.0 方向 |
| --- | --- |
| Multimem | `multimem.ld_reduce/st/red/st.async/red.async/cp.async.bulk/cp.reduce.async.bulk` |
| Fabric | CFT handle、try_get/put/red/pullred、submit、wait、fabric proxy fence |
| Texture | `tex/tld4/txq/istypep` |
| Surface | `suld/sust/sured/suq` |
| Stack | `stacksave/stackrestore/alloca` |
| Video | scalar/SIMD video instruction family |
| Extended precision | addc/subc/madc 与完整 carry-chain |
| Transcendental | rcp/sqrt/rsqrt/sin/cos/lg2/ex2/tanh 完整 cross-product |
| Legacy/deprecated | deprecated shfl/vote、legacy banks、历史 matrix variant |
| Full variant expansion | WGMMA/TCGEN05 全 shape/type/layout cross-product |
| Stateful analysis | CFG、async protocol、mbarrier phase、warpgroup/tcgen dependency proof |
| Modern simulator | TMA/WGMMA/TCGEN05 functional 与 timing execution |
| Timing model | 独立 analysis/simulator layer，不进入 frontend legality checker |

---

# 22. 最终验收原则

`ptx_frontend` 1.0 的价值不在于声称“支持整部 PTX”，而在于：

```text
已支持的 modern PTX
  -> source-faithful
  -> identity stable
  -> target-capability aware
  -> descriptor/shape typed
  -> diagnostics precise
  -> public API consumable

未支持的 official PTX
  -> exhaustive ledger 中可见
  -> 不 silent drop
  -> 有明确 disposition
  -> 可以按单 opcode / 单 slice 演进
```

只要这两个方向同时成立，1.0 就可以既诚实，又足以承载 SM80、Hopper 和 Blackwell
frontend consumer。
