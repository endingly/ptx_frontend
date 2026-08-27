# PTX Frontend 项目计划与 Roadmap

> 文档名称：[`.agents/project_roadmap.md`](project_roadmap.md)
> 仓库：`endingly/ptx_frontend`
> 状态视角：当前文档所在分支
> 功能事实基线：`dd9274812e7ce839bb15f902ed68771bf9178011`
> 基准提交内容：`fix: recover after unterminated debug sections`
> 基准日期：2026-08-27
> 项目阶段：pre-1.0
>
> 当前工作分支已完成除暂停的 M8-I14 外的 M8 实现与验证；本文状态以该分支的仓库事实
> 为准，不等待 PR 合入后再同步。

---

## 0. 本次基线核对与偏离 Review

### 0.1 结论

当前工作分支没有偏离此前确立的前端分层，并已完成：

- nested block CST/AST、词法作用域与 function-local control-flow identity；
- `.file`、`.loc`、`.section`、`.pragma` 与第一组 kernel-resource directive；
- ordered diagnostics、recovery node、synchronization 与 recovered CST → AST contract；
- CST round-trip、fuzz harness、debug metadata binding 与真实 PTX module corpus。

M8-I14 仍因缺少 optional fixed-address 的规范或工具链证据而暂停；其余 M8 功能项已在
当前工作分支实现并验证，因此统一标记为 ✅。

### 0.2 当前存在的文档偏离

#### D-01：README recovery 使用示例（本轮已修正）

根 `README.md` 的使用示例同时检查 AST value 与 diagnostics，避免把 recovered module
误当作无错误输入继续 resolve。

#### D-02：M8 状态与近期优先级（本轮已修正）

M8 功能项按当前工作分支的实现与验证事实标记为 ✅，不再因 PR 尚未合入而降级为 🚧。
下一主线是 M9 的 machine-readable opcode manifest 与 simulator MVP corpus；M8-I14
继续暂停。

#### D-03：已清理的 opcode-specific indirect-call 诊断

M7-C03 已删除 `resolve_fields()` 中的 opcode-string metadata fallback。合法 indirect
call 走正式 descriptor；malformed metadata-bearing call 走通用 layout diagnostic。

### 0.3 原 roadmap 本身的偏离

原 roadmap 中后期若干 instruction issue 粒度过大，例如单个 issue 同时包含十余条 floating-point instruction。这样的 issue 不能精确闭环，也不利于独立 review、bisect 和回滚。

本版将 instruction coverage 改为：

- 一个 opcode 一个 issue；或者
- 一个具有明确类型边界的 variant slice 一个 issue；
- 共用 domain 和端到端 corpus 才作为 milestone 尾部的耦合 issue。

---

# 1. 文档定位

本文是 `ptx_frontend` 的长期项目计划和 roadmap，用于统一记录：

1. 项目背景、边界以及与上下游项目的关系；
2. 当前仓库的组织方式和编译条件；
3. 各模块的职责和禁止承担的职责；
4. 项目实现所依据的规范、schema、设计文档和测试；
5. 当前文档所在分支已经实现并验证的能力；
6. 尚未实现的能力；
7. 后续 milestone 和精确闭环 issue；
8. milestone 内独立工作与耦合工作的顺序；
9. 当前实现与设计之间的已知偏离。

本文不等同于 release note，也不等同于某一个 PR 的 next-step 清单。

---

## 1.1 状态标记

| 标记 | 含义 |
| --- | --- |
| ✅ | 已完成：功能已在当前工作分支实现并验证，或文档 issue 已完成其同步 |
| 🚧 | 当前工作分支已经开始，但仍包含未完成 issue |
| ⬜ | 尚未实现 |
| ⏸ | 暂缓，必须先取得规范、实验或 consumer 证据 |
| ⚠️ | 已实现但存在待清理的技术债或文档偏离 |

功能在当前工作分支实现并完成相应验证后即可标记为 ✅；纯文档 issue 在其列出的同步完成
后也可标记为 ✅。PR 是否合入只表示交付状态，不改变当前分支已经成立的实现事实。

---

## 1.2 Issue 标识

每个 milestone 使用两类 issue：

- `M<n>-I<n>`：相对独立、可以单独实现、review、测试和闭环的 issue；
- `M<n>-C<n>`：耦合 issue，用于把此前已经完成的独立能力连接起来。

约束：

1. 一个 milestone 中，所有 `I` issue 必须位于 `C` issue 之前；
2. milestone 尾部的若干 issue 才允许是耦合 issue；
3. 耦合 issue 不得临时发明大块基础表示；
4. 如果耦合时发现缺少基础表示，必须新增独立 issue；
5. 一个 issue 只能属于一个 milestone。

---

# 2. 项目背景

## 2.1 项目要解决的问题

PTX 不是只包含 opcode 和若干逗号分隔 operand 的简单汇编文本。它具有：

- PTX ISA version；
- target SM 和 target family；
- module、function 和 nested lexical scope；
- variable、formal parameter、function、label 和 special-register identity；
- state space；
- scalar type 和 vector shape；
- modifier combination；
- operand role、access mode 和 layout；
- memory consistency 和 memory scope；
- direct/indirect control-flow metadata；
- declaration、initializer 和 constant-expression semantics；
- instruction-specific historical compatibility；
- source location 和 diagnostics。

仅将源码切分为 opcode 与字符串 operand，无法为 simulator、compiler pass 或静态分析器提供可靠输入。

`ptx_frontend` 的职责是把 PTX 源码逐步转换为：

```text
结构明确
+ identity 稳定
+ 类型已知
+ target-aware 可检查
+ 可以被下游消费
```

的前端表示。

---

## 2.2 核心流水线

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
lexical symbol binding
    |
    v
declaration semantics
    |
    v
Resolved IR
    |
    v
target-aware instruction checker
```

当前模块顺序在 CMake 中明确为：

```text
common
  -> base
  -> lexer
  -> cst
  -> syntax
  -> binding
  -> semantic
  -> resolved_ir
```



---

## 2.3 表示层之间的边界

### Lexer

回答：

> 这段源码由哪些 token 构成？

不回答：

> `.weak` 对当前 opcode 是否合法？

### CST

回答：

> 源码具体写成了什么结构？

不回答：

> 这个 identifier 绑定到哪个 declaration？

### Syntax AST

回答：

> 这段源码表达了哪一种语法结构？

不回答：

> 该 instruction 在 `sm_80` 上是否合法？

### Binding

回答：

> 这个名字指向哪个 symbol？

不回答：

> 这个 symbol 能否作为 `ld` address 或 `call` argument？

### Declaration semantics

回答：

> declaration、initializer、array、prototype 和 definition 本身是否合法？

不回答：

> 某条 instruction 的 operand combination 是否合法？

### Resolved IR

回答：

> 这条 instruction 选中了哪个 variant、哪个 layout，以及各 operand 的稳定语义是什么？

### Checker

回答：

> 该 Resolved IR 在指定 PTX ISA、SM 和 target family 上是否合法？

---

# 3. 与相关项目的关系

## 3.1 总体链条

```text
CUDA C++ / DSL / CUTLASS-CUTE / 手写 PTX / custom compiler
                              |
                              | 产生 PTX 文本
                              v
                     +------------------+
                     |   ptx_frontend   |
                     | parse / bind /   |
                     | resolve / check  |
                     +------------------+
                              |
                              | ResolvedModule
                              | diagnostics
                              v
                     +------------------+
                     |    ptxsim        |
                     | functional model |
                     | timing model     |
                     | trace            |
                     +------------------+
                              |
                              v
                 调度分析 / latency 分析 / kernel 优化
