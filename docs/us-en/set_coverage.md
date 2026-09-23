# Ordinary SET Coverage

The frontend models the ordinary scalar `set` forms in [PTX ISA 9.3 §9.7.6.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-set). The canonical contract is `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`. Resolution preserves typed comparison, optional Boolean operation, optional flush control, and result/source types. Checking validates their combinations and operands; the frontend does not evaluate the comparison or write the result value.

`set.CmpOp{.BoolOp}{.ftz}.dtype.stype d, a, b{, c}` accepts result `.dtype` of `.u32/.s32/.f32`. The comparison sources `a` and `b` are register-or-immediate values of `.stype`; `d` is a register of `.dtype`. The optional Boolean source `c` is required exactly when `.BoolOp` is `.and/.or/.xor`; it accepts a plain or complemented predicate register or an integer predicate constant. Integer predicate constants follow the PTX C truth rule, including a leading `!`. Predicate special registers and floating constants are excluded.

| Source `.stype` | Comparison suffixes | `.ftz` | Minimum target |
| --- | --- | --- | --- |
| `.b16/.b32/.b64` | `.eq/.ne` | No | PTX 1.0 |
| `.s16/.s32/.s64` | `.eq/.ne/.lt/.le/.gt/.ge` | No | PTX 1.0 |
| `.u16/.u32/.u64` | Signed-family suffixes plus `.lo/.ls/.hi/.hs` | No | PTX 1.0 |
| `.f32` | Ordered suffixes, unordered `.equ/.neu/.ltu/.leu/.gtu/.geu`, `.num/.nan` | Optional | PTX 1.0 |
| `.f64` | Same floating suffixes | No | PTX 1.0, `sm_13` |

The selected `.stype`, rather than `.dtype`, determines the comparison domain and `.ftz` availability. Each source family has separate Boolean-absent and Boolean-required generated variants. The [C++ tests](../../submod/resolved_ir/test/test_set_completeness.cpp) cover the families, modifier and operand negatives, target gate, and mutated typed IR. The [Python spec test](../../python/tests/spec/test_set_completeness.py) checks the disjoint domains and operand descriptors. Half/bfloat `set` in §9.7.7.1 is outside this ordinary slice.

The C++ package version 0.2.0 replaces the former generated `Set::EqU32U32` and `Set::LtAndF32S32` alternatives with the typed ordinary families `Bit`, `Signed`, `Unsigned`, `Float`, `FloatF64` and their `Boolean` counterparts. Consumers should select the new alternatives and read `dtype`/`stype` fields to determine the exact type. This is a source and binary API change; rebuild installed C++ consumers with the new headers and library.
