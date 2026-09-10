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

整数字面量与指令立即数共享十进制、前导零八进制和十六进制解码规则。例如，
`010` 的初始化值为 8，用作数组维度时也为 8；无符号后缀不改变基数。

无法在 64 位源码数值域内解码的整数字面量属于非法表达式，而非待求值的合法值。
声明检查在字面量自身的 range 报告 `InvalidIntegerLiteral`，`resolveModule()`
保留该类别和位置。即使三元表达式的两个分支相同，或字面量位于未选中的分支，
折叠也不能掩盖解码失败。合法的待求值表达式仍被单独处理：尚未求值的合法比较
作为条件时，等值分支仍可折叠为常量。

array dimension 必须能求值为正整数 constant。求值器以带 `.s64/.u64` signedness 的
64-bit bit pattern 保存每个整数子表达式，支持负数中间值、cast、usual arithmetic
conversion，以及一元/二元/三元运算；因此 `-1 + 2` 等合法表达式不会在中间阶段被
误判。`WARP_SZ` 同样在此阶段求值；symbol address 不能作为 dimension。
只有第一维可以省略：有 initializer 时由最外层 list 推导其长度；external storage
declaration 也可在没有 initializer 时保留未知的首维。

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

显式 alignment、parameterized count 和 ABI-preserve count 按解码后的整数值比较，
因此等值的八进制与十进制拼写相匹配。

function prototype 与 definition 各自仍拥有 lexical scope。function symbol 的
`owned_scope` 优先指向 definition scope，从而使后续 module resolution 使用 definition
中的 parameter/local declaration。

## Control-flow metadata

同一 pass 还检查 function-local 的 indirect-control metadata。`.calltargets` member 必须是
此前已声明的 device `.func`；重复 member 会以两个 member range 诊断，所有有效 member 必须有
相同的 canonical `FunctionSignature`。`.branchtargets` member 必须是所属 function 中的 label，
允许 forward label。`N<5>` 这样的 compact entry 会基于已有 local label 检查，不创建 synthetic
symbol；缺失 label 使用 compact-entry range 诊断。分支表是有序的索引序列：允许显式目标
重复、compact 与显式目标交叠，以及 compact entry 之间交叠，不对这些目标去重。

`.callprototype` 拒绝同时出现 return parameter 与 `.noreturn`，对 formal 使用既有
alignment/array-extent 检查，并要求 array formal 使用 `.param`。duplicate declaration label 仍由
binding 负责。module resolution 会把有效 prototype 转为与 function 相同的 canonical signature，
并复用已验证的首个 `.calltargets` member signature 进行 indirect-call ABI checking；ABI suffix
availability 仍留给后续工作。

## 参数声明

参数另有按上下文区分的[覆盖与验证契约](parameter_declarations.md)，包括支持的类型、
unsized array 位置、pointer attribute，以及可静态判定的 ISA version/target 与 entry
大小限制。该契约适用于 entry/device header、call-prototype formal 与 body-local `.param`
声明；保留 type spelling 或 array syntax 本身不表示声明合法。

## Entry resource constraint

对已支持的 entry-header `.maxnreg`、`.maxntid`、`.reqntid` 与 `.minnctapersm`，本 pass
以已声明的 module `.version` 检查最低 PTX 版本（依次为 1.3、1.3、2.1 与 2.0）。四者均支持所有 SM，
故此处不额外检查 `.target`。同一 entry 中 `.reqntid` 与 `.maxntid` 互斥；诊断指向后出现的
directive，并以先出现的 range 为上下文。单独的 `.minnctapersm` 在 PTX 中是 warning 而非 error；
warning severity、backend resource feasibility 与数值上限仍不属于本 pass。

## Debug metadata 边界

`.file`/`.loc` 与 `.debug_str` 的 identity table 由 binding 负责：重复 file index 幂等，
`.loc` file reference 必须解析，`function_name` 只能标识 `.debug_str` 自身或其中 raw label。
本 declaration pass 不增加 DWARF payload expression、source attachment 或 resource
feasibility semantic。

## 当前边界

declaration 检查之后，module resolution 将已支持的 storage form 投影到
[拥有自身数据的存储元信息](storage_declarations.md)，包含 checked byte extent 与 typed
initializer value/reference。即使 declaration checker 接受某个 expression category，
超出可表示范围的 form 仍可能在这一步产生 diagnostic。declaration category 与关联 range
会通过 `ResolveDiagnostic` 保留。

该 pass 不负责 opcode-specific instruction type checking，也不实现 link-time 的跨 module
symbol 选择。integer constant expression 当前覆盖已有 AST grammar，并按 PTX 的
`.s64/.u64` 类型传播规则求值；后续若增加新的 constant operator，需要同时扩展分类、
signedness 传播与求值逻辑。
