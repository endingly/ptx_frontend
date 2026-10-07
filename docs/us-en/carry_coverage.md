# Extended-precision integer coverage

The canonical arithmetic YAML models all 48 documented §9.7.2 type/mode/effect
combinations: `add.cc`, `addc{.cc}`, `sub.cc`, `subc{.cc}`, `mad.{hi|lo}.cc`,
and `madc.{hi|lo}{.cc}` for `.u32/.s32/.u64/.s64`, following the
[PTX 9.3 extended-precision arithmetic specification](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions).
The existing add/sub 32-bit forms require PTX 1.2; their 64-bit forms require
PTX 4.3 and SM 20. `mad` and `madc` 32-bit forms require PTX 3.0 and SM 20;
their 64-bit forms require PTX 4.3 and SM 20. Their `.hi` or `.lo` mode is
required: no implicit default mode is modeled. Rounding, saturation, wide, and
16-bit forms are not legal. All three multiply-add sources accept registers or
integer immediates under the existing same-width integer policy.

Every generated variant exposes a static `condition_code_effect` of type
`ConditionCodeEffect`; the corresponding `ResolvedVariantDescriptor` carries
the same typed value. Ordinary arithmetic defaults to `None`.

| Form | Effect |
| --- | --- |
| `add.cc` | `CarryOut` |
| `addc` | `CarryIn` |
| `addc.cc` | `CarryInOut` |
| `sub.cc` | `BorrowOut` |
| `subc` | `BorrowIn` |
| `subc.cc` | `BorrowInOut` |
| `mad.hi.cc` / `mad.lo.cc` | `CarryOut` |
| `madc.hi` / `madc.lo` | `CarryIn` |
| `madc.hi.cc` / `madc.lo.cc` | `CarryInOut` |

Carry and borrow refer to the same implicit architectural `CC.CF` bit. An
execution predicate gates both explicit results and this implicit effect.
The bit is not preserved across calls. The frontend retains those semantic
contracts; it does not execute arithmetic or maintain simulator state.
Consumers must not infer effects from opcode spelling or assume the incoming
bit is initialized. The effect is fixed by generated variant identity, rather
than mutable instruction metadata.

The checked-in [CUDA 13.1 `ptxas` evidence](../extended_precision_ptxas.json)
contains 32 self-contained cases: 11 accepted, 14 contract rejects, 3
omitted-mode observations rejected, 2 version boundaries, 1 target boundary,
and 1 tool-unavailable case. It also records two assembler-accepted duplicate
`.cc` observations; the frontend continues to require each modifier at most
once.

The dedicated C++ tests cover all types and effects, predication retention,
target boundaries, required modes, invalid modifiers/layouts, and owned typed
IR. The installed-package consumer reads typed `Mad` and `Madc` effects and
revalidates a modified type without depending on an AST or source-spelling
dispatch.

## Generated scale and local checks

The canonical database grows from 81 opcodes and 476 variants to 82 and 488.
The 24 documented new type/mode/effect forms are represented by 12 canonical
variants: four appended `Mad` carry-out variants and eight `Madc` carry-in or
carry-in/out variants. Existing `Mad` variant names and indices remain stable.
The generated resolved-IR subtree remains 13 files and 9 C++ sources; it grows
from 18,444,809 to 18,785,864 bytes and from 381,098 to 388,329 C++ lines
(+341,055 bytes and +7,231 lines).

The final Debug build ran 75 Ninja actions in 57 seconds using the existing
configured, cache-enabled `out/build/ci-linux-gcc-debug` tree; it was not a
clean build. Full Python discovery passed 222 tests in 55.392 seconds. Ordinary
Debug CTest excluding the separately run consumer passed 886/886 in 11.58
seconds, and the installed-package consumer passed 1/1 in 190.27 seconds.
Release validation remains with CI.
