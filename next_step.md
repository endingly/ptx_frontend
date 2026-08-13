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
- same-scope duplicate symbol 与非法/零 parameterized count 诊断；
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

## 下一步

1. 分类 special register、external symbol，并区分真正未声明的 reference；
2. 增加 declaration semantic pass，校验 initializer type、array shape/元素数量以及
   linkage-compatible redeclaration；
3. 推进 call/branch 专用 operand grammar 与其余 module directive；
4. 为 execution predicate 与后续 address/symbol operand 增加对应的 binding-aware
   Resolved IR 表示。

## 验证结果

- Debug CTest：108/108（包含 C++、Python IR 与 installed package consumer）；
- `git diff --check`：通过。
