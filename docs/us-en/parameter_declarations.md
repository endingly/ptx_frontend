# Parameter declarations

This is the frontend's PTX ISA 9.3 declaration boundary, not a launch ABI,
instruction-completeness claim, or runtime allocation model. Parsing alone does
not establish that a declaration is valid. `resolveModule()` runs declaration
validation before publishing metadata.

## Coverage matrix

| Form / context | Parser | Declaration validation | Installed public metadata |
| --- | --- | --- | --- |
| Entry header scalar/vector / sized multidimensional array | Retained | Fundamental non-predicate types, constant positive extents, alignment, pointer attributes, version and total-size checks | Ordered typed `parameter_declarations` with `EntryInput` role |
| Device input / return scalar/vector / sized multidimensional array | Retained | Same type/extent checks; `.param` needs PTX 2.0 / SM 20; modern functions have at most one return | `parameter_declarations`, `DeviceInput` / `DeviceReturn` |
| Final device input `.param .b8 bytes[]` | Retained | Only final input; PTX 6.0 / SM 30 minimum | Unknown outer extent, checked known inner axes and unknown byte extent, distinct from a scalar |
| Unsized entry, return, non-final input, or non-`.b8` input | Retained | Rejected | No resolved module |
| PTX 1.0–1.3 entry body input | Retained | Fully sized fundamental scalar/vector/axes/groups, read-only, actual entry scope, checked logical aggregate <=32764 | Sole `parameter_declarations` table, `LegacyEntryInput`; header signature remains header-only |
| Body-local scalar/vector / sized array, including multiple dimensions | Retained | Fundamental non-predicate type, positive constant extents, checked size, alignment, PTX 2.0 / SM 20 | `parameter_declarations`, `BodyLocal`, lexical scope and full array shape |
| Unsized body-local `.param` | Retained | Rejected | No resolved module |
| `.callprototype` scalar/vector / array formals | Retained | Device signature rules; prototype itself requires PTX 2.1 / SM 20 | Owned `declaration_semantics::FunctionSignature` from the semantic API; not a declaration table entry |
| Entry-header opaque `.texref`, `.samplerref`, `.surfref` | Retained | Name-only entry inputs; resource kind, shape, and version checked | Ordered `opaque_entry_parameters` with bound symbol identity, resource kind, and source shape; no byte layout |
| Device formal, return, or body-local opaque object | Type spelling retained | Rejected | None; illegal because opaque-object use is limited to module globals and entry parameter lists |
| Device `.reg .v2` / `.v4` input or return, including `.callprototype` | Retained as one formal | Non-predicate fundamental elements, at most 128 bits total; scalar/vector and width must match at calls | Typed `FunctionParameterContract::vector_shape`, bound vector width; no `.param` declaration record |
| Header multidimensional parameter array | Every axis retained | Positive sized axes, checked products; only a supported final scalar byte input may omit its outer extent | Exact axes in signature and own declaration records; total element count for call ABI |
| Body-local parameterized `.param` group (`name<count>`) | Retained | Supported scalar/vector parameter elements, positive uint32 count, no arrays or initializers; existing body-local availability applies | One `BodyLocal` parameter record and one owning symbol with count; members use the same `SymbolId` plus an index |
| `.pred`, unknown or instruction-only type spelling | Type spelling retained | Rejected for `.param` | None |
| Module-scope `.param` | Retained | Rejected | None |

Supported parameter elements are `.s8/.s16/.s32/.s64`, `.u8/.u16/.u32/.u64`,
`.b8/.b16/.b32/.b64/.b128`, and `.f16/.f16x2/.f32/.f64`. The public
`declaration_semantics::parameterScalarType()` classifies this supported set;
its result is a type classification, not validation of context or availability.
Instruction alternate formats such as `.bf16` and `.tf32` do not become legal
declaration types by retaining their spelling. `.reg` formals retain their
existing separate signature/address path; they are not `.param` table entries.
Within the owning function body, register-space input and return formals are
resolved as typed register operands and retain their lexical symbol identities.
Device `.reg` inputs and returns support `.v2` and `.v4` of non-predicate
fundamental elements, including `.f16x2`, with at most 128 bits in total. A
vector remains one formal and one named-register call argument; scalar literals
and differing vector widths do not satisfy its signature. Omitted alignment is
the total vector size. Direct calls, `.callprototype`, `.calltargets`, and owned
call-table signatures preserve the same typed shape after AST destruction.
Parameter-space formals remain data objects: use an instruction appropriate to
their parameter address or storage semantics rather than an arithmetic register
operand.

