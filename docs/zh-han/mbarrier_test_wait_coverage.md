# `mbarrier.test_wait` 限定符覆盖范围

前端为 [PTX ISA 9.3 §9.7.14.16.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-mbarrier-test-wait-mbarrier-try-wait)
中既有的十种 `mbarrier.test_wait` 结构形式建模 `.sem.scope` 组合。
两个限定符必须同时写出，且在共享内存空间限定符和 `.b64` 之前依次写为
`.acquire|.relaxed`、`.cta|.cluster`。单独写一个限定符、顺序颠倒，或使用
`.shared::cluster` wait 对象都会被拒绝。scope 限定符表示同步范围，
`.shared::cta` 表示对象的地址空间；两者相互独立。

十种组合形式沿用 generic、`.shared`、`.shared::cta` 地址的 token/parity
操作数。显式 `.phase_type::primary` 保留普通、report predicate 和
report value 三种布局。显式 `.phase_type::conditional` 必须搭配 `.parity`，
且没有 report 操作数。前端检查地址的八字节对齐与 `0..1` 的立即数阶段奇偶值；
owned resolved IR 保留显式限定符的值和源码位置。

基本显式组合从 PTX 8.0、`sm_80` 开始可用。`.relaxed` 从 PTX 8.6 开始，
且要求 `sm_90`；`.cluster` 要求 `sm_90` 和 cluster capability。
phase-type 与 report 形式要求 PTX 9.3、`sm_90`。无显式限定符的形式
保持独立：源码模型记录限定符缺省，实际语义默认为 acquire、CTA scope。
前端检查指令局部语法、操作数和目标可用性；不证明 mbarrier 阶段推进、
参与线程或运行时内存可见性。`mbarrier.try_wait` 的限定符形式不在本文范围内。
