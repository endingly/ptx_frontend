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
or `.b32`. BF16 register carriers remain `.b16`/`.b32` physical storage; the packed BF16x2 source has the logical format described below.
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
The [PTX ISA 9.3 fundamental-type rule](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fundamental-types)
is narrower for floating scalar operands; the vector `atom`/`red` sections do
not explicitly define lane declaration types. The integer-lane allowance here
is scoped to vector `.f16`/`.f32`: CUDA 13.3 `ptxas` V13.3.73 with
`-O0 -arch=sm_90` assembled all eight `atom`/`red` combinations of `.f16`
with `.u16`/`.s16` and `.f32` with `.u32`/`.s32` in the
[reproducible PTX source](../../../examples/atomic_vector_register_types.ptx).
This establishes assembler acceptance, not GPU execution behavior.
Among register lanes, bit types are neutral, while integer and floating
register families cannot mix; immediate lanes are excluded from that test. The destination and source vectors are checked independently.
Without declarations, standalone resolution retains unknown lane types;
declaration-bound module resolution checks the stated register-type domains.
A destination lane may be `_`, while source lanes may be admitted registers or floating immediates and an
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
| Shared completion | `inc/dec.u32`, `min/max.{u32,s32}`, `and/or/xor.b32`, `add.{u32,s32,u64}` | `red.async.relaxed.cluster{.shared::cluster}{.mbarrier::complete_tx::bytes}.{op}.{type} [a], b, [mbar]`; PTX 8.1 / SM 90 |
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
release `.sys`; release `.gpu` has no MMIO form.

For relaxed mode, the specific PTX Conditions clause supplies
`.mbarrier::complete_tx::bytes` when completion is omitted; it is also the only
permitted explicit mechanism. The third `[mbar]` operand remains required in
both forms. This specific default takes precedence over the schematic syntax
that prints completion as mandatory and the generic Qualifiers prose describing
end-CTA completion on omission. Release mode remains a separate two-operand
form and forbids explicit completion. Historical assembler rejection of relaxed
omission is supplemental tool divergence, rather than the frontend grammar.
The frontend cannot prove that the shared destination and initialized barrier
are in the same remote CTA, or execute reduction/transaction/synchronization
semantics; those remain runtime obligations.

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

The twelve shared classes (`RedAsyncSharedIncU32`, `RedAsyncSharedDecU32`,
`RedAsyncSharedMinU32`, `RedAsyncSharedMinS32`, `RedAsyncSharedMaxU32`,
`RedAsyncSharedMaxS32`, `RedAsyncSharedAndB32`, `RedAsyncSharedOrB32`,
`RedAsyncSharedXorB32`, `RedAsyncSharedAddU32`, `RedAsyncSharedAddS32`,
`RedAsyncSharedAddU64`) now expose instance `WithLocs<bool> completion` instead
of a static constant. Read `object.completion.value` for the effective mechanism
(always true), and `.locs` for written presence: omission has empty locations,
explicit spelling retains its token. False is outside this singleton domain,
even without a token. Ordinary two-valued optional flags retain their existing
value/token consistency rule. Generated C++ layouts change and require consumer
rebuilding; the four release classes and all three shared operand payloads stay
unchanged. Clearing a true completion's locations is a valid current IR edit;
association with a supplied AST independently checks retained source identity.

[Default tests](../../../submod/resolved_ir/test/test_red_async_default.cpp)
cover all 48 shared modifier products and 64 release candidates (24 allowed,
40 rejected), target/version boundaries, singleton-domain mutations and actual
AST-dead copies/clones. The [installed consumer](../../../examples/resolved_ir_consumer/main.cpp)
reads all twelve migrated fields and retains a release control without source
storage. These checks do not prove initialized barriers or runtime completion.

### Atomic vector value sources

