# C++ Resolved IR Design

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#resolved-ir-design). This page retains technical reference material and evidence.

## Integrated vector data movement

[SDK support: Integrated vector data movement](../sdk_support.md#resolved-ir-design--integrated-vector-data-movement).

## Explicit vector MOV

[SDK support: Explicit vector MOV](../sdk_support.md#resolved-ir-design--explicit-vector-mov).

Whole hardware MOV retains intrinsic register identity and native metadata. The
checker applies instruction-local typed compatibility descriptors without
rewriting the owned source; source ranges and target availability remain checked.

## Named array addresses

[SDK support: Named array addresses](../sdk_support.md#resolved-ir-design--named-array-addresses).

## Current validation limits

[SDK support: Current validation limits](../sdk_support.md#resolved-ir-design--current-validation-limits).

## Status and scope

[SDK support: Status and scope](../sdk_support.md#resolved-ir-design--status-and-scope).

`ResolvedFunction::body` is a vector of `unique_ptr<Instruction>`. Each modeled
semantic form is a distinct final class with an immutable kind. There is no
opcode owner wrapper or instruction union in the active API; selected small
operand-layout payloads may still use typed variants. Exact-class
queries use `dynamic_cast` to a final class, not opcode identity alone; the
module implementation has a constrained private helper for these casts. Copying a
`ResolvedFunction` clones every non-null instruction and all metadata, while
preserving null slots so validation can reject them. Borrows survive vector
growth and function moves, and end with instruction destruction or replacement.
Internal `IReferenceObserver` callbacks borrow typed foundation values and
location spans synchronously; validation forbids reentrant payload mutation.
Source ranges, symbol identity, Call literal normalization, and the distinct
resolution-only versus final-validation guarantees remain in force.

The active public entry point is
`<ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>`.
It aggregates handwritten foundation and module containers with generated
final semantic-form classes. Narrow per-opcode headers live under
`model/<category>/<opcode>.gen.hpp`. Model headers retain owned data and
read-only descriptors, complete owned diagnostics, and selector declarations
without requiring a complete Syntax AST. Include parsing headers when calling
a standalone resolver, and `ptx_resolved_ir_resolution.hpp` for module or
bound-context resolution. Resolution is
exposed through `ptx_resolved_ir_resolution.hpp`, and checking through virtual
`Instruction::check` plus the handwritten support header
`ptx_resolved_ir_checker_support.hpp`.

This document describes the implemented Resolved PTX IR, not a future CFG,
SSA, or backend IR. The frontend's core flow is:

```text
PTX source -> Token stream -> Syntax AST -> symbol binding -> Resolved IR -> checker
```

`Opcode` and `InstructionKind` have opaque declarations in the common base
header. Each final form still exposes typed `Form::opcode` and `Form::kind`
constants. The broad generated aggregate includes the complete named
enumerators, with numeric assignments centralized in opcode-local
`identity/<category>/<opcode>.gen.hpp` headers. Model and form-shard headers
reference their named typed constants. A narrow consumer that spells
`Opcode::Name` or
`InstructionKind::Name` includes `ptx_instruction_catalogue.gen.hpp`.
Their 8/8/16-bit values use a fixed category prefix and normalized opcode/form
ordinals. Insertion may renumber later members within the affected category
or opcode; integer values and generated C++ layout are not a stable binary ABI.

The public layer also provides an opcode-independent boundary:

```cpp
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic>
resolveInstruction(const syntax_ast::AstInstruction& ast);

std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveModule(const syntax_ast::AstModule& ast);
```

The [entry-point success contracts](../sdk_support.md#resolved-ir-design--status-and-scope)
are maintained in the SDK support contract.

`ResolvedFunction::declaration_scope` identifies a declaration occurrence:
a prototype and definition may share a `SymbolId` but have different scopes.
Binding retains the declaration range and exposes `functionScope(range)`;
resolution, storage collection, and declaration checking use that association
instead of pairing independent traversal indices. `instruction_ranges` and
`instruction_opcodes` own one entry per flattened instruction. `source_target`
and `source_version` retain the original source context, and `source_identity`
owns a location-independent syntax identity used to check correspondence.
`ResolvedModule::source_identity` additionally covers module declarations,
aliases, and address size, so changing a global's type or initializer cannot
silently reuse instruction bindings from another source.

Functions own their normalized signature, linkage and canonical/alias identity,
`.noreturn` and ABI-preservation contracts, normalized numeric resource values,
cluster dimensions (including inferred trailing dimensions), `.blocksareclusters`,
and language value. Entry resources are source launch contracts, not occupancy
calculations or physical allocations. Function-local `.branchtargets` owns its
ordered expanded bound labels (so `L<2>` is `L0`, `L1`, not two `L` entries);
explicit repeated labels retain separate logical entries. `.calltargets` owns ordered
bound/canonical functions plus their common signature; `.callprototype` owns
its signature and ABI/noreturn suffixes. These records and their source ranges
remain valid after AST destruction.

Module call literals are formal-driven `ResolvedImmediate` values after a
successful direct, alias, or metadata-backed call check. A standalone
instruction without a module call contract may retain a `ResolvedCallLiteral`
with no value; consumers must not guess a type. A declared external module
call still retains its signature and formal-typed literals; actual linking or
relocation remains deferred. `base::DeclarationStateSpace` and
`base::LiteralCategory` name the
semantic values exposed by resolved/binding data. The legacy `AstStateSpace`
and `AstImmediateKind` aliases remain source-compatible names, not a requirement
to retain a syntax AST.

Instruction ranges/opcodes and all owned records are semantic provenance. The
frontend retains `.language` and function ABI/resource contracts; `.file`,
`.loc`, `.section`, and `.pragma` remain syntax/debug or advisory metadata and
are not executable resolved payloads. The owned symbol table retains `.file` declaration payloads for debug identity validation. There is no promise of stable generated
C++ struct layout or binary ABI. Other raw module directives still require an AST;
the owned model is a semantic handoff for the documented subset, not a complete
source serialization contract.

`ResolveDiagnostic` owns its message and source ranges. Module resolution
preserves the originating `binding_kind`, `declaration_kind`, or `checker_kind`,
as well as the primary `range` and any `previous_range` supplied by binding or
declaration semantics. `stage()` derives `Binding`, `DeclarationSemantics`, or
`Checking` from that typed category. Exactly one category is populated for an
imported diagnostic; native resolver errors keep all three empty and report
`Resolution`. Native resolver errors do not yet have a finer-grained code.
Consumers can inspect imported error categories and related locations without
parsing the human-readable message, even after source text and AST destruction.
Diagnostic ordering and existing early-return boundaries are unchanged. The
existing aggregate fields remain in order, with new optional categories appended.

`ResolvedModule::pragmas` owns source-traversal-ordered `ResolvedPragma` values:
`placement` (`Module`, `EntryHeader`, or `Statement`), `scope_id`, raw owning
`AstSyntax` string leaves and the whole directive `range`. Module placement uses
module scope; header placement uses the actual entry declaration occurrence;
statement scopes reach an actual function definition through its lexical blocks,
not a canonical symbol's first prototype scope. Pragma strings create no names
and have no frontend backend/loop/execution interpretation. Empty quoted content
is valid; a retained empty list, unquoted leaf, invalid scope/placement or invalid
whole/leaf range is not. Known current and supplied module ranges each contain
their own records; an all-zero owner range defers containment. Within-list order is checked; records are not sorted or
joined to historical text. Grouped spellings/ranges do not replace exact CST
comma/trivia serialization.

Module resolution additionally performs direct and metadata-backed indirect
call ABI and call-context work
that cannot live in the generated single-instruction checker: it obtains the
canonical prototype/definition signature, checks return/input actuals and
formal-typed literals, and enforces function-local `.param` qualification,
predication, and staging adjacency. The generated checker remains responsible
for one resolved instruction and target-aware descriptor rules.

`<ptx_frontend/semantic/ptx_function_contract.hpp>` exposes the canonical
function-signature contract without a Syntax AST dependency. Its parameter
contracts use semantic state-space and pointer-space enums, `ScalarType` and a
typed vector shape, retained invalid spelling for diagnostics, and optional
tagged numeric values: omitted, validated constants, or invalid structural
keys. Direct-call ABI validation consumes those normalized values without
reparsing alignment text or the former array-extent string protocol; invalid
structural data is never silently replaced by a default.

Parameter records also retain fundamental `.f16x2` and legal `.param` v2/v4 shapes,
with lane-aware extents, natural vector alignment and AST-free padded entry limits.
The [parameter declaration contract](parameter_declarations.md) distinguishes packed
scalar dtype from explicit vector lanes and the numeric packed-literal reference gap.

## Named memory-vector sources

[SDK support: Named memory-vector sources](../sdk_support.md#resolved-ir-design--named-memory-vector-sources).

## Locations and primitive values

[SDK support: Locations and primitive values](../sdk_support.md#resolved-ir-design--locations-and-primitive-values).

Every independently diagnosable resolved value uses:

```cpp
template <typename T>
struct WithLocs {
  T value;
  std::vector<SourceRange> locs;
};
```

`locs` can associate one semantic value with several source fragments; an empty
list denotes no direct source location, such as a compile-time fixed modifier
or an instance value injected from an optional modifier's YAML `default`. The
latter remains a `WithLocs<T>`: `value` holds the semantic default and empty
`locs` means it was not written explicitly.
Modifier primitives include `bool`, `ScalarType`, and `RoundingMode`; the latter
turns `.rn/.rz/.rm/.rp` into statically checkable values instead of runtime
strings. Operand primitives include `ResolvedRegisterRef`, `ResolvedImmediate`,
`ResolvedPredicate`, `ResolvedBranchTarget`, `ResolvedSpecialRegisterRef`,
`ResolvedFunctionRef`, `ResolvedSymbolRef`, `ResolvedAddress`, `ResolvedMovSource`, and `RegOrImm`. A `ResolvedImmediate` stores use-width bits
and `ScalarType`. Integer forms also retain evaluated 64-bit source bits and
numerical signed-negativity, so fixed-control checks never reinterpret literal
text or trust a narrowed value.
When original integer source bits are present, a true negative flag requires bit63
of that original word to be set, after use-width bits are checked against the
source. A false flag with bit63 set can represent an unsigned source. A negative
original value may narrow to positive use-width bits; neither the destination
type nor the narrowed sign reconstructs source signedness. Cache-absent scalar
legacy inputs retain their existing behavior.

Owned constant-expression operands are evaluated through the shared semantic
evaluator before conversion at their numeric use site. Integer source bits and
signedness survive folding; floating expressions evaluate in f64 and final f32
narrowing uses the existing round-to-nearest, ties-to-even bit conversion.
Operand-specific narrowing, strict representability, predicate truth and fixed
controls apply to evaluated values. Symbolic/deferred expressions cannot supply
instruction numeric operands. Call expression arguments retain an owned typed
numeric source in `ResolvedCallLiteral::source_value` until a formal supplies
the destination type; owned-module validation rechecks that conversion after
the AST is released. Source identity retains the expression operation tree in
the CST/AST boundary; Resolved IR requires no generic expression tree.

Public-IR revalidation checks execution predicates independently of the selected
opcode variant and operand layout, including operandless instructions. A known
non-predicate register class, non-`.pred` declaration type, or vector shape is
invalid; missing standalone declaration metadata remains unknown. Owned-module
validation additionally requires the guard's identity to name an actual scalar
`.reg .pred` declaration in the function's symbol table, rather than trusting
cached register metadata. This includes legal register-valued function formals
and parameterized predicate-register members. Plain and negated guards share
the same contract. Diagnostics use the guard's retained location when present,
otherwise the instruction range; neither check requires the original source or
syntax AST to remain alive.

A single scalar variant carries a dynamic type modifier covering
`.b16/.u16/.s16`, `.b32/.u32/.s32/.f32`, and `.b64/.u64/.s64/.f64`. The checker applies PTX
fundamental-type compatibility: a same-width bit type agrees with any
fundamental type, signed and unsigned integers agree, and integer/float mixes
remain invalid. The `.f64` value additionally carries its SM 13 requirement.

Predicate constants, logical `!`, and WGMMA scale-d numeric controls require an
integer result source before truth or range conversion. A `B64` conversion
carrier does not establish integer origin: decimal/exact-F64 literals and
floating-valued expressions remain invalid in these roles, even with payload
zero or one. Floating comparisons that return an integer, and `!` of that
integer result, remain valid. Equal-width floating `B64` bit-data operands keep
their separate compatibility rule. Owned predicate constants contain only a
canonical Boolean; validation does not reconstruct source syntax erased by
normalization.

For ordinary coherent `ld`, `ld.global.nc`, and `st`, `.L2::cache_hint` may
appear with or without a trailing 64-bit register `cache_policy`. A policy
without the hint remains invalid. The existing exact form classes and
`InstructionKind` identities are unchanged: their original policy-present
`default` layout remains index 0, and `without_policy` selects index 1.
The generated public `cache_policy` member is now
`std::optional<WithLocs<ResolvedRegisterRef>>`; consumers access a present
policy through `cache_policy->value`. The fixed `cache_hint` field remains
true for both layouts. Both retain the same payload, address, modifier,
PTX-version, and target restrictions, including modern-vector gates.
The installed example in `examples/memory_cache_hint_consumer` exercises both
layouts for scalar/vector coherent loads, noncoherent loads, and stores after
source text and AST destruction.

## Reading the generated C++ code

Start with one instruction, such as `mov.u32 %r0, %r1;`. Its **opcode** is
`mov`; its selected **semantic form** is `Scalar`, represented by the final
class `MovScalar`. The other current MOV forms are `MovB128PackUnpack`,
`MovV4U32`, and `MovPred`. A form can still have several **operand layouts**:
`MovScalar`, for example, supports scalar and pack/unpack arrangements. The
layout determines which operand members are populated. Opcode identity, form
identity, and the form-local `operand_layout` tag belong to different numbering
domains; none is an interchangeable array index.

`mov.u32` and `mov.f32` can both be `MovScalar`: their types are values in the
dynamic `type` field, not a separate class for every modifier spelling.

Keep three things separate while reading: the final IR object owns this
instruction's values and locations; static arrays hold shared read-only rules
and indexes; selection results and field scratch hold intermediate state for
one resolve call. The call flow below describes C++ parsing, conversion, and
checking, not execution of the PTX instruction.

The result you keep is a final-class object. `MovScalar` owns its resolved
`type`, selected operand values, and their `WithLocs` source locations, together
with inherited state such as the execution predicate. Its generated
`instruction_kind()` returns the exact form identity, `clone()` copies the
owned object, `check()` checks it through the checker descriptors, and
`visit_references()` lends its references to an observer for that call.
`static constexpr` members such as `kind`, `opcode`, and form-specific constants
are shared facts, not a separate copy of metadata in every instruction.
Opcode identity, the opcode-local form ordinal, public `InstructionKind`, and
the form-local layout tag are separate domains. Scalar's local ordinal is 0,
while `MovScalar::kind` is its public form identity; a layout tag of 0 names
the first layout within that form, not the first form of MOV.

### Find the files for one opcode

In the following map, `G` means `<build>/submod/resolved_ir/generated`, and
`P` means `G/public/ptx_frontend/resolved_ir`. These are build outputs; edit the
generator or its inputs rather than editing the generated files.

| File under `G` or `P` | What to read it for |
| --- | --- |
| `P/identity/data_movement/mov.gen.hpp` | Typed MOV opcode/form identity constants, without the concrete classes. |
| `P/ptx_instruction_base.gen.hpp` | Shared generated `Instruction` base interface and identity declarations. The handwritten `ptx_instruction_base.hpp` is its include wrapper. |
| `P/model/data_movement/mov.gen.hpp` | Public final-class members and operations, descriptor getters, and MOV resolver declarations. Large families also have `*_forms_NNN.gen.hpp` declaration shards. |
| `P/ptx_resolved_ir.gen.hpp` | Broad model umbrella that includes the opcode model headers. |
| `P/ptx_instruction_catalogue.gen.hpp` | Broad identity catalogue, including named `Opcode`/`InstructionKind` enumerators; it does not contain all concrete form classes. |
| `G/private/resolved_ir_data_movement_mov.gen.cpp` | MOV descriptor storage, method definitions, private selection tables, and `resolveMov`. This is the main file to follow below. |
| `G/private/resolved_ir_matrix_wgmma_{descriptors,methods}_NNN.gen.cpp` | Large-family descriptor/method shards. WGMMA construction helpers also live in its method shards. |
| `G/private/resolved_ir_dispatch.gen.cpp` | Global opcode dispatch and the `resolveInstruction` entry points. |

Generated files use shared handwritten types and algorithms. `WithLocs`, the
resolved value carriers, and `ResolvedModule` come from the handwritten
foundation/module layers. The descriptor types are handwritten too, as are
`ModifierCandidateRow`, `ModifierCandidatePlan`, `select_indexed_variant`, and
`resolve_selected_fields`. The generator supplies their concrete storage,
arrays, and calls. `Instruction` is a shared base emitted by the generator;
its presence beside handwritten types does not make every supporting type
generated. Auxiliary domain lookup code is explained in the relevant family
documentation; it is not necessary to trace one MOV resolution.

### Read storage as shared metadata

Near the beginning of MOV's source, three storage structs organize the facts:

| Generated storage and getter | Question answered |
| --- | --- |
| `MovDescriptorStorage` → `mov_syntax_descriptor()` | Which modifier slots/spellings and operand shapes/layouts can express this form? |
| `MovResolvedDescriptorStorage` → `mov_resolved_descriptor()` | Which resolved fields and value kinds receive those modifiers/operands, and through which bindings? |
| `MovCheckerDescriptorStorage` → `mov_checker_descriptor()` | Which availability, type-compatibility, and other legality constraints must the selected object satisfy? |

Within them, `variants` follows canonical form order; the modifier, layout,
binding, and constraint arrays describe those forms. Descriptor `std::span`
fields view static-lifetime arrays, and getters return stable references to
those catalogs. These arrays describe all instructions of the family; they
are not the values of the current instruction. Runtime code reads compiled
C++ metadata rather than running a YAML interpreter. The later
[three-descriptor section](#three-descriptors) details their contract boundaries.

Within one emitted resolved storage block, complete equal operand-binding
sequences share one static array and its arrays of vector arities, register
types, and address spaces. The generator collects these groups first and emits their storage
before the independent form/layout fields and references. Sharing stays within
that block, including a single shard when sharded. Each form/layout keeps its
own identity, fields, and checker/diagnostic associations; equal binding-array
pointers do not identify equal semantic forms.

### Decode the private selection tables

The selection block near the end of the opcode source is an inexpensive way to
exclude impossible forms before exact matching. Its `SelectedFormOrdinal` enum
names the zero-based canonical positions for this opcode. `selected_kinds`
maps each position to the corresponding final class's typed `kind`; the nearby
`static_assert`s check those identities against the generator context. Public
kind values are not obtained by treating the local ordinal as a kind.

`candidate_rows` is sorted by `spelling`. A row's `known` flag only records
whether the legacy whole-opcode spelling check recognizes it, including
spellings mentioned in canonical `Absent` slots. It does **not** say that the
spelling is accepted in this form or source position. Candidate membership
instead comes from `candidate_words`: for `F` forms, each row occupies
`W = ceil(F / 64)` consecutive 64-bit words. Row `r` starts at `r * W`; form
`j` uses word `r * W + j / 64`, bit `j % 64`. The bit is set if that form can
consume the spelling in a non-absent canonical or alias slot. This union
intentionally forgets order, multiplicity, and required-slot completeness.
`candidate_plan` is a view of the two static arrays, not another owned table.

For the current MOV snapshot, the ordinals are `0 = Scalar`,
`1 = B128PackUnpack`, `2 = V4U32`, and `3 = Pred`, so `W = 1`. Some rows are:

| Spelling | `known` | Mask, with bit 0 on the right | Remaining form candidates |
| --- | --- | --- | --- |
| `.u32` | `true` | `0x5` = `0101` | Scalar, V4U32 |
| `.v4` | `true` | `0x4` = `0100` | V4U32 |
| `.pred` | `true` | `0x8` = `1000` | Pred |
| `.b128` | `true` | `0x2` = `0010` | B128PackUnpack |

This table joins a row with its mask for readability; the actual C++ arrays
store rows and words separately. For `.v4.u32`, intersection yields
`0x4 & 0x5 = 0x4`. That leaves one candidate, not a completed validity check.
`.u32.v4` produces the same intersection, but does not become legal: the exact
matcher still checks written order, required/optional/absent slots, repetitions,
and declared order aliases. All known-spelling checks happen before matching;
failure or ambiguity falls back to the full general selector for its original
diagnostic and source range. With no modifiers, all forms start as candidates.
When several exact modifier matches remain, the existing arity-then-shape
disambiguation applies; a single modifier match proceeds to field resolution
even if its operands are malformed.

WGMMA uses the same scheme at a larger scale: the current snapshot has 2,151
forms, 166 spelling rows, `W = 34`, and 5,644 words. Its zero-based row 9 is
`.fence`, whose mask contains canonical form bit 2,148. These numbers are
snapshot examples and may change with the specification; they are not an ABI
to depend on. Physical shards do not restart the full-opcode ordinal. WGMMA's
getters assemble static contiguous catalog rows in canonical order, while its
selection tables and thin resolver remain in `resolved_ir_matrix_wgmma.gen.cpp`.
The final construction call goes to the appropriate `wgmma_construct_NNN`
helper in a method shard.

### Follow one runtime resolution

For the scalar example, read the calls in this order:

```text
AST for mov.u32 %r0, %r1;
  -> resolveInstruction -> global resolve_instruction opcode dispatch
  -> resolveMov
  -> select_form
       stack candidates array + static candidate_plan exclude impossible forms
       select_indexed_variant runs the original exact matcher/disambiguation
       selection errors use the full general selector
  -> SelectedForm { ordinal, kind, syntax reference, resolved reference }
  -> resolve_selected_fields
       choose the layout using ResolveContext (including texture mode)
       bind the winning modifiers again; populate existing string-keyed field maps
  -> construct MovScalar and populate its resolved values/locations
  -> later checker use through check(...)
```

For this `.u32` source, the mask first leaves Scalar and V4U32; exact matching
rejects V4U32 because its required vector modifier (`.v2` or `.v4`) is missing,
so `SelectedForm` refers to Scalar.
The two identifiers select its `scalar` layout (current layout tag 0).
Field resolution converts `.u32` to `ScalarType::U32` and resolves `%r0`/`%r1`
through the `dst`/`src` bindings, retaining their locations. Construction fills
`MovScalar::type`, `dst_register`, and `src_mov_source`; the pack/unpack-only
members remain disengaged. That object, not the selection table or scratch
maps, is the value returned through `std::unique_ptr<Instruction>`.

`select_form` creates the opcode-sized stack `candidates` scratch array and
returns `SelectedForm`. Its `ordinal` identifies the local form; `kind` is its
public typed identity; `syntax` and `resolved` borrow that form's static
catalog rows. This temporary object carries no final operand payload and is
not the IR object returned to the caller. `resolve{Opcode}` functions such as
`resolveMov` connect selection, shared field resolution, and final-class
construction. `resolve_selected_fields` returns temporary
`ResolvedInstructionFields`, whose modifier/operand `ResolvedFieldMap`s keep
the existing string-keyed scratch representation until construction populates
the final object's typed members. The public `select_variant_name` remains the
general compatibility path for caller-provided descriptors; the generated
resolver explicitly passes its private plan to use indexed selection.

`resolveInstruction` alone is not target-aware module validation. Declaration
binding, module-wide obligations, and checker calls belong to the surrounding
module pipeline described elsewhere in this document. The
[resolution protocol](#resolution-protocol) gives the detailed error and
ownership rules for the steps above.

To trace a generated block back to its source, start with
[`selection_index.py`](../../python/src/ptx_frontend/code_gen/selection_index.py)
for spelling masks,
[`render_models/resolved_source.py`](../../python/src/ptx_frontend/code_gen/render_models/resolved_source.py)
for typed selection and construction facts, and
[`templates/resolved/resolve.j2`](../../python/src/ptx_frontend/code_gen/templates/resolved/resolve.j2)
for selection storage, `SelectedForm`, and resolver calls.
[`resolved_model.py`](../../python/src/ptx_frontend/code_gen/emit/resolved_model.py)
emits public model/identity surfaces, while
[`resolved_dispatch.py`](../../python/src/ptx_frontend/code_gen/emit/resolved_dispatch.py)
emits global dispatch. The shared handwritten implementation is in
[`ptx_resolved_ir_modifiers.cpp`](../../submod/resolved_ir/src/ptx_resolved_ir_modifiers.cpp),
[`ptx_resolved_ir_selection_private.hpp`](../../submod/resolved_ir/src/ptx_resolved_ir_selection_private.hpp),
and [`ptx_resolved_ir.cpp`](../../submod/resolved_ir/src/ptx_resolved_ir.cpp).

[`resolved_checks.py`](../../python/src/ptx_frontend/code_gen/emit/resolved_checks.py)
adapts final-form checker rendering to typed check models and fixed macros;
its pure builder owns ordered layout/family/cross-rule checks and the named-array
address policy shared with resolver preparation. `resolved_source` composes nested
source models for fixed resolver, construction, descriptor and reference-visit templates.
Checker views receive the final class qualifier and payload member expression
directly; callers guard optional payloads before use, while diagnostic field IDs
continue to come from metadata.

## Opcode-generated structures

Every semantic form generates one final class derived from `Instruction`. An
opcode has a narrow generated header with all of its forms, descriptor access,
and a per-opcode resolver. For example:

```cpp
class AddIntegerNoSat final : public Instruction {
public:
  ResolvedOperandLayoutTag operand_layout;
  WithLocs<ScalarType> type;
  WithLocs<ResolvedRegisterRef> dst;
  WithLocs<RegOrImm> src1;
  WithLocs<RegOrImm> src2;
};
```

The optional `execution_predicate` and virtual operations are inherited from
`Instruction`.

A fixed modifier is not mutable per-instance state. In the merged `AddSat`,
`.sat` is fixed while the type is an allowed value with its own availability,
so it generates:

```cpp
inline static constexpr bool saturate = true;
WithLocs<ScalarType> type;
```

This avoids re-inferring the fixed fact while retaining the selected type and
its source location.

One variant may have multiple named modifier slots of the same value kind. The
mixed-precision Add, for example, emits a static `result_type = F32` and a
dynamic `WithLocs<ScalarType> input_type`; its three operand type expressions
refer to `result_type`, `input_type`, and `result_type`. Slot IDs are local to a
variant, so `.f32` may bind `type` in standard Add and `result_type` in mixed
Add without becoming a global spelling-to-kind map.

## Multiple operand layouts in one variant

The same modifier combination can admit different operand shapes. It must not
be split into artificial semantic forms. A final form class retains one layout
tag, direct fields present in every layout, and typed optional fields present
only in some layouts. `bar.sync a{, b}` is represented approximately as:

```cpp
class BarSync final : public Instruction {
public:
  ResolvedOperandLayoutTag operand_layout;
  inline static constexpr bool sync = true;
  std::optional<WithLocs<ResolvedImmediate>> barrier_immediate;
  std::optional<WithLocs<RegOrImm>> barrier_reg_or_imm;
  std::optional<WithLocs<RegOrImm>> thread_count;
};
```

`ResolvedOperandLayoutTag` is the index of the layout in generated descriptors.
The checker verifies tag validity, required and forbidden optional-field
presence, and every operand binding before dereferencing a layout-specific
field. Fields with the same descriptor ID but different C++ value types get
deterministic value-kind suffixes, as `barrier_immediate` and
`barrier_reg_or_imm` illustrate. A disagreement is corrupted Resolved IR and produces
`OperandLayoutPayloadMismatch`.

`Flat` handles comma-separated positional slots. `Call` is the only other
implemented layout algorithm: it recognizes the three direct-call group
arrangements and resolves the input group as one variadic field. It is a fixed
algorithm, not a general repeat DSL; variadics and call groups still must not
be disguised as `Flat`.

## Resolution protocol

Each per-opcode resolver and its `ResolveContext` overload share one generated
implementation. Shared logic performs these steps:

1. The common matcher diagnoses spellings unknown to the whole syntax
   descriptor, then binds spellings to ordered slots separately inside each
   candidate variant. Required/fixed slots may share a spelling when their
   positions disambiguate it; repeated optional spellings are rejected by the
   database. Reusing one slot is a user diagnostic.
2. Form selection chooses exactly one semantic form from those form-local
   bindings. `absent`, `optional`, and `required/fixed` match by slot and
   allowed value in canonical or explicitly declared alias order. Order aliases
   do not create new semantic variants or change field bindings.
3. The selected variant chooses exactly one `OperandLayout` from AST shapes and
   arity.
4. `resolve_fields` resolves the common execution predicate and converts
   modifiers and operands through resolved bindings into location-carrying
   values. With a binding context, a guard must bind to a `.pred` register and
   ordinary registers must resolve to visible `.reg` declarations; both retain
   their `SymbolId` and declaration type, while a direct branch target must bind
   to a label in the current function.
5. The generated builder constructs the exact final class and populates its
   direct and optional fields.

No matching variant/layout is a user diagnostic. Multiple matching layouts, or
a mismatch between descriptors and generated structures, is a generator bug and
uses `ResolveException`, distinct from `ResolveDiagnostic`.

`select_variant_name` remains the general non-template descriptor matcher for
caller-supplied descriptors. Generated opcode resolvers use a private sorted
spelling index to conservatively intersect form bitsets, then run the same exact
ordered binder in canonical form order. Known spellings include canonical
`absent` slots; aliases contribute consuming candidates. All unknown-spelling
checks finish before binding, and selection failure returns to the general
matcher for unchanged diagnostic priority. Successful selection retains an
opcode-local typed ordinal, a context-validated `InstructionKind`, and direct
static syntax/resolved descriptor references. `resolve_fields` still binds the
winner again and selects its layout using the resolution context. Index tables
and thin calls stay in each opcode's existing `.gen.cpp`; WGMMA construction
shards remain separate. Generated final class definitions and
resolver declarations use a stable per-opcode aggregate header under the YAML
`codegen_category`; callers with only model needs can use the generated base
header. The aggregate `ptx_resolved_ir.gen.hpp` includes all opcode
aggregates. An opcode with more than 64 forms has deterministic public
`*_forms_NNN.gen.hpp` declaration shards and private `*_methods_NNN.gen.cpp`
and `*_descriptors_NNN.gen.cpp` shards, each covering at most 64 forms. The
per-opcode header remains the public include entry point. Its descriptor
getters have a `const&`/`noexcept` contract: a bounded, one-time function-local
`static const std::array` concatenates canonical shard rows into stable
contiguous storage without a heap allocation. Non-inline method definitions
are compiled into the library. The internal fixed-domain observer is not a
public all-family visitor template; adding a form to an existing shard does
not promise zero recompilation elsewhere.

For WGMMA, the stable opcode resolver keeps selection and field resolution in
their established order, then dispatches each exact kind to a private builder
in the owning method shard. The builder consumes the borrowed scratch fields
and returns the same exact final class; no public class or resolver signature
changes.

## Three descriptors

One YAML specification generates three static descriptors with distinct roles:

| Descriptor | Responsibility |
| --- | --- |
| Syntax descriptor | modifier spellings/presence, AST operand shapes, and layout slots |
| Resolved descriptor | resolved field kinds, modifier/operand bindings, structured type/state-space expressions, static state-space allowlists with per-entry availability, roles, and access |
| Checker descriptor | variant/layout/value availability, operand type compatibility, and rule ID |

They must not duplicate each other: syntax descriptors do not store resolved C++
types, resolved descriptors do not recognize modifier spellings, and checker
descriptors do not redo resolve bindings.

The normalized specification model converts modifier kind and presence, and
operand kind, role, and access, from YAML spellings into semantic enums at the
loading boundary. Resolved-IR validation uses those enums and PTX constraints;
it does not load C++ domain mappings. C++ type names, enum expressions, and
member aliases are applied only by code generation. For example, the semantic
`.sat` field remains `sat` in the IR while the backend may emit the historical
`saturate` C++ member spelling. Checker operand views likewise dispatch by
`ResolvedValueKind` and field origin, so a backend type-name change cannot
select a different semantic branch.

## Checker contract

[SDK support: Checker contract](../sdk_support.md#resolved-ir-design--checker-contract).

Each final form's virtual `Instruction::check` override uses common checking for:

The caller supplies `checker::Context::target` and `instruction_range` for a
single-instruction check. The latter is the stable fallback diagnostic range
when the edited field has no retained source provenance.

Semantic-domain membership and target availability are separate questions. The
domain is derived from the normalized variant modifier values together with the
individual optional field default; it is not inferred from availability entries,
and it does not admit a blanket enum sentinel. Availability keeps its existing
source-presence behavior, because an omitted default need not have the same PTX
or SM requirement as an explicitly spelled value. Therefore a legal value can
pass domain membership yet still fail its target availability check.

## Extension rules

[SDK support: Extension rules](../sdk_support.md#resolved-ir-design--extension-rules).

- A YAML semantic variant is defined by its modifier combination; never add a
  fake variant merely for C++ layout convenience.
- Every generated member represents a resolved PTX fact or its provenance; do
  not expose `direct` or `sub_struct` as backend layout switches.
- Add a Syntax AST shape and syntax descriptor before adding its resolved value.
- A new multi-layout instruction needs tests for normal resolution, an invalid
  layout tag, and a tag/payload mismatch.

Implementation entry points are
`submod/resolved_ir/include/ptx_resolved_ir.hpp`,
`submod/resolved_ir/include/ptx_resolved_ir_checker_support.hpp`, and generated
`ptx_frontend/resolved_ir/ptx_resolved_ir.gen.hpp`.

## Owned video operands

`ResolvedVideoOperand` owns a located `RegOrImm`, an optional located `VideoSelector` variant, a written register-negation Boolean and optional minus range. Identifier locations remain separate from the enclosing selector/negation range for binding diagnostics. Source arrays preserve written high-to-low digit order and index concatenated A+B carriers. `video_default_selector` and `video_effective_selector` expose defaults without inventing written provenance. Generated forms expose static `video_lanes`/`video_operation`. `video_mad_interpretation` derives product/input-C/final signs independently of written dtype; numeric negative constants never become register-negation controls. Public helpers are in `<ptx_frontend/resolved_ir/ptx_video.hpp>`. Checker operand views borrow owned Video payloads only for synchronous checking; module reference traversal uses the inner carrier's identifier locations.


## Conversion special-register sources

[SDK support: Conversion special-register sources](../sdk_support.md#resolved-ir-design--conversion-special-register-sources).

## Historical omitted floating controls

[SDK support: Historical omitted floating controls](../sdk_support.md#resolved-ir-design--historical-omitted-floating-controls).

### Owning debug section values

[SDK support: Owning debug section values](../sdk_support.md#resolved-ir-design--owning-debug-section-values).

### Target debug absence and recognition boundary

[SDK support: Target debug absence and recognition boundary](../sdk_support.md#resolved-ir-design--target-debug-absence-and-recognition-boundary).

## Historical entry body inputs

[SDK support: Historical entry body inputs](../sdk_support.md#resolved-ir-design--historical-entry-body-inputs).

## Exclusive numeric SM availability bound

[SDK support: Exclusive numeric SM availability bound](../sdk_support.md#resolved-ir-design--exclusive-numeric-sm-availability-bound).

## Historical 101 source profiles and validation features

[SDK support: Historical 101 source profiles and validation features](../sdk_support.md#resolved-ir-design--historical-101-source-profiles-and-validation-features).

### Deprecated maximum-CTA hint

[SDK support: Deprecated maximum-CTA hint](../sdk_support.md#resolved-ir-design--deprecated-maximum-cta-hint).

### Legacy debug serialization

[SDK support: Legacy debug serialization](../sdk_support.md#resolved-ir-design--legacy-debug-serialization).

### Indirect function-address carriers

[SDK support: Indirect function-address carriers](../sdk_support.md#resolved-ir-design--indirect-function-address-carriers).

### Selected scalar call operands

[SDK support: Selected scalar call operands](../sdk_support.md#resolved-ir-design--selected-scalar-call-operands).

### Alternate scalar literal conversion

[SDK support: Alternate scalar literal conversion](../sdk_support.md#resolved-ir-design--alternate-scalar-literal-conversion).

### Packed32 exact words and native call formals

[SDK support: Packed32 exact words and native call formals](../sdk_support.md#resolved-ir-design--packed32-exact-words-and-native-call-formals).

### Referenced debug function-name bytes

[SDK support: Referenced debug function-name bytes](../sdk_support.md#resolved-ir-design--referenced-debug-function-name-bytes).

### Floating value-vector source contract

[SDK support: Floating value-vector source contract](../sdk_support.md#resolved-ir-design--floating-value-vector-source-contract).

## Explicit scalar Bit16 sources

[SDK support: Explicit scalar Bit16 sources](../sdk_support.md#resolved-ir-design--explicit-scalar-bit16-sources).

## Packed SETP raw and exact words

[SDK support: Packed SETP raw and exact words](../sdk_support.md#resolved-ir-design--packed-setp-raw-and-exact-words).

### Named matrix fragments

[SDK support: Named matrix fragments](../sdk_support.md#resolved-ir-design--named-matrix-fragments).

### Scalar components in shared matrix descriptor carriers

[SDK support: Scalar components in shared matrix descriptor carriers](../sdk_support.md#resolved-ir-design--scalar-components-in-shared-matrix-descriptor-carriers).

### TCGEN mandatory named output-lane masks

[SDK support: TCGEN mandatory named output-lane masks](../sdk_support.md#resolved-ir-design--tcgen-mandatory-named-output-lane-masks).

### Ordinary BF16 FMA/SET source members

[SDK support: Ordinary BF16 FMA/SET source members](../sdk_support.md#resolved-ir-design--ordinary-bf16-fmaset-source-members).

### Scalar F16 atomic source members

[SDK support: Scalar F16 atomic source members](../sdk_support.md#resolved-ir-design--scalar-f16-atomic-source-members).

### Scalar BF16 atomic source members

[SDK support: Scalar BF16 atomic source members](../sdk_support.md#resolved-ir-design--scalar-bf16-atomic-source-members).

## Packed32 atomic source members

[SDK support: Packed32 atomic source members](../sdk_support.md#resolved-ir-design--packed32-atomic-source-members).

### Scalar packed32 source members

[SDK support: Scalar packed32 source members](../sdk_support.md#resolved-ir-design--scalar-packed32-source-members).

### Atomic vector value sources

[SDK support: Atomic vector value sources](../sdk_support.md#resolved-ir-design--atomic-vector-value-sources).


## Scalar store immediate representation

The [SDK scalar ST producer contract](../sdk_support.md#st-coverage--contract)
includes a compiler/assembler compatibility extension despite the manual's
register-only source note. All ordinary synchronous scalar ST data fields are
`WithLocs<RegOrImm>`. Consumers must replace direct register-member reads with
`std::get_if<ResolvedRegisterRef>(&store.src.value)` or
`std::get_if<ResolvedImmediate>(&store.src.value)` and rebuild against matching
headers/libraries. Vector, async and auxiliary policy fields keep their existing
types. Scalar cache-policy forms share the same direct `src` union across
policy-present and policy-omitted layouts; their optional `cache_policy` stays a
register.

The existing numeric converter owns selected-width bits, type and original
integer source bits/sign; locations retain the written source range. Float
numeric conversion and equal-width exact-word preservation use the existing
converter, including lexical-width restrictions for bit carriers. No temporary
register or replacement operation is introduced. B128 immediates and integer
magnitudes beyond the evaluated 64-bit domain remain representation limits.
Owned checks reject payload bits outside the selected width and invalid floating
integer-origin/negative flags, but do not demand erased lexical history. AST
comparison separately checks source identity. Copies, assignments and clones
retain these owned records after source/AST release; scalar register reference
traversal follows the union's register arm.
