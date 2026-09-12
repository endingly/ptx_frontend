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

Loading the canonical `ptx-instr/v1` database yields 71 instruction names.
The relevant entries are listed here to make the audited model boundary
reviewable without copying YAML into a hand-maintained ledger.

| Opcode | YAML variants | Variant/layout boundary loaded from the canonical database |
| --- | ---: | --- |
| `mov` | 4 | scalar with `.b16/.b32/.b64` pack/unpack, `.b128` pack/unpack, `v4.u32`, predicate |
| `add` | 11 | ordinary forms plus [carry-out](carry_coverage.md) |
| `addc` | 4 | [carry-in and optional carry-out](carry_coverage.md) |
| `sub` | 10 | ordinary forms plus [borrow-out](carry_coverage.md) |
| `subc` | 4 | [borrow-in and optional borrow-out](carry_coverage.md) |
| `mul` | 23 | [MUL coverage](mul_coverage.md) |
| `setp` | 18 | [SETP coverage](setp_coverage.md) |
| `ld` | 40 | [LD and noncoherent-load coverage](ld_coverage.md) |
| `st` | 24 | [ST coverage](st_coverage.md) |
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

wanted = {"mov", "add", "addc", "sub", "subc", "mul", "setp", "ld", "st", "bar", "bra", "exit", "fma"}
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
| `mov`: [ordinary move](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov) and [pack/unpack](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov-2) | `.pred`, scalar `.b16/.b32/.b64`, `.u16/.u32/.u64`, `.s16/.s32/.s64`, `.f32/.f64`, register/immediate/address/function/supported-special-register sources, and 2/4-element bit pack/unpack are modeled. `Mov::Scalar` retains scalar plus `.b16/.b32/.b64` pack/unpack layouts; fixed-type `Mov::B128PackUnpack` alone admits `.b128`. `mov.pred` accepts plain or negated predicate registers, integer predicate constants normalized to Boolean values, and plain or negated predicate special registers; the destination must remain an unnegated predicate register. `mov.v4.u32` is modeled only for the four-element cluster special registers documented in [§10](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#special-registers-clusterid), not as a general `.v4` modifier of §9.7.9.3/§9.7.9.4. | **Frontend conclusion: the modeled ordinary move and pack/unpack forms retain the PTX type and negation boundaries.** The fixed `.b128` variant prevents scalar `.b128` while retaining the established public contracts for narrower pack/unpack layouts. | Historical scalar execution is only `b32`/`u32`/`b64` with narrow special-register reads.  [ptxsim#21](https://github.com/endingly/ptxsim/issues/21) owns whole-op execution. |
| `add`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-add), [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-add), [half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-add), [mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-add) | All regular-manual families are modeled: integer scalar/packed and their legal `.sat` sets; f32/f32x2/f64 rounding and `.ftz`/`.sat` rules; f16/f16x2/bf16/bf16x2; and mixed `.f32.{f16,bf16}`.  Availability is represented per form. | The ordinary arithmetic sections are modeled. The separate extended-precision add/`addc` contract is now documented in [carry/borrow coverage](carry_coverage.md), with typed implicit-state effects and its own availability tests. | All nine projected regular forms/23 type paths execute at the pin; carry state is expressly excluded. |
| `sub`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-sub), [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sub), [half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-sub), [mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-sub) | All regular-manual integer, f32/f32x2/f64, f16/f16x2/bf16/bf16x2, and mixed `.f32.{f16,bf16}` families, with their legal rounding/FTZ/saturation boundaries, are modeled. | The ordinary arithmetic sections are modeled. The separate extended-precision sub/`subc` contract is now documented in [carry/borrow coverage](carry_coverage.md), with typed implicit-state effects and its own availability tests. | All eight projected regular forms/17 type paths execute at the pin; borrow state is excluded. |
| `mul`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-mul), [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mul), [half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-mul) | The current canonical schema models the integer and floating families described in [MUL coverage](mul_coverage.md). | See [MUL coverage](mul_coverage.md) for modifier, operand, availability, and downstream validation boundaries. | Only the original five forms execute at the historical pin. |
| `setp`: [ordinary](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) and [half](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp) | The current canonical schema models the integer and floating families described in [SETP coverage](setp_coverage.md). | See [SETP coverage](setp_coverage.md) for modifier, operand, availability, and downstream validation boundaries. | Only the original five forms execute at the historical pin. |
| `ld`: [ld](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld) and [ld.global.nc](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld-global-nc) | Canonical families and their target/operand boundaries are described in [LD coverage](ld_coverage.md). | The generated model covers the documented modifier combinations, address provenance, and public-IR revalidation; runtime semantics remain separate. | Ordinary transfers only; no complete ordering, MMIO, cache/noncoherent, or function-parameter resources. |
| `st`: [st](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st) | Canonical families and their target/operand boundaries are described in [ST coverage](st_coverage.md). | The generated model covers the documented modifier combinations, address provenance, and public-IR revalidation; runtime semantics remain separate. | Ordinary transfers only; [ptxsim#24](https://github.com/endingly/ptxsim/issues/24) owns whole `ld`/`st` execution. |
| `bar`: [CTA `bar`/`barrier`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar) and [warp sync](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar-warp-sync) | `bar{.cta}.sync`, `arrive`, `red.popc.u32`, `red.{and,or}.pred`, their immediate/register and required/omitted-count layouts, negated reduction predicate, and `bar.warp.sync` are modeled with their availability. | **Frontend conclusion: complete for `bar` and `bar.warp.sync`.**  `barrier{.cta}` (whose `.aligned` semantics are distinct), `barrier.cluster`, asynchronous barriers, and `mbarrier` are separately named operations and are excluded. | The corresponding pinned warp/CTA/reduction forms execute, but this is not hardware proof. |
| `bra`: [direct branch](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-bra) | Label target, optional `.uni`, and the ordinary/negated predicate guard are modeled and checked. | **Frontend conclusion: complete for direct `bra`.**  `brx.idx`, calls, and returns are separately named control-flow operations. | The pin updates the authoritative PC for its direct branch family. |
| `exit`: [exit](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-exit) | The sole bare form and ordinary predicate guard are modeled. | **Frontend conclusion: complete for `exit`.**  `ret` and `trap` are separate opcodes. | The pin handles conditional exit and barrier release; hardware behavior was not tested. |
| `fma`: [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-fma), [half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-fma), [mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-fma) | All 16 FMA variants, operand contracts, modifiers, and target availability are documented and independently tested in [FMA coverage](fma_coverage.md). | **Frontend conclusion: complete for the documented FMA family.**  This is reused evidence, not an inference from a variant count. | No FMA execution semantics at the pin.  [ptxsim#34](https://github.com/endingly/ptxsim/issues/34) is only a narrow `.oob` hardware observation. |

## Completion and follow-up policy

The existing downstream trackers are [ptxsim#21](https://github.com/endingly/ptxsim/issues/21)
for whole-op `mov`, [ptxsim#24](https://github.com/endingly/ptxsim/issues/24)
for `ld`/`st` execution, and [ptxsim#23](https://github.com/endingly/ptxsim/issues/23)
for enforcement of whole-op execution claims. Frontend
[#126](https://github.com/endingly/ptx_frontend/issues/126) records the
historical `mov.pred` syntax/layout gap; the current canonical schema admits
negated predicate sources without admitting negated destinations.

Issue #51's whole-operation frontend follow-ups are now tracked without
splitting them into variant tickets:

1. [#132](https://github.com/endingly/ptx_frontend/issues/132) owns the
   carry/condition-code `add.cc`/`addc` and `sub.cc`/`subc` implicit-state
   contract;
2. [#127](https://github.com/endingly/ptx_frontend/issues/127) records MOV's
   former `.b128` scalar-layout over-admission. Together with
   [#126](https://github.com/endingly/ptx_frontend/issues/126), it is addressed
   by the current scalar/pack-unpack variant split;
3. [#128](https://github.com/endingly/ptx_frontend/issues/128) completes PTX
   9.3 `mul` modeling and checking beyond the original five forms;
4. [#129](https://github.com/endingly/ptx_frontend/issues/129) completes PTX
   9.3 `setp` modeling and checking beyond the original five forms; and
5. [#130](https://github.com/endingly/ptx_frontend/issues/130) and
   [#131](https://github.com/endingly/ptx_frontend/issues/131) separately own
   form-by-form `ld` and `st` frontend completeness, linked to ptxsim#24 for
   the required runtime semantics.

The existing frontend [#53](https://github.com/endingly/ptx_frontend/issues/53)
and simulator [ptxsim#25](https://github.com/endingly/ptxsim/issues/25) are
the whole-FMA trackers; their closed state is de-duplication evidence only, not
a new execution verification.  Each frontend follow-up must use the canonical
YAML/database path, preserve negative diagnostics, and revalidate the
downstream projection after an accepted frontend form changes.  It must not
restore a retired handwritten opcode coverage registry or introduce a simulator
runtime variant-support API.  Creating these links establishes ownership only;
it does not change the per-operation completeness conclusions or the historical
execution evidence above.
