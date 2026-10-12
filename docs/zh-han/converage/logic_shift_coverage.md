# Logic 与 Shift 覆盖

生成的 [PTX 9.3 §9.7.8 logic-and-shift
contract](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#logic-and-shift-instructions)
会 resolve 并 target-check 完整的具名 logic-and-shift family：38 个 concrete
spelling 由 37 个 canonical variant 表示（`lop3` Boolean-result variant 同时覆盖
`.and` 与 `.or`）。这仅是 frontend validation；不提供 execution semantics 或
simulator support。

`and`、`or`、`xor` 与 `not` 接受规范的 `.pred`、`.b16`、`.b32`、`.b64`
type。predicate source 保留 PTX predicate constant 与 negation。`cnot` 接受
`.b16`、`.b32`、`.b64`。

`lop3.b32` 要求 truth-table immediate 位于 `0..255`。其 `.and` 与 `.or`
layout 自 PTX 8.2 / SM 70 起可用，保留 `d|p` output 与 predicate control；仅
这些 layout 可将 data output 写为 `_`。base `lop3` 仍为 PTX 4.3 / SM 50。

`shf` 在 PTX 3.1 / SM 32 支持全部官方 `.l/.r` 与 `.clamp/.wrap` combination。
其 count 始终是完整 unsigned 32-bit operand；frontend 不会错误地将它限制为
mode 的 runtime effective range。`shl` 支持 `.b16/.b32/.b64`，而 `shr` 支持
bit、unsigned、signed 的 16/32/64-bit spelling。shift count 始终是 32-bit，且
destination/source width 必须匹配。
