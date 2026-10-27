# Historical coverage baseline and evidence boundaries

The technical entry point is the [GitHub Wiki](https://github.com/endingly/ptx_frontend/wiki).
The [reference index](../index.md) preserves detailed design and instruction-family
contracts alongside source. This page preserves the historical reconciliation at
revision `2bd78d605f7fc545c11b77f42e9cd5bc7fe376a4`. The inventory counts and
six-gap table below describe that snapshot. For the current implementation
boundary, consult the detailed family references, public contracts and tests;
documentation changes do not extend implementation coverage.

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
detailed family contracts and tests, and issue merge status is separate.

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

## Validation and runtime limits

The parse/resolve/check pipeline validates modeled source forms. Strict initial
header order, target-item categories, scalar storage occurrence/layout
consistency, and function signature/declaration ABI consistency are revalidated
by both module validation overloads, including the owned path after AST release.
See the [module validation contract](../resolved_ir_design.md),
[storage metadata](../storage_declarations.md), and
[parameter metadata](../parameter_declarations.md) for their precise boundaries.
Complete source context is required by `resolveAndValidateModule`; this is not
an assertion of exhaustive PTX validation.

Caller-known descriptor, scale and sparsity facts are conditional assertions.
TCGEN05 K96 scale-layout role rows retain open semantic obligations. The
frontend does not prove live descriptor bytes, collector/protocol history,
memory allocation, cross-thread ordering, numerical execution or GPU results.
Historical simulator pins and build/ptxas measurements remain historical.

## Maintenance

Update corresponding English/Chinese references with changed source contracts
and focused tests. Keep the Wiki as a technical guide linking those references,
and keep README limited to background, build/install, use and licensing. Do not
recreate a handwritten opcode ledger or duplicate its counts as a support gate.
Code and reproducible checks decide drift; issue status and roadmap checkboxes
alone do not prove acceptance. Public APIs remain pre-1.0.
