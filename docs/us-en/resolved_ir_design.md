# C++ Resolved IR Design

## Named array addresses

Dedicated address-source MOV and ordinary LD (ISA §9.7.9.8) / ST (§9.7.9.11)
accept `A[index]`. LDNC, LDU, ST.async/bulk, atom/red, cp.async, prefetch,
matrix, tensor and texture families remain outside this supported subset;
this is not a claim that their PTX ISA syntax is invalid. Admission is selected
from the final variant's canonical metadata and independently rechecked on owned IR.
The named form does not compose with a `.unified` suffix. Known unified storage
still requires the existing bracket-qualified LD path and remains read-only for
ST; MOV may take its address, including named indexing, without dereferencing it.

`ResolvedAddress::named_index` owns an integer index's bits/signedness or the
exact bound scalar register, optional integer displacement, written operator,
scalar byte stride and source ranges. It is mutually exclusive with the old
byte offset. `A[1]` scales by the declaration's scalar base element size:
u32, v2.u32 and multidimensional u32 arrays all use four bytes, while u64 uses
eight. Array shape comes from owned storage/parameter records keyed by SymbolId;
standalone resolution without that context fails explicitly. Negative and
out-of-bounds constants are not rejected merely for array bounds.

Dynamic integer/bit registers retain their declared width and signedness,
including narrow types and s64, without runtime evaluation, casts, promotion,
truncation or invented sign extension. Only the constant contribution is
checked and scaled before MOV's signed-64 or memory's signed-32 byte domain.
Direct minus and plus-negative normalize to the same constant contribution
while retaining different source operations. Dynamic alignment is
`gcd(base alignment, scalar stride)`, further reduced by the constant bytes.
Owned validation rejoins declaration shape and register identity, rechecks
cached stride/displacement and ranges, and needs no Syntax AST lifetime.

Local ptxas CUDA 13.3 V13.3.33 probes confirm scalar-element scaling and permit
negative/out-of-bounds constants. That compiler rejects direct-minus dynamic
spelling while accepting plus-negative; the frontend deliberately accepts both.
These are compiler observations, not GPU execution or runtime width/sign rules.

## Status and scope

This document describes the implemented Resolved PTX IR, not a future CFG,
SSA, or backend IR. The frontend's core flow is:

```text
PTX source -> Token stream -> Syntax AST -> symbol binding -> Resolved IR -> checker
```

Syntax AST preserves source spelling, modifier order, and `SourceRange`.
Resolved IR records the selected instruction variant, resolved operand values,
and diagnostic locations. Both are stable frontend boundaries. Lexical symbol
binding is connected to module resolution, execution predicates resolve to
declaration-aware values, and special registers, external symbols, and
genuinely undeclared references are distinct. The 16/32/64-bit scalar `mov`
type families accept register, immediate, and special-register sources; the
32/64-bit forms also accept data-symbol, `symbol+offset`, function-address, and
legal formal-parameter-address sources. Bit-size forms also support two/four-
element vector pack/unpack; `.b128` is vector-only. `mov.pred` reuses the declaration-aware
`ResolvedPredicate` representation. Generic and basic explicit-space scalar
plus braced-vector `ld`/`st` exercise the dereferenced-address path
for 14 bit-size, integer, and floating-point types from 8 through 64 bits.
Legacy memory-vector payloads are capped at 128 bits: `.v2` accepts modeled
types through 64 bits and `.v4` through 32 bits. PTX 8.8/SM 100 additionally
accepts exactly 256-bit `.v8` × 32-bit and `.v4` × 64-bit forms. Static natural
alignment checks bound data symbols with constant byte offsets and absolute
immediate addresses; register and standalone unresolved addresses stay unknown.
Other source forms, remaining qualifier extensions, CFG/SSA, and target
lowering remain later work. `ResolvedIndirectCallee` provides descriptor-
independent identity for a non-predicate `.reg` indirect target or a bound
function-local `.callprototype`/`.calltargets` label. Its enclosing
`ResolvedFunction` owns the matching ordered metadata payload and normalized
ABI separately, so an operand remains compact without making the metadata
unavailable. The final `CallDirect` class now has three additional
`IndirectCall` layouts (target/metadata, target/input/metadata, and
return/target/input/metadata), each available from PTX 2.1 / SM 20; normal
module indirect calls preserve the bound target and metadata identities, then
reuse the direct-call ABI contract through metadata-indexed canonical
signatures. ABI comparison does not create a second indirect-call model.

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

