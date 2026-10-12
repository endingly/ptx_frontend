# PTX 9.3 非 bulk `cp.async` 覆盖范围

前端建模 PTX ISA 9.3 §9.7.9.26.3 及 §9.7.14.16.18 中的旧式 mbarrier
桥接。复制完成仍由程序员保证；前端不执行异步复制，也不证明线程调度。

| 形式 | 约束 | 可用性 |
| --- | --- | --- |
| `cp.async.ca.shared{::cta}.global` | `cp-size` 为 4、8 或 16 字节 | PTX 7.0 / SM 80；`::cta` 为 PTX 7.8 |
| `cp.async.cg.shared{::cta}.global` | `cp-size` 固定为 16 字节 | PTX 7.0 / SM 80；`::cta` 为 PTX 7.8 |
| 任一复制形式加 `src-size` | `.u32`、`.s32` 或 `.b32` 寄存器，或无符号立即数；已知立即数须小于 `cp-size`；目标剩余字节补零 | 基础复制形式的可用性 |
| 任一复制形式加 `ignore-src` | 谓词寄存器控制全部补零 | PTX 7.5 / SM 80 |
| 任一复制形式加 `.L2::64B/128B/256B` | 有类型的预取大小提示 | PTX 7.4 / SM 80 |
| 任一复制形式加 `.L2::cache_hint` | 可附带 `.b64`、`.u64` 或 `.s64` 缓存策略寄存器；出现策略操作数时必须带该限定符 | PTX 7.4 / SM 80 |
| `cp.async.commit_group`、`cp.async.wait_group`、`cp.async.wait_all` | 已有的逐线程分组完成语法 | PTX 7.0 / SM 80 |
| `cp.async.mbarrier.arrive{.noinc}{.shared{::cta}}.b64` | 已有的 CTA shared mbarrier 地址及 pending count 形式 | PTX 7.0 / SM 80；`::cta` 为 PTX 7.8 |

目标地址和源地址在来源已知时分别须为 shared 和 global，且均按
`cp-size` 对齐。寄存器 `src-size` 保留运行时小于 `cp-size` 的义务。第四个
复制操作数以有类型的源大小、忽略源谓词或缓存策略保留其含义；缓存策略也可
作为源控制之后的第五个操作数。原有三操作数 `.ca.shared.global` 公共变体的
`dst`、`src`、`cp_size` 字段保持可用。

没有声明信息时，单条指令解析将不带缓存提示的第四个普通寄存器视为源大小、第四个
谓词视为 `ignore-src`，并将 `.L2::cache_hint` 后的第五个寄存器视为缓存策略；
声明类型在绑定前保持未知。带 `.L2::cache_hint` 的第四个普通寄存器可能是
32 位源大小，也可能是 64 位策略，因此必须提供声明。自包含模块验证会把缓存的
控制操作数类型及选定角色与绑定的寄存器声明核对。

`.ca`、`.cg` 缓存模式及 L2 限定符是源码控制和性能提示。前端检查形式与
可用性，不验证缓存效果。`cp.async.bulk` 及其完成模型另行覆盖。

PTX 9.3 语法允许 `.L2::cache_hint` 不带 `cache_policy` 操作数；仅在提供策略
操作数时要求该限定符。但 `ptxas` 13.3.73 对
`cp.async.ca.shared.global.L2::cache_hint [dst], [src], 4;` 报出
`Arguments mismatch for instruction 'cp.async'`。加上 b64 策略操作数后，
同一版本的汇编器接受该形式。前端遵循已发布的语法；使用此版本汇编器时，
仅带提示的代码可能需要补充策略操作数。
