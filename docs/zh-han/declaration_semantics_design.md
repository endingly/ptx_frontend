# Declaration Semantics 设计

## 定位与 API

declaration semantics 位于 lexical binding 之后、Resolved IR 之前。公开入口为：

```cpp
auto diagnostics =
    declaration_semantics::checkDeclarations(module, binding.table);
```

该 pass 使用结构化 Syntax AST 检查单个 declaration 的 initializer/array 约束，并使用
module 级 declaration 序列检查跨声明兼容性。`resolveModule()` 会自动运行 binding 与本
pass，并在解析 instruction 前累积两者的诊断。

## Array 与 initializer

array dimension 必须能求值为正整数 constant。求值器以带 `.s64/.u64` signedness 的
64-bit bit pattern 保存每个整数子表达式，支持负数中间值、cast、usual arithmetic
conversion，以及一元/二元/三元运算；因此 `-1 + 2` 等合法表达式不会在中间阶段被
误判。`WARP_SZ` 同样在此阶段求值；symbol address 不能作为 dimension。
只有带 initializer 的第一维可以省略，其长度由最外层 initializer list 推导。

initializer 的 brace nesting 必须与 array 维数一致；vector declaration 额外形成长度为
2 或 4 的最内层 aggregate。每一维允许少于声明长度，剩余元素按 PTX 规则补零；只有
超出该维长度才产生元素数量诊断。

scalar leaf 区分 integer、floating 和 symbol address expression。整数与浮点 expression
必须进入相应类型类别，symbol address 只能初始化 `.u32/.u64`；initializer symbol 必须
指向 function 或 `.global/.const` variable。`generic()` 与 mask operator 作为 initializer
operator 处理，而不是普通 function call。

## Redeclaration

module scope 的同名 item 先由 binding 合并到稳定的 `SymbolId`，再由本 pass 判断是否合法：

- 多个签名相同的 `.extern` variable declaration 合法；
- 签名相同的 `.func` prototype 可以与至多一个 definition 合并；
- variable 的 state space、alignment、vector/base type、parameterized count 与 array
  shape 必须兼容；
- function kind、`.noreturn`、return/input parameter interface 与 linkage 必须兼容；
- symbol kind 冲突、linkage 冲突、签名变化和多个 definition 均产生带 previous range 的
  诊断；
- `.extern .func` 只能是 prototype，不能带 body。

function prototype 与 definition 各自仍拥有 lexical scope。function symbol 的
`owned_scope` 优先指向 definition scope，从而使后续 module resolution 使用 definition
中的 parameter/local declaration。

## 当前边界

该 pass 不负责 opcode-specific instruction type checking，也不实现 link-time 的跨 module
symbol 选择。integer constant expression 当前覆盖已有 AST grammar，并按 PTX 的
`.s64/.u64` 类型传播规则求值；后续若增加新的 constant operator，需要同时扩展分类、
signedness 传播与求值逻辑。