Matrix data forms expose immutable class topology through
`matrix_descriptor()`; mutable operand fields retain source-selected values and
locations. Neither a mutable logical-form tag nor a `matrix_logical_index`
lookup is part of this contract. Shared matrix descriptors retain a bound
register identity while their runtime bits remain opaque. The separate
[`TensorMapKnownFacts` query](tensor_map_known_facts.md) accepts caller-supplied
descriptor facts. Its
`project_tensor_known_access_context(const Instruction&, const checker::Context&)`
adapter copies selected typed TMA access facts without retaining the AST:
non-tensor forms yield no access or diagnostics, malformed tensor metadata
yields diagnostics without access, and target unavailability preserves the
copied access with availability false. It does not decode raw descriptor bytes.

The module entry points have distinct success contracts:

| Entry point | Success means |
| --- | --- |
| `resolveModuleOnly(ast)` | Binding, declaration semantics, instruction resolution, and call-ABI/staging checks passed. It does not run the final instruction/directive checker. |
| `resolveAndValidateModule(ast)` | Resolution and final checking passed, with a recognized source target and PTX version for each checked region. Missing context is an error. |
| `resolveModule(ast)` | Compatibility behavior: resolution plus final checks where context is available; targetless fragments remain accepted. |
| `validateModule(ast, module, policy)` | Source correspondence and final instruction/directive checks passed under the explicit policy (default: `RequireCompleteContext`). The module must already have passed resolution. |
| `validateModule(module, policy)` | Revalidates owned header, declaration/member identities, control metadata, typed call literals, operand layouts, and available source-region checker rules without traversing an AST. Manually constructed or mutated public IR must use this entry point before consumption. |
| `checkModuleAvailability(ast, module)` | Compatibility wrapper for validation with `AvailableContext`; despite its historical name, it runs the full instruction checker in contextualized regions. |

Resolution-only still enforces declaration availability when its source contains
the relevant version/target; it is not a bypass for malformed declarations.
Binding, declaration shape/type rules, operand resolution, and call ABI/staging
belong to resolution. The final generated checker owns remaining instruction
constraints (including target-independent layout/type relationships) as well as
PTX/SM/profile availability. Thus resolution-only does not guarantee that all
target-independent instruction constraints passed. All validation guarantees
are limited to the currently modeled instruction and declaration subset.
Each `.target` replaces the active source context, including clearing a previous
recognized target when the new spelling is unknown. Function headers, nested
body declarations, and local call prototypes use their containing function's
source region. This is separate from choosing a deployment target for lowering.
The explicit validation catalog includes historical `sm_13` and `sm_20` with no modern
capabilities, allowing the PTX 6.0 `sm_20`/`sm_30` declaration boundary to be
checked consistently; arbitrary numeric target spellings remain unrecognized.

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

Validation rejects missing, extra, ambiguous, or structurally different
function/instruction associations with `ModuleSourceMismatch`. It still accepts
a separately parsed equivalent function body with shifted line numbers and
different target/version. Resolution-significant directives must still match.
Validation rebinds the supplied AST and repeats declaration semantics, including
availability under its replacement source context. Duplicate equivalent
declarations need an unambiguous occurrence match; they are not silently paired
by order. Instruction diagnostics use the IR's original owned ranges, while
directive diagnostics refer to the supplied AST. Missing strict-validation
context is reported as `MissingValidationContext`.

`ResolvedModule::header` owns the effective version, ordered source target
options, and address-size values for a targetless prefix and every subsequent
`.target` region. Each value records `Missing`, `Explicit`, or `Defaulted`
provenance. An omitted `.address_size` owns the PTX-defined 32-bit value with
`Defaulted` provenance; it never depends on the host. Source target order
defines source availability only; it is not a deployment or physical-backend
target. Invalid, duplicate, or inconsistent
header directives are retained as invalid ranges and rejected by owned
validation. `ResolvedFunction::source_region` selects this same owned context.

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

