# Symbol Binding 设计

## 定位

symbol binding 位于 Syntax AST 与 Resolved IR/checker 之间：

```text
source -> CST -> Syntax AST -> symbol binding -> Resolved IR/checker
```

`include/ptx_ir/bind/ptx_symbol_table.hpp` 提供公开 API：

```cpp
auto binding = binding::bindSymbols(module);
```

返回值同时包含 `SymbolTable` 与可累积的 `BindDiagnostic`。表和诊断都拥有所需字符串，
不依赖 Syntax AST 的生命周期。

## Scope 与 symbol

每个 module 有一个根 scope，每个 `.entry/.func` item 有一个以 module scope 为 parent 的
function scope。当前收集：

- module/function variable declaration；
- function input 与 return parameter；
- function symbol；
- label。

`SymbolId` 与 `ScopeId` 是强类型索引。`Symbol` 保留名称、kind、声明位置，以及变量或
parameter 的 state space/type。function symbol 通过 `owned_scope` 指向其 function
scope。

同 scope 的查找优先 exact name，再查 parameterized name，最后沿 parent scope 向上。
因此 function-local declaration 可以遮蔽 module symbol。

## Parameterized variable name

`name<count>` 依 PTX 语法表示 `name0` 到 `name(count-1)`。symbol table 不展开这些名称，
而是保存 base name 与 count；lookup `%r2` 会返回同一个 declaration `SymbolId`，并在
`SymbolLookup::parameterized_index` 中记录 `2`。这样不会因较大的 count 生成大量 symbol。

Parameterized name 可用于任意 state space，但不能同时声明 array 或 initializer。原先
只允许 `.reg` 的限制已移除，公共 CST/AST 字段也统一命名为 `parameterized_count`。

## Reference binding

binding pass 会访问：

- instruction predicate 与各种 operand shape 中的 identifier；
- array dimension constant expression；
- scalar/递归 initializer 内的 symbol expression。

每个 `SymbolReference` 保留 spelling、range、引用种类和可选 target。成功解析的
parameterized reference 同时保存成员 index。未解析引用仍进入表但 `target` 为空，供后续
结合 opcode、special register 集合和链接规则产生准确诊断；本 pass 不会把所有未知 `%`
名称误报为普通未声明寄存器。

`generic()` 是 initializer operator，不作为 symbol reference；其 argument 仍正常绑定。
mask operator 的 callee 是 literal，同样只绑定其 argument。

## 当前诊断与边界

当前累积诊断包括同 scope duplicate symbol 和无效/为零的 parameterized count。后续语义
阶段仍需完成：

- linkage-compatible redeclaration 与 function prototype/definition 合并；
- special register 与外部 symbol 分类；
- initializer type、array shape 和元素数量校验；
- 将绑定后的 register/symbol `SymbolId` 接入 Resolved IR 与 checker。
