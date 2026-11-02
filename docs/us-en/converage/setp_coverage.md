# SETP Coverage

This document records the complete PTX 9.3 `setp` contract modelled by the frontend. It supplements the [syntax coverage matrix](syntax_coverage.md); it does not claim simulator execution or physical-GPU behavior. The canonical machine-readable source is `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`.

The normative sources are NVIDIA's PTX ISA 9.3 archive: [ordinary SETP §9.7.6.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) and [half/bfloat SETP §9.7.7.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp).

Every row is modelled through syntax, Resolved IR, and target-aware checking. These are frontend contracts only; no row defines simulator semantics.

| Forms | Minimum PTX / target | Comparison and Boolean modifiers | Destination and source contract |
| --- | --- | --- | --- |
| Ordinary bit `.b16/.b32/.b64` | 1.0 / all targets | `.eq/.ne`, with optional `.and/.or/.xor` | One predicate or a predicate pair; either ordinary destination position may be `_`, but a pair cannot discard both results. Sources are same-width register-or-immediate values. |
| Ordinary signed `.s16/.s32/.s64` | 1.0 / all targets | `.eq/.ne/.lt/.le/.gt/.ge`, with optional Boolean form | The ordinary single/pair sink and source rules apply. |
| Ordinary unsigned `.u16/.u32/.u64` | 1.0 / all targets | Signed-family comparisons plus `.lo/.ls/.hi/.hs`, with optional Boolean form | The ordinary single/pair sink and source rules apply. |
| Ordinary `.f32` | 1.0 / all targets | `.eq/.ne/.lt/.le/.gt/.ge/.equ/.neu/.ltu/.leu/.gtu/.geu/.num/.nan`; optional Boolean form and `.ftz` | The ordinary single/pair sink and floating register-or-immediate source rules apply. |
| Ordinary `.f64` | 1.0 / `sm_13` | The same floating comparison and optional Boolean form; `.ftz` is not legal | The ordinary single/pair sink and floating register-or-immediate source rules apply. |
| `.f16` | 4.2 / `sm_53` | All floating comparisons, optional Boolean form, optional `.ftz` | Exactly one predicate result. Sources are `.f16` or same-width `.b16` registers, or numeric F16 floating constants; explicit raw B16 integer words are also accepted. `_` is not a destination. |
| `.f16x2` | 4.2 / `sm_53` | All floating comparisons, optional Boolean form, optional `.ftz` | Exactly two predicate results and native `.f16x2` or `.b32` register sources; sink destinations are not legal. |
| `.bf16` | 7.8 / `sm_90` | All floating comparisons and optional Boolean form; `.ftz` is not legal | Exactly one predicate result. Sources use actual `.b16` registers, numeric BF16 words or explicit raw B16 integer words; `_` is not a destination. |
| `.bf16x2` | 7.8 / `sm_90` | All floating comparisons and optional Boolean form; `.ftz` is not legal | Exactly two predicate results and exact `.b32` register sources; sink destinations are not legal. |

## Deliberate boundaries

The ordinary syntax permits a single `_` destination or one `_` lane of a predicate pair. The frontend represents that explicitly, and checker revalidation rejects a public-IR pair edited to contain two sinks. Half and bfloat forms retain their required single or pair predicate destinations; native F16x2 sources admit only `.f16x2` or `.b32`, while nonfundamental BF16x2 sources retain exact `.b32` containers. Arbitrary same-width types remain excluded.

The optional Boolean operand is a predicate source, not a predicate-register-only field. It accepts a plain or complemented predicate register and PTX integer predicate constants. Constants are canonicalized with C truth rules: zero is false, nonzero is true, and a leading `!` complements that truth value. Predicate special registers are not admitted by `setp`, even though `mov.pred` admits them.

The `.f32` and `.f64` forms are separate generated variants so `.ftz` remains legal only for `.f32`. This keeps the PTX modifier language disjoint without a separate capability registry. The expanded `ComparisonOperator` domain keeps the pre-existing `Eq`, `Lt`, and `Ge` numeric identities and appends the remaining normative suffixes.

As corroborating compiler evidence, CUDA Toolkit 13.1 `ptxas` probes using PTX 8.0 and `sm_90` accepted Boolean integer constants (including complemented constants), while rejecting numeric immediates as `.f16`, `.f16x2`, `.bf16`, and `.bf16x2` comparison sources. Those forms predate PTX 8.0 and PTX 9.3 documents no immediate-source extension. This older evidence is historical corroboration, not a normative blanket scalar-half literal ban or a current PTX 9.3/GPU result; the explicit scalar support below supersedes that inference.

## Verification sources

[Dedicated C++ SETP tests](../../../submod/resolved_ir/test/test_setp_completeness.cpp) exercise every family through parsing, resolution, declared-operand checking, target minima, modifier/layout negatives, predicate-constant truth values, and mutated-IR sink validation. The [focused Python database test](../../../python/tests/spec/test_setp_completeness.py) verifies variants, comparison domains, availability, sink boundaries, predicate-source kinds, and packed containers. These sources define the frontend boundary; they do not verify simulator or hardware execution.

Scalar F16 numeric floating sources retain RegOrImm and the common binary16 RNE converter. The implementation below supplies a separate explicit Bit16 integer arm for four F16 and four BF16 fields; it reuses the accepted logical BF16 converter and does not change Boolean/predicate or packed policies. Its current-parent model, Debug and installed-ABI checks have passed; independent final acceptance remains pending.

## Explicit scalar Bit16 sources

The implementation has passed full-model/default audits, normal generation, focused
Debug checks and a matching installed public consumer. Independent final acceptance
remains pending. The `integer_immediate_type` descriptor member is
an optional trailing `ScalarType`, absent by default. Exactly eight existing
scalar SETP source rows engage B16: src1/src2 on the plain and Boolean F16/BF16
forms. Four BF16 source members migrate from register references to `RegOrImm`,
with logical BF16 and SameWidth; the four F16 members keep their existing kind,
logical type and width. The descriptor ABI requires rebuilding all consumers.

The source is evaluated once. An integer result chooses the explicit raw B16 arm:
`1` is word `0x0001`, and `0x10001` narrows to the same word while retaining its
64-bit integer origin. A floating result uses the existing logical converter:
`1.0` gives F16 `0x3c00` or BF16 `0x3f80`. Direct unsigned `0f` keeps its existing
root route and lexical precision; no wrapper or trial conversion classifies it.
Ordinary logical floating operands with absent policy still reject integer
origin, including the packed formats. This support does not extend packed
SETP, atomic, storage, call or other scalar source permissions.

Current immediates select their arm by their actual type. Both words fit 16 bits;
numeric logical words have no integer origin or integer negative flag. B16
origin is optional; if present its mask/sign consistency remains checked, and a
cache-free word requires a false negative flag. Coherent word/arm edits do not
authenticate erased source history. Changed supplied syntax still has its
existing separate source association. Complete target context is required for
instruction-check proof; AvailableContext deferral is not a legality certificate.
Actual F16 carriers remain B16 or F16; BF16 carriers remain actual B16, with the
existing intrinsic declaration/class guards. No Base compatibility rule changes.