`ResolvedFunctionAttribute::values` has migrated from source spelling storage
to the typed optional `ResolvedUnifiedId` `unified_id` payload. For
`.attribute(.unified(uuid1, uuid2))`, `unified_id->upper` is UUID `uuid1`
(upper 64 bits) and `unified_id->lower` is UUID `uuid2` (lower 64 bits); no
byte-order or host-address conversion occurs. Function attributes and storage
declarations use this same named value type.
Malformed source UUID tokens remain declaration diagnostics, while AST-free
validation rejects a retained `.unified` attribute without that typed payload.

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
are explicitly not resolved payloads. There is no promise of stable generated
C++ struct layout or binary ABI. Raw module directives still require an AST;
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

`resolveInstruction` is generated from the instruction database and dispatches
to per-opcode functions such as `resolveAdd`, each constructing an exact final
semantic-form class. This keeps opcode dispatch out of callers.
`resolveModule` first builds a `SymbolTable`, then constructs an explicit
`ResolveContext` for each function scope. The resulting `ResolvedModule` owns
that table, and each `ResolvedFunction` is identified by its function
`SymbolId`. Each function owns a single `parameter_declarations` table for its
validated `.param` declarations. Filtering by `ParameterDeclarationRole::EntryInput`
selects entry header inputs in source order.
`ResolvedFunction::label_positions` records each function label as
its bound `SymbolId` and a source-order boundary in the recursively flattened
instruction body: labels before the first instruction are at zero, consecutive
labels share a boundary, and a trailing label is at `body.size()`. Standalone
`resolveInstruction` and per-opcode resolvers remain declaration-free for
single-instruction tools. Raw directives and declarations remain in the Syntax
AST/symbol table instead of being copied into Resolved IR as unresolved string
fields; owned, normalized parameter and storage metadata are deliberate exceptions.
Bound `.file` and `.debug_str`
identities validate `.loc` metadata there, but `.loc`, `.section`, and
`.pragma` do not add Resolved IR nodes or instruction attachment.

`ResolvedFunction::parameter_declarations` owns validated `.param`
declarations: return formals first, input formals next, then body-local declarators
in lexical traversal order (including nested blocks). Each
`ResolvedParameterDeclaration` retains its `symbol_id`, `scope_id`, role
(`EntryInput`, `DeviceInput`, `DeviceReturn`, or `BodyLocal`), fundamental
`scalar_type`, effective byte `alignment`, `explicit_alignment`, `vector_width`,
outer-to-inner `array_extents`, checked `byte_extent`, and optional pointee
properties. Scalar shapes have no array extents; supported unsized device input
arrays have a null extent and no byte extent. The owning function and symbol
table preserve lexical identity even for shadowed local names. Values remain
inspectable after source text and Syntax AST destruction. A non-pointer has no
pointer properties; a pointer with no pointed state space is generic, and omitted
pointee alignment defaults to four bytes. This sole parameter table does not
include `.reg` formals or `.callprototype` signatures. See the
[parameter coverage and migration contract](parameter_declarations.md)
for declaration validation, unsupported forms, and version boundaries. None of
these fields describes packed argument offsets or a runtime allocation.

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

Call-staging adjacency follows the executable instruction sequence within the
current lexical body. Ordinary variable declarations, `.loc`, and `.pragma` do
not interrupt argument stores before a call or return loads after it. Labels,
nested blocks, and call/branch metadata remain scan boundaries; nested bodies
are checked separately with their own symbol scope. Actual intervening
instructions and predicated staging accesses remain invalid. The declaration's
placement does not change which bound parameter identity the call must use.

## Locations and primitive values

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

`AstImmediateKind` retains the lexer's literal classification. Decimal, octal, and hex
integers, including their optional `U` suffix, first evaluate in the PTX
64-bit signed/unsigned source domain; unary minus preserves that source type
and unsigned negation wraps. Ordinary data uses retain the low target-width
bits. `WARP_SZ` is the source-defined signed integer constant `32`, including
in ordinary instruction-immediate positions; it is not a query of a target's
physical warp width. The generated operand descriptor independently selects
narrowing or
strict target-width representability for each semantic use; a fixed scalar type
expresses provenance only. Generated range, exact-value, and multiple-of
controls compare preserved source bits, while unconstrained controls opt into
strict conversion explicitly. Call literals checked against formal parameter
types and address offsets retain strict target-width representability. A signed
`-0` is numerically zero,
whereas floating negative zero retains its IEEE sign bit. Decimal floats
currently convert to `F32` and `F64`; a single leading `+` is normalized only
at decimal decoding, while a leading `-`, signed zero, and exponent signs retain
their normal floating semantics. Raw `0f`/`0d` bit-pattern rules are unchanged.
`0f<8 hex>` and `0d<16 hex>` are raw IEEE bit patterns for `F32` and `F64`
respectively. Other floating formats require explicit quantization rules and
must not silently take the integer path.

