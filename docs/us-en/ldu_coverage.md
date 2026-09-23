# PTX 9.3 `ldu` coverage

The frontend models the documented supported forms in PTX ISA 9.3 §9.7.9.10. `ldu` loads
read-only global data from an address uniform across the warp. The frontend
checks instruction shape and address provenance; it does not prove that the
address is uniform or that the data is read-only at runtime.

| Form | State space | Destination | Availability |
| --- | --- | --- | --- |
| `ldu.type d, [a]` | Omitted suffix uses generic addressing to global data | Scalar register | PTX 2.0 / SM 20 |
| `ldu.global.type d, [a]` | Explicit global | Scalar register | PTX 2.0 |
| `ldu.v2/v4.type d, [a]` | Omitted suffix uses generic addressing to global data | Two or four registers | PTX 2.0 / SM 20 |
| `ldu.global.v2/v4.type d, [a]` | Explicit global | Two or four registers | PTX 2.0 |

Scalar forms accept `.b/.u/.s` at 8, 16, 32, and 64 bits, plus `.f32`,
`.f64`, and `.b128`. Vector loads are limited to 128 total bits: v2 accepts
elements through 64 bits, including `.f64`; v4 accepts elements through
32 bits. `.b128` is scalar only. `.f64` requires SM 13; `.b128` requires PTX
8.3 and SM 70. The generic-addressing minimum also applies when the type has
a lower minimum.
Destination register elements must be at least as wide as the load type. The
checker enforces type-compatible registers, vector arity, alignment, and known
global address provenance. An unknown address register may represent a global
generic pointer. Vector destination placeholders (`_`), `.v8`, cache controls,
and state spaces other than `.global` are outside the documented frontend
contract. Some `ptxas` versions accept `_` vector elements, but PTX 9.3 does
not document them for `ldu`.