```

---

## 3.2 上游项目

### CUDA 编译器或 custom compiler

上游编译器负责：

- CUDA C++、DSL 或其他高层 IR 到 PTX 的 lowering；
- kernel ABI 和 function signature 的生成；
- module directive、function declaration 和 instruction stream 的输出；
- 高层 operation 到 PTX instruction sequence 的映射。

`ptx_frontend` 不负责 C++ 到 PTX 的 lowering，也不替代 NVCC、Clang NVPTX backend 或 custom compiler backend。

### CUTLASS、CUTE 和手写 kernel

CUTLASS/CUTE、模板化 kernel generator 和手写 PTX 可以作为 PTX 文本来源。

它们当前与 `ptx_frontend` 没有仓库级或 CMake 级硬依赖。

---

## 3.3 下游项目

### `ptxsim`

建议负责：

- thread/warp execution state；
- register 和 predicate state；
- PC 和 control-flow execution；
- functional memory model；
- PTX instruction execution semantics；
- execution trace。

### 职责边界

| 能力 | `ptx_frontend` | simulator |
| --- | --- | --- |
| 解析源码 | 是 | 否 |
| symbol binding | 是 | 否 |
| declaration legality | 是 | 否 |
| opcode variant selection | 是 | 否 |
| PTX/SM legality | 是 | 否 |
| 执行寄存器更新 | 否 | 是 |
| 模拟 memory state | 否 | 是 |
| warp scheduling | 否 | 是 |
| cycle/latency | 否 | 是 |
| execution trace | 只提供 source identity | 是 |

当前仓库的安装依赖只有自身组件、`fmt` 和 `magic_enum`，尚未直接链接 `ptxsim`。因此上述关系是预期架构，不是当前已经完成的 integration。

---

## 3.4 Target lowering 与 binary encoder

以下工作不属于本项目：

```text
ResolvedModule
  -> target-specific lowering
  -> custom ISA / SASS-like IR
  -> binary encoder
  -> cmodel binary
```

目标机器码编码、relocation、linking 和 runtime launch 应位于独立项目或 adapter 中。

---

# 4. 项目范围

## 4.1 本项目负责

- PTX tokenization；
- trivia 和 source text preservation；
- SourcePos 和 SourceRange；
- lossless CST；
- typed Syntax AST；
- module/function lexical scope；
- symbol binding；
- declaration semantics；
- constant-expression evaluation；
- instruction variant selection；
- modifier binding；
- operand-layout selection；
- typed Resolved IR；
- PTX ISA、SM 和 target-family availability；
- cross-operand 和 cross-modifier constraint；
- 安装式 CMake package；
- 面向下游 consumer 的公共前端 API；
- 精确且稳定的 diagnostics。

---

## 4.2 本项目不负责

- CUDA C++ 到 PTX lowering；
- PTX instruction execution；
- warp/thread machine state；
- functional 或 cycle-accurate simulation；
- PTX 到目标 binary 的编码；
- device linker 和 relocation；
- kernel launch；
- runtime driver；
- 根据未知 symbol spelling 猜测语义；
- 在没有规范依据时发明 grammar；
- 当前阶段的 CFG、SSA 或优化 pass。

CFG/SSA 如有需要，应在 Resolved IR 稳定后建立独立 analysis layer，而不是污染 parser、binding 或 checker。

---

# 5. 项目组织方式

## 5.1 仓库结构

```text
ptx_frontend/
├── submod/
│   ├── common/
│   ├── base/
│   ├── lexer/
│   ├── cst/
│   ├── syntax/
│   ├── binding/
│   ├── semantic/
│   └── resolved_ir/
├── instructions/
│   ├── ptx_spec/
│   ├── ptx_cpp_backend_spec/
│   └── schemas/
├── python/
│   ├── base/
│   ├── ir/
│   ├── code_gen/
│   ├── scripts/
│   └── tests/
├── cmake/
├── docs/
│   ├── deprecated/
│   │   └── next_step.md
│   ├── us-en/
│   └── zh-han/
├── .github/workflows/
├── .agents/
│   └── project_roadmap.md
├── AGENTS.md
├── CMakeLists.txt
├── CMakePresets.json
├── requirements.txt
├── vcpkg.json
└── README.md
```

---

## 5.2 两条实现主线

### C++ 前端主线

```text
source
  -> lexer
  -> CST
  -> Syntax AST
  -> binding
  -> declaration semantics
  -> Resolved IR
  -> checker
```

### 数据驱动生成主线

```text
PTX YAML facts
       +
C++ backend mapping
       +
schemas
       |
       v
Python loading / normalization
       |
       v
generated C++ types
generated descriptors
generated lookup
generated dispatch
generated checker data
generated category implementations
```

---

## 5.3 Instruction database

当前 instruction facts 位于：

```text
instructions/ptx_spec/
├── control_flow.yaml
├── data_movement.yaml
├── floating_point.yaml
├── integer_arith.yaml
└── parallel_sync_and_communication.yaml
```

C++ spelling 和 runtime representation 位于：

```text
instructions/ptx_cpp_backend_spec/ptx_frontend.yaml
```

Schema 位于：

```text
instructions/schemas/ptx-instr-v1.schema.yaml
instructions/schemas/ptx-cpp-backend-v1.schema.yaml
```

当前 control-flow YAML 已包含 `call` 和 `bra`；`call` 使用一个 `call_direct` variant 和三个固定 `kind: call` layout。

---

## 5.4 Generated code

生成代码属于 `resolved_ir` 的 build artifact：

```text
submod/resolved_ir/<binary-dir>/generated/
├── public/
└── private/
```

生成入口：

```text
python/scripts/gen_all.py
```

CMake 在 configure 阶段枚举输出，在 build 阶段生成：

- public Resolved IR；
- runtime lookup tables；
- syntax descriptor；
- resolved descriptor；
- checker descriptor；
- resolve/check dispatch；
- category implementation。

生成文件：

- 不手工修改；
- 不作为 source-of-truth；
- 不承诺 pre-1.0 ABI；
- 不应直接提交以替代生成器修改。

---

# 6. 编译条件和构建方式

## 6.1 当前编译条件

| 条件 | 当前要求 |
| --- | --- |
| CMake | 3.28 或更高 |
| C++ | C++23 |
| Project version | 0.0.1 |
| Preset compiler | GCC/G++ |
| Build system | Ninja |
| Python | Python 3 |
| Lexer generator | Flex |
| Format tool | `clang-format` |
| C++ dependencies | `fmt`、`magic_enum` |
| Test dependency | GoogleTest |
| Package manager | vcpkg manifest |
| Optional cache | ccache |

顶层 CMake 默认：

```text
CMAKE_CXX_STANDARD = 23
PTX_USE_CCACHE = ON
BUILD_TESTING = OFF
```



“GCC/G++”是 checked-in CI preset 的选择，不是公共 package 对 compiler vendor 的硬编码限制。

---

## 6.2 本地构建

安装 Python 依赖：

```bash
python3 -m pip install -r requirements.txt
```

Debug：

```bash
export VCPKG_ROOT=/path/to/vcpkg

