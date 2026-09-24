# PTX 9.3 atomic and reduction coverage

The frontend parses, resolves, and checks synchronous scalar and vector `atom` and `red`, plus two `red.async` modes,
operation/type pairs from [PTX ISA 9.3 §9.7.14.5–6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-atom).
It checks operand shape and type, known address provenance, natural alignment,
and target availability. It does not execute atomic operations.

| Operation/type pairs | `atom` | `red` | Base PTX / SM floor |
| --- | --- | --- | --- |
| `add/min/max.{u32,s32}`, `inc/dec.u32`, `and/or/xor.b32` | Yes | Yes | `atom.global`: 1.1 / 11; `red.global`: 1.2 / 11 |
| `exch/cas.b32` | Yes | No | 1.1 / 11 |
| `add.u64` | Yes | Yes | 1.2 / 12 |
| `exch/cas.b64` | Yes | No | 1.2 / 12 |
| `min/max.{u64,s64}`, `and/or/xor.b64` | Yes | Yes | 3.1 / 32 |
| `add.f32` | Yes | Yes | 2.0 / 20 |
| `add.f64` | Yes | Yes | 5.0 / 60 |
| `cas.b16` | Yes | No | 6.3 / 70 |
| `cas/exch.b128` | Yes | No | 8.3 / 90; explicit `.sys` requires PTX 8.4 |
| `add.noftz.f16x2` | Yes | Yes | 6.2 / 60 |
| `add.noftz.f16` | Yes | Yes | 6.3 / 70 |
| `add.noftz.{bf16,bf16x2}` | Yes | Yes | 7.8 / 90 |
| `v2/v4/v8.{f16,bf16}.{add,min,max}.noftz` | Yes | Yes | 8.1 / 90 |
| `v2/v4.{f16x2,bf16x2}.{add,min,max}.noftz` | Yes | Yes | 8.1 / 90 |
| `v2/v4.f32.add` | Yes | Yes | 8.1 / 90 |

Each scalar pair and vector operation/type cohort has one public variant; vector width is a typed modifier. Optional suffixes are independent: `atom`
accepts `.relaxed`, `.acquire`, `.release`, or `.acq_rel`; synchronous `red` accepts
`.relaxed` or `.release`. Both accept `.cta`, `.cluster`, `.gpu`, or `.sys`
scope. Omitted semantics and scope remain `MemoryConsistency::Omitted` and
`MemoryScope::None` in owned IR; their effective ISA defaults are relaxed and
GPU. Both qualifier orders used by the earlier explicit global forms remain
accepted. Explicit scope requires PTX 5.0 / SM 60, explicit semantics PTX 6.0 /
SM 70, and `.cluster` scope PTX 7.8 / SM 90. These floors apply independently,
including scope-only and semantics-only forms.

The written address qualifier is omitted (generic), `.global`, `.shared`,
`.shared::cta`, or `.shared::cluster`. `Atom::address_qualifier` and
`Red::address_qualifier` retain these five spellings as a typed
`AtomicAddressQualifier`, separately from the address operand's effective
state-space provenance. Generic addressing requires PTX 2.0 / SM 20 and a
known address must refer to global or shared memory. Ordinary `.shared` has a
PTX 1.2 / SM 12 floor; 64-bit shared `add` and atomic `exch/cas` require PTX
2.0 / SM 20. Written `::cta` requires PTX 7.8 / SM 30, and `::cluster` requires
PTX 7.8 / SM 90. An unqualified `.shared` suffix has effective `::cta` address
subspace, but retains a different written qualifier. Address subspace and
memory scope are independent; for example `atom.shared::cluster.cta.add.u32`
is accepted. Known address provenance must match an explicit global/shared
qualifier, and the checker revalidates owned IR after mutation.
The checker derives the allowed written qualifiers from each variant's
state-space modifier: synchronous vectors and async release reductions admit
generic/global, while shared-completion `red.async` admits generic or
`.shared::cluster`. Invalid enum values and out-of-domain mutations are rejected.
All `atom`/`red` bracketed address offsets, including the `red.async` barrier
address, use the signed 32-bit PTX source domain. The checker revalidates that
range in owned IR. Wider `mov` address relocations remain separate.

Eligible scalar and vector `atom` and `red` operations also accept
`.L2::cache_hint`. Scalar forms place it immediately before the type (after
`.noftz` for half and bfloat add); the ISA vector spelling places it before
`.vN.type`, as in `atom.global.add.noftz.L2::cache_hint.v2.f16`. The suffix requires a final 64-bit `cache_policy` register and
PTX 7.4 / SM 80. The owned IR keeps the written hint and selects a separate
typed operand layout for its policy. Cache hints allow explicit `.global` or
generic addressing. A known global address is accepted; an unknown-provenance
generic register address is accepted with the runtime obligation to point to
global memory. Explicit shared and known shared generic addresses are rejected.
`atom.cas` has no cache-hint form. A policy operand without the suffix, or the
suffix without its policy, is rejected.