## Alignment, pointers, and size

Explicit declaration and pointee alignments must be positive powers of two.
Omitted declaration alignment is scalar size or total vector size, independently of
array count; an explicit value
is retained separately from whether it equals that natural value. The frontend
does not infer a maximum alignment from the ABI's wording about multiples of
1, 2, 4, 8, or 16 bytes.

`.ptr` belongs to entry inputs and requires PTX 2.2. The supported scalar pointer
types are `.b32`, `.b64`, `.s32`, `.s64`, `.u32`, and `.u64`. The declared
integer or bit type is preserved; both widths are accepted with either
`.address_size 32` or `.address_size 64`. Pointed state space may be `.const`, `.global`,
`.local`, or `.shared`; omission denotes generic space. Omitted pointee alignment
defaults to four bytes. Parameter storage alignment and pointee alignment are
different fields. No host pointer width or launch-address policy is inferred.
The PTX 2.2 pointer introduction is checked from current owned entry-input
metadata even when target context is missing. A known older version rejects;
an unknown version defers under available-context validation and still requires
context under strict validation. Source-aware validation uses the supplied
source version, while AST-free validation uses the selected owned source region.
This does not equate the declared pointer container width with address size.
Canonical function signatures compare effective alignments: omitted natural
alignment and an explicit equal value are compatible. Declaration metadata still
retains the explicit-alignment distinction. Direct and metadata-backed indirect
array calls use the same natural-alignment default.
Formal `.param` objects cannot be used directly as call actuals. Load an input
(including an entry pointer) into a register or stage it through a body-local
`.param` object first. Device formals do not acquire an entry-only `.ptr` attribute.

Entry header parameters require PTX 1.4. For normal, non-opaque entry parameters,
statically computed aggregate storage includes inter-parameter alignment padding.
The version-selected limit is 256 bytes at PTX 1.4, 4352 bytes at PTX 1.5–8.0,
and 32764 bytes at PTX 8.1 and later. Checked arithmetic rejects overflowing
declaration extents and aggregates. These are ISA limits, not CUDA/OpenCL
driver-specific limits; byte extents are not packed offsets or allocated memory.
Missing version/target information cannot prove an availability violation.
Body-local `.param` initializers are rejected by the parser; semantic validation
also guards against them in directly constructed ASTs.
Calls may omit a validated final unsized byte input when there is no payload;
other required input and return arguments remain subject to exact arity checks.

## Owned metadata and remaining scope

Each function owns `parameter_declarations` in return-list, input-list, then
body lexical traversal order. Entries retain symbol and lexical-scope identities,
role, scalar type enum, vector width, ordered array extents, declared bytes,
effective alignment, explicit-alignment provenance, and optional pointer
properties. Values remain inspectable after source and AST destruction. Nested
same-name body declarations have distinct identities. This is the sole owned
parameter declaration table. Consumers select `ParameterDeclarationRole::EntryInput`
to inspect entry header inputs in source order; other roles are not launch slots.

Both `validateModule` overloads compare the ordered return and input signatures
with binding formals in each function's `declaration_scope`. Scalar/vector `.param`
formals must match the corresponding declaration record's role, identity, type,
shape, effective alignment, checked byte extent, and pointer properties. `.reg`
formals rejoin their binding type, vector shape, and alignment without entering the parameter
table. Opaque entry inputs keep their separate name-only records and their
positions in the full input signature. Body-local records are validated separately
and cannot become formals. Compatible prototypes and definitions share function
identity but retain independent declaration scopes. Signature-only type mutations,
including `.u32` to `.f64`, therefore fail even after source and AST destruction.

