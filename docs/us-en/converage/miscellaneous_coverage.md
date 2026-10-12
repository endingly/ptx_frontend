# PTX 9.3 miscellaneous instruction coverage

The frontend models the five opcodes in [PTX ISA 9.3 §9.7.20](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#miscellaneous-instructions). Parsing and owned resolution retain the operation, modifier, typed operand, and predicate guard; the target-aware checker applies the PTX and target gates. The frontend does not run a debugger, delay a thread, count performance events, or allocate physical registers.

| Instruction | Supported form and operand | PTX ISA floor | Target floor |
| --- | --- | --- | --- |
| `brkpt` | Bare instruction, optionally predicated | 1.0 | `sm_11` |
| `nanosleep` | `.u32` with a 32-bit register or integer immediate narrowed to 32 bits, in nanoseconds | 6.3 | `sm_70` |
| `pmevent` | Immediate event index `0..15` | 1.4 | All targets |
| `pmevent.mask` | Immediate 16-bit event mask | 3.0 | `sm_20` |
| `trap` | Bare instruction, optionally predicated | 1.0 | All targets |
| `setmaxnreg` | `.inc.sync.aligned.u32` and `.dec.sync.aligned.u32`, each with an immediate count `24..256` divisible by eight | 8.0 | See below |

The target catalog starts at `sm_13`, so `sm_11` is a documented floor for `brkpt`, not a separately modeled target. `pmevent` always requires an immediate; `.mask` selects a mask rather than an index. `nanosleep` accepts both register and immediate sources and preserves the programmer's source form. Its integer constants follow the general [PTX constant conversion rule](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constants): for example, `-1` narrows to `0xffffffff` and `4294967296` narrows to zero at the `.u32` use.

`setmaxnreg` accepts exact `sm_90a` from PTX 8.0, exact `sm_100a` from PTX 8.6, exact `sm_120a` from PTX 8.7, enabled `sm_100f` and `sm_120f` families from PTX 8.8, and the enabled `sm_110f` family from PTX 9.0. The model follows the repository's explicit target catalog; it does not infer unknown target spellings. Both action modifiers are distinct resolved variants. `.sync`, `.aligned`, and `.u32` are mandatory. The checker validates the literal count but cannot prove the runtime warpgroup participation, synchronization, register pool state, or launch configuration required for effective register adjustment.

The 16-bit `pmevent.mask` operand accepts `0..65535`, including zero; the frontend retains that literal even when no event bit is set.

No form in this section is marked deprecated by the fixed PTX 9.3 reference. `nanosleep` specifies an approximate duration, at most one millisecond, and permits wakeup changes within a warp; the frontend performs type checking, not timing prediction.

Offline cross-checks with CUDA 13.3 `ptxas` V13.3.73 assembled the documented forms, narrowed `nanosleep` constants, the `pmevent.mask` endpoints `0` and `65535`, and the enabled `setmaxnreg` targets. They rejected invalid event indices, masks outside `0..65535`, register operands for `pmevent`, invalid register counts, and generic `sm_90`/`sm_100`/`sm_110`/`sm_120` for `setmaxnreg`. One assembler behavior differs from the fixed PTX 9.3 reference: `ptxas` accepts `nanosleep` in a PTX 6.2 module although the reference says it was introduced in 6.3. The frontend retains the documented 6.3 floor. These checks verify assembly only; they do not establish GPU runtime behavior.
