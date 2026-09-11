# Core Opcode Completeness Audit

## Scope and evidence boundary

This audit records the status of eleven commonly used PTX operation names:
`mov`, `add`, `sub`, `mul`, `setp`, `ld`, `st`, `bar`, `bra`, `exit`, and
`fma`.  It is an evidence record for [issue #51](https://github.com/endingly/ptx_frontend/issues/51), not a claim that a nonempty generated variant set implements PTX ISA 9.3.

The normative baseline is the archived [PTX ISA 9.3 instruction
set](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html),
including its operand, availability, state-space, memory-ordering, and
separately named instruction rules.  The database evidence below is produced
by `ptx_frontend.code_gen.database.load_codegen_database()` from
`python/code_gen/resources/ptx_spec`; it is the single input used to derive
the syntax descriptors, Resolved IR, and target-aware checker.  It is not a
second coverage registry.

The execution column is deliberately historical and narrow.  It reports the
simulator contract at
[`ptxsim@2ea9c476f362eeeee93de703e6e4191c8f47d3a6`](https://github.com/endingly/ptxsim/tree/2ea9c476f362eeeee93de703e6e4191c8f47d3a6),
which was pinned to
`ptx_frontend@fdb5ef575087b530c2cd6db6cb3631cf430a8ce0`.  That contract is
useful downstream evidence, but neither the old pin nor a simulator handler
establishes complete PTX 9.3 support.  This audit performed no hardware run;
simulator execution and NVIDIA-GPU behavior are distinct evidence classes.

For this document:

- **Syntax/model/frontend** means the generated syntax descriptor, generated
  Resolved IR layout, and checker only for the listed YAML boundary.  It does
  not mean every legal spelling in the manual is admitted.
- **Execution** means only the pinned simulator family contract.  It does not
  establish physical GPU behavior, a memory model, or a public
  variant/modifier support API.
- Carry/state operations and separately named operations remain separate
  completion units.  For example, `add.cc`/`addc`, `sub.cc`/`subc`, `brx`,
  `barrier`, and `mbarrier` must not be silently counted as ordinary `add`,
  `sub`, `bra`, or `bar` coverage.

## Reproducible frontend-model evidence

Loading the canonical `ptx-instr/v1` database yielded 69 instruction names.
The relevant entries are listed here to make the audited model boundary
reviewable without copying YAML into a hand-maintained ledger.

| Opcode | YAML variants | Variant/layout boundary loaded from the canonical database |
| --- | ---: | --- |
| `mov` | 3 | scalar (scalar/pack/unpack layouts), `v4.u32`, predicate |
| `add` | 9 | f32/f32x2/f64, half, bfloat, mixed f32, integer, saturating, packed saturating |
| `sub` | 8 | f32/f32x2/f64, half, bfloat, mixed f32, integer, optional saturating |
| `mul` | 5 | `rn.f32`, `lo.u32`, `hi.u32`, `wide.u32`, `wide.s32` |
| `setp` | 5 | `lt.u32`, `ge.s32`, `lt.and.u32`, `eq.u32` pair, `lt.and.s32` pair |
| `ld` | 7 | generic/explicit scalar and vector, two global cache-hint forms, and global noncoherent L1 no-allocate |
| `st` | 6 | generic/explicit scalar and vector plus two global cache-hint forms |
| `bar` | 11 | CTA sync/arrive/reduction spellings and warp sync layouts |
| `bra` | 1 | direct branch with optional `.uni` |
| `exit` | 1 | bare exit |
| `fma` | 16 | the complete frontend FMA contract described in [FMA coverage](fma_coverage.md) |

The generator model explains why these entries affect all three frontend
stages: [syntax descriptors](python_generator_model.md#syntax-model) admit a
variant/layout, while the same normalized model emits the
[Resolved-IR bindings](python_generator_model.md#resolved-model) used by
resolution and checking.  Acceptance remains bounded by the individual
variant's operands, modifiers, availability, and negative diagnostics.

### Reproduction command

This command uses the installed editable package in the repository virtual
environment; it is the command used for the counts above, rather than an
invented registry.

```sh
.venv/bin/python - <<'PY'
from pathlib import Path
from ptx_frontend.code_gen.database import load_codegen_database

wanted = {"mov", "add", "sub", "mul", "setp", "ld", "st", "bar", "bra", "exit", "fma"}
database = load_codegen_database(spec_dir=Path("python/code_gen/resources/ptx_spec"))
for instruction in database.instructions:
    if instruction.opcode in wanted:
        print(instruction.opcode, len(instruction.variants),
              [variant.name for variant in instruction.variants])
PY
```

## PTX 9.3-to-model delta

The following is the actual normative comparison, not a restatement of the
variant counts.  “Covered” means the named type/modifier/layout family is in
the generated frontend boundary; it does not mean simulator or hardware
execution.

| Opcode and primary PTX 9.3 sections | Covered frontend family | Concrete delta and conclusion | Pinned execution boundary |
| --- | --- | --- | --- |
| `mov`: [ordinary move](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov) and [pack/unpack](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov-2) | `.pred`, scalar `.b16/.b32/.b64`, `.u16/.u32/.u64`, `.s16/.s32/.s64`, `.f32/.f64`, register/immediate/address/function/supported-special-register sources, plus 2/4-element bit pack/unpack are modeled.  `mov.v4.u32` is modeled only for the four-element cluster special registers documented in [§10](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#special-registers-clusterid), not as a general `.v4` modifier of §9.7.9.3/§9.7.9.4. | The manual permits `.b128` only for pack/unpack, but the common scalar variant also admits `.b128`; this is an over-admission to correct.  Legal negated `mov.pred` sources are rejected before resolution; [#126](https://github.com/endingly/ptx_frontend/issues/126) owns it.  Therefore MOV is not frontend-complete. | Historical scalar execution is only `b32`/`u32`/`b64` with narrow special-register reads.  [ptxsim#21](https://github.com/endingly/ptxsim/issues/21) owns whole-op execution. |
| `add`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-add), [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-add), [half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-add), [mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-add) | All regular-manual families are modeled: integer scalar/packed and their legal `.sat` sets; f32/f32x2/f64 rounding and `.ftz`/`.sat` rules; f16/f16x2/bf16/bf16x2; and mixed `.f32.{f16,bf16}`.  Availability is represented per form. | **Frontend conclusion: complete for the four ordinary `add` sections.**  [`add.cc`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions-add-cc) and [`addc`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions-addc) are separately named, stateful operations and are not included. | All nine projected regular forms/23 type paths execute at the pin; carry state is expressly excluded. |
| `sub`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-sub), [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sub), [half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-sub), [mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-sub) | All regular-manual integer, f32/f32x2/f64, f16/f16x2/bf16/bf16x2, and mixed `.f32.{f16,bf16}` families, with their legal rounding/FTZ/saturation boundaries, are modeled. | **Frontend conclusion: complete for the four ordinary `sub` sections.**  [`sub.cc`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions-sub-cc) and [`subc`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions-subc) are separate condition-code operations. | All eight projected regular forms/17 type paths execute at the pin; borrow state is excluded. |
| `mul`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-mul), [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mul), [half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-mul) | Only `lo.u32`, `hi.u32`, `wide.u32`, `wide.s32`, and register-only `rn.f32` are modeled. | Missing `.hi/.lo` forms for `u16/u64/s16/s32/s64`, and `.wide.u16/.wide.s16`; `.wide.u64/.wide.s64` are not legal PTX forms and are intentionally not requested.  The f32 default-rounding spelling `mul.f32` is also missing, as are directed rounding, `.ftz`, `.sat`, f32x2, f64, and all f16/f16x2/bf16/bf16x2 forms with their legal `.rn/.ftz/.sat`.  **Not frontend-complete.** | Exactly the same five forms execute at the pin. |
| `setp`: [ordinary](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) and [half](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp) | Five examples are modeled: `lt.u32`, `ge.s32`, `lt.and.u32`, `eq.u32` pair, and `lt.and.s32` pair. | Missing the ordinary `.b16/.b32/.b64`, other integer, f32/f64 types; the full `eq/ne/lt/le/gt/ge/lo/ls/hi/hs/equ/neu/ltu/leu/gtu/geu/num/nan` comparison set; optional `.and/.or/.xor`, negated combine predicates, single/pair sink destinations, and f32 `.ftz`; every f16/f16x2/bf16/bf16x2 family is also absent.  **Not frontend-complete.** | Exactly the five modeled forms execute at the pin. |
| `ld`: [ld](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld) and [ld.global.nc](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld-global-nc) | Scalar plus v2/v4 and constrained v8/v4-64 vector layouts cover `.b8/.b16/.b32/.b64`, signed/unsigned 8–64, f32/f64; generic and `.const/.global/.local/.param{::entry,::func}/.shared` forms; selected weak/volatile/relaxed/acquire, scopes, cache, alignment, and two cache-hint variants. | Missing `.b128`; `.shared::cta/.shared::cluster`; the full legal L1/L2 eviction, prefetch-size, cache-policy, `.unified`, and `ld.global.nc` type/cache/vector matrix; and the full legality cross-product for MMIO/consistency/scope/cache.  **Not frontend-complete.** | Ordinary transfers only; no complete ordering, MMIO, cache/noncoherent, or function-parameter resources. |
| `st`: [st](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st) | It mirrors the modeled `ld` scalar/vector type subset, generic/explicit spaces, selected semantics/scope/cache, alignment, and two cache-hint variants. | Missing `.b128`, `.shared::cta/.shared::cluster`, legal L1/L2 eviction and cache-policy cross-products, `.unified`, and the full volatile/relaxed/release/MMIO/scope legality matrix.  **Not frontend-complete.** | Ordinary transfers only; [ptxsim#24](https://github.com/endingly/ptxsim/issues/24) owns whole `ld`/`st` execution. |
| `bar`: [CTA `bar`/`barrier`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar) and [warp sync](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar-warp-sync) | `bar{.cta}.sync`, `arrive`, `red.popc.u32`, `red.{and,or}.pred`, their immediate/register and required/omitted-count layouts, negated reduction predicate, and `bar.warp.sync` are modeled with their availability. | **Frontend conclusion: complete for `bar` and `bar.warp.sync`.**  `barrier{.cta}` (whose `.aligned` semantics are distinct), `barrier.cluster`, asynchronous barriers, and `mbarrier` are separately named operations and are excluded. | The corresponding pinned warp/CTA/reduction forms execute, but this is not hardware proof. |
| `bra`: [direct branch](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-bra) | Label target, optional `.uni`, and the ordinary/negated predicate guard are modeled and checked. | **Frontend conclusion: complete for direct `bra`.**  `brx.idx`, calls, and returns are separately named control-flow operations. | The pin updates the authoritative PC for its direct branch family. |
| `exit`: [exit](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-exit) | The sole bare form and ordinary predicate guard are modeled. | **Frontend conclusion: complete for `exit`.**  `ret` and `trap` are separate opcodes. | The pin handles conditional exit and barrier release; hardware behavior was not tested. |
| `fma`: [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-fma), [half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-fma), [mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-fma) | All 16 FMA variants, operand contracts, modifiers, and target availability are documented and independently tested in [FMA coverage](fma_coverage.md). | **Frontend conclusion: complete for the documented FMA family.**  This is reused evidence, not an inference from a variant count. | No FMA execution semantics at the pin.  [ptxsim#34](https://github.com/endingly/ptxsim/issues/34) is only a narrow `.oob` hardware observation. |

## Completion and follow-up policy

The existing downstream trackers are [ptxsim#21](https://github.com/endingly/ptxsim/issues/21)
for whole-op `mov`, [ptxsim#24](https://github.com/endingly/ptxsim/issues/24)
for `ld`/`st` execution, and [ptxsim#23](https://github.com/endingly/ptxsim/issues/23)
for enforcement of whole-op execution claims.  Frontend [#126](https://github.com/endingly/ptx_frontend/issues/126)
is the already-open, concrete `mov.pred` syntax/layout blocker.

No new external issue is recorded by this audit.  Issue #51 requires every
uncovered frontend family to have dedicated remediation tracking; that
acceptance remains pending until the following whole-operation follow-ups are
created and linked, rather than split into variant tickets:

1. carry/condition-code `add.cc`/`addc` and `sub.cc`/`subc`, with an owned
   implicit-state contract;
2. correct MOV's `.b128` scalar-layout over-admission; [#126](https://github.com/endingly/ptx_frontend/issues/126)
   remains the separate already-filed negated-predicate defect;
3. complete PTX 9.3 `mul` modeling and checking beyond the current five forms;
4. complete PTX 9.3 `setp` modeling and checking beyond the current five forms;
5. separate form-by-form frontend completeness work for `ld` and `st`, linked
   to ptxsim#24 for the required runtime semantics; and
6. a whole-FMA simulator execution contract if execution, rather than the
   already documented frontend model, is required.

Each such follow-up must use the canonical YAML/database path, preserve
negative diagnostics, and revalidate the downstream projection after an
accepted frontend form changes.  It must not restore a retired handwritten
opcode coverage registry or introduce a simulator runtime variant-support API.
