# Symbol binding：当前进度与下一步

通用 variable declaration 已覆盖 module/function scope、linkage qualifier、
`.reg/.param/.local/.shared/.global/.const`、`.align`、`.v2/.v4`、parameterized
variable name、多维 array，以及 initializer/constant-expression 的主要 grammar shape。

## 已完成：lexical symbol binding

新增公开的 lexical symbol-binding pass：

```cpp
auto result = ptx_frontend::binding::bindSymbols(module);
```

`SymbolTable` 当前支持：

- module root scope 与每个 `.entry/.func` 的 function scope；
- module/function variable、input/return parameter、function、label symbol；
- function local 对 module symbol 的 lexical shadowing；
- instruction predicate/operand、initializer symbol、array-dimension symbol reference；
- `name<count>` 的紧凑表示和成员 lookup，不展开成大量 symbol；
- same-scope duplicate symbol、parameterized name-set overlap 与非法/零
  parameterized count 诊断；
- 未解析 reference 的保留，供后续 special-register/linkage/opcode-aware 诊断。

公共类型位于 `submod/binding/include/ptx_symbol_table.hpp`，设计说明位于：

- `docs/zh-han/symbol_binding_design.md`；
- `docs/us-en/symbol_binding_design.md`。

同时修正了 parameterized variable name 的已有偏差：PTX 允许 `<count>` 用于任意 state
space，并非仅 `.reg`；但它不能同时声明 array 或 initializer。CST/AST 字段已从
`register_count` 直接更名为 `parameterized_count`，没有保留重复兼容字段。

## 关于 fixed address

最新版 NVIDIA PTX ISA 的 variable-declaration 概述仍有“optional fixed address”一句，但
规范没有提供独立语法、约束或示例。当前不据此发明 parser grammar；只有取得规范性
grammar 或可验证的 `ptxas` 行为后再加入对应 CST/AST 节点。

## 已完成：binding-aware module resolution

新增公开入口：

```cpp
auto result = ptx_frontend::resolved_ir::resolveModule(module);
```

- `ResolvedModule` 拥有本次解析的 `SymbolTable`；
- `ResolvedFunction` 使用 function `SymbolId`，不再只依赖字符串名称；
- module resolution 为每个 function scope 显式传入 `ResolveContext`；
- `ResolvedRegisterRef` 保存 declaration `SymbolId`、parameterized member index 与
  declaration `ScalarType`，并支持 named register；
- module context 中 typed instruction operand 的未声明 register、非 `.reg` symbol 和
  predicate/general register class 不匹配都会产生 resolve diagnostic；
- checker 现在能按 declaration type 校验 register operand；
- standalone `resolveInstruction`/`resolve<T>` 仍可无 symbol table 使用，保持原有单指令
  边界。

## 已完成：reference classification

- `Symbol` 记录 `.extern/.visible/.weak` linkage；
- `SymbolReference` 区分 declared、external、special-register 与 unresolved；
- `.extern` reference 仍绑定本 module 中的 declaration `SymbolId`；
- special register 按 PTX ISA 预定义名称和有界 family 精确匹配；
- 真正 unresolved 的 predicate、instruction operand、initializer 与 array-dimension
  reference 会产生按来源区分的 binding diagnostic；
- module resolution 对 special register 给出“已识别但当前 operand 不支持”的独立诊断。

## 已完成：declaration semantics

新增公开的 `declaration_semantics::checkDeclarations(module, symbols)` pass，并接入
`resolveModule()`：

- array dimension 必须求值为正整数 constant；未定长仅允许第一维且由 initializer 推导；
- array/vector initializer 的 brace nesting 与各维元素上限会被校验，同时保留 PTX
  允许少填并补零的规则；
- scalar initializer 区分 integer、floating 与 symbol address，并限制 address target 与
  destination type；
- module scope 同名 declaration 共享稳定 `SymbolId`，随后校验 variable/function signature、
  linkage、prototype/definition 组合与 multiple definition；
- function symbol 的 `owned_scope` 在存在 definition 时指向 definition scope。

## 已完成：P1 correctness hardening

- integer constant evaluator 现在保存 `.s64/.u64` 类型与完整 64-bit bit pattern，支持负数
  中间值、cast、usual arithmetic conversion 和 signed shift；
- 未建模的 function-header token 在 CST parser 直接报错，不再经 `header_tokens` 进入 CST
  后由 AST lowering 静默丢弃；