cmake --preset ci-linux-gcc-debug
cmake --build --preset ci-linux-gcc-debug
ctest --preset ci-linux-gcc-debug --output-on-failure
```

Release：

```bash
cmake --preset ci-linux-gcc-release
cmake --build --preset ci-linux-gcc-release
ctest --preset ci-linux-gcc-release --output-on-failure
```

Python tests：

```bash
PYTHONPATH=python python3 -m unittest discover \
  -s python/tests \
  -t python \
  -p 'test_*.py' \
  -v
```

也可以运行完整 workflow preset：

```bash
cmake --workflow --preset ci-linux-gcc-debug
cmake --workflow --preset ci-linux-gcc-release
```

---

## 6.3 安装和外部消费

安装：

```bash
cmake --install out/build/ci-linux-gcc-debug
```

Consumer：

```cmake
find_package(
    ptx_frontend 0.0.1 CONFIG REQUIRED
    COMPONENTS resolved_ir
)

add_executable(consumer main.cpp)
target_link_libraries(
    consumer
    PRIVATE ptx_frontend::resolved_ir
)
```

当前导出组件：

```text
ptx_frontend
common
base
lexer
cst
syntax
binding
semantic
resolved_ir
```



---

## 6.4 当前 CI

当前 Linux CI：

- 只由 pull request 的 `opened`、`synchronize`、`reopened` 触发；
- 使用 Ubuntu 26.04；
- 同时运行 Debug 和 Release；
- 使用 GCC/G++、Ninja、vcpkg 和 ccache；
- 调用 CMake workflow preset；
- 当前没有 `main` push gate；
- 当前没有 Clang、ASan、UBSan 或 fuzz job。



---

# 7. 模块划分与职责

## 7.1 `common`

职责：

- `SourcePos`；
- `SourceRange`；
- location-bearing wrapper；
- 不依赖 PTX instruction domain 的通用工具。

禁止承担：

- scalar type；
- instruction grammar；
- symbol table；
- target checker。

---

## 7.2 `base`

职责：

- scalar type；
- scalar kind 和 width；
- PTX/SM 等基础 domain；
- special-register stable identity；
- special-register 当前 type、shape 和 intrinsic availability；
- 多个前端阶段共享的基础语义。

禁止承担：

- module scope；
- opcode-specific resolver；
- `mov` 专属历史规则；
- target-dependent Resolved IR mutation。

---

## 7.3 `lexer`

职责：

- 可重入 Flex scanner；
- token text；
- token SourceRange；
- leading trivia；
- literal、identifier、dot identifier、punctuation；
- 少量结构稳定的 module directive token。

当前 lexer 明确不把 opcode、type suffix、cache operator、memory scope 等全部枚举为专用 token，而是保留为 `Ident` 或 `DotIdent`，交给 parser 和 generated matcher 解释。

禁止承担：

- opcode legality；
- modifier combination；
- scope；
- target availability。

---

## 7.4 `cst`

职责：

- 对当前支持子集产生 lossless CST；
- 保留 token、trivia、source order 和 source range；
- 表示 module、function、declaration、expression 和 initializer；
- 表示 call group、callee、target-set syntax 和 branch target；
- 对未建模 header structure 明确报错。

禁止承担：

- symbol lookup；
- declaration compatibility；
- opcode variant selection；
- target checking。

“lossless”只针对成功进入当前 CST grammar 的受支持子集，不代表已经解析全部 PTX ISA。

---

## 7.5 `syntax`

职责：

- typed Syntax AST；
- CST 到 AST lowering；
- `PtxSyntaxParser` convenience facade；
- 保存 opcode、modifier、operand 和 declaration 的源码语义结构；
- call/branch 专用 operand shape。

禁止承担：

- stable symbol identity；
- target-dependent node mutation；
- instruction legality；
- 根据 spelling 猜测 symbol kind。

---

## 7.6 `binding`

职责：

- module root scope；
- function scope；
- lexical shadowing；
- variable、parameter、function 和 label symbol；
- parameterized name set；
- reference classification；
- stable `SymbolId`；
- declared、external、special-register 和 unresolved reference；
- function/label/call-parameter reference kind。

禁止承担：

- initializer shape；
- instruction operand type compatibility；
- PTX/SM availability；
- call ABI comparison。

---

## 7.7 `semantic`

职责：

- declaration semantics；
- array extent；
- 未定长首维推导；
- initializer nesting 和元素上限；
- scalar initializer；
- symbol-address initializer；
- linkage-compatible redeclaration；
- prototype/definition consistency；
- multiple definition；
- constant-expression evaluation。

后续还应承担：

- reusable `FunctionSignature` 构建；
- function-local call-argument `.param` declaration semantics；
- `.callprototype/.calltargets` declaration-level contract。

禁止承担：

- opcode suffix parsing；
- generated variant selection；
- runtime target-specific mutation。

---

## 7.8 `resolved_ir`

职责：

- selected variant；
- selected operand layout；
- typed runtime modifier；
- register、predicate、immediate、special register；
- symbol、address、function、branch target；
- register vector；
- direct-call target、return value 和 input argument group；
- standalone instruction resolution；
- module-aware resolution；
- target-aware checking；
- generated descriptor 和 dispatch。

当前 direct `call` 已增加：

```text
ResolvedFunctionRef
ResolvedCallParameterRef
ResolvedCallLiteral
ResolvedCallArguments
OperandLayoutKind::Call
```



禁止承担：

- simulator machine state；
- call instruction execution；
- target binary lowering；
- 在缺少 signature 时猜测 immediate type。

---

## 7.9 `instructions`

职责：

- instruction variant facts；
- modifier facts；
- operand facts；
- layout facts；
- availability；
- cross constraints；
- PTX facts与 C++ backend spelling 分离。

禁止：

- 在多个 C++ switch 中复制 YAML facts；
- 为 modifier 组合爆炸复制大量 variant；
- 把 source-absence sentinel 伪装成可拼写 syntax value；
- 把 C++ class name 写入 PTX fact 层。

---

## 7.10 `python`

职责：

- schema loading；
- YAML normalization；
- typed Python model；
- backend model；
- output topology；
- generated public types；
- generated descriptors；
- generated lookup；
- generated dispatch；
- generated category implementations；
- generator/model tests。

---

## 7.11 `cmake`、CI、文档和治理

`cmake/`：

- generated source registration；
- install/export；
- package config；
- component handling。

`.github/workflows/`：

- build/test gate。

`docs/`：

- 双语设计文档；
- syntax coverage；
- YAML 和 generator 说明。

`AGENTS.md` 与 `.agents/`：

- agent routing；
- design、implementation、validation 和 repository-operation 责任边界。

---

# 8. 模块边界硬规则

1. Lexer 不枚举全部 PTX opcode 和 modifier。
2. CST 不执行语义判断。
3. Syntax AST 不依赖 target。
4. Binding 只解析 identity，不判断 opcode legality。
5. Declaration semantics 与 instruction semantics 分离。
6. 已建模 instruction 的事实优先写入 YAML。
7. C++ spelling 和 PTX fact 分离。
8. Generated file 不手工修改。
9. 未知信息必须保持未知。
10. Register address 或 standalone unresolved address 不推断 state space。
11. Resolved IR 保持 target-independent identity。
12. Target compatibility 在 checker 中临时应用。
13. 新增语义必须保存 SourceRange。
14. 公共 API 变更必须有 installed consumer test。
15. Temporary opcode-specific fallback 必须进入技术债清单并设置删除 milestone。
16. 新 opcode 不得要求修改 lexer，除非它引入真正新的 lexical form。
17. 一个 issue 不得顺带实现另一个 instruction family。

---

# 9. 参照文档

## 9.1 规范和证据优先级

冲突时采用：

1. NVIDIA PTX ISA 的规范性 grammar 和 instruction section；
2. 可复现的 `ptxas` 正反例；
3. `instructions/schemas`；
4. `instructions/ptx_spec`；
5. C++ backend spec；
6. 本仓库设计文档；
7. C++ 和 Python tests；
8. README、coverage matrix 和历史日志。

仓库当前 instruction specification 标注的 PTX ISA 基线是 9.2；这表示仓库数据模型的声明版本，不自动意味着已经实现 PTX 9.2 的全部内容。

---

## 9.2 设计文档

词法和语法：

```text
docs/zh-han/lexer_design.md
docs/zh-han/syntax_ast_design.md
docs/zh-han/control_flow_syntax_design.md
```

Binding 和声明语义：

```text
docs/zh-han/symbol_binding_design.md
docs/zh-han/declaration_semantics_design.md
```

Resolved IR 和生成器：

```text
docs/zh-han/resolved_ir_design.md
docs/zh-han/yaml_instruction_spec.md
docs/zh-han/python_generator_model.md
```

英文版位于：

```text
docs/us-en/
```

---

## 9.3 Coverage 文档

```text
docs/zh-han/syntax_coverage.md
docs/us-en/syntax_coverage.md
```

Coverage matrix 负责回答：

- 是否能 tokenize；
- 是否能进入 CST；
- 是否能进入 Syntax AST；
- 是否能 binding；
- 是否能进入 Resolved IR；
- 是否能 target-check。

它不负责决定长期实现顺序。

当前 matrix 已覆盖 direct/metadata-backed indirect `call`、`brx.idx`、M8 module directive、
recovery 与 debug metadata binding 的实际边界。

---

## 9.4 历史日志

[`docs/deprecated/next_step.md`](../docs/deprecated/next_step.md) 保留详细的设计演进和验证记录。
它是内容冻结的历史文件，不再更新。

从本文建立后：

- [`.agents/project_roadmap.md`](project_roadmap.md) 是 roadmap 和状态的唯一权威来源；
- `syntax_coverage.md` 是能力边界的权威来源；
- [`docs/deprecated/next_step.md`](../docs/deprecated/next_step.md) 是冻结的历史实现日志；
- README 是公开项目简介；
- 四者不得各自维护互相冲突的优先级。

---

# 10. Issue 的统一闭环标准

除纯文档或纯构建 issue 外，一个功能 issue默认必须满足：

1. 找到对应 PTX ISA section；
2. 冻结本 issue 精确支持的 syntax/variant 列表；
3. schema 能表达并拒绝非法配置；
4. Python normalizer 有正反测试；
5. Python typed model 有测试；
6. generator 输出完整；
7. standalone resolver 有成功和失败测试；
8. module-aware resolver 有成功和失败测试；
9. checker 有 operand、modifier、layout 和 target 测试；
10. diagnostics 包含准确 SourceRange；
11. 不支持的邻接 form 有明确拒绝测试；
12. 英文和中文 coverage 同步；
13. README 在公开能力变化时同步；
14. `git diff --check` 通过；
15. Debug CI 通过；
16. Release CI 通过；
17. package consumer 不被破坏；
18. generated files 没有被手工修改。

只有 parser 能接受某条源码，不算支持该 instruction。

---

# 11. Roadmap 总览

| Milestone | 状态 | 目标 |
| --- | --- | --- |
| M0 | ✅ | 建立 source-faithful lexer、CST 和 Syntax AST |
| M1 | ✅ | 建立 YAML 驱动的 Resolved IR/checker 平台 |
| M2 | ✅ | 建立 binding、declaration semantics 和 direct branch |
| M3 | ✅ | 建立 typed value、address、special-register 和 `mov` |
| M4 | ✅ | 完成当前已建模 basic scalar/vector `ld/st` 子集 |
| M5 | ✅ | 完成模块化构建、安装 package、consumer test 和 PR CI |
| M6 | ✅ | 完成 direct-call signature、ABI 和 call-context |
| M7 | ✅ | 完成 indirect call 和 control-flow metadata |
| M8 | ✅ | 完成除暂停 I14 外的 module grammar、nested scope 和 parser recovery |
| M9 | ⬜ | 完成 simulator MVP 的核心 opcode 集 |
| M10 | ⬜ | 扩展 memory、atomic、warp、async-copy 和 matrix instruction |
| M11 | ⬜ | 稳定公共 API、接入下游 simulator 并形成 1.0 gate |

---

# 12. 详细实现计划

## M0：Source-faithful 语法前端

### 目标

为当前支持的 PTX 子集建立准确、可定位、保留源码的解析基础。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M0-I01 | ✅ | 独立 | 建立 C++23 与 SourceRange 基础 | `SourcePos`、`SourceRange` 和 location wrapper 有测试 |
| M0-I02 | ✅ | 独立 | 实现可重入 Flex lexer | scanner 实例独立；token 自持文本、range 和 trivia |
| M0-I03 | ✅ | 独立 | 实现受支持子集的 lossless CST | 已支持 grammar 的 token、trivia 和顺序不丢失 |
| M0-I04 | ✅ | 独立 | 实现 typed Syntax AST | CST 可以降为 target-independent AST |
| M0-C01 | ✅ | 耦合 | 打通 source → CST → AST | facade 与显式 parser/lowering 行为一致 |
| M0-C02 | ✅ | 耦合 | 建立语法回归和 coverage | tokenize/CST/AST 边界分别记录并测试 |

### 出口

- 已支持源码拥有准确 SourceRange；
- lexer 不承担 PTX semantic；
- 未建模 header 不会静默进入 AST。

---

## M1：数据驱动 Resolved IR 平台

### 目标

建立 PTX facts、backend mapping 和 generated C++ 表示之间的稳定链条。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M1-I01 | ✅ | 独立 | 定义 instruction schema | variant、modifier、operand、layout、availability 可校验 |
| M1-I02 | ✅ | 独立 | 定义 C++ backend schema | C++ enum/class/field spelling 集中管理 |
| M1-I03 | ✅ | 独立 | 建立 normalized Python model | reference、value set、default 和 expression 统一展开 |
| M1-I04 | ✅ | 独立 | 生成公共 Resolved IR | variant payload、modifier field 和 location 可生成 |
| M1-I05 | ✅ | 独立 | 生成三类 descriptor | syntax、resolved、checker descriptor 贯通 |
| M1-I06 | ✅ | 独立 | 生成 lookup 和 dispatch | opcode/suffix/resolve/check dispatch 数据驱动 |
| M1-I07 | ✅ | 独立 | 支持多 operand layout | layout tag、payload alternative 和 checker 一致 |
| M1-C01 | ✅ | 耦合 | 收敛 codegen ownership | 生成拓扑和输入依赖由 `resolved_ir` CMake 管理 |
| M1-C02 | ✅ | 耦合 | 打通首批 opcode | `add`、`sub`、`bar` 等完整 select/resolve/check |

---

## M2：Binding、声明语义与 direct branch

### 目标

使 module resolution 拥有稳定 symbol identity。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M2-I01 | ✅ | 独立 | 建立 lexical SymbolTable | module/function scope、shadowing 和 parameterized symbol 可查询 |
| M2-I02 | ✅ | 独立 | 实现 reference classification | declared/external/special/unresolved 分离 |
| M2-I03 | ✅ | 独立 | 实现 declaration semantics | array、initializer、redeclaration、prototype/definition 可检查 |
| M2-I04 | ✅ | 独立 | 加固 constant evaluator | `.s64/.u64`、cast、unary/binary/conditional 和 shift 正确 |
| M2-I05 | ✅ | 独立 | 建立 call/branch 专用 syntax node | call group 不再伪装成 vector pack |
| M2-I06 | ✅ | 独立 | 实现 binding-aware execution predicate | 保存 SymbolId、type、negation 和 range |
| M2-I07 | ✅ | 独立 | 实现 direct `bra` | label target 保存 stable identity 并检查 function scope |
| M2-C01 | ✅ | 耦合 | 建立 `resolveModule()` | binding、declaration 和 instruction diagnostics 汇总 |
| M2-C02 | ✅ | 耦合 | 建立 module diagnostic regression | duplicate、unresolved、wrong kind 等诊断可区分 |

---

## M3：值、地址与 `mov`

### 目标

建立可以被 memory、call 和后续 instruction 复用的 resolved value 表示。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M3-I01 | ✅ | 独立 | 建立 special-register catalog | stable identity、现行 type/shape 和 intrinsic availability 集中 |
| M3-I02 | ✅ | 独立 | 建立 resolved operand 基础类型 | register/predicate/immediate/symbol/address/function 分离 |
| M3-I03 | ✅ | 独立 | 建立 `ResolvedMovSource` | identifier 在 binding 后按实际语义分类 |
| M3-I04 | ✅ | 独立 | 支持 formal-parameter address | entry/device、input/return 和 effective state space 保留 |
| M3-I05 | ✅ | 独立 | 支持 function address | `.func/.entry` identity 和 availability 可检查 |
| M3-I06 | ✅ | 独立 | 完成 scalar `mov` family | 16/32/64-bit scalar/predicate 语义闭环 |
| M3-I07 | ✅ | 独立 | 表达历史 special-register width | instruction-specific compatibility 由 checker data 表达 |
| M3-I08 | ✅ | 独立 | 实现 vector `mov` | `.b16/.b32/.b64/.b128` pack/unpack 和 sink 规则闭环 |
| M3-C01 | ✅ | 耦合 | 统一 stable identity 与 target checker | checker 不修改 Resolved IR identity |
| M3-C02 | ✅ | 耦合 | 统一 standalone/module 两条路径 | 两条路径共享 descriptor，未知 identity 不猜测 |

---

## M4：当前已建模 `ld/st` 子集

### 目标

完成当前 basic scalar、legacy vector 和 PTX 8.8 modern vector 子集。

这里的“完成”不表示全部 `ld/st` ISA 扩展已经实现。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M4-I01 | ✅ | 独立 | generic/basic-explicit scalar `ld/st` | generic/explicit state-space policy 分离 |
| M4-I02 | ✅ | 独立 | `.param` direction 和 function context | load input、store return、entry/device availability 正确 |
| M4-I03 | ✅ | 独立 | 14 种 scalar memory type | `.b/.u/.s` 8–64 bit 和 `.f32/.f64` 覆盖 |
| M4-I04 | ✅ | 独立 | legacy cache operator | load/store 集合分离，omission sentinel 保留 |
| M4-I05 | ✅ | 独立 | wider-register policy | memory operand 可 equal-or-wider，其他 opcode 保持 same-width |
| M4-I06 | ✅ | 独立 | legacy `.v2/.v4` vector | 最多 128-bit，无 sink |
| M4-I07 | ✅ | 独立 | memory consistency | weak/volatile/scoped consistency/mmio 已建模子集闭环 |
| M4-I08 | ✅ | 独立 | PTX 8.8 modern vector | 精确 256-bit、SM 100、global 和 partial sink 规则 |
| M4-C01 | ✅ | 耦合 | static total-access alignment | symbol+offset 和 absolute address 联合访问宽度检查 |
| M4-C02 | ✅ | 耦合 | 统一 memory diagnostics | state space、direction、consistency、vector、alignment 优先级一致 |

### 未包含

- scalar `.b128`；
- cache hint；
- eviction priority；
- prefetch extension；
- unified-memory qualifier；
- 尚未进入 YAML 的其他 `ld/st` form；
- function-local call-argument `.param`；
- texture/surface instruction。

这些工作进入 M10。

---

## M5：工程化、安装包和 CI

### 目标

使项目可以作为独立 package 被外部项目消费。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M5-I01 | ✅ | 独立 | 拆分 CMake target | 各前端阶段 dependency 明确 |
| M5-I02 | ✅ | 独立 | 收敛 codegen ownership | 顶层 CMake 不直接拥有 generated source |
| M5-I03 | ✅ | 独立 | 导出 CMake package | config、version、namespace 和 components 可用 |
| M5-I04 | ✅ | 独立 | 建立 installed consumer test | 外部 configure/build/run 成功 |
| M5-I05 | ✅ | 独立 | 建立 Debug/Release preset | 本地和 CI 命令一致 |
| M5-I06 | ✅ | 独立 | 建立 Ubuntu 26.04 PR CI | Debug/Release matrix 通过 |
| M5-I07 | ✅ | 独立 | 建立 README 和双语文档基础 | 项目边界、构建和 package 用法明确 |
| M5-C01 | ✅ | 耦合 | 打通 generated public header 安装 | installed target 不依赖 source-tree private path |
| M5-C02 | ✅ | 耦合 | 建立工程门禁 | Python、C++、install 和 external consumer 共同通过 |

---

## M6：Direct-call signature、ABI 与 call context

### 当前状态

M6 已完成 direct-call operand resolution、canonical signature ABI comparison 与
PTX 9.3 call-context 约束。

当前实现精确支持三个 layout：

```text
call function;
call function, (arguments);
call (return), function, (arguments);
```

并保存：

- direct `.func` target；
- optional return group；
- variadic input group；
- `.reg/.param` identity；
- declared type；
- state space；
- per-element SourceRange；
- standalone resolution 的 untyped immediate spelling；
- `.uni`；
- execution predicate。

module resolution 会使用 prototype/definition 的 canonical signature 检查 direct call 的
return/input 数量、顺序与 argument compatibility，并按 formal 定型 literal；function-local
`.param` 的 qualifier、predication 与 staging adjacency 同时生效。indirect
`.calltargets/.callprototype` metadata 仍留给 M7。



| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M6-I01 | ✅ | 独立 | 建立 call 专用 CST/AST 和 binding shape | return/input group、callee 和 optional target-set syntax 不与 vector 共用 |
| M6-I02 | ✅ | 独立 | 实现 direct-call Resolved IR | 三个 `Call` layout、direct `.func` target、argument identity/range 和明确 indirect rejection |
| M6-I03 | ✅ | 独立 | 修正 direct-call 基线文档 | 本轮已同步 README、coverage 和设计文档；priority 只由本文维护 |
| M6-I04 | ✅ | 独立 | 建立 canonical `FunctionSignature` | prototype/definition 可转换为统一 return/input parameter contract |
| M6-I05 | ✅ | 独立 | 建立 call literal typing helper | immediate 按一个 formal parameter 定型；溢出和 kind mismatch 可诊断 |
| M6-I06 | ✅ | 独立 | 建立 call argument compatibility helper | `.reg/.param`、type、alignment、array、pointer 和 state-space 可逐项比较 |
| M6-I07 | ✅ | 独立 | 建模 function-local call-argument `.param` | 与 formal parameter 分离，scope、lifetime、direction 和 address rule 明确 |
| M6-I08 | ✅ | 独立 | 建模 call-context syntax/semantics | PTX 9.3 `::entry/::func`、function-local `.param` staging adjacency/predication 与 predicated `call.uni` 规则已明确 |
| M6-C01 | ✅ | 耦合 | 实现 direct-call ABI checker | canonical callee signature 与 return/input actual 按数量、顺序、type/shape、array/pointer 和 literal 完整比较 |
| M6-C02 | ✅ | 耦合 | 完成 direct-call 端到端回归 | prototype、definition、extern、recursive、arity/type/space、literal 与 diagnostic range 覆盖 |
| M6-C03 | ✅ | 耦合 | 同步 direct-call 文档和 package regression | README、双语设计/coverage 与 installed consumer 验证 module ABI 已通过 |

### 出口

```text
.func prototype/definition
        |
        v
