# Symbol Binding 设计

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

每个 module 有一个根 scope，每个 `.entry/.func` item 有一个以 module scope 为 parent 的
function scope，每个 nested `AstBlock` 则有按 source range 识别的 child block scope。当前收集：

- module/function variable declaration；
- function input 与 return parameter；
- function symbol；
- label，以及 function-local `.callprototype`、`.calltargets` 与
  `.branchtargets` declaration；即使写在 nested block 内，也放入所属的 function scope。

`SymbolId` 与 `ScopeId` 是强类型索引。`Symbol` 保留名称、kind、声明位置，以及变量或
parameter 的 state space/type；function symbol 还记录 `.func/.entry` 类别。
`SymbolLinkage` 直接记录 `.extern/.visible/.weak`；function symbol 通过 `owned_scope` 指向其
function scope。若同一 function 同时存在 prototype 与
definition，每个 item 都有独立 scope，而 `owned_scope` 优先指向 definition。
每个 function scope 同时拥有该次声明的 `SourceRange`。`functionScope(range)` 返回唯一
匹配的 occurrence；缺失或含糊时返回空值。调用者无需对齐 function 与 scope vector 的遍历，
处理 prototype 局部 parameter 时也不能用 `owned_scope` 取代此 occurrence identity。

同 scope 的查找优先 exact name，再查 parameterized name，最后沿 parent scope 向上。
因此 block declaration 可以遮蔽 outer/module symbol，sibling block 彼此不可见；label 和
control-flow metadata 则有意保持 function-local，而非 block-local。

debug identity 使用独立的 module metadata namespace。`.file` index 会规范化为
`uint64_t`（decimal/octal/hex 与可选 `u`/`U` suffix），并以 `DebugFile` symbol 绑定；重复 index
有意复用第一个 `SymbolId`。`.debug_str` section 本身及其 raw `name:` payload label 都会成为
`DebugStringLabel` symbol。普通 `SymbolTable::lookup()` 会跳过这两种 debug kind，因此 PTX
program declaration 可以使用相同 spelling。`.loc` 的 basic 与 `inlined_at` file field 会生成
`DebugFile` reference，而 `function_name` 生成 `DebugFunctionName` reference；后者只能解析为
`.debug_str` section 自身或其中 label。这些 metadata identity 会诊断 unresolved reference，
但绝不作为 PTX operand。

## Parameterized variable name

`name<count>` 依 PTX 语法表示 `name0` 到 `name(count-1)`。symbol table 不展开这些名称，
而是保存 base name 与 count；lookup `%r2` 会返回同一个 declaration `SymbolId`，并在
`SymbolLookup::parameterized_index` 中记录 `2`。这样不会因较大的 count 生成大量 symbol。
成员后缀使用规范十进制拼写：`%r<3>` 匹配 `%r0..%r2`，但不匹配 `%r02`。

收集 declaration 时会比较其实际名称集合。parameterized declaration 与 explicit name，
以及两个不同 base 的 parameterized declaration，只要展开后存在同 scope 成员重叠，都会
产生带 previous range 的 duplicate diagnostic；parameterized base 本身不属于展开集合，
所以 `name<2>` 与 explicit `name` 仍是两个不同 symbol。
ordinary/ordinary 只比较两个 literal spelling；ordinary/group 用 ordinary spelling
匹配 group 的成员；group/group 只比较生成出的成员。非法的 zero-count group 没有成员，
不会产生 overlap candidate；同 base 的 compact declaration 仍沿用 exact-declaration
duplicate policy。

Parameterized name 可用于任意 state space，但不能同时声明 array 或 initializer。原先
只允许 `.reg` 的限制已移除，公共 CST/AST 字段也统一命名为 `parameterized_count`。

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
`.reg` function pointer、call parameter 属于 `.reg/.param`、direct branch target 是当前
function 的 label，且 indirect target-set operand 是 `.callprototype` 或 `.calltargets`
symbol。三种 metadata declaration 都拥有稳定的 function-scope `SymbolId`。member validation、
duplicate policy 与 prototype/signature semantics 由 declaration semantics 检查；binding 不
resolve metadata member 或 instruction use。详见 `control_flow_syntax_design.md`。

## 当前诊断与边界

当前累积诊断包括 same-scope duplicate symbol、parameterized name-set overlap、无效/为零
的 parameterized count、冲突的 linkage qualifier，以及真正未声明的 reference。module scope 的同名
declaration 会先共享稳定的 `SymbolId`，再交给 declaration semantic pass 判断是合法
redeclaration、签名冲突还是多个 definition。module resolver 会保留 special register
与 unresolved reference 的区别；scalar `mov` 的统一 source 已将前者解析为带稳定身份与
component 的 `ResolvedSpecialRegisterRef`。现行声明类型与 intrinsic availability 由语义注册表
按该身份提供，指令相关的历史读取兼容由 generated checker descriptor 提供。尚未声明
special-register shape 的 opcode 仍会得到 operand 不支持诊断，而不是“未声明”。declaration
semantic pass 的设计见 `declaration_semantics_design.md`。

32/64-bit integer/bit-size `mov d, symbol[+offset]` 与 `ld.u32 d, [address]` 也已消费
binding identity：前者的
direct symbol 生成 `ResolvedSymbolRef`，带 offset 时把该表示嵌入 `ResolvedAddress` base；
后者的 symbol address base 使用同一表示。module resolution 保存稳定 `SymbolId`、
parameterized member、declaration kind、声明 state space 与实际 address state space；direct
parameter memory address 与 kernel formal parameter 的 `mov` 地址属于 `.param`，
device-function formal parameter 的 `mov` 地址属于 `.local`，且 return parameter 取址由
checker 在所有 device parameter 上要求 PTX 2.0 / SM 20 baseline，并将 return parameter 的
最低 PTX 提升至 6.0。function-local `.param` call-argument variable 与 formal parameter 分开绑定；
direct `ld.param`/`st.param` 可取址，`mov` 仍拒绝取址。standalone resolution 只保留 spelling。bare function name 绑定为 `ResolvedFunctionRef`，保存
同一稳定 `SymbolId` 与 `.func/.entry` 类别；kernel function 地址由 checker 要求 PTX 3.1 /
SM 35，device-function 地址沿用 `mov` 的 PTX 1.0 baseline。
后续语义阶段仍需完成：

- state-space compatibility，以及其余 special-register/type form。