- parameterized declaration 会与 explicit/generated name set 检查 overlap，lookup 只接受
  无前导零的规范成员后缀，同时允许 parameterized base 与同名 explicit symbol 共存。

## 已完成：call/branch 专用 operand grammar

- `call` 的 return/input group、callee 和 target-set/prototype 现在拥有独立 CST/AST 节点；
- direct `bra` target 现在是独立 label-target node；
- parser 按 opcode 校验 call/branch layout，不再把 call group 当成 vector pack；
- binding 使用独立 reference kind，并校验 function/function-pointer、`.reg/.param` call
  parameter 与 function-local label；
- descriptor-facing operand shape 已同步到 C++ 与 Python model，但尚未把非 `Flat` call
  layout 伪装成 generated opcode。

## 已完成：binding-aware execution predicate

- 每个 generated opcode 外层保存可选的 `WithLocs<ResolvedPredicate>`，不再在
  AST→Resolved IR 时丢失 `@%p/@!%p`；
- module resolution 要求 guard 绑定到当前 scope 的 `.pred` declaration，并保存
  `SymbolId`、声明类型、negation 与 source range；
- standalone resolver 继续支持无需 symbol table 的 numbered `%pN` guard。

## 已完成：binding-aware direct branch resolution

- 新增 `bra` YAML 规格并接入 generated public type、private resolve/check 实现及三类
  descriptor；
- `ResolvedBranchTarget` 保存源码 spelling，module resolution 还保存当前 function label 的
  稳定 `SymbolId`；
- `.uni` 与 execution predicate 都会保留，checker 校验独立的 branch-target operand shape；
- standalone `resolveInstruction("bra target;")` 保持无 symbol table 可用，目标 identity 为空。

## 已完成：binding-aware special-register resolution

- 新增独立的 `special_registers` 语义注册表，以预定义名称为键记录 element type、vector
  width、最低 PTX ISA 与最低 SM；binding 的分类逻辑复用该注册表，不复制名称集合；
- 新增 `ResolvedSpecialRegisterRef`，保存源码 spelling、稳定 identity 与可选 vector component；
  vector family 仅在选择 `.x/.y/.z` 标量 component 后进入当前 scalar operand；
- checker 除 operand shape/type 外，还按具体 special-register value 检查 PTX/SM 可用性；
- `mov.u32 d, sreg` 首先消费该表示并覆盖 standalone 与 binding-aware module resolution；
  后续统一 source 阶段已在不复制 special-register registry 的前提下扩展 source form。

注册表只描述 special register 的稳定身份、现行声明类型/shape 与 intrinsic availability；
指令相关的历史读取版本由 YAML checker descriptor 描述，resolution 不生成 target-dependent
有效类型。

## 已完成：binding-aware address/symbol resolution

- 新增 `ResolvedSymbolRef`，module resolution 保存稳定 `SymbolId`、parameterized member、
  declaration kind、声明/有效 address state space 与可表示的 declaration scalar type；
  standalone resolution 保留 spelling 并令 declaration identity/state-space 为空；
- 新增 `ResolvedAddress`，base 明确区分 register、immediate address 与 data symbol，offset
  保留加减运算及已解析的 signed 64-bit value；
- `mov.u64 d, symbol` 首先覆盖非参数 addressable data variable 的取地址形式；后续统一
  source 阶段已加入 symbol+offset，parameter address 与 function address 也已完成；
- `ld.u32 d, [address]` 首先覆盖 generic scalar load，并要求方括号解引用；address base
  支持 register、immediate 与 binding-aware data symbol；
- generic `ld` 的 PTX 2.0 / SM 20 可用性由 generated checker 统一检查。

## 已完成：分类后的 mov scalar source

- 新增 `ResolvedMovSource`，在 binding 后统一区分 register、immediate、special register、
  data symbol 与 address expression，避免这些 source 共享 identifier syntax shape 时产生
  variant/layout 选择歧义；
- 该阶段使 32/64-bit scalar `mov` 支持 register、typed immediate、对应宽度的现行 special
  register、addressable data symbol、formal parameter 及 `symbol+constant-offset`；
- 两种宽度现在共享 `mov_scalar_src` operand kind；此前仅用于限制 `.u32` address shape 的
  `mov_data_src`/`mov_address_src` 分裂已不再表达真实语义，因此从 schema/backend model 移除；
