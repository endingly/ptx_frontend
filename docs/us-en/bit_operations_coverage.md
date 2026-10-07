# Integer Bit-Operation Coverage

This document records the PTX 9.3 bit-operation contract modelled by the
frontend. The canonical source is
`python/code_gen/resources/ptx_spec/arithmetic.yaml`; syntax descriptors,
Resolved IR, and target-aware checking derive from it. This is frontend
validation, not simulator or physical-GPU execution.

The normative source is NVIDIA's archived PTX ISA 9.3: [popc
§9.7.1.15](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-popc),
[clz §9.7.1.16](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-clz),
[bfind §9.7.1.17](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfind),
[brev §9.7.1.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-brev),
[bfe §9.7.1.20](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfe),
and [bfi §9.7.1.21](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfi).

| Opcode | Modelled forms | Operand contract |
| --- | --- | --- |
| `popc` | `.b32`, `.b64` | Source is register-or-immediate at the selected bit width; the count destination is `.u32`. |
| `clz` | `.b32`, `.b64` | Source is register-or-immediate at the selected bit width; the leading-zero count destination is `.u32`. |
| `bfind` | plain and `.shiftamt` forms for `.u32/.u64/.s32/.s64` | Source is register-or-immediate at the selected signed or unsigned width; the position or shift-amount destination is `.u32`. |
| `brev` | `.b32`, `.b64` | Source is register-or-immediate and the destination uses the selected bit width. |
| `bfe` | `.u32/.u64/.s32/.s64` | The extracted source and offset/width controls are register-or-immediate; destination uses the selected type and controls are `.u32`. |
| `bfi` | `.b32`, `.b64` | Inserted source, base, offset, and width are register-or-immediate; destination uses the selected bit width and controls are `.u32`. |

Every form requires PTX 2.0 and `sm_20`. Fundamental integer and same-width
bit register declarations remain compatible with the documented operand
widths, consistent with the frontend's [register type policy](register_type_policy.md).
The bit-container source contracts of `popc`, `clz`, `brev`, and `bfi` also
admit same-width floating register storage; the signed and unsigned `bfind` and
`bfe` forms remain typed integer contracts.

For `bfe` and `bfi`, an immediate offset or width is checked inclusively in
`0..255`. Register controls remain legal because their values are dynamic; the
frontend checks their `.u32` width but cannot establish the runtime range. The
ISA pseudocode masks each control to eight bits after stating that legal values
are restricted to this range, so it does not permit out-of-range immediates.

The `bfe.b32` example printed in the archive conflicts with that section's
explicit `.u32/.u64/.s32/.s64` syntax list and is not accepted. Existing public
variant names such as `Popc::B32`, `Bfind::ShiftamtU32`, `Bfe::U32`, and
`Bfi::B32` remain available while the additional forms are generated alongside
them.

Focused C++ resolver/checker tests and the [installed consumer
test](../../submod/resolved_ir/test/package_consumer/bit_operations_completeness.cpp)
exercise the expanded public forms. They establish parsing, resolution, type,
range, and target contracts only.
