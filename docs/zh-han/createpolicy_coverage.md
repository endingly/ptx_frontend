# PTX 9.3 `createpolicy` 覆盖范围

前端建模 PTX ISA 9.3 §9.7.9.19 中已文档化的 fractional、range 与
access-property 转换形式。所有形式都要求 PTX 7.4 和 SM 80，并将不透明的
`.b64` 缓存策略写入 64 位寄存器。

| 形式 | 操作数与源码控制项 |
| --- | --- |
| `createpolicy.fractional.L2::primary{.L2::secondary}.b64` | 目标寄存器和可选 `.f32` fraction；省略 fraction 时默认 `1.0` |
| `createpolicy.range{.global}.L2::primary{.L2::secondary}.b64` | 目标寄存器、global 地址、32 位 primary size 和 32 位 total size |
| `createpolicy.cvt.L2.b64` | 目标寄存器和 64 位 access-property 寄存器 |

primary priority 可为 `evict_last`、`evict_normal`、`evict_first` 或
`evict_unchanged`。显式 secondary priority 可为 `evict_first` 或
`evict_unchanged`；省略时默认为 `evict_unchanged`。resolved 形式保留
secondary 修饰符与 fraction 操作数是否出现在源码中。

立即数 fraction 必须是有限值，且位于 `(0.0, 1.0]`。前端接受动态 `.f32`
寄存器 fraction，但无法证明其运行时值。若两个 range size 都是立即数，
primary size 不得超过 total size；动态 size 保留此运行时前提。省略
`.global` 的 range 形式使用 generic 寻址，但运行时地址必须属于 global。
前端拒绝已知的非 global 符号，同时接受来源未知的地址寄存器。
每个 size 的源码整数常量必须可由 32 位操作数表示；前端拒绝
`4294967296` 和 `4294967297`，不采用 `ptxas` 13.3.73 所表现出的
模 32 位截断。这项源码契约不改变 ISA 所宣称的 total range 4 GB 语义上限。
可表示的负数拼写（如 `-1`）在无符号 size 比较中保留其 32 位位模式
（`0xffffffff`）；绝对值过大的负数会被拒绝。
前端检查策略表示与操作数，不执行缓存操作，也不证明缓存行为。
