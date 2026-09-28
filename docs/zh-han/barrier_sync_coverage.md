# CTA `barrier.sync` 与 `barrier.arrive` 覆盖范围

前端建模 [PTX ISA 9.3 §9.7.14.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar)
中的 `barrier{.cta}.sync{.aligned} a{, b}` 与
`barrier{.cta}.arrive{.aligned} a, b` 范围。这是独立的 `barrier`
opcode，不是 `bar.sync` 或 `barrier.cluster` 的别名。

`a` 是 `.u32` 立即数或寄存器 barrier 编号。立即数必须在 `0..15` 内；
寄存器中的实际编号仍是运行时义务。`sync` 的 `b` 是可选的 `.u32` 立即数或
寄存器参与线程数；立即数必须是 32 的倍数，省略 `b` 表示 CTA 中所有线程参与。
`arrive` 必须有 `b`，其立即数必须为正且为 32 的倍数。寄存器中的线程数
无法静态检查。前端保留 `.cta` 和 `.aligned` 是否写出，
并保留显式 `.aligned` 的源码位置；不证明 CTA 收敛，也不模拟 barrier 状态。
在 `sm_6x` 及更早目标上，未写 `.aligned` 的 `barrier` 指令仍有规范所述的
aligned 运行时限制，但 resolved `.aligned` 字段继续忠实记录源码中未写出。

无 `.cta` 的 `barrier.sync` 与 `barrier.arrive` 要求 PTX 6.0、`sm_30`；
其 `.cta` 形式要求 PTX 7.8、`sm_30`。`.cta` 不改变 CTA barrier 语义。
`arrive` 不等待其他参与 warp 到达。既有 `bar{.cta}` 和
`barrier.cluster` 形式保留各自独立契约。独立的
`barrier{.cta}.red` 以及 `mbarrier` wait 其余限定符组合不在本次范围。