- descriptor 仍按 variant 精确限制允许的 resolved operand shape，checker 对 register、
  immediate 与 special register 执行 instruction type 检查，并保留 special-register target
  availability；
- 16-bit、历史 special-register width 与 vector form 均在后续阶段完成。

## 已完成：mov formal-parameter address

- `mov.u32/.u64` 均可取得 ordinary data variable、kernel/device-function formal parameter 的
  地址，并支持 constant offset；function-local `.param` call-argument variable 仍按 PTX 规则
  拒绝取址；
- `ResolvedSymbolRef` 分开保存 declaration state space 与 effective address state space：kernel
  parameter 的 `mov` 地址属于 `.param`，device-function parameter 经 `mov` 取址会物化到
  stack，因此地址属于 `.local`；未经过 `mov` 物化的 direct parameter memory address 保持
  `.param`，供后续 `ld/st` state-space compatibility 使用；
- input/return parameter identity 通过 declaration `SymbolKind` 保留；device-function
  parameter 地址携带 PTX 2.0 / SM 20 baseline，return parameter 将最低 PTX 提升至 6.0；
  direct symbol 与带 offset address 都由 checker 检查这些门槛；
- standalone resolution 继续只保留 spelling，不在缺少 module/function context 时猜测参数类别、
  state space 或 availability。

## 已完成：mov function address

- 新增 `ResolvedFunctionRef`，module resolution 保存稳定 function `SymbolId` 与
  `.func/.entry` 类别；
- `mov.u32/.u64` 接受 bare device/kernel function name；带 offset 的形式继续按 data-symbol
  address 解析并拒绝；
- device-function 地址沿用 `mov` 的 PTX 1.0 baseline；kernel function 地址携带 PTX 3.1 /
  SM 35 门槛，并由 checker 按 target 检查；
- standalone resolution 无法区分未绑定名称是 data 还是 function，因此仍保留为无 identity 的
  `ResolvedSymbolRef`。

## 已完成：mov 16/32/64-bit scalar type family

- 原先固定 `.u32/.u64` 的两条 variant 合并为一个动态 type field variant，覆盖
  `.b16/.u16/.s16/.b32/.u32/.s32/.f32/.b64/.u64/.s64/.f64`；
- checker 在共享入口实现 PTX 基础类型兼容：同宽 bit-size 接受任意基础类型，signed/unsigned
  integer 互相兼容，integer/float 混用仍拒绝；
- `.f64` modifier value 携带 SM 13 门槛；data address 限制为 integer/bit-size type，function
  address 限制为 integer type；当前无 warning channel，因此 PTX 允许但会警告的 signed
  function-address form 保持成功；
- data/function address 继续限定为 32/64-bit，16-bit form 不会误接受 symbol address；
- vector form 在后续阶段完成。

## 已完成：mov predicate form

- 新增结构独立的 `mov_pred` variant，source/destination 复用现有 `ResolvedPredicate`，不扩展
  `ResolvedMovSource`；
- module resolution 要求两端绑定到未取反的 `.pred` register，并保留 declaration `SymbolId`
  与 parameterized member；standalone resolution 继续支持 numbered predicate register；
- checker 复用 fixed `.pred` operand type/shape descriptor，PTX 1.0 起适用于全部 target；
- vector form 在后续阶段完成。

## 已完成：mov historical special-register widths

- `%tid/%ntid/%ctaid/%nctaid` component 的 16-bit read 使用 PTX 1.0 baseline；
- `%gridid` 的 16/32-bit read 分别使用 PTX 1.0/1.3 baseline，现行 64-bit read 仍从 PTX
  3.0 可用；
- 历史规则由 `mov_scalar` 的 YAML `operand_type_compatibilities` 声明并生成到 checker
  descriptor；checker 按 stable identity 与 instruction width 创建临时有效类型/availability
  视图，不修改 Resolved IR；同宽 bit/signed form 复用现有基础类型兼容，float 和无历史
  规则的 special register 仍拒绝。

## 已完成：special-register target 分层重构

- special-register catalog 只保留 intrinsic identity、现行声明 type/shape 与自身 availability，
  删除 `mov` 专用历史字段和 `effectiveMovInfo`；
- `ResolvedSpecialRegisterRef` 只保留 target-independent identity、component 与 spelling；
- YAML variant 可用 `operand_type_compatibilities` 表达某个 operand/value/width 的上下文有效
  type 与 PTX/SM 门槛；generator 将其解析为 checker descriptor，不进入 syntax descriptor；
