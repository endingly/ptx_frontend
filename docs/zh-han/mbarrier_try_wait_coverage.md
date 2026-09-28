# `mbarrier.try_wait` 限定符覆盖范围

前端为 [PTX ISA 9.3 §9.7.14.16.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-mbarrier-test-wait-mbarrier-try-wait)
中既有的十种 `mbarrier.try_wait` 结构形式建模 `.sem.scope` 组合。
两个限定符必须显式写出，按 `.acquire|.relaxed` 紧接 `.cta|.cluster`
的顺序放在对象空间限定符和 `.b64` 之前。单独写一个限定符、顺序颠倒，
或使用 `.shared::cluster` wait 对象都会被拒绝。同步 scope 与
`.shared::cta` 对象地址空间是不同的概念。

每种组合形式沿用无显式限定符形式的 token 或 parity 操作数、generic/
`.shared`/`.shared::cta` 地址选择以及可选 `timeHint`。`timeHint` 是
以纳秒为单位的 `.u32` 寄存器或立即数。显式 `.phase_type::primary`
保留六种布局：普通、report predicate、report value 结果各有带和不带
`timeHint` 两种形式。显式 `.phase_type::conditional` 必须搭配 `.parity`，
且只支持普通的带或不带 hint 布局。立即数阶段奇偶值必须为 `0` 或 `1`，
对象地址必须按八字节对齐。owned resolved IR 保留显式限定符的值、
源码位置和所选操作数布局。

所有 `try_wait` 形式要求 `sm_90`。显式组合从 PTX 8.0 开始可用；
`.relaxed` 从 PTX 8.6 开始，`.cluster` scope 还要求 cluster capability。
phase-type 与 report 形式要求 PTX 9.3。无显式限定符的形式在源码模型中
保持独立，实际语义默认为 acquire、CTA scope。前端验证指令局部契约，
不模拟运行时挂起、时间上限、阶段推进或内存可见性。