`ResolvedRegisterRef` owns the complete source spelling and its
`ResolvedRegisterClass`. During module resolution it also stores the
declaration `SymbolId`, optional parameterized-member index, and declared
`ScalarType`, giving named registers such as `%tmp` and `name<count>` members a
stable identity. A numbered-register index remains an optional convenience,
not an identity. An ordinary `.xyzw/.rgba` selection retains this same base
identity and parameterized member, with owned `ResolvedRegisterComponent`
metadata: canonical lane, actual declaration width, base spelling/range, full
component range and written selector spelling/range. Its effective shape is
scalar, so `vector_width` is absent; `declared_type` remains the declaration's
scalar element type. `same_register_storage` compares base/member/lane, making
`V.x` and `V.r` aliases but `V.x` and `V.y` distinct. Scalar and brace
checking require valid `ExplicitSelector` provenance tied to actual operand
locations. Reserved `NamedProjection` metadata is not admitted by these source
forms and carries no fabricated written selector. Owned validation rejoins the
base identity, declaration shape/type and source ranges. Calls, address bases
and named-array indices do not admit new component syntax or transplanted
component references; Fabric handles retain their separate identifier-only
grammar and reject either component origin. Hardware components remain a distinct registry-backed
domain with only its existing selectors.
The context-free standalone resolver preserves its previous
boundary: it accepts numbered registers and leaves symbol/type fields empty.
Owned-module validation traverses generated operand references structurally,
including register alternatives and nested operand containers. For every bound
register it compares cached scalar type, vector width, and register class with
the owned declaration. A mutated public IR cannot validate by retaining stale
register metadata after its binding changes; operand locations remain the
diagnostic source when available.
For every bound address symbol, the same structural traversal compares the
cached declaration type and guaranteed address alignment exactly with its owned
declaration, including an absent alignment. An operand cannot claim stronger
alignment or retain metadata from a different symbol binding.
An instruction's optional execution predicate is stored as the opcode-level
common field `std::optional<WithLocs<ResolvedPredicate>>`. Module resolution
requires it to bind to a `.pred` register, while standalone resolution accepts
a numbered `%pN` guard. `ResolvedBranchTarget` follows the same two-boundary
rule: module resolution stores the current function label's `SymbolId`, while
standalone resolution retains the source spelling with no symbol identity.

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

`ResolvedSpecialRegisterRef` retains the exact spelling, a stable
`SpecialRegisterId`, and an optional vector component. It does not store an
effective type that depends on an instruction or target. The independent
special-register semantic registry is the single source of truth for names,
stable identities, current declared element types, vector widths, and intrinsic
minimum PTX/SM targets; binding only reuses it for classification. A scalar
operand accepts a scalar special register or a component such as `%tid.x`, but
not an unselected vector base.

Read forms widened by the ISA are instruction semantics, not properties of the
register itself. The `mov` variant declares the special-register identity,
instruction width, effective type, and minimum PTX/SM in YAML
`operand_type_compatibilities`, which is generated into the checker descriptor.
The checker selects effective metadata only for that check and never mutates
Resolved IR. Current rules allow 16-bit `%tid`/`%ntid`/`%ctaid`/`%nctaid`
component reads from PTX 1.0 and 16/32-bit `%gridid` reads from PTX 1.0/1.3;
other uses retain the registry's current declared type and intrinsic
availability.

A single scalar variant carries a dynamic type modifier covering
`.b16/.u16/.s16`, `.b32/.u32/.s32/.f32`, and `.b64/.u64/.s64/.f64`. The checker applies PTX
fundamental-type compatibility: a same-width bit type agrees with any
fundamental type, signed and unsigned integers agree, and integer/float mixes
remain invalid. The `.f64` value additionally carries its SM 13 requirement.

