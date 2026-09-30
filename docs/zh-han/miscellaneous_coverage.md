# PTX 9.3 杂项指令覆盖范围

前端建模了 [PTX ISA 9.3 §9.7.20](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#miscellaneous-instructions) 中的五个 opcode。解析与 owned resolution 保留操作、修饰符、类型化操作数和谓词守卫；target-aware checker 检查 PTX 与目标门槛。前端不运行调试器、不延迟线程、不统计性能事件，也不分配物理寄存器。

| 指令 | 支持形式和操作数 | PTX ISA 下限 | 目标下限 |
| --- | --- | --- | --- |
| `brkpt` | 无修饰符、无操作数，可带谓词守卫 | 1.0 | `sm_11` |
| `nanosleep` | `.u32`，纳秒时长来自 32 位寄存器或收窄至 32 位的整数立即数 | 6.3 | `sm_70` |
| `pmevent` | 立即数事件索引 `0..15` | 1.4 | 所有目标 |
| `pmevent.mask` | 16 位立即数事件掩码 | 3.0 | `sm_20` |
| `trap` | 无修饰符、无操作数，可带谓词守卫 | 1.0 | 所有目标 |
| `setmaxnreg` | `.inc.sync.aligned.u32` 和 `.dec.sync.aligned.u32`，立即数计数为 `24..256` 且能被八整除 | 8.0 | 见下文 |

目标目录从 `sm_13` 开始，因此 `sm_11` 是 `brkpt` 的规范下限，不是单独建模的目标。`pmevent` 必须使用立即数；`.mask` 选择的是掩码而不是索引。`nanosleep` 同时接受寄存器和立即数源，并保留程序员所写的源形式。其整数常量遵循通用的 [PTX 常量转换规则](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constants)：例如，`-1` 在 `.u32` 使用处收窄为 `0xffffffff`，而 `4294967296` 收窄为零。

`setmaxnreg` 在 PTX 8.0 起接受 exact `sm_90a`，在 PTX 8.6 起接受 exact `sm_100a`，在 PTX 8.7 起接受 exact `sm_120a`，在 PTX 8.8 起接受已启用的 `sm_100f` 和 `sm_120f` 家族，在 PTX 9.0 起接受已启用的 `sm_110f` 家族。模型遵循仓库的显式目标目录，不推断未知目标拼写。两个 action 修饰符对应不同的 resolved variant。`.sync`、`.aligned` 和 `.u32` 都是必需的。checker 验证字面计数，但无法证明运行时 warpgroup 参与、同步、寄存器池状态或影响寄存器调整效果的启动配置。

16 位 `pmevent.mask` 操作数接受 `0..65535`，包括零；即使没有设置任何事件位，前端仍保留该字面值。

固定 PTX 9.3 参考文档未将本节形式标为 deprecated。`nanosleep` 指定近似时长，最长为一毫秒，并允许同一 warp 中的线程调整唤醒时间；前端检查类型，不预测计时行为。

使用 CUDA 13.3 `ptxas` V13.3.73 进行了离线交叉检查：文档列出的形式、收窄后的 `nanosleep` 常量、`pmevent.mask` 的端点 `0` 和 `65535`，以及已启用的 `setmaxnreg` 目标均可汇编。非法事件索引、超出 `0..65535` 的掩码、`pmevent` 的寄存器操作数、非法寄存器计数，以及 `setmaxnreg` 的 generic `sm_90`/`sm_100`/`sm_110`/`sm_120` 均被拒绝。有一处汇编器行为与固定 PTX 9.3 参考文档不同：虽然文档记载 `nanosleep` 从 6.3 引入，`ptxas` 仍接受 PTX 6.2 模块中的该指令。前端保留文档规定的 6.3 下限。这些检查只验证汇编，不证明 GPU 运行时行为。
