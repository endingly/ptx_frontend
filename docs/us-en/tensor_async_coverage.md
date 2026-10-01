# Tiled tensor asynchronous data movement

The frontend resolves the first tiled tensor-map forms in [PTX ISA 9.3 §5.5 and §9.7.9.26.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html). The composite `[tensorMap, {coords}]` keeps its nested address, coordinate elements, punctuation, and source ranges through CST and AST lowering. Resolved IR owns a `ResolvedTensorMapRef` and a rank 1–5 `ResolvedTensorOperand`. Each of the 20 semantic variants accepts the omitted or explicit `.tile` spelling; the `tile` modifier field records whether it was written.

| Form | Operands and completion | Earliest PTX / target |
| --- | --- | --- |
| `cp.async.bulk.prefetch.tensor.{1d…5d}.L2.global{.tile}` | `[tensorMap, {coords}]`; no completion operand | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cluster.global{.tile}.mbarrier::complete_tx::bytes` | `[dst], [tensorMap, {coords}], [mbar]`; mbarrier complete-tx-bytes | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cta.global{.tile}.mbarrier::complete_tx::bytes` | Same operands, CTA shared destination | 8.6 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.global.shared::cta{.tile}.bulk_group` | `[tensorMap, {coords}], [src]`; existing bulk-group commit/wait | 8.0 / SM 90 |

`tensorMap` is a generic pointer to an opaque 128-byte descriptor. A direct descriptor symbol may reside in kernel `.param`, `.const`, or `.global` storage; its declaration identity, storage space, alignment, and source range stay in owned IR. A register pointer retains its register identity, while its runtime storage and alignment remain unknown. Tensor data direction is modeled separately: load reads global tensor data into shared memory; store reads CTA shared memory and writes global tensor data.

The checker requires exactly one signed-32 coordinate per rank and accepts compatible 32-bit registers. PTX 64-bit integer constants are converted to signed 32 bits at coordinate use, with the original source value and sign retained in owned IR: `4294967296` converts to zero, while `4294967295` converts to −1. Loads and prefetch may use negative converted coordinates. Stores reject statically negative converted coordinates; register coordinate values require runtime validation. Known descriptor, shared data, and mbarrier addresses must be aligned to 64, 16, and 8 bytes respectively. The shared-data check is the tiled mode baseline; stronger alignment required by the descriptor's swizzle mode remains a runtime obligation because the descriptor is opaque here. Unknown register-pointer alignment is not treated as proven. The checker rejects a known shared/local or non-kernel parameter descriptor and verifies PTX/SM availability.

Descriptor contents, rank consistency with descriptor bytes, swizzle, stride, bounds, barrier locality, and runtime synchronization cannot be proven from this slice. `tensormap.replace`, `tensormap.cp_fenceproxy`, tensor reduction, im2col, gather/scatter, multicast, explicit CTA group, and cache policies or hints remain outside this supported subset. Unsupported adjacent spellings fail selection.