`mov.pred` has a separate variant because its destination is a
`ResolvedPredicate` and its source is a `ResolvedPredicateSource`, structurally
unlike the classified scalar source. Module
resolution requires an unnegated `.pred` destination, but accepts a plain or
negated predicate source and retains its negation and stable `SymbolId` value.
The source may instead be a `ResolvedPredicateConstant` holding a canonical
Boolean value, or a `ResolvedPredicateSpecialRegister` retaining both the special
register reference and negation. Integer constants normalize nonzero to true;
an optional `!` then inverts that value. Constants have no synthetic `SymbolId`.
The canonical `pred_source` shape used by SETP accepts predicate registers and
integer constants, including negation; MOV's `pred_or_sreg` additionally accepts
predicate special registers. Neither shape admits floating constants.
Standalone resolution continues to accept numbered predicate registers without
declaration context.

`Mov::Scalar` owns the scalar dynamic type modifier
`.b16/.u16/.s16`, `.b32/.u32/.s32/.f32`, and `.b64/.u64/.s64/.f64`, including
the established `.b16/.b32/.b64` pack and unpack layouts.
`Mov::B128PackUnpack` is a distinct generated public variant with fixed
`.b128` for vector pack/unpack only. `ResolvedRegisterVector` stores two or
four optional `ResolvedRegisterRef` elements; an empty element is the
destination-only `_` sink. Resolution and checking require a bit-size
instruction type, equal total vector/instruction widths, no source sink, at
least one real destination register, and no sub-byte element.
`Mov::B128PackUnpack` carries PTX 8.3 / SM 70 availability.

`ResolvedFunctionRef` retains source spelling, a stable function `SymbolId`,
and the `.func`/`.entry` classification. A device-function address uses the
base PTX 1.0 availability of `mov`; a kernel-function address carries the PTX
3.1 / SM 35 requirement for target checking. Only a bare function name is
accepted; an offset form continues through data-symbol address resolution and
is rejected.

`ResolvedSymbolRef` retains source spelling. Module resolution also records the
declaration `SymbolId`, parameterized member, declaration kind, declared state
space, effective address state space, and representable declaration scalar
type. The two state spaces are identical for ordinary data variables. Direct
parameter memory addresses and kernel formal parameters used by `mov` retain
a `.param` address, while taking a device-function formal parameter address
with `mov` materializes it on the stack and produces a `.local` address. A
device-function formal-parameter `mov` address carries a PTX 2.0 / SM 20
baseline; a return-parameter address raises the PTX minimum to 6.0. A
function-local `.param` call-argument variable is a distinct bound symbol:
direct `ld.param`/`st.param` addresses retain `.param`, satisfy either parameter
direction, and require PTX 2.0 / SM 20; it remains non-addressable through `mov`.
Standalone resolution cannot
perform lexical binding, so it leaves identity and state-space fields empty as
it does for branch targets. `ResolvedMovSource` classifies registers,
immediates, special registers, data symbols, and address expressions after
binding, avoiding identifier-shape ambiguity during variant/layout selection.
Standalone resolution cannot tell whether an unbound name denotes data or a
function, so it remains a `ResolvedSymbolRef` with no identity.

