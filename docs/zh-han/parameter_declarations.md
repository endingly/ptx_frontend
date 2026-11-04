# 参数声明

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#parameter-declarations) 为唯一维护依据；本文保留技术参考与历史证据。

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#parameter-declarations--parameter-declarations)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/parameter_declarations.md#parameter-declarations)。

## 覆盖矩阵

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#parameter-declarations--coverage-matrix)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/parameter_declarations.md#coverage-matrix)。

## Alignment、pointer 与大小

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#parameter-declarations--alignment-pointers-and-size)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/parameter_declarations.md#alignment-pointers-and-size)。

## Owned metadata 与后续范围

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#parameter-declarations--owned-metadata-and-remaining-scope)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/parameter_declarations.md#owned-metadata-and-remaining-scope)。

### 保留边界与规范分类

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#parameter-declarations--retained-boundaries-and-normative-classification)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/parameter_declarations.md#retained-boundaries-and-normative-classification)。

## 依据与回归

规范依据为归档的 [PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)：
[parameter state space](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parameter-state-space)、
[fundamental types](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fundamental-types)、
[entry](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#kernel-and-function-directives-entry)、
[device function](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#kernel-and-function-directives-func) 与
[call prototype](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-directives-callprototype)。
Assembler 实验只补充证据，不取代规范。

作为非硬件的补充观察，使用 PTX 9.1 / `sm_80` 运行 `ptxas` 13.1：independent texture
mode 的 opaque entry list 与 body-local parameterized group 可汇编；孤立 `.f16x2`、vector
`.param` 在 allocation 时被拒绝，header 多维 array 被该 parser 拒绝。该工具版本结果不会
覆盖上述 PTX 9.3 分类，也不是 execution test。

回归覆盖明确的 parser 边界、semantic type/role/version/size 验证、resolved declaration
identity/lifetime，以及单独编译的 installed-package consumer。GPU execution 不属于声明
验证 gate。

## Fundamental vector 与 packed element

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#parameter-declarations--fundamental-vectors-and-packed-elements)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/parameter_declarations.md#fundamental-vectors-and-packed-elements)。

## 历史 entry body input 与 typed role

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#parameter-declarations--historical-entry-body-inputs-and-typed-roles)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/parameter_declarations.md#historical-entry-body-inputs-and-typed-roles)。