canonical FunctionSignature
        |
        v
direct-call Resolved IR
        |
        v
ABI checker
```

---

## M7：Indirect call 和控制流 metadata

### 目标

正式建模 `.callprototype`、`.calltargets` 和 `.branchtargets`。

### 当前状态

M7-I01/I02/I03/I04/I05/I06/I07/M7-C01/C02/C03 已完成：function-local `.callprototype`、`.calltargets` 与
`.branchtargets` 以专用 lexer token 和 CST/AST node 保留 label、signature payload/ordered
target（包括未展开 compact branch entry）、PTX 9.3 suffix（prototype）与 SourceRange。三者
现在以稳定的 function-scope SymbolId 进入 binding，并检查 declaration order、member、scope、
duplicate 与 signature contract。`ResolvedIndirectCallee` 可保留 `.reg` target 或 metadata label
identity；`call_direct` 保持一个公开 modifier variant，并新增三种 PTX 2.1 / SM 20 的专用
`IndirectCall` layout，正常 module resolution 可绑定 `.reg` target 与 function-local metadata，
并以同一 canonical `FunctionSignature` / argument-compatibility contract 检查 indirect ABI。
`brx.idx` 已以 `.u32` index 与 function-local branch-target-set identity 连接 `.branchtargets`，
并要求 PTX 6.0 / SM 30；不会展开 metadata entry。合法 indirect call 全部走正式 descriptor，
malformed metadata-bearing call 使用通用 layout diagnostic。installed package consumer 以单一 PTX
9.3 corpus 覆盖 direct/indirect call、prototype/target-set 与 `brx.idx` metadata 闭环。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M7-I01 | ✅ | 独立 | 建模 `.callprototype` CST/AST | grammar、signature payload、scope 和 SourceRange 明确 |
| M7-I02 | ✅ | 独立 | 建模 `.calltargets` CST/AST | target list、empty/duplicate rule 和 range 明确 |
| M7-I03 | ✅ | 独立 | 建模 `.branchtargets` CST/AST | label list 和 function scope 明确 |
| M7-I04 | ✅ | 独立 | 扩展 metadata symbol kind | prototype、call-target set、branch-target set 拥有稳定 SymbolId |
| M7-I05 | ✅ | 独立 | 实现 metadata declaration semantics | duplicate、unresolved、scope 和 signature conflict 可检查 |
| M7-I06 | ✅ | 独立 | 建立 indirect callee resolved value | register target、prototype ref 和 target-set ref 表示明确 |
| M7-I07 | ✅ | 独立 | 建立 indirect-call descriptor layout | 专用 `IndirectCall` layout、target/metadata shape 与 module identity 贯通 |
| M7-C01 | ✅ | 耦合 | 实现 indirect-call ABI checker | prototype/target-set 复用 canonical signature、arity、literal 与 argument compatibility |
| M7-C02 | ✅ | 耦合 | 连接 `.branchtargets` 与 `brx.idx` | `.u32` index、target-list identity 与 current function scope 一致 |
| M7-C03 | ✅ | 耦合 | 删除 temporary call special case | 移除通用 resolver 中 opcode-string indirect rejection，malformed syntax 走通用 layout diagnostic |
| M7-C04 | ✅ | 耦合 | 完成 indirect-control-flow corpus | installed package consumer 覆盖 direct/indirect/prototype/target-set/branch metadata 闭环 |

---

## M8：Module grammar、nested scope 和错误恢复

### 目标

使前端能够处理更接近真实编译器输出的 PTX module，并在多个错误之间恢复。

当前工作分支已把 `.file`、`.loc`、`.pragma`、`.section`、`.maxnreg`、
`.maxntid`、`.reqntid` 和 `.minnctapersm` 等稳定 directive 接入 CST/AST，并完成 nested
scope、parser recovery、debug metadata binding、round-trip、fuzz harness 与真实 module corpus。
除继续暂停的 M8-I14 外，以下功能项均已实现并验证。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M8-I01 | ✅ | 独立 | 支持 nested block CST/AST | `{...}` nested scope 有独立 node 和 source range |
| M8-I02 | ✅ | 独立 | 支持 nested lexical binding scope | shadowing、sibling/outer visibility、function-local label/metadata、recursive semantic/resolution 与 block-local call staging 有测试 |
| M8-I03 | ✅ | 独立 | 支持 `.file` directive | outermost `.file` 的编号、路径和 2/4 参数 payload 有无损 CST 与带 range 的 AST；duplicate index/.loc table 留给 C02 |
| M8-I04 | ✅ | 独立 | 支持 `.loc` directive | function/nested-block `.loc` basic triple 与完整成对的 `function_name`/`inlined_at` payload 进入带 range 的 CST/AST；`.file`/DWARF 解析和 source-location attachment 留给 C02 |
| M8-I05 | ✅ | 独立 | 支持 `.section` directive | outermost `.section` 的 name、matched brace 和有序 raw DWARF payload 进入 CST/AST；payload/label/offset semantics 留给 C02 |
| M8-I06 | ✅ | 独立 | 支持 `.pragma` directive | module、entry header 与 function/nested statement 的非空 string list 进入 CST/AST，不误入 binding/semantic/Resolved IR |
| M8-I07 | ✅ | 独立 | 支持第一组 kernel-resource directive | entry header 的 `.maxnreg/.maxntid/.reqntid/.minnctapersm` 进入 typed CST/AST；`.version` minimum 与同 entry req/max conflict 有 declaration-semantic diagnostics，backend warning/feasibility 留后续 |
| M8-I08 | ✅ | 独立 | 建立 directive coverage registry | docs-only：PTX 9.3 Table 1 的 35 项与该表遗漏的 `.attribute/.abi_preserve/.abi_preserve_control/.blocksareclusters/.language` 共 40 项，逐 spelling 标记 tokenize/CST/AST/binding/Resolved IR/target-semantic/rejection boundary |
| M8-I09 | ✅ | 独立 | 建立 `DiagnosticCollection` | public CST parser、lowering 与 Syntax facade 以 optional value + ordered diagnostics result 贯通；I10/I11 前仍 fail-fast 单 diagnostic |
| M8-I10 | ✅ | 独立 | 增加 missing-token/recovery node | CST 的 tagged inserted/skipped/error node 已可作为 module/function-body item，并保留 source/token-span 不变量；parser 产生、synchronization 与 recovered CST→AST contract 仍分别留给 I11/C01 |
| M8-I11 | ✅ | 独立 | 定义 synchronization point | `parseModule()` 在 `;`、`}`、EOF、function boundary（含 qualifier）和 supported module-item start 有界恢复；partial nested block 保留 body 与 inserted `}`、无 closing token，返回 recovered CST + 有序 diagnostic；fragment 仍 fail-fast，C01/I12 分别定义 lowering/serialization contract |
| M8-I12 | ✅ | 独立 | 建立 CST round-trip serializer | docs/tests-only：复用 `CstFile::sourceText()` 从未修改 token buffer 逐字节重建；recovery marker 不输出，EOF sentinel 数非公开契约 |
| M8-I13 | ✅ | 独立 | 建立 lexer/CST fuzz harness | opt-in Clang libFuzzer target 与同 entry point 的 GTest seed smoke 覆盖 arbitrary bytes 和 malformed nesting；M11 再接入 sanitizer/CI matrix |
| M8-I14 | ⏸ | 独立 | 收集 optional fixed-address 证据 | 只有取得规范 grammar 或可复现 `ptxas` 行为后才开始实现 |
| M8-C01 | ✅ | 耦合 | 定义 recovered CST → AST contract | recovery node 保持 CST-only；module lowering 过滤它们、保留相邻合法 AST node，并只一次透传 parser diagnostic |
| M8-C02 | ✅ | 耦合 | 连接 directive 与 binding/semantic | `.file`/`.debug_str` identity 在独立 metadata namespace 进入 binding，`.loc` 递归解析 file/function-name reference；纯 metadata 不污染 Resolved IR |
| M8-C03 | ✅ | 耦合 | 建立真实 PTX module corpus | installed consumer 覆盖合法 PTX 9.3 directive module、semantic directive error 与 unknown directive recovery，均无 silent drop |

---

## M9：Simulator MVP 核心 opcode

### 目标

以 `ptxsim` 的最小可执行 kernel corpus 为驱动，补齐核心 control、logic、compare、conversion 和 arithmetic opcode。

每个 opcode issue 必须先在 issue body 中冻结：

```text
PTX section
支持的 variant 列表
明确排除的 variant
PTX/SM 范围
operand/type/modifier matrix
```

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M9-I01 | ⬜ | 独立 | 建立 machine-readable opcode coverage manifest | 每个 opcode 标记 syntax/resolved/checker/simulator 状态 |
| M9-I02 | ⬜ | 独立 | 建立 simulator MVP kernel corpus | 固定一组最小 kernel 和所需 opcode 清单 |
| M9-I03 | ⬜ | 独立 | 支持 `ret` | `ret` 的完整冻结 variant slice 进入 YAML/resolver/checker |
| M9-I04 | ⬜ | 独立 | 支持 `exit` | `exit` 的 syntax、availability 和 function-context 闭环 |
| M9-I05 | ⬜ | 独立 | 支持 `trap` | `trap` 的 target rule 和 diagnostics 闭环 |
| M9-I06 | ⬜ | 独立 | 支持 `and` | 单 opcode 全链条闭环 |
| M9-I07 | ⬜ | 独立 | 支持 `or` | 单 opcode 全链条闭环 |
| M9-I08 | ⬜ | 独立 | 支持 `xor` | 单 opcode 全链条闭环 |
| M9-I09 | ⬜ | 独立 | 支持 `not` | 单 opcode 全链条闭环 |
| M9-I10 | ⬜ | 独立 | 支持 `shl` | width、shift operand 和 target rule 闭环 |
| M9-I11 | ⬜ | 独立 | 支持 `shr` | signedness、width 和 shift operand 闭环 |
| M9-I12 | ⬜ | 独立 | 建立 comparison operator domain | comparison spelling、backend enum 和 availability 集中生成 |
| M9-I13 | ⬜ | 独立 | 建立 boolean operator domain | `and/or/xor` predicate-combine domain 集中生成 |
| M9-I14 | ⬜ | 独立 | 支持 `setp` | comparison、optional boolean combine 和 predicate output 闭环 |
| M9-I15 | ⬜ | 独立 | 支持 `selp` | predicate select、source/result type 和 operand role 闭环 |
| M9-I16 | ⬜ | 独立 | 支持 integer→integer `cvt` slice | width、signedness、round/sat rule 精确冻结 |
| M9-I17 | ⬜ | 独立 | 支持 float→float `cvt` slice | rounding、flush/saturation 和 availability 精确冻结 |
| M9-I18 | ⬜ | 独立 | 支持 integer↔float `cvt` slice | mixed-domain conversion 精确冻结 |
| M9-I19 | ⬜ | 独立 | 支持 `cvta` | source/destination state space 和 address width 闭环 |
| M9-I20 | ⬜ | 独立 | 支持 integer `mul` slice | low/wide/hi 等本 issue 冻结的 variant 闭环 |
| M9-I21 | ⬜ | 独立 | 支持 floating `mul` slice | type、rounding 和 target availability 闭环 |
| M9-I22 | ⬜ | 独立 | 支持 integer `mad` slice | source/result width 和 carry/hi policy 精确冻结 |
| M9-I23 | ⬜ | 独立 | 支持 `fma` slice | type、rounding、saturation 和 target availability 闭环 |
| M9-I24 | ⬜ | 独立 | 支持 `div` 的首个 MVP slice | integer或floating slice必须在 issue 中固定，不混合实现 |
| M9-C01 | ⬜ | 耦合 | 统一新增 domain 和 diagnostics | comparison/boolean/rounding/type domain 无重复定义 |
| M9-C02 | ⬜ | 耦合 | 打通 MVP kernel parse/check | corpus 中所有 kernel 可生成完整 ResolvedModule |
| M9-C03 | ⬜ | 耦合 | 打通 MVP functional execution | adapter 能驱动 simulator 并比对 register/memory 结果 |

---

## M10：Memory、atomic、warp、async-copy 和 matrix 扩展

### 目标

在 core scalar execution 稳定后扩展并发和现代 GPU instruction。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M10-I01 | ⬜ | 独立 | 建立现有 `ld/st` extension gap manifest | 每个尚未建模 qualifier/form 明确分类 |
| M10-I02 | ⬜ | 独立 | 支持 `ld/st` cache-hint/eviction slice | 精确冻结 modifier、operand 和 target matrix |
| M10-I03 | ⬜ | 独立 | 支持 `ldu` | 单 opcode 全链条闭环 |
| M10-I04 | ⬜ | 独立 | 支持 `prefetch` | level、state space 和 address rule 闭环 |
| M10-I05 | ⬜ | 独立 | 支持 `membar` | scope 和 target availability 闭环 |
| M10-I06 | ⬜ | 独立 | 支持 `fence` | memory semantics、scope 和 target rule 闭环 |
| M10-I07 | ⬜ | 独立 | 支持 `atom` 的首个 scalar slice | operation、state space、memory ordering 和 destination layout 冻结 |
| M10-I08 | ⬜ | 独立 | 支持 `red` 的首个 scalar slice | operation、state space 和 no-destination layout 冻结 |
| M10-I09 | ⬜ | 独立 | 支持 `activemask` | predicate-mask type 和 target rule 闭环 |
| M10-I10 | ⬜ | 独立 | 支持 `vote` 的首个 sync slice | mask、predicate 和 result layout 闭环 |
| M10-I11 | ⬜ | 独立 | 支持 `shfl.sync` 的首个 slice | mask、lane operand、predicate output 和 target rule 闭环 |
| M10-I12 | ⬜ | 独立 | 支持 `cp.async` copy | source/destination space、size 和 cache rule 闭环 |
| M10-I13 | ⬜ | 独立 | 支持 `cp.async.commit_group` | group state instruction syntax/check 闭环 |
| M10-I14 | ⬜ | 独立 | 支持 `cp.async.wait_group` | immediate group count 和 target rule 闭环 |
| M10-I15 | ⬜ | 独立 | 支持 `cp.async.wait_all` | standalone wait form 闭环 |
| M10-I16 | ⬜ | 独立 | 支持 `ldmatrix` 的首个 shape/type slice | shape、transpose、destination layout 和 target rule 冻结 |
| M10-I17 | ⬜ | 独立 | 支持 `mma` 的首个 shape/type slice | A/B/C/D type、shape、layout 和 target rule 冻结 |
| M10-C01 | ⬜ | 耦合 | 统一 memory-ordering domain | `ld/st/fence/atom/red` 使用同一 consistency/scope 基础 |
| M10-C02 | ⬜ | 耦合 | 统一 warp/matrix target constraint | mask、warp shape、matrix shape 和 availability 无重复实现 |
| M10-C03 | ⬜ | 耦合 | 建立 advanced-kernel corpus | async-copy、atomic、warp 和 matrix 正反例共同通过 |

---

## M11：公共 API、下游接入和 1.0

### 目标

冻结可供外部 simulator 和其他 consumer 使用的公共 contract。

| ID | 状态 | 类型 | Issue | 闭环条件 |
| --- | --- | --- | --- | --- |
| M11-I01 | ⬜ | 独立 | 建立 diagnostic code/severity | error/warning/note 分离，每类诊断有稳定 code |
| M11-I02 | ⬜ | 独立 | 提供统一 frontend session API | source → parse → bind → semantic → resolve → check 一站式运行 |
| M11-I03 | ⬜ | 独立 | 提供稳定只读 Resolved IR visitor | consumer 不依赖 generated private type 或 variant index |
| M11-I04 | ⬜ | 独立 | 定义 Resolved IR serialization | 格式携带 schema/version，支持 golden test |
| M11-I05 | ⬜ | 独立 | 定义 public API version policy | 1.x compatibility、deprecation 和 migration 规则明确 |
| M11-I06 | ⬜ | 独立 | 建立 generated-output reproducibility | 相同输入产生 byte-identical 输出 |
| M11-I07 | ⬜ | 独立 | 扩展 CI matrix | 增加 `main` push、Clang、ASan、UBSan 和 fuzz smoke |
| M11-I08 | ⬜ | 独立 | 定义 `ptxsim` adapter contract | opcode visitor、value identity、address、control flow 和 unsupported behavior 明确 |
| M11-I09 | ⬜ | 独立 | 建立 downstream compatibility suite | installed public API 可以构建独立 adapter consumer |
| M11-C01 | ⬜ | 耦合 | 打通 PTX text → functional simulator | parse/check/execute 最小 kernel 并比对结果 |
| M11-C02 | ⬜ | 耦合 | 建立 1.0 release-candidate gate | compiler、sanitizer、fuzz、corpus、package 和 adapter 全通过 |
| M11-C03 | ⬜ | 耦合 | 发布 1.0 contract | public API、diagnostic code、package component 和 serialization schema 冻结 |

---

# 13. 关键依赖关系

## 13.1 主关键路径

```text
M6 FunctionSignature + direct ABI
        |
        v
