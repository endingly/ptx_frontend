# Historical coverage baseline and evidence boundaries

This is a **historical evidence record**, not a current SDK support authority.
The inventory/model tables and missing-form statements below describe their
recorded revisions (including `2bd78d6`), and the simulator column retains its
separate pinned execution scope. Current accepted forms, restrictions and
limitations are maintained only in the [SDK support contract](../../sdk_support.md).

The technical entry point is the [GitHub Wiki](https://github.com/endingly/ptx_frontend/wiki).
The [reference index](../index.md) preserves detailed design and instruction-family
contracts alongside source. This page preserves the historical reconciliation at
revision `2bd78d605f7fc545c11b77f42e9cd5bc7fe376a4`. The inventory counts and
six-gap table below describe that snapshot. For current implementation scope, use the [SDK support contract](../../sdk_support.md).

## Historical ISA inventory

The fixed normative baseline is [CUDA 13.3 / PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
The source-model audit at that revision, covering all 14 instruction YAML files,
found 137 modeled opcode roots and 5,275 variants: all 136 official reserved
roots appear, plus `fabric`.
These counts establish inventory presence only. They do not prove every legal
modifier, operand layout, provenance case, availability profile, or runtime
behavior.

At that revision, models extended beyond the earlier seed slices: scalar arithmetic,
conversion, memory and synchronization, warp matrix/WMMA and WGMMA, TMA,
Tensor Memory/TCGEN05 dense/sparse/WS MMA, multimem, Fabric/CFT, texture,
surface, stack and video had explicit source contracts. Consult their family
references and tests for the exact accepted boundary.

## Source-model gaps recorded at the baseline

[Issue #280](https://github.com/endingly/ptx_frontend/issues/280) tracks the groups
recorded below against the fixed manual. This historical table records legal ISA
cases missing from the pinned snapshot; current support is defined by the
[SDK support contract](../../sdk_support.md), and issue merge status is separate.

| Baseline gap | Boundary at the pinned revision | Tracking issue |
| --- | --- | --- |
| LD/LDNC/ST cache hint | At the baseline, `L2::cache_hint` requires a final policy operand; the ISA permits omission. | [#281](https://github.com/endingly/ptx_frontend/issues/281) |
| Multimem scope with default semantics | `ld_reduce`/`st` with explicit scope but omitted semantics (default weak) is missing. | [#282](https://github.com/endingly/ptx_frontend/issues/282) |
| Legacy MAD | Implicit-rounding `mad.f32` on `sm_1x` is missing. | [#284](https://github.com/endingly/ptx_frontend/issues/284) |
| Legacy vote | Unsynchronized `vote` is missing; synchronized forms have a separate contract. | [#285](https://github.com/endingly/ptx_frontend/issues/285) |
| Legacy shuffle | Unsynchronized `shfl` is missing; synchronized forms have a separate contract. | [#286](https://github.com/endingly/ptx_frontend/issues/286) |
| Legacy mapped FP64 availability | Only `div.rn.f64`, `rcp.rn.f64`, `sqrt.rn.f64` on SM10/11/12 with `map_f64_to_f32` are the confirmed missing alternative. | [#287](https://github.com/endingly/ptx_frontend/issues/287) |

Assembler observations at older PTX versions do not override PTX 9.3 syntax.
Optional cp.async bounds, WGMMA shape questions and WMMA f64 ambiguities are
not promoted to confirmed missing-operation claims by this audit.

## Current validation and runtime boundary

See the [SDK support contract](../../sdk_support.md#validation-and-context).
Historical inventory presence is separate from current source-form acceptance.

## Maintenance

Keep this pinned evidence record historical. Current support claims, syntax and
operation restrictions, and retained limitations belong to the
[SDK support contract](../../sdk_support.md).
