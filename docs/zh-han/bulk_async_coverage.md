# 非 tensor bulk 异步数据移动

Frontend 建模 [PTX ISA 9.3 §9.7.9.12、.14、.26.4、.26.6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
中的非 tensor、非 multimem 形式。指令解析为自有、类型化的 `Cp` 或
`St` variant，并经过目标相关检查。普通 non-bulk `cp.async` 保持原有的
三操作数公开形状。`red.async`、tensor-map 和 multimem 传输各有独立契约。

| 形式 | 完成机制与操作数 | 最低 PTX / 目标 |
| --- | --- | --- |
| `cp.async.bulk.shared::cluster.global.mbarrier::complete_tx::bytes` | shared 目的、global 源、32-bit 字节数、shared mbarrier 对象地址；可选 multicast 的 16-bit `ctaMask` 和/或 L2 cache hint 的 64-bit policy | 8.0 / SM 90 |
| `cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes` | 相同基础操作数；可选 L2 policy 和/或携带两个 32-bit 忽略字节数的 `.ignore_oob` | 8.6 / SM 90；`.ignore_oob` 9.2 |
| `cp.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes` | shared 目的与源、字节数、shared mbarrier 对象地址 | 8.0 / SM 90 |
| `cp.async.bulk.global.shared::cta.bulk_group` | global 目的、shared 源、字节数；可选 L2 policy 和/或携带 16-bit `byteMask` 的 `.cp_mask` | 8.0 / SM 90；`.cp_mask` 8.6 / SM 100 |
| `cp.async.bulk.commit_group`、`wait_group{.read} N` | 独立 bulk-group 身份；`N` 是整数常量，`.read` 只等待源读取 | 8.0 / SM 90 |
| `cp.reduce.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes` | shared 归约，携带字节数和 mbarrier 对象地址 | 8.0 / SM 90 |
| `cp.reduce.async.bulk.global.shared::cta.bulk_group` | global 归约，携带字节数；可选 L2 cache-hint 的 64-bit policy | 8.0 / SM 90 |
| `cp.async.bulk.prefetch.L2.global` | global 地址、32-bit 字节数；可选 L2 cache-hint 的 64-bit policy | 8.0 / SM 90 |
| `st.async[.weak][.shared::cluster].mbarrier::complete_tx::bytes` | 标量或 v2/v4 寄存器数据及 shared mbarrier 对象地址 | 8.1 / SM 90；标量 `.b128` 9.2 |
| `st.async[.mmio].release.{gpu,sys}[.global]` | global 标量数据，无 mbarrier；`.mmio` 要求 `.sys` | 8.7 / SM 100 |
| `st.bulk[.weak][.shared::cta]` | u32/u64 寄存器或立即数字节数，初始化值必须是字面量 `0` | 8.6 / SM 100；u32 字节数寄存器 9.0 |

shared 归约支持 `add` × `u32/s32/u64`、`min/max` × `u32/s32`、
`inc/dec` × `u32`、`and/or/xor` × `b32`。global 归约支持
`add` × `u32/s32/u64/f32/f64`、`min/max` ×
`u32/s32/u64/s64/f16/bf16`、`inc/dec` × `u32`、`and/or/xor` ×
`b32/b64`。global `add.f16` 和 `add.bf16` 必须带 `.noftz`。

PTX 9.3 为 bulk copy 增加显式 `.weak`，以及带 `.b128` 的
`.relaxed.{cta,cluster,gpu,sys}`；shared→shared 的 relaxed copy 仅接受
`.cta/.cluster`。这些 copy 限定符要求 `sm_90a`、SM 100 起的
`sm_100f` family，或 SM 110 起的 `sm_110f` family。原有无显式限定符的
weak copy 仍可在基础 SM 90 上使用。归约的 `.relaxed.scope` 是独立的
PTX 9.3 扩展，在基础 SM 90 上即可使用；shared 归约只允许
`.cta/.cluster`，global 归约允许四种 scope。省略时依照 ISA 默认为
`.relaxed.sys`。multicast 的架构列表是性能建议，不是额外合法性门槛。

Checker 检查已知地址空间、静态对齐（bulk copy/reduce/prefetch 为 16
字节，barrier 为 8 字节）、修饰符和操作数配对、类型组合、立即数倍数
以及目标可用性。`st.bulk` 字节数必须为 8 的倍数且不大于
16,777,216。对寄存器字节数和地址，其运行时值、远端 CTA 身份、
group/phase 完成协议及缓存效果仍由 consumer 负责；frontend 不执行指令。