M7 indirect prototype/target-set contract
        |
        v
M9 simulator core opcode
        |
        v
M11 stable adapter and 1.0
```

Direct-call ABI 必须先于 indirect-call ABI。否则 direct callee signature 和 indirect prototype 很可能产生两套不兼容表示。

---

## 13.2 可并行路径

M6 的 canonical `FunctionSignature` 稳定后，可以并行：

```text
M7 control-flow metadata
M8 module grammar/recovery
M9 independent opcode slices
M11 diagnostic and CI infrastructure
```

并行约束：

- 不同时修改同一 schema domain；
- 不同时重构 `ResolvedFieldValue`；
- 不同时修改 common checker protocol；
- 不在两个分支中分别创建同义 enum；
- 最终由 milestone 尾部的耦合 issue 集成。

---

## 13.3 不应提前的工作

以下工作暂不应抢在 M9 的 manifest 与 MVP corpus 前面：

- 为尚无 corpus 需求的 opcode 批量扩张 descriptor；
- 未冻结 variant slice 的 arithmetic 或 conversion 大包实现；
- CFG/SSA；
- Resolved IR serialization freezing；
- simulator adapter 的稳定 ABI；
- 大规模 matrix instruction coverage。

---

# 14. 当前推荐实施顺序

基于当前工作分支的功能事实基线 `dd92748`，下一步应当是：

1. `M8-I14` 继续暂停，直到取得规范 grammar 或可复现 `ptxas` 行为；
2. `M9-I01`：建立 machine-readable opcode coverage manifest；
3. `M9-I02`：以 simulator MVP kernel corpus 冻结实际 opcode 需求；
4. 再按 corpus 需求逐个实现 M9 opcode slice；
5. M11 的 diagnostics/CI 基础设施可在不冻结公共 ABI 的前提下并行推进。

---

# 15. Roadmap 维护规则

1. 功能在当前工作分支实现并完成相应验证后标记为 ✅；PR/合入状态不作为实现状态。
2. 只有功能事实变化时才更新功能事实基线；文档同步不得以自身提交作为功能基线。
3. 一个 issue 只能属于一个 milestone。
4. 独立 issue 必须位于耦合 issue 之前。
5. 耦合 issue 不得引入未评审的新基础表示。
6. 每个 milestone 必须定义出口。
7. 公共能力变化必须同步 README。
8. 语法/语义边界变化必须同步双语 coverage。
9. YAML 变化必须有 Python 和 C++ 测试。
10. Generated file 不能代替 generator 修改。
11. 下游需求通过 adapter contract 进入 roadmap。
12. Simulator execution semantics 不进入 frontend。
13. Temporary opcode-specific fallback 必须记录删除 milestone。
14. Future instruction issue 必须是单 opcode 或单一明确 variant slice。
15. 未取得 consumer 需求或规范证据的抽象不得提前泛化。
16. [`docs/deprecated/next_step.md`](../docs/deprecated/next_step.md) 是冻结历史文件，不再作为 roadmap。
17. `syntax_coverage.md` 不再单独决定实现优先级。
18. 本文状态与仓库事实冲突时，应以当前工作分支为准并立即修本文。

---

# 16. 1.0 最低完成标准

项目不需要覆盖全部 PTX ISA 才能发布 1.0，但至少必须满足：

- module/function/declaration 的核心 grammar 稳定；
- nested scope 的支持边界明确；
- direct call signature/ABI 完整；
- indirect call 完整支持，或通过稳定 contract 明确排除；
- simulator MVP 所需 opcode 已进入 Resolved IR；
- 未支持 opcode/directive 不会被静默误解析；
- diagnostics code、severity 和 SourceRange 稳定；
- public CMake package 稳定；
- installed consumer 不依赖源码树 private generated file；
- generated output 可复现；
- GCC 和 Clang 通过；
- Debug 和 Release 通过；
- ASan 和 UBSan 通过；
- lexer/parser fuzz 不崩溃、不无限循环；
- 真实 PTX corpus 和错误 corpus 通过；
- `zxsim/ptxsim` adapter 只依赖 public API；
- PTX text → ResolvedModule → functional execution 闭环；
- serialization/schema 有版本；
- breaking-change 和 deprecation policy 已文档化；
- README、coverage、roadmap 和代码状态一致。

在这些条件满足前，项目继续保持 pre-1.0，并允许公共 IR 类型进行必要的 breaking adjustment。
