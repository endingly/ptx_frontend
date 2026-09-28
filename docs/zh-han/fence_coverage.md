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
