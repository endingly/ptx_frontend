# Conversion and Address-Query Coverage

This document defines the PTX 9.3 syntax accepted and target-validated by the
frontend for `isspacep`, `cvta`, `cvt`, `cvt.pack`, `prmt`, `mapa`, and
`getctarank`. The machine-readable authority is
`python/src/ptx_frontend/spec/resources/ptx_spec/data_movement_and_conversion.yaml`.
It is a source-acceptance and Resolved-IR validation contract. It does not
implement conversion results, address mapping, CTA-rank lookup, assembler
output, or GPU execution.

The normative source is NVIDIA's [PTX ISA 9.3 data-movement and conversion
chapter](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions).
Target minima below are part of the accepted source profile. They do not infer
binary translation compatibility between target spellings.
Family-qualified availability is evaluated against the source profile's enabled
feature families, rather than only its literal spelling. For example,
`sm_121a` enables the `sm_120f` family.

## Address-space predicates and conversion

`isspacep` accepts a predicate destination and a register generic address in
one of these spellings:

```text
isspacep.{global|local|shared|const|param} p, a
isspacep.shared::{cta|cluster} p, a
isspacep.param::entry p, a
```

The instruction has no size suffix. Its source is a 32-bit or 64-bit
fundamental integer/bit container under the frontend's equal-or-wider register
contract; it is not a floating, sub-word, packed, or `.b128` operand. The
unqualified forms mean PTX's default `.shared::cta` and `.param::entry`, rather
than an inferred runtime address provenance. Global, local, and shared are PTX
2.0 / SM 20; constant is PTX 3.1 / SM 20; parameter is PTX 7.7 / SM 70;
explicit `shared::cta` is PTX 7.8 / SM 30; explicit `shared::cluster` is PTX
7.8 / SM 90; and explicit `param::entry` is PTX 8.3 / SM 70. The slice accepts
register operands only, so variable addresses and variable-plus-offset forms
are outside it.

`cvta` and `cvta.to` cover unqualified `.global`, `.local`, `.shared`,
`.const`, and `.param` spellings, plus `.shared::cta`, `.shared::cluster`, and
`.param::entry`, with `.u32` and `.u64` in both directions. Global/local/shared
require PTX 2.0 / SM 20, constant PTX 3.1 / SM 20, parameter PTX 7.7 / SM 70,
explicit shared forms PTX 7.8 (SM 30 for `::cta`, SM 90 for `::cluster`), and
`param::entry` PTX 8.3 / SM 70. The retained public `GlobalU64` and
`ToGlobalU64` variant names remain stable.

Forward `cvta.space` accepts a same-width register, an addressable declaration
in the selected space, or that declaration plus an immediate offset. Its public
source field is therefore the existing `ResolvedMovSource` sum type. This
changes the API type from `ResolvedRegisterRef` for existing forward variants;
callers inspect a register alternative with `std::get` or `std::get_if`.
Variant names are preserved. `cvta.to` remains register-only. Declaration-bound
addresses retain their declared space
through owned resolution; a known wrong symbol space is rejected, and
`.param{::entry}` symbol sources must be kernel input parameters. Register
sources remain accepted without attempting to infer runtime pointer provenance.

`mapa{.shared::cluster}.{u32|u64}` and
`getctarank{.shared::cluster}.{u32|u64}`, together with their generic shared
spellings, remain the existing PTX 7.8 / SM 90 cluster-capability forms. They
validate syntax, physical operands, and target availability only; they do not
prove an address belongs to a live cluster mapping or calculate a CTA rank.

## `prmt`

All seven `prmt.b32` forms are modelled: the unqualified generic form and
`.f4e`, `.b4e`, `.rc8`, `.ecl`, `.ecr`, and `.rc16`. Every form is PTX 2.0 /
SM 20. Destination and all three inputs use the 32-bit bit-container contract;
each input is register-or-immediate. The frontend intentionally does not impose
a 16-bit immediate range on the selector: generic mode consumes its low 16
bits and specialized modes consume their documented low selector bits.

## `cvt` scalar and packed families

### Ordinary scalar conversions

The ordinary scalar domain is the 12-by-12 `.u8/.u16/.u32/.u64`,
`.s8/.s16/.s32/.s64`, `.bf16`, `.f16`, `.f32`, and `.f64` type grid. It uses the
PTX integer (`rni/rzi/rmi/rpi`) and floating (`rn/rz/rm/rp`) rounding classes,
plus the applicable `.ftz` and `.sat` flags. Typed checking rejects an
otherwise syntactic pair when the rounding class, loss direction, saturation,
or FTZ endpoint is invalid. Scalar source operands are register-or-immediate;
register endpoints retain the PTX width contract. `.f64` directions require
SM 13 or newer.

The half/bfloat/tf32 cohorts retain their distinct syntax and availability:

- Plain scalar `f16 <- f32` accepts the four floating rounding spellings
  `.rn/.rz/.rm/.rp`. The special `.relu` and `.satfinite` spellings use
  `.rn/.rz`: `.relu` begins at PTX 7.0 / SM 80, and `.satfinite` begins at
  PTX 8.1 without an additional SM minimum in the PTX availability note.
- `bf16 <- f32` and the packed `bf16x2 <- f32,f32` cohort begin at PTX 7.0 /
  SM 80. `f32 <- bf16` begins at PTX 7.1 / SM 80; its `.ftz` form begins at
  PTX 7.8 / SM 90. The remaining ordinary BF16 directions begin at PTX 7.8 /
  SM 90.
