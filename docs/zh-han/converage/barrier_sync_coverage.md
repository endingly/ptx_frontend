# CTA `barrier.sync`、`barrier.arrive` 与 `barrier.red` 覆盖范围

前端建模 [PTX ISA 9.3 §9.7.14.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar)
中的 `barrier{.cta}.sync{.aligned} a{, b}`、
`barrier{.cta}.arrive{.aligned} a, b` 及三种 `barrier{.cta}.red`
形式。这是独立的 `barrier`
opcode，不是 `bar.sync` 或 `barrier.cluster` 的别名。

`a` 是 `.u32` 立即数或寄存器 barrier 编号。立即数必须在 `0..15` 内；
寄存器中的实际编号仍是运行时义务。`sync` 的 `b` 是可选的 `.u32` 立即数或
寄存器参与线程数；立即数必须是 32 的倍数，省略 `b` 表示 CTA 中所有线程参与。
`red` 也使用相同的可选参与线程数。
`arrive` 必须有 `b`，其立即数必须为正且为 32 的倍数。寄存器中的线程数
无法静态检查。

`barrier{.cta}.red.popc{.aligned}.u32 d, a{, b}, {!}c` 将
True 谓词的数量写入 `.u32` 寄存器；
`barrier{.cta}.red.{and|or}{.aligned}.pred p, a{, b}, {!}c`
将谓词的合取或析取写入谓词寄存器。`c` 是可选取反的谓词寄存器。
归约会等待参与的 warp，并向各等待线程写入结果；前端保留取反的类型和
所有权，不计算归约结果。前端保留 `.cta` 和 `.aligned` 是否写出，
并保留显式 `.aligned` 的源码位置；不证明 CTA 收敛，也不模拟 barrier 状态。
在 `sm_6x` 及更早目标上，未写 `.aligned` 的 `barrier` 指令仍有规范所述的
aligned 运行时限制，但 resolved `.aligned` 字段继续忠实记录源码中未写出。

无 `.cta` 的 `barrier.sync`、`barrier.arrive` 与 `barrier.red` 要求 PTX 6.0、`sm_30`；
其 `.cta` 形式要求 PTX 7.8、`sm_30`。`.cta` 不改变 CTA barrier 语义。
`arrive` 不等待其他参与 warp 到达。既有 `bar{.cta}` 和
`barrier.cluster` 形式保留各自独立契约。规范禁止在同一活动 barrier 上
混用 `red` 与 `sync` 或 `arrive`，前端不证明此运行时协议。
`mbarrier` wait 其余限定符组合不在本次范围。
