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
parameter 的 state space/type。`SymbolLinkage` 直接记录 `.extern/.visible/.weak`；function
symbol 通过 `owned_scope` 指向其 function scope。若同一 function 同时存在 prototype 与
definition，每个 item 都有独立 scope，而 `owned_scope` 优先指向 definition。

同 scope 的查找优先 exact name，再查 parameterized name，最后沿 parent scope 向上。
因此 function-local declaration 可以遮蔽 module symbol。

## Parameterized variable name

`name<count>` 依 PTX 语法表示 `name0` 到 `name(count-1)`。symbol table 不展开这些名称，
而是保存 base name 与 count；lookup `%r2` 会返回同一个 declaration `SymbolId`，并在
`SymbolLookup::parameterized_index` 中记录 `2`。这样不会因较大的 count 生成大量 symbol。
成员后缀使用规范十进制拼写：`%r<3>` 匹配 `%r0..%r2`，但不匹配 `%r02`。

收集 declaration 时会比较其实际名称集合。parameterized declaration 与 explicit name，
以及两个不同 base 的 parameterized declaration，只要展开后存在同 scope 成员重叠，都会
产生带 previous range 的 duplicate diagnostic；parameterized base 本身不属于展开集合，
所以 `name<2>` 与 explicit `name` 仍是两个不同 symbol。

Parameterized name 可用于任意 state space，但不能同时声明 array 或 initializer。原先
只允许 `.reg` 的限制已移除，公共 CST/AST 字段也统一命名为 `parameterized_count`。

## Reference binding

binding pass 会访问：

- instruction predicate 与各种 operand shape 中的 identifier；
- array dimension constant expression；
- scalar/递归 initializer 内的 symbol expression；
- call target/return/input/target-set 与 direct branch target。

每个 `SymbolReference` 保留 spelling、range、引用种类、可选 target，并具有明确的
`ReferenceClassification`：

- `DeclaredSymbol`：当前 module 中的普通 declaration；
- `ExternalSymbol`：绑定到当前 module 的显式 `.extern` declaration；
- `SpecialRegister`：PTX 预定义 special register，不需要用户 declaration；
- `Unresolved`：以上均不匹配，是真正未声明的 reference。

成功解析的 parameterized declaration reference 同时保存成员 index。special register
通过独立的 `special_registers` 语义注册表精确识别；该注册表同时保存现行 element type、
vector width 与最低 PTX/SM，是名称分类与 Resolved IR 检查的单一事实来源。
`%envreg<32>`、`%pm<8>`、`%pm0_64..%pm7_64` 与
`%reserved_smem_offset_<2>` 使用有界匹配，不以任意 `%` 前缀代替。`%tid.x` 等 vector
member 在 AST 中绑定其 `%tid` base。`WARP_SZ` 已由 lexer 表示为 immediate，不进入
symbol-reference 路径。

`.extern` 表示 declaration 的定义位于其他 module，不等于允许无 declaration 的名称。
因此 external reference 仍有正常的 `SymbolId` target，只是 classification 与 symbol
linkage 明确标记为 external。

`generic()` 是 initializer operator，不作为 symbol reference；其 argument 仍正常绑定。
mask operator 的 callee 是 literal，同样只绑定其 argument。

call/branch 专用 AST 节点会产生独立 reference kind。binding 已检查 callee 是 function 或
`.reg` function pointer、call parameter 属于 `.reg/.param`，以及 direct branch target 是
当前 function 的 label。详见 `control_flow_syntax_design.md`。

## 当前诊断与边界

当前累积诊断包括 same-scope duplicate symbol、parameterized name-set overlap、无效/为零
的 parameterized count、冲突的 linkage qualifier，以及真正未声明的 reference。module scope 的同名
declaration 会先共享稳定的 `SymbolId`，再交给 declaration semantic pass 判断是合法
redeclaration、签名冲突还是多个 definition。module resolver 会保留 special register
与 unresolved reference 的区别；`mov.u32/.u64` 的统一 source 已将前者解析为带类型和
target availability 的 `ResolvedSpecialRegisterRef`。尚未声明 special-register shape 的 opcode
仍会得到 operand 不支持诊断，而不是“未声明”。declaration semantic pass 的设计见
`declaration_semantics_design.md`。

`mov.u64 d, symbol[+offset]` 与 `ld.u32 d, [address]` 也已消费 binding identity：前者的
direct symbol 生成 `ResolvedSymbolRef`，带 offset 时把该表示嵌入 `ResolvedAddress` base；
后者的 symbol address base 使用同一表示。module resolution 保存稳定 `SymbolId`、
parameterized member 与 state space；standalone resolution 只保留 spelling。
后续语义阶段仍需完成：

- function/parameter address、state-space compatibility，以及其余 special-register/type form。
