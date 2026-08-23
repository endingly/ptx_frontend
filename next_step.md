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

公共类型位于 `include/ptx_ir/bind/ptx_symbol_table.hpp`，设计说明位于：

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
- 新增 `ResolvedSpecialRegisterRef`，同时保存源码 spelling 与注册表元数据；vector family
  仅在选择 `.x/.y/.z` 标量 component 后进入当前 scalar operand；
- checker 除 operand shape/type 外，还按具体 special-register value 检查 PTX/SM 可用性；
- 新增首个消费该表示的 `mov.u32 d, sreg` generated variant，覆盖 standalone 与
  binding-aware module resolution；其他 `mov` source form 与 type width 不被这一小步伪装为已支持。

当前注册表描述 PTX 9.x 的现行类型。早期 ISA 中 `%tid` 的 `.u16`、`%gridid` 的
`.u16/.u32` 等历史读取形式尚未建模，应在扩展相应 `mov` type variant 时显式表示，而不是
放宽当前 `.u32` checker。

## 已完成：binding-aware address/symbol resolution

- 新增 `ResolvedSymbolRef`，module resolution 保存稳定 `SymbolId`、parameterized member、
  state space 与可表示的 declaration scalar type；standalone resolution 保留 spelling 并令
  declaration identity 为空；
- 新增 `ResolvedAddress`，base 明确区分 register、immediate address 与 data symbol，offset
  保留加减运算及已解析的 signed 64-bit value；
- `mov.u64 d, symbol` 首先覆盖非参数 addressable data variable 的取地址形式；function
  address、parameter address 与 symbol+offset 仍保持未支持；
- `ld.u32 d, [address]` 首先覆盖 generic scalar load，并要求方括号解引用；address base
  支持 register、immediate 与 binding-aware data symbol；
- generic `ld` 的 PTX 2.0 / SM 20 可用性由 generated checker 统一检查。

## 下一步评审与调整

原计划的方向正确，但依赖顺序需要拆开：

- `bra` 已有专用 AST operand 与完整 label binding，并且是单一平坦 operand layout；它不依赖
  `call` 所需的 group/variadic layout algorithm，应先独立接入 generated dispatch/checker；
- address/symbol Resolved IR 应和首个实际消费它们的 opcode 一起落地，避免先建立没有 descriptor
  使用者的悬空表示；
- `call` 的参数组、可选返回组和 target-set/prototype 才真正需要非 `Flat` layout，应在
  flat control-flow 闭环稳定后单独设计。

`bra`、special-register consumer 与首个 address/symbol consumer 已完成闭环；剩余顺序是：

1. 逐步补齐 `mov` 的 register/immediate、symbol+offset、parameter/function-address source
   form，并按明确的历史 ISA 规则扩展 special-register type width；
2. 为 `ld/st` 增加 state-space、memory consistency/cache modifier 与更多 scalar/vector type，
   同时把 state-space compatibility 纳入 checker；
3. 为 `call` group/variadic operand 增加非 `Flat` descriptor layout algorithm，再将 `call`
   接入统一 dispatch/checker；
4. 表示 `.calltargets/.callprototype/.branchtargets` 及其余 module/function directive。

## 验证结果

- Debug CTest：140/140（包含 C++、Python IR 与 installed package consumer）；
- `git diff --check`：通过。
