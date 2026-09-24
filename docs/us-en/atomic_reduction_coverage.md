# PTX 9.3 atomic and reduction coverage

The frontend models a synchronous scalar global-memory slice of `atom` and
`red` from PTX ISA 9.3 §9.7.14.5–6. It parses, resolves, and checks instruction
shape, owned operands, known address provenance, alignment, and target
availability. It does not execute atomic operations.

| Instruction | Operation and type | Operands | Legacy floor | Explicit `.relaxed.cta` floor |
| --- | --- | --- | --- | --- |
| `atom.global` | `.add`, `.min`, `.max` × `.u32`, `.s32` | `dst, [address], src` | PTX 1.1 / SM 11 | PTX 6.0 / SM 70 |
| `atom.global` | `.inc`, `.dec` × `.u32` | `dst, [address], src` | PTX 1.1 / SM 11 | PTX 6.0 / SM 70 |
| `atom.global` | `.and`, `.or`, `.xor`, `.exch` × `.b32` | `dst, [address], src` | PTX 1.1 / SM 11 | PTX 6.0 / SM 70 |
| `atom.global` | `.cas.b32` | `dst, [address], compare, swap` | PTX 1.1 / SM 11 | PTX 6.0 / SM 70 |
| `red.global` | `.add`, `.min`, `.max` × `.u32`, `.s32` | `[address], src` | PTX 1.2 / SM 11 | PTX 6.0 / SM 70 |
| `red.global` | `.inc`, `.dec` × `.u32` | `[address], src` | PTX 1.2 / SM 11 | PTX 6.0 / SM 70 |
| `red.global` | `.and`, `.or`, `.xor` × `.b32` | `[address], src` | PTX 1.2 / SM 11 | PTX 6.0 / SM 70 |

Legacy forms omit both memory semantics and scope suffixes. Their effective
ISA defaults are relaxed semantics and GPU scope. Explicit forms require
`.relaxed.cta`; both `atom.relaxed.cta.global` and
`atom.global.relaxed.cta` ordering resolve. Separate resolved variants preserve
whether qualifiers appeared in source. The destination is a 32-bit-compatible
register. Each value source accepts a 32-bit-compatible register or an integer
immediate with ordinary narrow conversion. Addresses must be known global when
their provenance is available and aligned to four bytes.

The current boundary requires explicit `.global`. Omitted state space means
generic addressing and is outside this slice. Other operations, types, state
spaces, memory orders and scopes, cache policies, vector and bit-bucket forms,
`red.cas`, `red.exch`, `red.async`, and `multimem.red.async` remain unsupported.
The `.inc` and `.dec` source supplies the operation's runtime bound; the
frontend preserves its typed operand and does not simulate the update.

The C++ package version is 0.4.0. The added named alternatives extend the
public `Atom::Variant` and `Red::Variant` alternatives, changing their source
and binary API; rebuild consumers against matching installed headers and library.
The earlier 0.3.0 change made the public
`Atom::GlobalRelaxedCtaAddU32::src` and `Red::GlobalRelaxedCtaAddU32::src`
members hold `RegOrImm`. A consumer reading a register uses
`std::get<ResolvedRegisterRef>(value.src.value)`; an immediate uses
`std::get<ResolvedImmediate>(value.src.value)`; the new one-source alternatives
use the same operand representation.
The Python wheel remains independently versioned at 0.1.0b0.
