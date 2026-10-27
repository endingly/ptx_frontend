# PTX 9.3 `prefetch` 与 `prefetchu` 覆盖范围

前端建模 PTX ISA 9.3 §9.7.9.16 中已文档化的普通预取和 tensor-map 预取形式。
它解析地址并检查已知状态空间来源与目标可用条件。预取是性能提示；前端不执行
预取，也不证明缓存行为。

使用 `ptxas` 13.3.73 时，直接的 `prefetch.L1 [shared_symbol]` 探针触发
内部错误 C7907。前端遵循 ISA 中 shared 预取为无操作的说明；代表性的汇编
验收探针省略这个直接符号形式。

| 形式 | 地址契约 | 可用条件 |
| --- | --- | --- |
| `prefetch.L1/L2 [a]` | 对 global 或 local 使用 generic 寻址；shared 为无操作 | PTX 2.0 / SM 20 |
| `prefetch.global.L1/L2 [a]` | 显式 global | PTX 2.0 / SM 20 |
| `prefetch.local.L1/L2 [a]` | 显式 local | PTX 2.0 / SM 20 |
| `prefetch.global.L2::evict_last/evict_normal [a]` | 显式 global | PTX 7.4 / SM 80 |
| `prefetch.tensormap [a]` | generic 地址；接受已知 global、const、kernel entry-input 参数存储，已知 shared 存储为无操作 | PTX 8.0 / SM 90 |
| `prefetch.const.tensormap [a]` | 显式 const tensor map | PTX 8.0 / SM 90 |
| `prefetch.param.tensormap [a]` | 显式输入参数 tensor map | PTX 8.0 / SM 90 |
| `prefetchu.L1 [a]` | 指向 uniform cache 的 generic 地址；接受已知 global，const/local/shared 为无操作 | PTX 2.0 / SM 20 |

已知的地址来源必须匹配显式 `.global`、`.local`、`.const` 或 `.param`
后缀。不带状态空间后缀的 `.tensormap` 形式接受 generic 地址寄存器或直接
命名的 global、const、shared 或 kernel entry-input 参数符号；已知 shared 地址为无操作。
该 generic tensor-map 形式拒绝 device input、device return、function-local call 参数，
以及已知 local 地址和显式 `.shared.tensormap`。前端保留具体状态空间、绑定的符号身份与
源码范围，不将声明重标为 generic，也不证明寄存器的运行时地址来源。
ISA 只对 const/param tensor map 承诺预取缓存效果；模型不推断
generic 地址也有此效果。`ptxas` 13.3.73 接受不带状态空间后缀形式中的
直接 global/local/shared 符号，拒绝直接 const/param 符号；这些旧汇编器观察不能覆盖
这里实现的文档化 ISA 契约。
tensor-map 形式不带 `.L1`/`.L2`
后缀。`prefetchu` 只有 `.L1`，且不带状态空间后缀；按照 ISA，
若 generic 地址映射到 const、local 或 shared 数据，该指令不执行操作。
`prefetchu.L1` 拒绝已知参数地址。模型拒绝未支持的层级、状态空间和 eviction 组合。
未知寄存器来源仍保持未知；`Generic` 不能作为已知状态空间的通配符。