The 26 existing `AtomVector*`/`RedVector*` classes expose `src` as
`WithLocs<ResolvedValueVector>` in both preserved layouts. Consumers must rebuild
against matching headers/libraries and inspect each `RegOrImm` lane with
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`. Destinations
remain register vectors; optional B64 cache policies and exact class identities,
field names, arity/type domains, qualifiers and target gates retain their contracts.
The source clauses remain brace-only: no whole named vector or source sink grant.

All 128 type/arity/layout branches accept floating immediate lanes, mixed with
existing physical register carriers where appropriate. The 80 F16/BF16/F32 numeric
branches reuse the existing logical converter; the 48 F16x2/BF16x2 branches accept
bare unsigned direct `0f` exact32 words. Packed words retain every bit, including
encoded sign and NaN payloads, with no quantization, splat or quieting. Integer
literals/results remain excluded for all floating source lanes. Packed decimal,
F64, unary/wrapped/operator or pair constructors remain reference-qualified
unavailable; typed current-word capacity is not a numeric source constructor.

Logical immediate type is separate from physical register type. Explicit Element
allowlists retain B16/F16/U16/S16 for F16, B16 for BF16, B32/F16x2 for F16x2,
B32 for BF16x2 and B32/F32/U32/S32 for F32. Membership cannot waive physical lane
width, General/scalar class, intrinsic declaration eligibility, component, owner
or provenance checks. Without an explicit list, existing compatibility applies.
Bit register lanes are neutral; integer and floating register families cannot mix,
and immediate Float lanes do not participate in that register-family test.
Scalar operand compatibility is unchanged.

Canonical current logical Float lanes are checked for in-width bits, absent
integer origin (optional zero still counts as present), and clean negative metadata.
This does not decide current Bit16/Bit32 arms for other logical formats from a
scalar F16 rule; those cases need their own format-specific evidence. Coherent canonical edits need no erased floating lexical history.
Source association separately checks provided AST identity. Actual ValueVector
owner pointers and all shape/type/register/immediate/bit/range mirrors must agree;
synchronous observers borrow that actual owner only during traversal. Real AST
release, copy/assignment/function copy/clone preserve ownership. Register sinks,
whole containers and stale mirrors cannot substitute for read-only scalar lanes.
These frontend contracts do not execute atomic arithmetic, ordering, cache effects
or runtime global-memory behavior.


### Scalar F16 atomic source members

`AtomGlobalAddNoftzF16::src` and `RedGlobalAddNoftzF16::src` use
`WithLocs<RegOrImm>` in both the ordinary and cache-policy layouts. Rebuild
consumers and select `ResolvedRegisterRef` or `ResolvedImmediate` with `get_if`
or `visit`; the existing class identities, field locations and layout order do
not change. The cache policy remains a separate B64 register operand, and the
atomic destination retains its register-or-sink contract.

Only these four scalar F16 source roles inherit floating constant use. They keep
logical F16 and SameWidth: actual B16/F16 general registers and their existing
legal components remain supported. Numeric floating expressions use the common
binary16 converter (`1.5` produces F16 word `0x3e00`); an integer-origin source
is not a floating permission. Current canonical F16 words need no floating
lexical history, but require low16 bits, no integer-origin cache and no floating
negative metadata. IEEE sign and NaN payload bits remain data. This is scalar
numeric conversion, not a packed raw-word, pair or splat constructor.

The existing same-width Bit compatibility rule also accepts a canonical current
B16 word such as `0x3e00` without integer-origin or negative metadata. This is
inherited current-data compatibility, independent of the original AST spelling;
it adds no Bit literal-source permission and does not change the F16 producer or
its integer-source exclusion. Other Bit flag and packed-format rules are unchanged.

The ordinary form keeps PTX6.3/SM70 availability; the global cache-policy layout
keeps PTX7.4/SM80 availability. Address, alignment, ordering, scope, `noftz`,
source identity and existing AvailableContext/RequireCompleteContext rules remain
independent. Packed F16x2, BF16/BF16x2, atomic vectors, compare/swap and all other
source permissions remain unchanged. Frontend checks do not certify GPU runtime
behavior or opaque cache-policy contents.

### Scalar BF16 atomic source members

`AtomGlobalAddNoftzBf16::src` and `RedGlobalAddNoftzBf16::src` use
`WithLocs<RegOrImm>` in both `no_hint` and `with_policy` layouts. Consumers
must rebuild with matching headers/libraries and inspect `.value` through
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`.
The two existing class identities and member names remain. Atomic destinations
remain B16/Exact register-or-sink values; cache policies remain B64 registers.
Addresses, alignment, qualifiers, modifier order and target floors are independent
of this source permission. Scalar F16, packed and vector atomic cohorts retain
their separately defined contracts.

