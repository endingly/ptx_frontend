# 整数位操作覆盖情况

本文记录 frontend 建模的 PTX 9.3 位操作 contract。唯一 canonical source 是
`python/code_gen/resources/ptx_spec/arithmetic.yaml`；syntax descriptor、Resolved IR 与
target-aware checker 均由它生成。这只定义 frontend validation，不表示 simulator 或
physical GPU execution。

规范依据是 NVIDIA archived PTX ISA 9.3：[popc
§9.7.1.15](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-popc)、
[clz §9.7.1.16](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-clz)、
[bfind §9.7.1.17](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfind)、
[brev §9.7.1.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-brev)、
[bfe §9.7.1.20](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfe)
及 [bfi §9.7.1.21](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfi)。

| Opcode | 已建模 form | Operand contract |
| --- | --- | --- |
| `popc` | `.b32`、`.b64` | source 是所选 bit width 的 register-or-immediate；count destination 为 `.u32`。 |
| `clz` | `.b32`、`.b64` | source 是所选 bit width 的 register-or-immediate；leading-zero count destination 为 `.u32`。 |
| `bfind` | `.u32/.u64/.s32/.s64` 的 plain 和 `.shiftamt` form | source 是所选 signed 或 unsigned width 的 register-or-immediate；position 或 shift-amount destination 为 `.u32`。 |
| `brev` | `.b32`、`.b64` | source 是 register-or-immediate，destination 使用所选 bit width。 |
| `bfe` | `.u32/.u64/.s32/.s64` | extracted source 与 offset/width control 都是 register-or-immediate；destination 使用所选 type，control 为 `.u32`。 |
| `bfi` | `.b32`、`.b64` | inserted source、base、offset 与 width 都是 register-or-immediate；destination 使用所选 bit width，control 为 `.u32`。 |

所有 form 都要求 PTX 2.0 和 `sm_20`。fundamental integer 与同宽 bit register declaration
保持与文档 operand width 兼容，规则见 [register type policy](register_type_policy.md)。
`popc`、`clz`、`brev` 与 `bfi` 的 bit-container source contract 也接受同宽 floating
register storage；signed/unsigned `bfind` 与 `bfe` 仍是 typed integer contract。

对 `bfe` 与 `bfi`，immediate offset/width 会被 inclusive 地检查为 `0..255`。register control
仍合法，因为其值在 runtime 才确定；frontend 检查其 `.u32` width，却不能证明 runtime range。
ISA pseudocode 在说明合法值受此范围限制后才将 control mask 到八位，因此不能借此接受
out-of-range immediate。

archive 中 `bfe.b32` example 与该节明确列出的 `.u32/.u64/.s32/.s64` syntax 冲突，故不接受。
既有 public variant name，如 `Popc::B32`、`Bfind::ShiftamtU32`、`Bfe::U32` 和 `Bfi::B32`，
仍保留；新增 form 在其旁生成。

focused C++ resolver/checker test 与 [installed consumer
test](../../submod/resolved_ir/test/package_consumer/bit_operations_completeness.cpp)
覆盖扩展后的 public form。它们只验证 parsing、resolution、type、range 与 target contract。
