# Matrix 参考解释

本文记录 frontend 对固定版本 [PTX ISA 9.3 参考文档](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
中局部矛盾的有界解释。这些是项目工程解释，并非 NVIDIA 发布的勘误。每项解释只覆盖
自己的范围，不代表整个 matrix 指令族覆盖完成或硬件执行符合性。
[English](../../us-en/converage/matrix_reference_interpretations.md)

## WMMA `m8n8k4` F64 fragments

此 shape 的 `wmma.load`、`wmma.mma`、`wmma.store` 中，每个线程的 A、B 各使用
一个 64 位寄存器，C、D 各使用两个 64 位寄存器。
[fragment 表格](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-fragment)
中 F64 C/D 的单寄存器条目与其他条款矛盾。本项目采用两个寄存器：
[shape 定义](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)
使 C/D 成为含 64 个 F64 元素的 8×8 矩阵，在 32 个线程中每线程需要两个元素。
A 为 8×4，B 为 4×8，每线程各需一个元素。
[store 示例](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-st)
与 [MMA 示例](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-mma)
均使用两个 C/D 寄存器，与容量推导一致。此解释只解决这一局部矛盾，不建立“示例普遍
优先于表格”的规则。

```ptx
wmma.load.c.sync.aligned.m8n8k4.col.global.f64 {c0,c1}, [p], 8;
wmma.mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {d0,d1}, {a}, {b}, {c0,c1};
wmma.store.d.sync.aligned.m8n8k4.row.global.f64 [p], {d0,d1}, 8;
```

C/D fragment 占 16 字节，内存地址要求 16 字节对齐。Leading dimension 仍是依赖
layout 的元素步长，最小值与整除条件独立检查。row/col 布局、现有内存空间、默认
`.rn` 与显式 `.rn/.rz/.rm/.rp` 舍入方式均保留。基础门槛为 PTX 7.0 / `sm_80`，
`shared::cta` 保留独立的 PTX 7.8 门槛。普通标量 F64 与兼容 B64 data carrier
沿用现有规则。元素在 lane/register 间的物理分布仍依赖具体架构。

Canonical 规则已表达此解释。
[`test_wmma_coverage.cpp`](../../../submod/resolved_ir/test/test_wmma_coverage.cpp)
与 [`test_wmma_coverage.py`](../../../python/tests/spec/test_wmma_coverage.py) 中的定向
回归分别检查精确的 36 个静态 identity 与实际 source 程序，拒绝 A/B 双寄存器及
C/D 单/三寄存器，在 syntax owner 销毁后独立修改 owned fragment 并检查。
Parser 接受 brace pack 本身不代表语义接受。Owned copy/clone、外部 source 验证、
对齐、标量宽度、target 和 stride 保持为独立检查。
