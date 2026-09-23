# SET Coverage

The frontend models ordinary scalar `set` in [PTX ISA 9.3 §9.7.6.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-set) and half/bfloat `set` in [§9.7.7.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-set). The canonical contract is `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`. Resolution preserves typed comparison, optional Boolean operation, optional flush control, and result/source types. Checking validates their combinations and operands; the frontend does not evaluate the comparison or write the result value.

## Ordinary scalar forms

`set.CmpOp{.BoolOp}{.ftz}.dtype.stype d, a, b{, c}` accepts result `.dtype` of `.u32/.s32/.f32`. The comparison sources `a` and `b` are register-or-immediate values of `.stype`; `d` is a register of `.dtype`. The optional Boolean source `c` is required exactly when `.BoolOp` is `.and/.or/.xor`; it accepts a plain or complemented predicate register or an integer predicate constant. Integer predicate constants follow the PTX C truth rule, including a leading `!`. Predicate special registers and floating constants are excluded.

| Source `.stype` | Comparison suffixes | `.ftz` | Minimum target |
| --- | --- | --- | --- |
| `.b16/.b32/.b64` | `.eq/.ne` | No | PTX 1.0 |
| `.s16/.s32/.s64` | `.eq/.ne/.lt/.le/.gt/.ge` | No | PTX 1.0 |
| `.u16/.u32/.u64` | Signed-family suffixes plus `.lo/.ls/.hi/.hs` | No | PTX 1.0 |
| `.f32` | Ordered suffixes, unordered `.equ/.neu/.ltu/.leu/.gtu/.geu`, `.num/.nan` | Optional | PTX 1.0 |
| `.f64` | Same floating suffixes | No | PTX 1.0, `sm_13` |

The selected `.stype`, rather than `.dtype`, determines the comparison domain and `.ftz` availability. Each source family has separate Boolean-absent and Boolean-required generated variants. The [ordinary C++ tests](../../submod/resolved_ir/test/test_set_completeness.cpp) cover the families, modifier and operand negatives, target gate, and mutated typed IR.

## Half and bfloat forms

The half/bfloat forms use the same comparison and optional Boolean-source fields, but have distinct destination and source contracts. `set.CmpOp{.BoolOp}{.ftz}.dtype.stype d, a, b{, {!}c}` writes `.f16`, `.bf16`, `.f16x2`, `.bf16x2`, or an allowed integer result. The optional `c` uses the same predicate-source contract as ordinary `set`. The 30 generated alternatives partition 15 result/source cohorts by whether Boolean combination is present.

| Result `.dtype` | Source `.stype` | Minimum PTX / SM | `.ftz` |
| --- | --- | --- | --- |
| `.f16` | `.b16/.b32/.b64`, integer 16/32/64, `.f16/.f32/.f64` | 4.2 / 53 | Only `.f16/.f32` source |
| `.bf16` | Same source list | 7.8 / 90 | No |
| `.u16/.s16/.u32/.s32` | `.f16` | 6.5 / 53 | Optional |
| `.u16/.s16/.u32/.s32` | `.bf16` | 7.8 / 90 | No |
| `.f16x2` | `.f16x2` | 4.2 / 53 | Optional |
| `.u32/.s32` | `.f16x2` | 6.5 / 53 | Optional |
| `.bf16x2/.u32/.s32` | `.bf16x2` | 7.8 / 90 | No |

Bit-source comparison permits `.eq/.ne`; signed and unsigned integer sources permit `.eq/.ne/.lt/.le/.gt/.ge`; floating and packed sources permit those six plus `.equ/.neu/.ltu/.leu/.gtu/.geu/.num/.nan`. The ordinary unsigned aliases `.lo/.ls/.hi/.hs` do not apply here. `ptxas` 13.3 rejects `.ftz` with bit, integer, and `.f64` sources even for an `.f16` destination, so these generated alternatives exclude it.

For scalar `.f16` sources, the frontend accepts `.f16` or compatible `.b16` registers; scalar `.bf16` sources require exact `.b16` registers. Packed `.f16x2` sources accept `.f16x2` or compatible `.b32` registers, while `.bf16x2` sources require exact `.b32`. Native `.f16x2` results accept `.f16x2` or `.b32`; native `.bf16x2` results require `.b32`. Integer results use width-compatible bit or integer register containers; packed-source integer results also accept a same-width `.f16x2` physical register. Half/bfloat source immediates are excluded; the ordinary bit, integer, `.f32`, and `.f64` source cohorts retain register-or-immediate operands where valid.

The [half/bfloat C++ tests](../../submod/resolved_ir/test/test_set_half_completeness.cpp) cover every generated alternative, target boundaries, invalid controls and containers, numeric immediates, owned IR, and mutated typed fields. The [Python spec test](../../python/tests/spec/test_set_completeness.py) checks both ordinary and half/bfloat descriptor domains. The installed [consumer](../../examples/conversion_consumer/main.cpp) exercises the public generated representation.

The C++ package version 0.2.0 replaces the former generated `Set::EqU32U32` and `Set::LtAndF32S32` alternatives with the typed ordinary families `Bit`, `Signed`, `Unsigned`, `Float`, `FloatF64` and their `Boolean` counterparts. Consumers should select the new alternatives and read `dtype`/`stype` fields to determine the exact type. This is a source and binary API change; rebuild installed C++ consumers with the new headers and library.