- `f16x2 <- f32,f32` and `bf16x2 <- f32,f32` use two scalar `.f32` sources
  and a packed physical destination. Their stochastic `.rs` forms add a
  required `.b32` register `rbits` and are limited to PTX 8.7 on exact
  `sm_100a` or `sm_103a`.
- `tf32 <- f32` preserves separate `.rna` and `.rn/.rz` forms. Its base
  destination is PTX 7.0 / SM 80; the directed ReLU cohort is PTX 7.8 / SM
  90. `.satfinite` has its own later availability, including PTX 8.6 / SM 100
  for the directed `.rn/.rz` form.

### FP8, low-bit, UE8M0, S2F6, stochastic, and scaled forms

The modern forms retain their physical packing rather than treating
instruction-only type spellings as declaration types:

- FP8 x2 forms use `e4m3x2`/`e5m2x2`, a `.b16` destination or source container
  as directed by the syntax, mandatory `.rn.satfinite` on FP8 destinations,
  and optional `.relu` where PTX specifies it. The f32 and packed `f16x2`
  source forms retain the original PTX 7.8 / SM 90 or PTX 8.1 / SM 89 paths
  independently of the packed `bf16x2` source extension, which is PTX 9.1 on
  the family-specific
  `sm_100f`, `sm_110f`, or `sm_120f` lines; BF16 x2 destinations from FP8 are
  PTX 9.2 on those same family lines.
- FP4 (`e2m1x2`) and FP6 (`e2m3x2`/`e3m2x2`) x2 forms use their `.b8` or `.b16`
  physical containers. Their mandatory `.rn.satfinite` destination forms and
  the reverse `f16x2` forms use the documented A/F target alternatives,
  beginning with PTX 8.6 `sm_100a` or the family-specific PTX 8.8 `sm_100f`
  path. Packed-source FP4 accepts its `.b8` destination under the normal
  equal-or-wider register policy, while FP6 retains its `.b16` minimum. The
  packed-source and BF16-destination extensions retain their PTX
  9.1/9.2 gates on the `sm_100f`, `sm_110f`, and `sm_120f` families.
- The x4 FP8, FP4, and FP6 forms are `.rs` conversions from a four-element
  `.f32` register vector. They require a separate `.b32` `rbits` register,
  mandatory `.satfinite`, and exact PTX 8.7 `sm_100a` or `sm_103a` targets.
- `ue8m0x2` accepts its `.rz/.rp` f32 and packed-BF16 inputs and its `.rn`
  BF16 output form with the specified low-bit target alternatives.
- `s2f6x2` is PTX 9.1. Its f32 and BF16 x2 directions retain mandatory
  `.rn.satfinite`; its BF16 x2 result direction retains the applicable optional
  flags. These forms are limited to `sm_100a`, `sm_103a`, `sm_110a`,
  `sm_120a`, or `sm_121a`.
- A `.scaled::n2::ue8m0` spelling selects the matching optional extra `.b16`
  scale-factor operand. Without that spelling, the shorter physical operand
  layout is used. The scale factor is never a substitute for a register
  declaration of an instruction-only scalar type.

`ScalarType` and `RoundingMode` additions append to their existing public
domains. The low-bit/modern identities used only by instruction syntax are
instruction-only: they are modifiers, not legal `.reg` declaration types.

### `cvt.pack`

`cvt.pack.sat.{u16|s16}.s32 d,a,b` has three operands. The four-operand form
is `cvt.pack.sat.{u2|s2|u4|s4|u8|s8}.s32.b32 d,a,b,c`. Physical `d` is `.u32`,
`a` and `b` are `.s32`, and four-operand `c` is `.b32` and may be a register
or immediate. The base forms require PTX 6.5 / SM 72; the `.u2/.s2/.u4/.s4`
values require SM 75. The existing fixed public names and constants are kept
while new dynamic modifier values select the added forms.

## Scope and tool evidence

The standalone [installed consumer](../../examples/conversion_consumer/README.md)
exercises the public conversion API and validates an owned module after its
source and syntax AST are destroyed.

The supported conversion scope is the set of forms listed above. This `cvta`
slice does not model PTX's implementation note that generic constant pointers
are disallowed in programs with kernel `.ptr.const` parameters. Runtime
conversion, address-mapping, and CTA-rank semantics are not executed by this
frontend.

The recorded probes use CUDA 13.1 `ptxas` V13.1.115, whose highest accepted PTX
version is 9.1; it rejects a PTX 9.3 profile. A 64-bit forward-source probe at
`sm_120` passed across the supported spaces, symbol-plus-offset forms,
sub-qualifiers, and `.to` u64 forms. The 32-bit forward and minimum-profile
probes were blocked by the tool's unsupported 32-bit compilation/ABI path, so
they are not semantic evidence. A device-formal `.param` probe was accepted by
the assembler even though this frontend rejects it for CVTA's kernel-parameter
contract; a device-formal `.local` probe was rejected for the expected space
mismatch. The probes are reproducible with `ptxas --version` and
`ptxas -arch=<target> <fixture>.ptx -o <fixture>.cubin`. The ordinary matrix
recorded 140 accepted cases and four 8-bit integer-width discrepancies. Those
observations are compiler-tool evidence, not a replacement for PTX 9.3, and
cannot validate PTX 9.2 forms with this tool version.
