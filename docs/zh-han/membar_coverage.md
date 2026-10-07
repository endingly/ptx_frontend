# `membar` 覆盖范围

前端按照 [PTX ISA 9.3 §9.7.14.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-membar-fence)
建模三种普通内存屏障层级：

| 源码形式 | 最低 PTX | 最低目标架构 | Owned IR scope |
| --- | --- | --- | --- |
| `membar.cta;` | 1.4 | 任意 | `MemoryScope::Cta` |
| `membar.gl;` | 1.4 | 任意 | `MemoryScope::Gpu` |
| `membar.sys;` | 2.0 | `sm_20` | `MemoryScope::Sys` |

此外，`membar.proxy.alias;` 是独立且无操作数的 variant，要求 PTX 7.5 和
`sm_60`。它在 generic proxy 与虚拟别名 proxy 之间建立双向顺序。固定的
`.proxy.alias` 写法由两个固定 flag 保留。

四种 async-proxy 写法同样没有操作数，owned IR 用类型化的 `AsyncProxyKind`
保留源码中的 state space：

| 源码形式 | 最低 PTX | 最低目标架构 | 额外 capability |
| --- | --- | --- | --- |
| `membar.proxy.async;` | 8.0 | `sm_90` | 无 |
| `membar.proxy.async.global;` | 8.0 | `sm_90` | 无 |
| `membar.proxy.async.shared::cta;` | 8.0 | `sm_90` | 无 |
| `membar.proxy.async.shared::cluster;` | 8.0 | `sm_90` | `cluster` |

这些形式在 generic proxy 和 async proxy 的访问之间建立双向顺序；源码中的
state space 限制该顺序的适用范围。`sm_90` 下限根据 ISA 的 async-proxy
目标说明及既有 `fence.proxy.async` 合同保守推断；ISA 没有单独给出
`membar.proxy.async` 的目标下限。CUDA 13.3 `ptxas` 即使以 `sm_90` 为目标
仍拒绝这些 `membar` 写法，因此本文不声称汇编器接受它们。建模合同限于前端
选择、owned IR 和目标检查。

每种受支持的写法对应独立的 `Membar` variant，且没有操作数。源码中的 `.gl` 映射为
类型化的 GPU scope；`.gpu` 是另一种写法，不适用于 `membar`。Checker 对
owned IR 检查 PTX 与目标架构下限。其他后缀和操作数不在此覆盖范围内。
前端不执行内存屏障。
