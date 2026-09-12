# SETP Coverage

This document records the complete PTX 9.3 `setp` contract modelled by the frontend. It supplements the [syntax coverage matrix](syntax_coverage.md); it does not claim simulator execution or physical-GPU behavior. The canonical machine-readable source is `python/code_gen/resources/ptx_spec/comparison_and_selection.yaml`.

The normative sources are NVIDIA's PTX ISA 9.3 archive: [ordinary SETP §9.7.6.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) and [half/bfloat SETP §9.7.7.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp).

Every row is modelled through syntax, Resolved IR, and target-aware checking. These are frontend contracts only; no row defines simulator semantics.

| Forms | Minimum PTX / target | Comparison and Boolean modifiers | Destination and source contract |
| --- | --- | --- | --- |
| Ordinary bit `.b16/.b32/.b64` | 1.0 / all targets | `.eq/.ne`, with optional `.and/.or/.xor` | One predicate or a predicate pair; either ordinary destination position may be `_`, but a pair cannot discard both results. Sources are same-width register-or-immediate values. |
| Ordinary signed `.s16/.s32/.s64` | 1.0 / all targets | `.eq/.ne/.lt/.le/.gt/.ge`, with optional Boolean form | The ordinary single/pair sink and source rules apply. |
| Ordinary unsigned `.u16/.u32/.u64` | 1.0 / all targets | Signed-family comparisons plus `.lo/.ls/.hi/.hs`, with optional Boolean form | The ordinary single/pair sink and source rules apply. |
| Ordinary `.f32` | 1.0 / all targets | `.eq/.ne/.lt/.le/.gt/.ge/.equ/.neu/.ltu/.leu/.gtu/.geu/.num/.nan`; optional Boolean form and `.ftz` | The ordinary single/pair sink and floating register-or-immediate source rules apply. |
| Ordinary `.f64` | 1.0 / `sm_13` | The same floating comparison and optional Boolean form; `.ftz` is not legal | The ordinary single/pair sink and floating register-or-immediate source rules apply. |
| `.f16` | 4.2 / `sm_53` | All floating comparisons, optional Boolean form, optional `.ftz` | Exactly one predicate result. Sources are `.f16` or same-width `.b16` registers; `_` is not a destination. |
| `.f16x2` | 4.2 / `sm_53` | All floating comparisons, optional Boolean form, optional `.ftz` | Exactly two predicate results and exact `.b32` register sources; sink destinations are not legal. |
| `.bf16` | 7.8 / `sm_90` | All floating comparisons and optional Boolean form; `.ftz` is not legal | Exactly one predicate result and exact `.b16` register sources; `_` is not a destination. |
| `.bf16x2` | 7.8 / `sm_90` | All floating comparisons and optional Boolean form; `.ftz` is not legal | Exactly two predicate results and exact `.b32` register sources; sink destinations are not legal. |

## Deliberate boundaries

The ordinary syntax permits a single `_` destination or one `_` lane of a predicate pair. The frontend represents that explicitly, and checker revalidation rejects a public-IR pair edited to contain two sinks. Half and bfloat forms retain their required single or pair predicate destinations; their packed source containers are exact rather than merely width-compatible.

The optional Boolean operand is a predicate source, not a predicate-register-only field. It accepts a plain or complemented predicate register and PTX integer predicate constants. Constants are canonicalized with C truth rules: zero is false, nonzero is true, and a leading `!` complements that truth value. Predicate special registers are not admitted by `setp`, even though `mov.pred` admits them.

The `.f32` and `.f64` forms are separate generated variants so `.ftz` remains legal only for `.f32`. This keeps the PTX modifier language disjoint without a separate capability registry. The expanded `ComparisonOperator` domain keeps the pre-existing `Eq`, `Lt`, and `Ge` numeric identities and appends the remaining normative suffixes.

As corroborating compiler evidence, CUDA Toolkit 13.1 `ptxas` probes using PTX 8.0 and `sm_90` accepted Boolean integer constants (including complemented constants), while rejecting numeric immediates as `.f16`, `.f16x2`, `.bf16`, and `.bf16x2` comparison sources. Those forms predate PTX 8.0 and PTX 9.3 documents no immediate-source extension. This evidence supports the frontend boundary but is not a PTX 9.3 assembler or physical-GPU execution result.

## Verification sources

[Dedicated C++ SETP tests](../../submod/resolved_ir/test/test_setp_completeness.cpp) exercise every family through parsing, resolution, declared-operand checking, target minima, modifier/layout negatives, predicate-constant truth values, and mutated-IR sink validation. The [installed consumer SETP test](../../submod/resolved_ir/test/package_consumer/setp_completeness.cpp) uses only installed public headers to check a floating form, an ordinary sink pair, a predicate constant, and checker rejection of a comparison enum outside the selected domain. The focused Python database test verifies variants, comparison domains, availability, sink boundaries, predicate-source kinds, and packed containers. These sources define the frontend boundary; they do not verify simulator or hardware execution.
