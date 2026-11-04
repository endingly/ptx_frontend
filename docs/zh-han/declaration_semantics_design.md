# Declaration Semantics 设计

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design) 为唯一维护依据；本文保留技术参考与历史证据。

## 共享数值求值

`numericConstantValue()` 将拥有源码树的表达式求值为整数 64-bit bits 与 signedness，
或 f64 值；带位置的错误区分 invalid expression 和没有数值的合法 symbolic/deferred
expression。指令数值位置拒绝这两类错误。原有 optional
`constantIntegerValue()` 与 `constantArrayExtent()` helper 保持兼容。
整数/浮点混合算术和相互 cast 无效。整数或浮点除零（包括负零）无效。
conditional 两个分支及 logical 两个 operand 均检查，即使值未被选中；
浮点溢出不新增统一的 finite-only 限制。

十进制浮点常量允许小数点两侧至少一侧有数字，例如 `1.`、`.5`、`1.e2`；
也允许带指数的数字序列，例如 `1e2`。指数由 `e`/`E`、可选符号及必需的数字组成。
浮点常量不带大小后缀，先解码为 binary64，再按现有规则转换到 `.f32` 或 `.f64`；
整数 destination 仍不兼容。依据是 [PTX ISA 9.3 浮点常量](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-constants)。
共享的浮点 lexical category 不扩展 `.version`：其 `major.minor` 语法仍要求小数点
两侧都有十进制数字。

二元 `&`、`|`、`^` 保持 usual arithmetic conversions：两个 signed 输入产生
signed 结果，任一 unsigned 输入产生 unsigned 结果。依据是 PTX 9.3 §4.5.5
正文：仅在 Table 5 的 `&`/`|`/`^` 行与详细二元操作规则冲突时，选择详细规则。
这是前端采用的解释，不是 NVIDIA 发布的勘误。历史 assembler 观察及其确切工具
版本见[位运算策略](bitwise_constant_policy.md)。表达式结果保留 64 位数据及符号性，
直到使用位置转换；destination 类型和规范化源符号缓存不会反向提供表达式类型。
一元 `~` 产生 unsigned，remainder 将两输入解释为 unsigned 并产生 signed。
常量表达式 shift 先将 count 求值为 64 位值，再将其数据解释为 unsigned，
仅接受 0–63。因此 `1 << (-1 + 2)` 合法；求值为 `-1`、`64`、`65` 或
`UINT64_MAX` 的 count 被拒绝，不执行 mask 或 clamp。即使 count 是 unsigned，
结果也保持左操作数的类型：signed 负数右移填充符号位，unsigned 右移填零，
count 为零时保留输入数据。

0–63 是前端采用的表达式 domain 策略。[手册的常量表达式规则](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constant-expression-evaluation)
没有为大于等于 64 的 count 定义结果；本策略不表示厂商要求一律报错，也不表示
PTX `shl`/`shr` instruction 的执行行为。具体 count 错误保留在失败 count 子树，
且不标记为 deferred，包括 conditional 或 logical 未选中子表达式中的非法 shift。
`symbol + 1` 这类合法符号重定位表达式在值未知时保持 deferred；
`symbol << 1` 和 `symbol >> 1` 是非法重定位操作，不是 deferred 数值 shift。
initializer 和 array-dimension 的
声明包装层保留原有覆盖整个表达式的 invalid-initializer/invalid-dimension 诊断；
对同一 AST 表达式调用公共 evaluator，仍得到具体 count 子树错误。MOV、call
及 address 使用位置则直接传播该 count 错误。

合法的规范化 immediate 和 storage payload 在源文本/AST 销毁、复制、赋值及
instruction clone 后，继续接受现有 owned IR 检查。非法 shift 不产生此类 payload，
因而 owned count tree 重校验不适用，也不引入 count-origin 缓存。数组 extent、
destination 类型及地址范围限制仍是独立的使用位置约束。

## 定位与 API

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--shared-numeric-evaluation)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#shared-numeric-evaluation)。

## Array 与 initializer

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--arrays-and-initializers)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#arrays-and-initializers)。

## Unified UUID attribute

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--unified-uuid-attributes)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#unified-uuid-attributes)。

## Redeclaration

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--redeclarations)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#redeclarations)。

## Control-flow metadata

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--control-flow-metadata)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#control-flow-metadata)。

## 参数声明

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--parameter-declarations)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#parameter-declarations)。

## Entry resource constraint

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--entry-resource-constraints)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#entry-resource-constraints)。

## Debug metadata 边界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--debug-metadata-boundary)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#debug-metadata-boundary)。

## 当前边界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--current-boundary)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#current-boundary)。

## 历史 entry body input

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--historical-entry-body-inputs)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#historical-entry-body-inputs)。

### 已弃用的最大 CTA 提示

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#declaration-semantics-design--deprecated-maximum-cta-hint)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/declaration_semantics_design.md#deprecated-maximum-cta-hint)。