A `ResolvedAddress` base is a variant of `ResolvedRegisterRef`,
`ResolvedImmediate`, and `ResolvedSymbolRef`. A bound register base must be an
integer or bit-size declaration no wider than 64 bits: floating declarations
and `.b128` are rejected before checker projection. This preserves PTX address
extension/truncation for narrower integer/bit declarations without treating a
known floating register as an unknown address. Declaration-free standalone
resolution has no type fact and therefore keeps the address base deferred. Its
optional offset retains the add/subtract operator and magnitude. A bracketed
memory address uses PTX's unsigned 32-bit immediate base and signed 32-bit
offset domain after that operator is applied: `-2147483648` is represented by
a subtraction magnitude of `2147483648`. Unbracketed `mov symbol+offset`
retains the separate signed 64-bit addend domain used by symbol-address and
relocation consumers. The shared IR continues to retain offset magnitudes as
signed 64-bit values; the 32-bit rule is a source-form legality check rather
than a relocation-domain narrowing.
A 32/64-bit integer or bit-size `mov d, symbol+offset` uses an unbracketed
address value restricted to an addressable data-symbol or formal-parameter
base. Scalar and braced-vector `ld`/`st` require bracketed dereference and
cover register, immediate, and bound-symbol bases. Each opcode uses
`GenericScalar`, `ExplicitScalar`, `GenericVector`, and `ExplicitVector`
variants. Their runtime type field accepts `.b8/.b16/.b32/.b64/.b128`,
`.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`, and `.f32/.f64`; the selected
`.b128` value requires PTX 8.3 / SM 70, and `.sys` with `.b128` requires PTX
8.4. Vector variants add a required runtime
`.v2/.v4/.v8` field, and the register-vector operand descriptor links its expected
element count to that field rather than duplicating variants per arity. Memory
vectors use element type policy: each register element is checked against the
instruction type, with `EqualOrWider` register width accepted. Legacy payloads
are capped at 128 bits; the generated cross constraint adds only exact 256-bit
`.v8` × 32-bit and `.v4` × 64-bit forms at PTX 8.8/SM 100, global when known,
with partial sinks. The default register-width policy is `SameWidth`, preserving
`mov/add/sub`, immediate, and special-register behavior. Only `ld` destination
and `st` source register descriptors select `EqualOrWider`, so the declared
register may be at least as wide as the instruction type. After that size
check, either side being a bit type is compatible, signed/unsigned fundamental
integers are mutually compatible, floats require the exact type/size, and
integer/float combinations remain incompatible. This covers wider load
destinations and store sources through 64-bit declared registers, including
store truncation. A wider actual `.b128` register is deliberately rejected by
the `EqualOrWider` policy for a narrower selected instruction; an exact `.b128`
declaration is accepted for a selected `.b128` memory instruction. Exact
`.b128` compatibility remains unchanged for existing `mov` vector consumers.
The focused [LD coverage](ld_coverage.md) and [ST coverage](st_coverage.md)
documents define the current memory subset.

Explicit loads accept `.const/.global/.local/.param/.shared`, while stores
accept `.global/.local/.param/.shared`. `WithLocs` retains both runtime
state-space/type modifiers and their source ranges. Explicit `.f64` adds SM 13
through modifier-value availability; generic `.f64` needs no separate SM rule
because the generic variant already requires SM 20. Generated operand views
translate a bound symbol's effective address space, rather than its declaration
spelling, and the checker reports `AddressStateSpaceMismatch` when it differs
from the runtime field. Generic operand descriptors carry a static bound-space
allowlist with per-entry availability: loads accept known
`.const/.global/.local/.shared` addresses, with `.const` requiring PTX 3.1,
while stores accept `.global/.local/.shared` and reject known `.const` or
`.param` addresses. Explicit `ld.const` itself remains in the PTX 1.0 basic
explicit baseline.

Legacy cache operators are modeled on the same scalar/vector `ld/st` variants
rather than by duplicating variants per cache spelling. Loads accept
runtime `.ca/.cg/.cs/.lu/.cv`, stores accept runtime `.wb/.cg/.cs/.wt`, and
every explicit cache spelling carries PTX 2.0 / SM 20 modifier-value
availability. When source omits cache, Resolved IR stores
`CacheOperator::Unspecified` with empty `locs`. That sentinel is intentional
provenance metadata, not a claim that PTX lacks an effective hardware default:
the ISA still makes omitted `ld` behave as `.ca` and omitted `st` behave as
`.wb`, while the IR preserves `Unspecified` so omitted source does not masquerade
as an explicit modifier and does not trigger cache-value availability checks.

Memory consistency is a generated cross-modifier descriptor, not a Cartesian
set of `ld/st` variants. `MemoryConsistency::Omitted` (empty `locs`) remains
distinct from explicit `.weak`; `.volatile`, `.relaxed`, `.acquire`, and
`.release` retain their modifier locations. The checker requires scope only
for relaxed/acquire/release, rejects cache with volatile/ordered/mmio forms,
uses known address-space identity without guessing unknown generic addresses,
and applies the global/shared, `volatile.local` PTX 9.1, and scalar
`.mmio.relaxed.sys` rules. A generated `memory_vector` cross constraint detects
arity > 4, payload > 128, or sinks; it requires a 256-bit payload, global space
when known, and PTX 8.8/SM 100. Partial sinks are valid only for those modern
load/store vectors; all-sink and legacy sinks remain invalid. Static natural
alignment covers scalar, legacy `.v2/.v4`, and modern 256-bit total access sizes
when the address is known.