- checker 在验证期间临时选择上下文规则，覆盖历史 `mov` read，同时确保其他 consumer 和
  Resolved IR 不继承 `mov` 语义；
- syntax baseline 继续由编译期支持范围固定，不接收 target version/architecture 输入。

## 已完成：mov vector pack/unpack

- `mov_scalar` 保持单一 modifier variant；scalar、pack、unpack 由三个 operand layout 表示，
  避免为相同 `.b16/.b32/.b64` modifier 组合复制 variant；
- 新增 `ResolvedMovVector`，以 2/4 个可选 `ResolvedRegisterRef` 保存 vector payload；空元素只表示
  destination `_` sink，binding 不再把 `_` 当作未声明 symbol；
- YAML `kind: mov_vector` 的 `vector.arity` 进入 resolved/checker descriptor；resolver 与 checker
  都验证 arity、bit-size instruction type、element width、source sink 与全 sink destination；
- `.b128` 仅由 pack/unpack layout 接受，并按 modifier value 要求 PTX 8.3 / SM 70；scalar
  `.b128` 明确拒绝；
- C++ 回归覆盖 2/4-element pack、unpack、sink、`.b128` 双向读取、target 门槛及损坏
  Resolved IR 的 element-width 检查。

## 已完成：小模块的 source-bearing interface library

- `base`、`cst`、`syntax`、`binding` 与 `semantic` 改为 CMake interface library，
  由最终实体 target 统一编译这些小模块的实现源；
- 实现源通过 `target_sources(... INTERFACE ...)` 传播，而不是作为
  `add_library(... INTERFACE ...)` 的私有 source 丢失；
- include path 与依赖使用 `INTERFACE` usage requirements，模块测试和
  `resolved_ir` 等消费者得到同一组源码与编译依赖。

## 已完成：resolved_ir CMake codegen 归属

- `gen_all.py` 的输出发现、生成命令、输入依赖和 topology reconfigure tracking 已迁入
  `cmake/generate_ptx_frontend.cmake`，仅由 `submod/resolved_ir` include；
- 生成文件位于 `submod/resolved_ir` 自身 binary directory，`resolved_ir_codegen` target
  负责生成，`resolved_ir` 消费生成源码及 public/private include path；
- `gen_all.py` 当前保持原子调用；包括 syntax descriptor 在内的全部输出均实现或依赖
  generated Resolved IR 类型，因此不按文件名伪拆到更早的 submodule；
- 顶层 CMake 仅保留工程配置、子模块与 facade target，不再拥有 resolved IR codegen。

## 已完成：英文项目 README

- 根目录 `README.md` 说明项目定位、当前 PTX/Resolved IR 覆盖边界、frontend pipeline、
  构建测试命令、source-tree CMake 接入方式、最小 parse/resolve 示例与代码生成流程；
- 明确 pre-1.0 API、未启用 install/export、非完整 PTX validator/code generator，以及已添加
  [MIT license](LICENSE) 文件，避免对未实现能力作出承诺；
- README 示例已通过独立 source consumer 编译和运行验证。

## 下一步评审与调整

原计划的方向正确，但依赖顺序需要拆开：

- `bra` 已有专用 AST operand 与完整 label binding，并且是单一平坦 operand layout；它不依赖
  `call` 所需的 group/variadic layout algorithm，应先独立接入 generated dispatch/checker；
- address/symbol Resolved IR 应和首个实际消费它们的 opcode 一起落地，避免先建立没有 descriptor
  使用者的悬空表示；
- `call` 的参数组、可选返回组和 target-set/prototype 才真正需要非 `Flat` layout，应在
  flat control-flow 闭环稳定后单独设计。

`bra`、special-register consumer 与首个 address/symbol consumer 已完成闭环；剩余顺序是：

1. 为 `ld/st` 增加 state-space、memory consistency/cache modifier 与更多 scalar/vector type，
   同时把 state-space compatibility 纳入 checker；
2. 为 `call` group/variadic operand 增加非 `Flat` descriptor layout algorithm，再将 `call`
   接入统一 dispatch/checker；
3. 表示 `.calltargets/.callprototype/.branchtargets` 及其余 module/function directive。

## 验证结果

- Python unit tests：51/51；
- Debug C++ build：通过；
- Debug CTest：152/152（包含 C++、Python IR 与 installed package consumer）；
- `git diff --check`：通过。
