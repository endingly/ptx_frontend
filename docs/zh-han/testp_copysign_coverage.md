# `testp` 与 `copysign` 覆盖范围

这个首个浮点切片通过正常的 YAML、typed resolved IR 与 target-aware checker 路径建模
[PTX 9.3 §9.7.3.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-testp)
与 [§9.7.3.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-copysign)，不执行浮点运算。

`testp` 支持 `.f32` 与 `.f64` 的 `finite`、`infinite`、`number`、`notanumber`、
`normal` 和 `subnormal`，写入 predicate，并要求 PTX 2.0 / `sm_20`。property 使用
typed `TestProperty` domain，而不是 comparison operator。`normal` 遵循 ISA 的分类
契约；frontend 不计算操作数的值。其 source 可使用 typed floating literal 或相同宽度的
floating/bit register container；integer literal 和错误宽度 container 会被拒绝。

`copysign.f32` 与 `copysign.f64` 同样要求 PTX 2.0 / `sm_20`。第一个 source 提供
sign，第二个 source 提供 magnitude。两个 source 位置都可使用 typed floating literal
或相同宽度的 floating/bit register container；integer literal 和错误宽度的 container
会被拒绝。两条指令都不允许 destination sink；`testp` 也拒绝 negated predicate source。

PTXAS 证据使用 CUDA 13.3.73、`sm_90` 收集，命令为
`/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`：54 个 probe 中
29 个接受、25 个拒绝；另外五个 sink/negated-predicate follow-up probe 都被拒绝。probe
覆盖两个 source 位置、floating immediate、相同宽度 bit container、非法 integer
literal/width 以及 predicate destination。该 assembler 不能以 `sm_20` 为目标，因此
PTX 2.0 / `sm_20` 边界由 frontend checker test 强制，而非 assembler target 证据。

[explicit 浮点 MAD](mad_coverage.md) 已单独建模。Issue 142 后续工作会处理 DIV、
reciprocal/square-root、transcendental、MIN/MAX，以及现有 ADD/SUB 与 mixed-family
contract 的审计；这些内容刻意不属于本文档的实现范围。