`ResolvedAddress` separately records the enclosing function kind. Generated
address views derive an optional parameter direction only from a bound
`InputParameter`, `ReturnParameter`, or a function-local call argument; they do
not infer it from spelling.
For explicit `.param`, the generated operand constraint requires input
parameters for `ld` and return parameters for `st`. A known wrong direction
reports `ParameterDirectionMismatch` without stacking target diagnostics. A
device-function `ld.param`, and every `st.param`, apply the YAML-provided PTX
2.0 / SM 20 function-availability constraint. A kernel input `ld.param`
retains the explicit-form baseline, while an address with unknown identity is
not assigned a direction. Its known device-function provenance still triggers
the load constraint; an unknown standalone load context does not. This
contextual rule does not change `ResolvedSymbolRef::address_availability`,
which continues to describe address-value semantics such as `mov`.

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

A fixed modifier is not mutable per-instance state. In the merged `Add::Sat`,
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
2. `select_variant_name` selects exactly one semantic form from those form-local
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

`select_variant_name` is the non-template descriptor matcher. At the resolver
boundary its selected name maps once to a typed form index; subsequent
construction uses typed dispatch. Generated final class definitions and
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

Each final form's virtual `Instruction::check` override uses common checking for:

- membership of every projected dynamic modifier value in the selected
  variant's generated semantic domain. This check is independent of source
  locations and modifier spelling-presence: an omitted optional modifier is
  checked using that field's declared default, while an out-of-domain edited
  value with no provenance remains invalid and uses the instruction range as
  its diagnostic fallback. `ModifierValueDomainMismatch` reports a value
  outside this domain.
- minimum PTX version, SM version, and target family for the variant, selected operand layout, and actual modifier value;
- layout-tag bounds;
- layout-tag and required/forbidden optional-field agreement;
- operand field identity, resolved shape, and immediate or bound-register
  declaration types from structured descriptors.
- special-register intrinsic metadata and contextual type/availability selected
  by the current instruction width; this creates only a temporary checking view
  and does not change Resolved IR.
- static generic state-space allowlists and explicit modifier-derived
  constraints against known effective bound-symbol address spaces, including
  per-entry target availability; unknown register/immediate/standalone bases
  are not inferred.
- explicit `.param` input/return direction and function-context availability
  from the generated operand constraint; direction mismatches take precedence
  over that contextual availability.

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

Generated vector projections also accept caller-constructed or mutated public
IR without requiring a separate vector-size preverification pass. They retain
the original width/element count in `OperandView::vector_arity` and bound writes
to the fixed-capacity element arrays. The common checker rejects zero or
over-capacity vector counts with `InvalidVectorOperand` before inspecting
elements; supported counts still undergo their instruction-specific checks.
An oversized payload is never narrowed or clamped into a valid vector. This
guarantee covers vector projection sizes, not every possible malformed-IR
invariant or cross-instruction constraint.

`rule_id` is reserved for typed instruction-specific rules. Register visibility
and `.reg` state space are checked during module resolution; the common checker
handles generated address-space constraints, while cross-instruction
constraints remain outside its ABI.

## Extension rules

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

Direct/indirect-call ABI plus function-local call-argument `.param` memory, qualified
`::entry`/`::func` forms, and call adjacency/predication constraints are covered
by module resolution. Scalar `.b128` is rejected by the generated `Mov::Scalar`
type domain; declaration-type availability for wider `.b128` registers remains
outside this slice. Legacy scalar/vector `ld`/`st` cache operators, PTX 8.8 modern memory
vectors, static memory-address alignment, and memory-consistency qualifiers are
covered here.

## Owned video operands

`ResolvedVideoOperand` owns a located `RegOrImm`, an optional located `VideoSelector` variant, a written register-negation Boolean and optional minus range. Identifier locations remain separate from the enclosing selector/negation range for binding diagnostics. Source arrays preserve written high-to-low digit order and index concatenated A+B carriers. `video_default_selector` and `video_effective_selector` expose defaults without inventing written provenance. Generated forms expose static `video_lanes`/`video_operation`. `video_mad_interpretation` derives product/input-C/final signs independently of written dtype; numeric negative constants never become register-negation controls. Public helpers are in `<ptx_frontend/resolved_ir/ptx_video.hpp>`. Checker operand views borrow owned Video payloads only for synchronous checking; module reference traversal uses the inner carrier's identifier locations.
