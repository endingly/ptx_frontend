# Parameter declarations

This is the frontend's PTX ISA 9.3 declaration boundary, not a launch ABI,
instruction-completeness claim, or runtime allocation model. Parsing alone does
not establish that a declaration is valid. `resolveModule()` runs declaration
validation before publishing metadata.

## Coverage matrix

| Form / context | Parser | Declaration validation | Installed public metadata |
| --- | --- | --- | --- |
| Entry header scalar / sized 1-D array | Retained | Fundamental non-predicate types, constant positive extents, alignment, pointer attributes, version and total-size checks | Ordered typed `parameter_declarations` with `EntryInput` role |
| Device input / return scalar / sized 1-D array | Retained | Same type/extent checks; `.param` needs PTX 2.0 / SM 20; modern functions have at most one return | `parameter_declarations`, `DeviceInput` / `DeviceReturn` |
| Final device input `.param .b8 bytes[]` | Retained | Only final input; PTX 6.0 / SM 30 minimum | One unknown extent and unknown byte extent, distinct from a scalar |
| Unsized entry, return, non-final input, or non-`.b8` input | Retained | Rejected | No resolved module |
| Body-local scalar / sized array, including multiple dimensions | Retained | Fundamental non-predicate type, positive constant extents, checked size, alignment, PTX 2.0 / SM 20 | `parameter_declarations`, `BodyLocal`, lexical scope and full array shape |
| Unsized body-local `.param` | Retained | Rejected | No resolved module |
| `.callprototype` scalar / array formals | Retained | Device signature rules; prototype itself requires PTX 2.1 / SM 20 | Owned `declaration_semantics::FunctionSignature` from the semantic API; not a declaration table entry |
| Entry-header opaque `.texref`, `.samplerref`, `.surfref` | Type spelling retained | Explicit unsupported diagnostic | None; opaque entry objects are legal, but need identity-only metadata and dedicated texture/surface use rather than an invented byte layout |
| Device formal, return, or body-local opaque object | Type spelling retained | Rejected | None; illegal because opaque-object use is limited to module globals and entry parameter lists |
| `.f16x2` in an entry header, device formal/return, or body-local declaration | Type spelling retained | Explicit unsupported diagnostic | None; `.f16x2` is a fundamental type, so this is a retained legal-but-unsupported boundary, not an alternate-format rejection |
| Header `.v2` / `.v4` parameter | Explicit unsupported parse diagnostic | Not reached | None; retained legal-but-unsupported boundary pending context-specific vector shape, size, and ABI metadata |
| Header multidimensional parameter array | Explicit unsupported parse diagnostic | Not reached | None; context-specific header-array grammar remains unresolved rather than being classified as illegal |
| Body-local vector `.param` | Retained | Explicit unsupported diagnostic | None; retained legal-but-unsupported boundary pending vector shape and call-staging metadata |
| Body-local parameterized `.param` group (`name<count>`) | Retained | Explicit unsupported diagnostic | None; legal declaration shorthand needs per-expanded-name identity and declaration-order metadata |
| Vector or multidimensional `.param` formal in a `.callprototype` | Explicit unsupported parse diagnostic where the header grammar rejects it | Not reached | Scalar and one-dimensional array contracts remain supported; vector shape and higher-rank header syntax are not inferred |
| `.pred`, unknown or instruction-only type spelling | Type spelling retained | Rejected for `.param` | None |
| Module-scope `.param` | Retained | Rejected | None |

Supported parameter elements are `.s8/.s16/.s32/.s64`, `.u8/.u16/.u32/.u64`,
`.b8/.b16/.b32/.b64/.b128`, and `.f16/.f32/.f64`. The public
`declaration_semantics::parameterScalarType()` classifies this supported set;
its result is a type classification, not validation of context or availability.
Instruction alternate formats such as `.bf16` and `.tf32` do not become legal
declaration types by retaining their spelling. `.reg` formals retain their
existing separate signature/address path; they are not `.param` table entries.
Within the owning function body, register-space input and return formals are
resolved as typed register operands and retain their lexical symbol identities.
Parameter-space formals remain data objects: use an instruction appropriate to
their parameter address or storage semantics rather than an arithmetic register
operand.

## Alignment, pointers, and size

Explicit declaration and pointee alignments must be positive powers of two.
Omitted declaration alignment is natural element alignment; an explicit value
is retained separately from whether it equals that natural value. The frontend
does not infer a maximum alignment from the ABI's wording about multiples of
1, 2, 4, 8, or 16 bytes.

`.ptr` belongs to entry inputs and requires PTX 2.2. The supported scalar pointer
types are `.u32` and `.u64`. Pointed state space may be `.const`, `.global`,
`.local`, or `.shared`; omission denotes generic space. Omitted pointee alignment
defaults to four bytes. Parameter storage alignment and pointee alignment are
different fields. No host pointer width or launch-address policy is inferred.
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
Legacy PTX 1.0–1.3 entry inputs declared inside the body are a retained
exclusion. PTX 9.3 establishes that PTX 1.x had kernel `.param` objects while
device `.param` formals arrived in PTX 2.0, but it does not provide the legacy
body-input grammar and identity rules needed to map those declarations onto the
current table. A body-local metadata role therefore never reinterprets such an
object as an entry input. This is unresolved legacy grammar, not a claim that
the historic form was ISA-illegal.
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
hidden. Device and body opaque declarations consequently remain rejected.

`.f16x2` is listed as a fundamental type, unlike the alternate packed formats.
The parameter-passing rules discuss base-type scalar and vector `.param`
formals, so the frontend records `.f16x2` as legal but unsupported rather than
collapsing it into the instruction-only-type diagnostic. Supporting it requires
coherent declaration, direct-call ABI, literal, and resolved-memory behavior;
the existing `ScalarType::F16x2` value alone is not that contract.

The same distinction applies to `.v2`/`.v4` parameter shapes and local
parameterized groups. The variable rules permit two- and four-lane vectors of
non-predicate fundamental types and permit parameterized names for fundamental
types in any state space. The frontend retains those source forms where its
grammar already does so, but does not manufacture expanded declarations,
vector bytes/alignment, call staging, or public metadata. The generic array
rules establish constant dimensions, but do not settle the distinct header
multidimensional parameter grammar; that row remains unresolved.

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
