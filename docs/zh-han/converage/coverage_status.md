# 历史覆盖基线与证据边界

本文为**历史证据记录**；表格、缺口、计数与工具结果只描述其记录版本（含 `2bd78d6`），不定义当前 SDK 支持范围。当前语法、操作、类型、目标及保留限制统一见英文 [SDK 支持契约](../../sdk_support.md)。

技术入口为 [GitHub Wiki](https://github.com/endingly/ptx_frontend/wiki)。
[参考索引](../index.md)将详细设计和各指令族契约与源码一同维护。
本页保留版本 `2bd78d605f7fc545c11b77f42e9cd5bc7fe376a4` 的历史对齐记录。
下方 inventory 计数和六项缺口表描述该历史快照。当前实现的精确边界应查看
详细指令族参考、公共契约及测试；文档修改不扩大实现覆盖。

## 历史 ISA inventory

固定规范基线为 [CUDA 13.3 / PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)。
该版本覆盖全部 14 个指令 YAML 的源码模型审计发现 137 个 opcode root、5,275 个 variant：
136 个官方 reserved root 均出现，另有 `fabric`。这些计数只说明 inventory 中存在，
不证明所有合法 modifier、operand layout、provenance case、availability profile 或运行时行为。

该版本模型已超出更早的 seed slice：scalar arithmetic、conversion、memory/synchronization、
warp matrix/WMMA/WGMMA、TMA、Tensor Memory/TCGEN05 dense/sparse/WS MMA、
multimem、Fabric/CFT、texture、surface、stack 和 video 均有明确源码契约。
精确接受边界应查看对应族文档及测试。

## 基线记录的源码模型缺口

[Issue #280](https://github.com/endingly/ptx_frontend/issues/280)按固定手册跟踪下表记录的组。
此历史表记录固定版本快照缺少的合法 ISA 情形；当前支持边界由详细指令族契约及测试
确定，issue 合并状态是独立事项。

| 基线缺口 | 固定版本的边界 | 跟踪事项 |
| --- | --- | --- |
| LD/LDNC/ST cache hint | 基线版本的 `L2::cache_hint` 要求末尾 policy operand，ISA 允许省略。 | [#281](https://github.com/endingly/ptx_frontend/issues/281) |
| Multimem 默认语义与显式 scope | `ld_reduce`/`st` 显式 scope 但省略 semantics（默认 weak）的形式缺失。 | [#282](https://github.com/endingly/ptx_frontend/issues/282) |
| Legacy MAD | `sm_1x` 上隐式 rounding 的 `mad.f32` 缺失。 | [#284](https://github.com/endingly/ptx_frontend/issues/284) |
| Legacy vote | 未同步 `vote` 缺失；同步形式有独立契约。 | [#285](https://github.com/endingly/ptx_frontend/issues/285) |
| Legacy shuffle | 未同步 `shfl` 缺失；同步形式有独立契约。 | [#286](https://github.com/endingly/ptx_frontend/issues/286) |
| Legacy 映射 FP64 可用条件 | 已确认缺少的替代条件仅为 SM10/11/12 加 `map_f64_to_f32` 的 `div.rn.f64`、`rcp.rn.f64`、`sqrt.rn.f64`。 | [#287](https://github.com/endingly/ptx_frontend/issues/287) |

旧 PTX 版本汇编器观察不能覆盖 PTX 9.3 语法。optional cp.async bounds、
WGMMA shape 疑问与 WMMA f64 歧义没有被本审计提升为已确认 missing-operation 结论。


## 当前验证与维护边界

当前验证、运行时限制与支持范围见英文 [SDK 支持契约](../../sdk_support.md)。本文只保留上述固定版本的历史记录。
