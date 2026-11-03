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

非 parity state input 在生成的 descriptor 中保留显式 Register token policy，source resolution 与 AST-free checker 均拒绝 sink payload。同一 common rule 区分 CTA arrival 的 RegisterOrSink 与 remote cluster 的 Sink output；普通 report-value register 不属于该 token 规则。Optional descriptor 缺省表示非 token policy。公共 optional member 迁移要求重新编译 consumer；这不改变 独立的 carrier 类型规则、source qualifier、availability 或运行时 token/phase 义务。

书写的 `.b64` 后缀不表示所有 state role 都是 Bit operand。非 parity wait input 的类型为 U64，使用同宽 B64/S64/U64 carrier，拒绝 F64。Opaque arrival output 与 `pending_count` input 则接受同宽 B64/S64/U64/F64。实际声明与 source provenance 仍可见，form、target、address、count、parity 与 time-hint 检查不变。Report value 保留带限定的 logical B8，使用 B8/S8/U8 carrier；固定 PTX 9.3 的 report-role width 对应仍有参照缺口。这些检查不认证运行时 state，也不证明 phase 进展。