The former `ResolvedFunction::entry_parameters` member and `ResolvedEntryParameter`
type have been removed. This is a breaking C++ API change: consumers must migrate
and rebuild, not merely rename the member. Use `scalar_type` instead of the old
type string, the effective non-optional `alignment`, and `array_extents` /
`byte_extent` instead of `is_array` / `array_extent`. An empty extent list denotes
a scalar; an unknown axis denotes a supported unsized array. ABI layout and
packing remain consumer responsibilities.

### Retained boundaries and normative classification

The archived specification distinguishes four categories used by this document:
**legal supported** forms appear in the table, **legal but unsupported** forms
remain deliberately diagnosed until their typed metadata and access rules are
implemented, **illegal** forms remain rejected, and **unresolved** forms are
not promoted to either category from parser behavior alone.

Opaque entry parameters are legal ISA objects: the `.entry` directive permits
them, while the opaque-type section limits their declaration to module globals
and entry parameter lists. They are name-only texture/surface objects and
ordinary `ld.param` cannot load them; their physical layout is intentionally
hidden. Each function owns its opaque entry inputs in source order in
`opaque_entry_parameters`, retaining bound identity, kind, alignment, array
shape, and source location after AST release. Device and body opaque
declarations consequently remain rejected.

Fundamental `.f16x2` and legal `.param .v2/.v4` objects retain native type and
shape throughout declaration, call ABI and owned layout checks. Numeric packed
literal representation remains the separate reference gap described below.

Body-local scalar/vector `.param` compact groups reuse the ordinary group binding:
`value<2>` owns one symbol with count 2, while `value0` and `value1` refer to that
`SymbolId` with indices 0 and 1. Each declarator has one `BodyLocal` parameter
record and no storage declaration record. Its `byte_extent` is the extent of
one member object, not count times an allocation size; the symbol owns the count.
Lexical shadowing, member bounds, and call staging use the typed member identity.
Owned validation rejects missing/out-of-range member indices and indices on
ordinary symbols. A coherent in-range member-index edit is a valid semantic
change; owned validation does not reconstruct the original spelling. When an AST
is supplied, source association separately retains compact counts and written
member references. No expanded source declarations or per-member symbols are
manufactured.

Header dimensions are ordered outer-to-inner. Sized entry inputs and device/prototype inputs
and returns admit multiple axes. Only the outer axis of a supported final scalar
`.b8` device/prototype input may be omitted, for example `bytes[][2][3]`.
Known inner products are checked even when total bytes remain unknown. A caller's
payload need not have the same rank or be divisible by that inner product:
call ABI compares element type, vector shape, total count and effective alignment.
For example `[2][3]` and `[6]` are ABI-compatible. Each function's own signature
and declaration record must still agree on every axis. Source association also
distinguishes written shapes even when their ABI totals agree.

This is a source-breaking C++ member migration: `CstFunctionParameter` removes
`left_bracket`, `array_size`, `right_bracket` in favor of
`std::vector<CstArrayDimension> array_dimensions`; `AstFunctionParameter` removes
`is_array` and `array_size` in favor of `std::vector<AstArrayDimension>
array_dimensions`. `FunctionParameterContract` replaces `is_array` and
`array_extent` with `std::vector<std::optional<NormalizedNumericValue>>
array_extents`; `ResolvedOpaqueEntryParameter` replaces those same old fields with
`std::vector<std::optional<uint64_t>> array_extents`. Empty vectors denote scalars.
Numeric `ResolvedParameterDeclaration::array_extents` is unchanged. Binding
`Symbol::is_array` and `CallArgumentProperties::is_array/array_size` remain derived
category/ABI-total facts; they do not own exact axes. Invalid axes and overflowing
products remain invalid, rather than becoming ordinary unknown extents. Opaque
axes carry no invented physical byte size. No multi-bracket memory operand or
runtime allocation/bounds semantics is added.

No instruction families, simulator execution, argument packing, or physical
allocation are added here.

## Evidence and regressions

