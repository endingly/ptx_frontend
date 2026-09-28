# 普通 `fence` 覆盖范围

前端建模 [PTX ISA 9.3 §9.7.14.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-membar-fence)
中的线程屏障语法 `fence{.sem}.scope;`。必须写出 scope，支持 `.cta`、
`.gpu`、`.sys` 和 `.cluster`；显式语义支持 `.sc`、`.acq_rel`、`.acquire`
和 `.release`。前端也接受 `fence.scope.sem;`，该顺序出现在 ISA 的 cluster
示例中，CUDA 13.3 `ptxas` 也接受。两种顺序解析为相同的类型化 variant 和值。

| 形式 | 最低 PTX | 最低目标架构 | 额外 capability |
| --- | --- | --- | --- |
| CTA/GPU/SYS scope，省略语义、`.sc` 或 `.acq_rel` | 6.0 | `sm_70` | 无 |
| cluster scope，省略语义、`.sc` 或 `.acq_rel` | 7.8 | `sm_90` | `cluster` |
| CTA/GPU/SYS scope，`.acquire` 或 `.release` | 8.6 | `sm_90` | 无 |
| cluster scope，`.acquire` 或 `.release` | 8.6 | `sm_90` | `cluster` |

省略 `.sem` 时，owned IR 保留 `MemoryConsistency::Omitted`，且没有语义限定符
的源码位置；ISA 中实际采用 `.acq_rel` 行为。显式 `.sc` 使用
`MemoryConsistency::Sc`。显式 `fence.acq_rel.cta` 和
`fence.cta.acq_rel` 保留既有的 `Fence::AcqRelCta` variant。其他普通形式
分别使用互不重叠的 CTA、GPU/SYS 和 cluster variant。所有形式均无操作数。
Checker 对 owned IR 检查目标和 capability 限制；前端不执行 fence。

独立的无操作数 variant `fence.mbarrier_init.release.cluster;` 要求 PTX 8.0、
`sm_90` 和 `cluster` capability。按照 ISA 规定，它把 fence 的 release 效果
限制在同一线程先前对 `.shared::cta` 内对象执行的 `mbarrier.init` 操作。
Owned IR 以固定类型字段保存 `.mbarrier_init`、`.release` 与 `.cluster`
控制。省略或重排这些限定符、改变语义或 scope、添加操作数都会被拒绝。
普通的 `fence.release.cluster;` 仍是另一种 variant，其 PTX 下限为 8.6。

另有两种无操作数、非 proxy 的形式要求 PTX 8.6、`sm_90` 与 `cluster`
capability：`fence.acquire.sync_restrict::shared::cluster.cluster;` 和
`fence.release.sync_restrict::shared::cta.cluster;`。acquire 形式只对
`.shared::cluster` 中对象的操作提供顺序保证；release 形式只对
`.shared::cta` 中对象的操作提供顺序保证。Owned IR 为每种形式保存固定的
类型化语义、限制符和 cluster scope。交换语义或受限 state space、修改
scope、重排限定符或添加操作数都会被拒绝。这些 variant 与既有的
`fence.proxy.async::generic.*.sync_restrict` 形式分别建模。
