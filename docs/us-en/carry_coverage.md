# Extended-precision add/sub coverage

The canonical arithmetic YAML models `add.cc`, `addc{.cc}`, `sub.cc`, and
`subc{.cc}` for `.u32/.s32/.u64/.s64`, following the
[PTX 9.3 extended-precision arithmetic specification](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions).
32-bit forms require PTX 1.2; 64-bit forms require PTX 4.3 and SM 20.
Rounding and saturation modifiers are not legal. Sources accept registers or
integer immediates, with the existing integer type and register-width policies.

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

Carry and borrow refer to the same implicit architectural `CC.CF` bit. An
execution predicate gates both explicit results and this implicit effect.
The bit is not preserved across calls. The frontend retains those semantic
contracts; it does not execute arithmetic or maintain simulator state.
Consumers must not infer effects from opcode spelling or assume the incoming
bit is initialized. `mad.cc` and `madc` are outside this change.

The dedicated C++ tests cover all types and effects, predication retention,
target boundaries, invalid modifiers/layouts, and mutated typed IR. The
installed-package consumer reads the typed effect and revalidates a modified
type without depending on an AST or a source-spelling dispatch table.