The four source keys use logical BF16 and SameWidth with the existing converter.
Numeric Float sources `1.5`, `1.0+0.5`, `0d3ff8000000000000` and
`0f3fc00000` produce the existing BF16 `0x3fc0` golden. Actual register sources
still require the ordinary scalar B16 declaration under intrinsic type and
bit-compatibility rules; SameWidth does not admit F16/U16/S16, standalone BF16,
special registers or whole vectors. Integer-origin literals/expressions are
excluded; no integer-immediate policy or new conversion path is introduced.

The producer stores logical BF16. Logical BF16 current words require low sixteen
bits and clean origin/negative flags; this grant does not change pre-existing
current Bit-carrier compatibility. Floating lexical history is unnecessary, so a coherent canonical word can
validate after AST destruction, copy, assignment and clone. Stale high bits,
type/origin/class/provenance or range facts remain errors. Source association
checks the supplied AST against retained syntax independently of current-word
retargeting. Synchronous observers borrow the actual member and ranges only for
the traversal. These scalar numeric rules add no packed pair/splat/truncation
permission. The frontend validates representation and availability; it does not
execute an atomic update, memory ordering, cache behavior or BF16 arithmetic.

Current family references retain a real declaration/member identity: erasing a
required family index or contradicting its current spelling is invalid. Coherent
current spelling/member retargets and erasing only the numbered convenience
cache remain valid; validation does not authenticate old operand spelling.
Known reference contradictions are checked under partial context, while the
existing whole-instruction deferral remains. Required delivery evidence includes
actual generated views and real borrowed callbacks, not a manual projection.

## Packed32 source words and member migration

The scalar `atom` and `red` `add.noftz.f16x2` / `add.noftz.bf16x2` source slots accept compatible registers or a **bare unsigned direct `0f` word** in both `no_hint` and `with_policy` layouts. The eight source keys share four existing public `src` members. For example, `0f3e003e00` supplies the complete F16x2 word `0x3e003e00`, and `0f3fc03fc0` supplies the complete BF16x2 word `0x3fc03fc0`. All 32 bits are data, including encoded signs and NaNs: the frontend does not split, splat, quantize or quiet the word.

This grant reuses the existing packed-literal helper. Integer literals and Integer-result expressions are incompatible with logical Float sources. Decimal numbers, numeric pairs/splats, unary `+`/`-`, parenthesized/wrapped/expression forms, new `0h` constructors and truncation from `0d` are not packed source constructors. Numeric packed construction remains **Source_N_A / reference-qualified**; it is not a positive producer or a reason to add a converter. The source converter produces the selected logical F16x2/BF16x2 type. Canonical current-word positives use that selected type, only low32 bits, no `integer_source_bits`, and false floating-negative metadata, without retained floating lexical history. Existing current Bit-carrier compatibility remains unchanged; these examples do not narrow the inherited `SameWidth` policy or establish a new B32 rule.

The BF16x2 **source** is logically BF16x2 with `SameWidth`, replacing its previous B32/`Exact` projection. Its lawful physical register carrier remains B32: declaring a standalone `.bf16x2` register remains invalid. F16x2 sources retain their existing native F16x2/B32 carriers and logical `SameWidth` policy. General class, intrinsic declaration validity, scalar/component provenance, exact physical width and SymbolId binding remain checked. This is not permission for F32/U32/S32, whole vectors or predicate carriers. BF16x2 atomic destinations remain B32/`Exact`; the additional cache-policy operand remains a B64 register. Address qualifiers/alignment, modifier order, availability and scalar-half/vector/CAS/EXCH cohorts are unchanged.

`AtomGlobalAddNoftzF16x2`, `AtomGlobalAddNoftzBf16x2`, `RedGlobalAddNoftzF16x2` and `RedGlobalAddNoftzBf16x2` keep their names and identities, but `src` changes from `WithLocs<ResolvedRegisterRef>` to `WithLocs<RegOrImm>`. Consumers must rebuild with matching generated headers and libraries and use `std::get_if<ResolvedRegisterRef>` / `std::get_if<ResolvedImmediate>` on `.value`. See the [IR migration](../resolved_ir_design.md#packed32-atomic-source-members). Views and observer callbacks borrow the actual owner only synchronously and must be reacquired after copying or cloning. Source association remains separate from current-word validity. These frontend checks do not execute atomics or certify runtime memory, ordering or floating results.