Every scalar `atom` destination accepts either a compatible register or the `_` bit
bucket. The owned IR retains the bit bucket as an absent
`ResolvedRegisterOrSink::register_ref`; it remains an `Atom` instruction.
Integer and bitwise destinations otherwise use a compatible register.
Sources accept a compatible register or an integer immediate with ordinary
narrow conversion. `cas` takes compare and swap sources. Float `add` accepts
native `.f32`/`.f64` or equal-width `.b32`/`.b64` registers, decimal floating
literals, and `0f`/`0d` bit-pattern literals; integer literals are rejected.
Half and bfloat `add` require the written `.noftz` suffix. Scalar `.f16`
accepts `.f16` or `.b16` registers, and packed `.f16x2` accepts `.f16x2`
or `.b32`. The BF16 forms retain exact `.b16`/`.b32` bit containers.
The `.b16` CAS form accepts compatible same-width registers for its destination
and register sources; `.b128` CAS/exchange use exact `.b128` registers. CAS always has four
operands and no cache hint. Addresses require natural two-, four-, eight-, or
sixteen-byte alignment. Float addition rounds
to nearest even. Global `.f32` atomics flush subnormal inputs and results to
sign-preserving zero; shared `.f32` and `.f64` atomics do not. These last rules
are ISA behavior notes, not simulated execution.

Vector forms require global memory: explicit `.global` or generic addressing. A
known shared address is rejected; an unknown generic register address is
accepted with the runtime obligation to point to global memory. `atom` has
brace-enclosed destination and source vectors; `red` has a brace-enclosed
source vector. Atom destination and source vectors have the same exact arity.
The PTX syntax places the operation, optional `.noftz`, and optional
`.L2::cache_hint` before `.vN.type`, for example
`atom.global.add.noftz.L2::cache_hint.v2.f16`. The existing
`.vN.type.operation` spelling remains accepted.
`.f16` lanes accept `.b16`, `.f16`, `.u16`,
or `.s16` registers; `.f32` lanes accept the corresponding 32-bit register
types. `.bf16` lanes require `.b16`; packed `.f16x2` lanes accept `.f16x2`
or `.b32`, while `.bf16x2` lanes require `.b32`.
Within each vector, bit-type lanes are neutral, while integer and floating
lanes cannot mix. The destination and source vectors are checked independently.
Without declarations, standalone resolution retains unknown lane types;
declaration-bound module resolution checks the stated register-type domains.
A destination lane may be `_`, while source lanes must be registers and an
all-sink destination is invalid. Written lanes in a destination vector must
name distinct registers, including when parameterized declarations bind them;
read-only source vectors may repeat lanes. The
whole access requires vector length times element width alignment: for example
`v8.f16` needs 16 bytes. Atomicity applies to each scalar element, not to the
vector as one aggregate transaction. The half/bfloat vectors require `.noftz`;
`v2/v4.f32.add` does not accept it. Packed and `.f32` forms have no `v8` form.
A cache hint keeps the same final 64-bit policy layout and global-address rule.

`red.async` adds two disjoint typed modes under `Red::Async...` alternatives:

| Mode | Legal operation/type pairs | Form and target floor |
| --- | --- | --- |
| Shared completion | `inc/dec.u32`, `min/max.{u32,s32}`, `and/or/xor.b32`, `add.{u32,s32,u64}` | `red.async.relaxed.cluster{.shared::cluster}.mbarrier::complete_tx::bytes.{op}.{type} [a], b, [mbar]`; PTX 8.1 / SM 90 |
| Global release | `add.{u32,s32,u64,s64}` | `red.async{.mmio}.release.{gpu\|sys}{.global}.add.{type} [a], b`; PTX 8.7 / SM 100 |

The optional address suffix is retained independently as `Red::address_qualifier`:
omitted generic or `.shared::cluster` for shared completion, and omitted generic
or `.global` for global release. The destination `a` must use a register base,
optionally followed by a signed 32-bit offset. Direct symbol and immediate
bases are rejected for both the destination and mbarrier addresses. Known
shared-mode destination and mbarrier addresses must be shared; unknown generic
register addresses carry a runtime shared-cluster obligation. Known release-mode
destinations must be global; unknown generic register addresses carry a runtime
global obligation. Destinations require natural 4- or 8-byte alignment, and
the mbarrier address requires 8-byte alignment. `.mmio` is available only with
release `.sys`; release `.gpu` has no MMIO form. The shared completion suffix
is mandatory: although ISA Conditions prose describes an omitted default,
PTX 9.3 syntax and `ptxas` reject omission. The frontend cannot prove that the
shared destination and initialized barrier are in the same remote CTA; that is
a runtime obligation.

Other synchronous scalar operation/type pairs, including `.add.s64`, 64-bit `.inc/.dec`,
typed 64-bit bitwise suffixes, scalar float `.min/.max`, and `red.cas`/`red.exch`,
remain unsupported. Multimem forms are outside this coverage.

The C++ package version is 0.7.0. Consumers must rebuild against matching
installed headers and library. The 83 synchronous operation/type and vector-cohort alternatives replace the
former 92 legacy/`.relaxed.cta` alternatives. For example, both
`atom.global.add.u32` and `atom.relaxed.cta.global.add.u32` now use
`Atom::GlobalAddU32`; inspect its `semantics`, `scope`, and `state_space`
fields together with `Atom::address_qualifier` to recover the written form.
Non-CAS synchronous scalar and vector variants now expose `NoHintOperands` and `WithPolicyOperands`
through their `operands` field; the selected layout keeps the final policy
register distinct from the ordinary source. CAS fields remain direct. Asynchronous
reductions have separate `Red::AsyncShared...` and `Red::AsyncRelease...`
alternatives with three and two operands respectively; inspect the retained
address qualifier, completion, scope, and MMIO fields instead of assuming a
synchronous layout. The 16 asynchronous alternatives are appended under `Red`.
The Python wheel version is 0.1.0b3 because its packaged YAML and generated
model contract changed.
