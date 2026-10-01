# TCGEN 专用同步覆盖

前端建模固定 PTX 9.3 的 `tcgen05.commit` 和两个无操作数的专用 fence。Commit 有八种书写形式：CTA group 1 或 2、通用地址或显式 `.shared::cluster` 屏障地址，以及有无 multicast。Multicast 必须提供标量 General 类 `.b16`、`.u16` 或 `.s16` 寄存器掩码；不接受直接立即数。每一位选择目标 `%cluster_ctarank`，屏障信号作用于该 CTA 共享内存中的相同偏移。`before_thread_sync` 和 `after_thread_sync` 两种 fence 都没有操作数，也没有 CTA-group 限定符。这十种形式共用已有的 `tcgen05` 指令根和一条生成的类型化描述符路径。

```ptx
.version 9.0
.target sm_110a
.address_size 64
.visible .entry kernel() {
  .shared .align 8 .b64 barrier;
  .reg .b16 %mask;
  tcgen05.fence::before_thread_sync;
  tcgen05.commit.cta_group::1.mbarrier::arrive::one.multicast::cluster.b64 [barrier], %mask;
  tcgen05.fence::after_thread_sync;
  ret;
}
```

Commit 描述同一线程先前发出的、CTA group 匹配的适用异步 TCGEN 操作。操作完成时，系统在 cluster 作用域执行计数为一的 `mbarrier::arrive::one` 信号。即使显式写出 `.shared::cluster`，访问屏障仍使用 generic proxy。已知屏障必须八字节对齐且位于 shared 内存；动态指针仍须在运行时落入 cluster-shared 地址窗口。前端检查已知标量指针与掩码声明、源形式、目标以及每个函数体内的 CTA-group 一致性。不同函数体可使用不同 group。前端不证明先前操作存在、屏障初始化与进展、对端 CTA、动态地址有效性、跨函数约定或控制流顺序。

`before_thread_sync` 将先前异步 TCGEN 操作排序在后续 TCGEN 和执行顺序操作之前；`after_thread_sync` 将后续操作排序在先前 TCGEN 和执行顺序操作之后。它们不完成操作，也不替代 `fence.proxy.async`。Commit 的 mbarrier-arrive-one 完成身份与 Tensor Memory load/store wait、bulk group、普通 async group 和 complete-tx-bytes 不同。

这些形式从 PTX 8.6 的精确 `sm_100a`、8.8 的 `sm_100f` 家族，或 9.0 的精确 `sm_110a` / `sm_110f` 家族开始可用。普通或无关目标不能替代。官方 13.3.73 汇编证据支持八种书写形式、两种 fence、兼容的 16 位寄存器掩码和有效目标端点。配对寄存器对照通过，而直接立即数掩码失败；汇编器不能证明运行时掩码值或同步行为。编码描述符/布局基础及四种 TCGEN MMA 形式仍是独立待完成工作。

来源：[CUDA 13.3 / PTX 9.3 commit 与 fence 条款](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-commit)。[复制与位移](tensor_memory_copy_shift_coverage.md)可作为同一 group 的先前 TCGEN 工作；它们的源代码形式本身不触发 barrier 信号。
