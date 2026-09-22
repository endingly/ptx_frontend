# 浮点 `mad` 覆盖

此切片通过 canonical YAML、typed Resolved IR 与 target-aware checker 扩展既有的 `Mad::RnF32` seed。它覆盖 [PTX 9.3 §9.7.3.7](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mad) 的 explicit-rounding form，保留 integer 与 carry `mad` variant 和 source opcode，也不添加算术 execution。

| Form | Minimum | Contract |
| --- | --- | --- |
| `mad.{rn,rz,rm,rp}{.ftz}{.sat}.f32` | PTX 2.0 / SM 20 | 必须指定 rounding。`.ftz` 和 `.sat` 独立 optional。每个 source 可为 typed floating literal 或同宽 `.f32`/`.b32` register；destination 为同宽 `.f32`/`.b32` register。 |
| `mad.{rn,rz,rm,rp}.f64` | PTX 1.0 / SM 13 | 必须指定 rounding。没有 `.ftz` 或 `.sat`。每个 source 可为 typed floating literal 或同宽 `.f64`/`.b64` register；destination 为同宽 `.f64`/`.b64` register。 |

integer literal 或 integer register container、错误宽度、destination sink、遗漏 rounding、不支持的 FP64 flag 及错误 operand count 都会被拒绝。文档规定 PTX 1.4 起 `mad.f64` 必须有 rounding；这不是 explicit rounded FP64 form 的 introduction floor。CUDA 13.3.73 `ptxas` 在 `sm_90` assembler override 下接受 PTX 1.3 的 explicit `.rn` 与 `.rz` FP64 form，符合 PTX 1.0 model floor。该历史 probe 只证明 syntax/contract，不表示旧 GPU codegen。

`sm_1x` 的 omitted-rounding `mad{.ftz}{.sat}.f32` syntax、legacy omitted `mad.f64` 及 PTX 3.0/3.1 errata/default behavior 刻意不在此切片中。它们需要 explicit contemporary form 不需要的 target-profile semantics；checker 会拒绝 omitted rounding，而不会把它作为 universal default。

CUDA 13.3.73 `ptxas` 证据使用 `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`。32 个 modern matrix module 接受 FP32/FP64 的四个 rounding form、floating immediate 与匹配 bit container；拒绝 14 个非法 flag、type、shape 与 sink case。[explicit 浮点 DIV](div_coverage.md) 已单独建模。reciprocal 与 square-root family、transcendental、MIN/MAX，以及既有 ADD/SUB 审计仍不属于本文档范围。
