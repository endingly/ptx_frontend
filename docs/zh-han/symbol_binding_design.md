# Symbol Binding 设计

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#symbol-binding-design) 为唯一维护依据；本文保留技术参考与历史证据。

## 定位

symbol binding 位于 Syntax AST 与 Resolved IR/checker 之间：

```text
source -> CST -> Syntax AST -> symbol binding -> Resolved IR/checker
```

`submod/binding/include/ptx_symbol_table.hpp` 提供公开 API：

```cpp
auto binding = binding::bindSymbols(module);
```

返回值同时包含 `SymbolTable` 与可累积的 `BindDiagnostic`。表和诊断都拥有所需字符串，
不依赖 Syntax AST 的生命周期。

## Scope 与 symbol

当前接受条件与限制见英文 [SDK 支持契约](../sdk_support.md#symbol-binding-design--scopes-and-symbols)；API、算法及证据见 [英文技术参考](../us-en/symbol_binding_design.md#scopes-and-symbols)。

## Parameterized variable name

当前接受条件与限制见英文 [SDK 支持契约](../sdk_support.md#symbol-binding-design--parameterized-variable-names)；API、算法及证据见 [英文技术参考](../us-en/symbol_binding_design.md#parameterized-variable-names)。

## Lookup index 与紧凑 group

表仍以拥有字符串的 `symbols` vector 作为稳定 `SymbolId` 的唯一来源；另外维护与 scope
对齐的私有 index，其 key 也独立拥有：ordinary exact name、parameterized exact base，以及
parameterized base 的 prefix trie。lookup 因此在当前 scope 先查 ordinary exact name，再只查
可能成为 query spelling prefix 的 parameterized base，之后才走向 parent scope。这保持了
“current-scope ordinary、current-scope parameterized、parent”的既有优先级。debug metadata
有意不进入这些 lexical index。
当 scope 只有一个 parameterized group 时，lookup 在 ordinary-name probe 之后直接检查
该 group 的 member，省去 prefix trie 的开销，但不改变边界、canonical suffix 或 parent fallback。

`exactDeclaration(scope, spelling, parameterized)` 为需要将 AST declarator 关联到其
bound identity 的 consumer 暴露同 scope 的 exact index；它不走 parent，也不把 compact group
生成的 member 当作 declaration。metadata 仍被排除，合法 redeclaration 保留第一个稳定
`SymbolId`。`initializerReference(range)` 则仅按精确 source range 索引 initializer reference，
即使首条记录是 unresolved 也会保留它，因此 declaration semantics 和 storage lowering 不必为
每个 symbol 重扫全部 instruction/initializer reference。

Parameterized overlap check 使用按 canonical spelling decomposition 建立的稀疏 32-bit member
range trie。它定位与新 base/count 有关的已有 explicit name 和 nonempty group 的
first-member spelling，并为 diagnostic 保留最先存储的 overlap identity；group base 因不属于
member 而不进入此 index，现有的 name-set-overlap predicate 仍是最终事实来源。index 不会展开逻辑 member：count 很大的 declaration
只消耗与 spelling 长度及固定 32-bit trie path 成比例的存储，而不与 count 成比例。index key
和 trie storage 都由表拥有，所以 `symbols` vector 增长以及支持的 table copy/move 都不会留下
悬空的 borrowed name key。

[扩展性基准](../../submod/resolved_ir/benchmark/README.md) 分别记录 parser、binding、
直接 lookup 和 module resolution 的测量结果，并包含 compact group 对照及生成 PTX 语料。

## Reference binding

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#symbol-binding-design--reference-binding)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/symbol_binding_design.md#reference-binding)。

## 当前诊断与边界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#symbol-binding-design--current-diagnostics-and-boundary)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/symbol_binding_design.md#current-diagnostics-and-boundary)。
