# PTX 9.3 `applypriority` 与 `discard` 覆盖范围

前端支持 PTX ISA 9.3 §§9.7.9.17–18 中这两种 L2 缓存范围操作的两种文档化
拼写。两者都要求 PTX 7.4、SM 80、128 字节对齐的地址，以及值为 `128`
的立即数大小。

| 操作 | generic 拼写 | 显式 global 拼写 |
| --- | --- | --- |
| 普通逐出优先级 | `applypriority.L2::evict_normal [a], 128` | `applypriority.global.L2::evict_normal [a], 128` |
| 丢弃范围 | `discard.L2 [a], 128` | `discard.global.L2 [a], 128` |

省略 `.global` 后缀时使用 generic 寻址。运行时地址必须位于 global 窗口，
否则 PTX 将其行为定义为未定义。前端接受来源未知的地址寄存器，并拒绝来源
已知且非 global 的符号。静态已知的地址在两种拼写中都必须可证明具有
128 字节对齐。前端检查这些缓存指令的语法和类型化操作数，不执行指令。
