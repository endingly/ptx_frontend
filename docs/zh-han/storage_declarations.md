# Resolved 存储声明

`ResolvedModule::storage_declarations` 为 `.global`、`.const`、`.shared`、`.local`
declarator 提供拥有自身数据的元信息。数据类型位于
`<ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>`；`resolveModule()` 仍通过
通常的 resolved-IR header 使用。成功解析后可以销毁源码及 Syntax AST。

这是从声明到 consumer 的契约，不是内存分配器，也不表示完整 PTX 声明合法性已验证。
参数继续独立处理：`ResolvedFunction::entry_parameters` 仍描述 entry 输入参数；
register 与 call parameter 不进入存储列表。

## 身份与布局输入

每个源码 declarator 按源码遍历顺序产生一条记录。`symbol_id` 指向既有 module symbol；
`scope_id` 标识 lexical scope；module-level storage 没有 `owner_function`。
因此，nested block 和同名 local 仍可区分。重复且兼容的 external declaration 共享
symbol identity，但保留各自的声明记录。既有 linkage 与 multiple-definition 检查保持有效；
external declaration 后出现同名 plain definition 会被拒绝。

| 字段 | 契约 |
| --- | --- |
| `element_type`、`vector_width` | typed scalar 或明确的 opaque object；vector lane 与 array dimension 分开 |
| `array_extents` | 从外到内的元素数量；scalar 无维度；external 未定长首维没有值 |
| `byte_extent` | 单个声明对象的 checked size；不是 runtime address、frame offset 或 parameterized name 的总大小 |
| `alignment`、`explicit_alignment` | 有效 byte alignment 以及源码是否显式指定 |
| `linkage`、`declaration_kind` | 源码 linkage 与 external declaration／definition 状态 |
| `is_dynamic_shared` | 首维未定长的 external shared declaration |
| `parameterized_count` | 分别命名的对象数量；instruction reference 通过 parameterized index 标识成员 |
| `is_managed`、`unified_id` | 保留 attribute；不是模拟分配策略或 host/device address |

Opaque texture、sampler、surface identity 不会获得臆造的 physical size 或默认 alignment。
未知大小用缺失值表示，绝不用零表示。零或非法 dimension、非法 alignment、乘法溢出会产生
diagnostic，而不会使 size 回绕。

## 初始化器

初始化分为四种明确状态：shared/local 或 opaque object 使用 `Uninitialized`；
global/constant 隐式初始化使用 `Zero`；源码 initializer（包括空 brace list）使用
`Explicit`；本 module 不提供初值的 external declaration 使用 `External`。
显式 initializer 是稀疏的 scalar value 列表，各项带有对象内部的 byte offset。
`Explicit` 中未列出的位置按零初始化；frontend 不会把大型 aggregate 展开成 byte buffer。
递归 array/vector brace 的位置决定各项 offset。

`StorageConstant` 保存已规范化的 element bits：`bits` 为低 64 位，`high_bits` 为高
64 位，与 host byte order 无关。不超过 64 bit 的元素，其高位 word 为零。
`StorageRelocation` 保存 bound symbol、
可选的 parameterized member、state-space/generic/function address interpretation、
byte addend 及可选原始 byte mask（`0xff` 左移 0、8、…、56 bit）。
mask 在应用 addend 之后选择一个 byte，并将其放入低八位；提取之后的算术会被拒绝，
不会被重排到提取之前。
symbol identity 沿用 lexical binding，包括内层 scope 的同名声明。
function reference 可以直接使用或提取 byte mask；对 function reference 做 generic
转换或地址算术会被拒绝。
未解析的 external symbol 和 function reference 都不会
获得臆造地址。下游根据自己的链接／分配模型解析这些引用，无需重新解析 initializer 文本。

已知 source version 早于 PTX 3.1 时，global symbol 保留隐式 generic addressing；
没有 version 的 fragment 使用当前 state-space interpretation。提供 source version 时，
address mask 要求 PTX 7.1，constant-integer mask 要求 PTX 7.3；kernel-function
initializer 要求 PTX 3.1。versionless fragment
不构成 target validity 的证明。

scalar type alternative 只允许 fundamental declaration type；instruction-only 的
packed/alternate format 必须使用对应 bit-container type。integer/bit initializer element
支持 `.b128`，array stride 为 16 byte。整数 literal 与 expression 仍在 PTX 的
`.s64/.u64` 范围求值；更宽的 destination 不会启用 128-bit literal 或 expression 算术。
对于 `.b128`，frontend 只在求值完成后对 signed result 做符号扩展，对 unsigned result
做零扩展。因此 `-1` 的两个 word 均全为 1，而 `-1U` 只有低位 word 全为 1。
稀疏 aggregate 中未列出的位置保持零初始化。

这一扩展是明确的 frontend 策略，不表示已经通过 GPU conformance 验证：PTX 规定在
初始化处转换宽度，但未明确说明 `.b128` 高半部分的规则。当前没有 GPU 验证条件，
本契约也不复现 ptxas 13.3.33 输出中观察到的异常高位 word。

floating initializer 支持 `.f32/.f64` literal、符号及括号，不规范化复合浮点算术。
opaque object metadata 限于 module-level scalar `.global` declaration；其
field-assignment initializer 仍不在 parser/normalizer 的支持范围。
这些边界不会把不支持的值变为零。

## Diagnostic 与边界

`ResolveDiagnostic::declaration_kind` 和 `previous_range` 保留 declaration semantics
给出的类别与关联位置。其他 diagnostic stage 保持既有契约；这不是通用 diagnostic API 重构。

规范化路径复用既有 parser、binding identity 和 constant-expression 机制。超出 typed
initializer domain 的 form 必须产生明确 diagnostic，不能变成空 initializer 或猜测值。
存储元数据不保证已经验证所有 declaration type 的 target rule、linker rule、launch limit
或 runtime access restriction。

规范基线为 [PTX ISA 9.3 variable
rules](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#variables)。
可执行契约由[存储回归测试](../../submod/resolved_ir/test/test_storage_declaration_metadata.cpp)
和[安装包 consumer](../../submod/resolved_ir/test/package_consumer/main.cpp)验证。

分配顺序、不同对象之间的 padding、external symbol resolution、dynamic shared-memory
size、per-CTA/per-thread instance 和 runtime memory content 均由下游负责。
