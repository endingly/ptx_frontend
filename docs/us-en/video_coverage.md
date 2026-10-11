# PTX 9.3 video instruction coverage

The fixed [CUDA 13.3 / PTX 9.3 video clauses](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#video-instructions) define 23 opcodes, represented by 23 typed variants and 39 operand layouts. Canonical `video.yaml` owns their closed type/modifier and operand contracts; generated concrete classes expose `video_lanes` and `video_operation`. `VideoType::{S32,U32}` remains separate from ordinary integer arithmetic types.

| Family | Opcodes | Minimum PTX / SM |
| --- | --- | --- |
| Scalar | `vadd`, `vsub`, `vmad`, `vabsdiff`, `vmin`, `vmax`, `vshl`, `vshr`, `vset` | 2.0 / 20 |
| Two lanes | `vadd2`, `vsub2`, `vavrg2`, `vabsdiff2`, `vmin2`, `vmax2`, `vset2` | 3.0 / 30 |
| Four lanes | `vadd4`, `vsub4`, `vavrg4`, `vabsdiff4`, `vmin4`, `vmax4`, `vset4` | 3.0 / 30 |

No maximum target or removal gate is specified. Old minimum targets are frontend contract tests; current assembler support for those targets is a separate tool limitation. Modern SM80/100 forms remain accepted. Narrow `u8/s8/u16/s16` suffix aliases are rejected.

Every register carrier is scalar, general, 32-bit integer/bit storage. Scalar source roles also accept integer constants with existing 32-bit conversion and original 64-bit source provenance; SIMD source roles require registers. Float, vector, predicate, sink, special-register and symbol operands are rejected. Immediate selectors are unsupported.

An ordinary vector's explicit `.xyzw/.rgba` component is an effective scalar
carrier wherever the existing role accepts a plain 32-bit register, including
permitted `vmad` register minus. The component selector is not a VIDEO
`.b0/.h0` selection: its VIDEO selector remains absent. Merge destinations still
require their actual VIDEO selector; chained `V.x.b0`, hardware components and
unpermitted minus remain rejected. Owned and standalone checking retain the
component child's range separately from an outer minus and reject fabricated
component-plus-VIDEO-selector combinations. This frontend domain follows the
ISA scalar-carrier contract; bounded CUDA 13.3 V13.3.33 probes rejected tested
`vadd V.x` and `vmad -V.x` forms even with initialized inputs, while ordinary
`add V.x` and negated scalar `vmad` controls assembled. This is a compiler
compatibility difference, not evidence of GPU behavior or complete assembler
parity.

`ResolvedVideoOperand` owns its located register-or-immediate value, optional typed selector, and register-negation flag/minus range. Scalar byte selectors are `b0..b3`, halfword selectors `h0..h1`. Scalar three-operand forms have an unselected destination. Four-operand secondary forms use `.add/.min/.max`; merge forms require a selected destination and no secondary suffix. Scalar saturation may combine with secondary operations. Shift instructions require `.clamp/.wrap` after optional `.sat` and use unsigned `btype`. `vset` has two source types and comparison only, with unsigned destination/C interpretation.

Packed source swizzles index the concatenated A+B carriers: two-lane digits range `0..3`, four-lane digits `0..7`. Repetition is legal for sources. Arrays retain written high-to-low digit order. Destination masks are nonempty descending subsets without repetition, including masked `.add` accumulation. Default selectors are A=`h10`/`b3210`, B=`h32`/`b7654`, destination=`h10`/`b3210`. `video_effective_selector` returns these typed defaults while preserving omission in the owned operand. SIMD saturation and `.add` cannot coexist; `vset2/4` has no saturation.

`vmad` owns each written source minus, optional `.po`, saturation and `.shr7/.shr15`. A/B may have scalar selectors; D/C are full carriers. Product minus is A-minus XOR B-minus; effective product minus cannot coexist with C-minus. `.po` forbids every written register minus, including a cancelling pair. Negative numeric constants are values and never feed these controls. C constants use B32 carrier conversion. `video_mad_interpretation` derives product/input-C/final signs from source types and register controls without executing arithmetic or changing the independent written dtype.

Two manual inconsistencies are resolved explicitly: `vset2/4` preserved lanes follow the syntax and pseudocode's supplied C, although nearby prose says B; `vmad` input-C interpretation follows the Description's intermediate sign, while its pseudocode's internal c128 extension uses final sign. The public sign query describes arithmetic interpretation rather than that internal extension action. Noncanonical `.max` SIMD comparison examples, duplicate/reversed destination masks and `varvg` spellings remain rejected.

The installed [video consumer](../../examples/video_consumer/main.cpp) demonstrates AST-free ownership, selectors/defaults, register-minus versus literal-minus, derived sign and mutable-IR rejection. Frontend tests cover every opcode, legal type/control combinations, target boundaries, source binding/provenance and malformed mutations. CUDA 13.3.73 `ptxas` static probes accept all 23 representative rich forms and reject SIMD saturation-plus-add and saturated SIMD comparison. These checks establish frontend/static assembler legality; arithmetic execution and GPU observations are outside this frontend contract.
