# PTX 9.3 `createpolicy` coverage

The frontend models the documented fractional, range, and access-property
conversion forms in PTX ISA 9.3 §9.7.9.19. Every form requires PTX 7.4 and
SM 80, and writes an opaque `.b64` cache policy to a 64-bit register.

| Form | Operands and source controls |
| --- | --- |
| `createpolicy.fractional.L2::primary{.L2::secondary}.b64` | Destination and optional `.f32` fraction; omitted fraction defaults to `1.0` |
| `createpolicy.range{.global}.L2::primary{.L2::secondary}.b64` | Destination, global address, 32-bit primary size, and 32-bit total size |
| `createpolicy.cvt.L2.b64` | Destination and 64-bit access-property register |

Primary priority is `evict_last`, `evict_normal`, `evict_first`, or
`evict_unchanged`. An explicit secondary priority is `evict_first` or
`evict_unchanged`; omission defaults to `evict_unchanged`. The resolved form
retains whether the secondary modifier and fraction operand appeared in source.

An immediate fraction must be finite and in `(0.0, 1.0]`. The frontend accepts
a dynamic `.f32` register fraction and cannot prove its runtime value. If both
range sizes are immediate, the primary size must not exceed the total size;
dynamic sizes retain this runtime precondition. The range form without
`.global` uses generic addressing, but its runtime address must be global.
Known non-global symbols are rejected, while an unresolved address register
is accepted. Source integer literals for each size must fit the 32-bit operand;
the frontend rejects `4294967296` and `4294967297` instead of adopting
`ptxas` 13.3.73's observed modulo-32-bit truncation. This source contract does
not change the ISA's advertised 4 GB semantic maximum for the total range.
Representable negative spellings such as `-1` retain their 32-bit bit pattern
(`0xffffffff`) for the unsigned size comparison; larger negative magnitudes
are rejected.
The frontend checks the policy representation and operands; it does not
execute cache operations or prove cache behavior.
