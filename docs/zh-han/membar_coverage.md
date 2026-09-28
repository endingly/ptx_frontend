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
`.proxy.alias` 写法由两个固定 flag 保留；`membar.proxy.async` 不在此覆盖范围内。

每种受支持的写法对应独立的 `Membar` variant，且没有操作数。源码中的 `.gl` 映射为
类型化的 GPU scope；`.gpu` 是另一种写法，不适用于 `membar`。Checker 对
owned IR 检查 PTX 与目标架构下限。其他后缀和操作数不在此覆盖范围内。
前端不执行内存屏障。
