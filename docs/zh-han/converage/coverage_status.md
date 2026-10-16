# 当前覆盖范围与证据边界

技术入口为 [GitHub Wiki](https://github.com/endingly/ptx_frontend/wiki)。
[参考索引](../index.md)将详细设计和各指令族契约与源码一同维护。
本次对齐描述版本 `2bd78d605f7fc545c11b77f42e9cd5bc7fe376a4`；
文档修改不扩大实现覆盖。

## ISA 范围

固定规范基线为 [CUDA 13.3 / PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)。
覆盖全部 14 个指令 YAML 的源码模型审计发现 137 个 opcode root、5,275 个 variant：
136 个官方 reserved root 均出现，另有 `fabric`。这些计数只说明 inventory 中存在，
不证明所有合法 modifier、operand layout、provenance case、availability profile 或运行时行为。

当前模型已超出历史 seed slice：scalar arithmetic、conversion、memory/synchronization、
warp matrix/WMMA/WGMMA、TMA、Tensor Memory/TCGEN05 dense/sparse/WS MMA、
multimem、Fabric/CFT、texture、surface、stack 和 video 均有明确源码契约。
精确接受边界应查看对应族文档及测试。

## 已确认源码模型缺口

[Issue #280](https://github.com/endingly/ptx_frontend/issues/280)按固定手册跟踪以下七组。
它们是当前模型缺少的合法 ISA 情形，不是新接纳形式：

| 缺口 | 当前边界 | 后续事项 |
| --- | --- | --- |
| LD/LDNC/ST cache hint | 当前 `L2::cache_hint` 要求末尾 policy operand，ISA 允许省略。 | [#281](https://github.com/endingly/ptx_frontend/issues/281) |
| Multimem 默认语义与显式 scope | `ld_reduce`/`st` 显式 scope 但省略 semantics（默认 weak）的形式缺失。 | [#282](https://github.com/endingly/ptx_frontend/issues/282) |
| Generic prefetch 来源 | 无状态空间后缀 `prefetch.tensormap` 的已知 const/kernel-param，以及 `prefetchu.L1` 的已知 global 或文档化 const/local/shared no-op 地址缺失。 | [#283](https://github.com/endingly/ptx_frontend/issues/283) |
| Legacy MAD | `sm_1x` 上隐式 rounding 的 `mad.f32` 缺失。 | [#284](https://github.com/endingly/ptx_frontend/issues/284) |
| Legacy vote | 未同步 `vote` 缺失；同步形式有独立契约。 | [#285](https://github.com/endingly/ptx_frontend/issues/285) |
| Legacy shuffle | 未同步 `shfl` 缺失；同步形式有独立契约。 | [#286](https://github.com/endingly/ptx_frontend/issues/286) |
| Legacy 映射 FP64 可用条件 | 已确认缺少的替代条件仅为 SM10/11/12 加 `map_f64_to_f32` 的 `div.rn.f64`、`rcp.rn.f64`、`sqrt.rn.f64`。 | [#287](https://github.com/endingly/ptx_frontend/issues/287) |

旧 PTX 版本汇编器观察不能覆盖 PTX 9.3 语法。optional cp.async bounds、
WGMMA shape 疑问与 WMMA f64 歧义没有被本审计提升为已确认 missing-operation 结论。

## 验证与运行时边界

parse/resolve/check 流水线验证已建模源码形式。两个 module validation overload
都会重新检查严格初始 header 顺序、target item 分类、scalar storage occurrence/layout
一致性和 function signature/declaration ABI 一致性，包括 AST 销毁后的 owned 路径。
具体边界见 [module validation contract](../resolved_ir_design.md)、
[storage metadata](../storage_declarations.md) 和
[parameter metadata](../parameter_declarations.md)。
`resolveAndValidateModule` 要求完整源码上下文，不表示穷尽 PTX 验证。

调用方已知 descriptor、scale 和 sparsity fact 是条件性断言。TCGEN05 K96
scale-layout role row 仍保留开放语义义务。前端不证明实时 descriptor byte、
collector/protocol history、memory allocation、跨线程 ordering、数值执行或 GPU 结果。
历史 simulator pin 与 build/ptxas 测量仍是历史记录。

## 维护方式

源码契约变化应同步英中参考文档与相关测试。Wiki 作为链接详细参考的技术指南；
README 仅保留背景、构建安装、使用与许可。不要重建手写 opcode ledger，也不要把重复
计数当作 support gate。代码与可复现检查决定漂移，issue 状态和 roadmap checkbox
本身不证明验收。公共 API 仍处于 pre-1.0。