The authority is the archived [PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html):
[parameter state space](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parameter-state-space),
[fundamental types](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fundamental-types),
[entry](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#kernel-and-function-directives-entry),
[device functions](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#kernel-and-function-directives-func),
and [call prototypes](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-directives-callprototype).
Assembler observations supplement, but do not replace, these contracts.

As a supplementary non-hardware observation, `ptxas` 13.1 was run against
PTX 9.1 / `sm_80`: an independent-texture-mode opaque entry list and a
body-local parameterized group assembled, while isolated `.f16x2` and vector
`.param` declarations were rejected during allocation and a header
multidimensional array was rejected by that parser. Those tool-version results
do not override the PTX 9.3 classifications above and are not execution tests.

Regressions cover explicit parser boundaries, semantic type/role/version/size
validation, resolved declaration identity and lifetime, and a separately compiled
installed-package consumer. GPU execution is not a declaration-validation gate.

## Fundamental vectors and packed elements

All 17 non-predicate fundamental element types are classified, including scalar
`.f16x2`. Its two half components form one four-byte packed scalar, not an implicit
`.v2` formal. Explicit `.v2/.v4` parameters contain two/four such elements; vectors
may total at most 16 bytes, giving 16 legal v2 and 12 legal v4 element combinations.
Header and body `.param` objects, device/prototype inputs and returns preserve this
shape. Sized parameter vector arrays are supported; natural alignment is the vector
size and bytes multiply that size by the dimensions. Compact counts remain separate
from each member object's extent. Register vector arrays, vector pointers, predicate,
opaque and instruction-only elements remain rejected. Final unsized byte input is scalar only.

Source and AST-free owned validation recompute the same padded entry byte limits
and checked extents. Native typed F16x2 parameter/register actuals retain exact dtype
and explicit shape; calls do not substitute instruction-style bit compatibility.
Use existing b32 memory access containers for packed storage, while named array
indices still scale by the declared scalar element (four bytes for F16x2), not vector size.

A numeric one-to-pair F16x2 call-literal constructor remains a reference gap:
integers, zero, decimal and F64 expressions do not define a splat or lane placement.
The exact-word contract requires a same-width bare unsigned direct `0f` binary32
literal to retain the entire 32-bit word for an otherwise eligible scalar `.f16x2`
Reg or Param input. Direct calls, `.callprototype`, `.calltargets`, and storage
function-list routes use the same formal-driven conversion. This does not admit a
scalar literal to a vector formal or return list, or classify `.bf16x2` as a call
formal. This exact-word route is distinct from ordinary scalar F16 numeric conversion.

Known opaque entry inputs remain name-only: numeric aggregate traversal skips them
without assigning bytes or padding, and continues checking later numeric/vector inputs.

Complete-module direct-call targets must have a prior declaration/definition of
the actual bound function or alias identity. Prior compatible prototypes suffice;
whole-module lookup of a later declaration does not establish visibility. This
source/owned check is independent of the exact parameter ABI and staging rules.

Under the default stack ABI, register-state device and call-prototype input and
return formals require at least 32 bits in the **whole value** when both PTX >=2.0
and the active recognized target architecture >=20 are known. The width is scalar
storage width times explicit vector lanes: `.v2.u8` and scalar `.f16/.pred` fail,
while `.v4.u8`, `.v2.f16` and native scalar `.f16x2` meet the minimum. Existing
fundamental/vector eligibility and maximum width remain independent. Source types
and shapes are preserved, with no implicit formal promotion. A known older version
or target disables this minimum; missing context defers it in available-context
validation, while strict validation retains its missing-context errors. Each
function uses its active target region; source-aware validation uses the supplied
AST and AST-free validation uses owned regions. Unused prototypes are checked too.
This is a formal rule: ordinary small/predicate register declarations, small
`.param` formals and matching small-register actuals to `.param` remain legal.

Otherwise eligible scalar F16 Reg and Param inputs share numeric literal conversion, including direct and metadata-backed indirect calls. A floating 1.5 or evaluated F64 equivalent becomes F16 word 0x3e00; original lexical/precast source is retained for owned recomputation. Modern Register-formal minimum width still rejects scalar Reg F16 independently, while established nonstack legacy calls and partial AvailableContext follow their existing gates. Strict missing context and scalar-to-vector no-splat remain. This does not add a packed F16x2 constructor.

Parameter address validation also rejoins the current symbol ID, lexical scope,
compact member and declaration record before context can be skipped. A known
Parameter object's effective space must belong to a nonempty operand domain
and agree with an associated current state-space modifier. Readonly entry input
checks use explicit Write/ReadWrite operand requirements or the existing Return
parameter constraint; DeviceInput is not generally readonly. Diagnostics merge
only an identical local address cause, preserving unrelated failures. This
check does not authorize new source forms or replace full target-aware checking.


## Historical entry body inputs and typed roles

PTX 1.0–1.3 entry body `.param` objects are read-only kernel inputs with
`ParameterDeclarationRole::LegacyEntryInput`. Their sole owning handoff is
`ResolvedFunction::parameter_declarations`, in lexical depth-first/declarator
order, including nested scopes and distinct shadowed identities. Compact groups
retain one record per bound group with per-member `byte_extent` and a count on
the symbol; arrays retain all axes. This order defines metadata traversal, with
no host launch slots, relative addresses, padding, or allocated offsets.

`FunctionSignature::parameters` and returns describe syntactic header ABI only.
A legacy entry can have an empty header signature and nonempty body inputs.
Consumers enumerate `EntryInput` records for header inputs and `LegacyEntryInput`
records for legacy body inputs; opaque header inputs remain separate. Legacy body
inputs require fully sized fundamental objects and keep existing type-specific
availability. This adds no legacy opaque, `.ptr`, unsized, or initializer permission.
PTX 1.4–1.x rejects entry body `.param`; PTX 2.0 and later selects `BodyLocal`
with independently known SM 20 minimum. Device body objects remain `BodyLocal`.

Fresh `resolveModuleOnly` and `resolveModule` need a PTX version only when an
entry contains body `.param` and its role cannot otherwise be selected. They
report `MissingValidationContext`, including with an SM 1.x target; they do not
guess a role from the target or require a target just to type it. Ordinary
fragments, entries without body parameters, header inputs, and device body
objects retain their previous context behavior. Already typed modules keep their
current role with missing context: available-context validation defers only the
unknown window; strict validation still requires complete context.

`ResolvedSymbolRef::parameter_role` is a synchronous record-derived cache and
preserves the structural `CallParameter` binding kind. Known legacy direct
references require the matching cache. Every engaged cache rejoins the actual
parameter record; corrupt, missing legacy, forged kind, or mismatched role caches
reject. Address, MOV, and tensor projections share the role-to-direction mapping:
entry/device/legacy inputs are `Input`, device returns `Return`, and body call
slots `CallArgument`. Legacy LD is an ordinary input read without return-load
staging. MOV takes its `.param` address without device-local materialization.
Actual records independently enforce read-only/entry-frame semantics and exclude
direct legacy input call actuals/returns; loading a value into a register preserves
legal register ABI calls. Modern body call-store/call/load adjacency remains.
Both validators check current records, caches, shapes, scopes, accesses, and
logical capacity before a missing-target skip; valid original syntax cannot
exempt a current corrupted payload. Replacement context checks the current role,
with no automatic reinterpretation or producer history.

The known global legacy-input ceiling is 32764 bytes. A checked logical sum uses
each per-member `byte_extent` times its compact count (otherwise one), across
all inputs, including unused and nested declarations. Axes/vector lanes already
contribute to `byte_extent` and are not multiplied again. Overflow or a total
above 32764 rejects during resolution and both validation routes, even without
context. The PTX 1.4 header-specific 256-byte limit is not applied to legacy body
inputs; existing later header limits remain unchanged.
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` remains reference-boundary qualified:
passing the known logical ceiling is not proof of physical ABI packing, a lower
historical threshold, or acceptance by every legacy driver/hardware launch ABI.


The shared current operand-domain pass classifies both actual `EntryInput` and
`LegacyEntryInput` records as read-only entry objects. It uses current typed
layout/field requirements before context skips, including non-LD/ST consumers;
MOV address acquisition stays legal, while an explicit writable operand rejects.
Legacy readonly diagnostics use their original label and instruction range;
only identical local causes merge. No opcode-specific writer classification or
new historical instruction permission is inferred.
