# C++ Resolved IR Design

## Integrated vector data movement

The complete-module regression chains below exercise parsing, binding, resolution
and checking together. They also validate owned IR after the original source and
AST are destroyed and the module is copied and moved. This is frontend evidence,
not GPU execution, broadcast/truncation semantics or assembler parity.

| Supported chain or boundary | Preserved contract and compatibility |
| --- | --- |
| Named v4 f32 LD → scalar component → named/brace MOV → ST | One real declaration identity underlies canonical lanes; `.xyzw` and `.rgba` are explicit ordinary-register selectors, while named projections retain the whole identifier range without fabricated selectors. Ordinary `.w` does not add hardware `%tid.w`. |
| v2 u32 constant-expression MOV → alias/overlap MOV → ST | Folded typed values, parameterized register members and source form survive ownership transfer. Destination aliases of one lane are duplicates; repeated sources and source/destination overlap remain legal. |
| Named v2 f64 NC/LDU → MOV → ST | Memory and MOV retain their own type policies. Existing availability remains distinct: NC needs PTX 3.1 / SM 32, generic LDU PTX 2.0 / SM 20, explicit-global LDU PTX 2.0 plus f64 SM 13, and f64 MOV SM 13. |
| Named-array constant address → vector LD → MOV → ST | A declared `.v4 .f32 A[]` still has scalar index stride four bytes, not sixteen. `A[4]` contributes sixteen bytes and can satisfy v4 alignment; a dynamic index reduces the known alignment to `gcd(base alignment, 4)`, so the same vector access is rejected when only four-byte alignment is guaranteed. |
| Adjacent unsupported combinations | Atom/red remain brace-only; scalar bit pack/unpack accepts exact-width named ordinary aggregates and retains its sink and B128 gates; floating instruction types and scalar-to-scalar B128 remain excluded. Modern 256-bit memory uses legal scalar brace lanes, not oversized ordinary named declarations or widened vector MOV. |

Owned revalidation checks complete source forms, component origin/order, actual
declaration width, type/arity and source ownership, with located failures for
tampered payloads. These integration checks supplement the individual family
regressions; they do not imply the full ISA cross-product or a stable 1.0 API.

## Explicit vector MOV

The existing `mov_v4_u32` / `MovV4U32` semantic identity now covers explicit
`.v2`/`.v4` MOV with `b/u/s16`, `b/u/s32`, `f32`, `b/u/s64`, and `f64`, with
at most 128 total bits (so no `.v4` × 64). It retains PTX 1.0 availability and
the f64 SM 13 floor. This is element-width MOV, not scalar bit-aggregate packing.

Public API migration: `MovV4U32::dst` is now `WithLocs<ResolvedRegisterVector>`;
`src` is `WithLocs<ResolvedMovVectorSource>`. The latter is a variant of
`ResolvedMovValueVector` (ordered `ResolvedMovVectorLane` register/immediate/
scalar-special values plus common `ResolvedVectorSource`) and the existing
`ResolvedVectorSpecialRegisterRef`. Its typed reference callback is
`IReferenceObserver::mov_vector_source`. Generic `RegOrImm`, `ResolvedValueVector`,
and scalar `ResolvedMovSource` have not widened.

Named and braced destinations/sources combine freely with exact arity and scalar
MOV same-width type compatibility, never memory widening. Named source/base/lane
identity, member, canonical order and exact whole-identifier locations reuse the
complete named provenance contract below; only that container admits implicit
projections. Brace destination sinks require at least one real register and no
duplicate physical destination (aliases count as duplicates). Source repeats and
source/destination overlap are legal. Brace sources accept scalar ordinary or
explicit-component registers, numeric literals/constant expressions and scalar
special reads. Each special lane retains its real registry identity, component,
historical scalar MOV type compatibility and PTX/SM/capability checks.

Whole hardware vectors remain separate width-four intrinsic-u32 reads, usable
only with u32/s32/b32 and their existing availability. No fourth scalar hardware
selector is invented. Sinks, predicate lanes, nested whole vectors, addresses,
functions and symbols are not source lanes. Bare scalar vector sources such as
`mov.v2.u32 V, 1` remain an unsupported frontend subset, not an ISA-invalid claim
or a broadcast/truncation promise. Scalar `.b16/.b32/.b64/.b128` pack/unpack
accepts braces or a complete ordinary named vector with exactly matching total
width, retaining B128 PTX 8.3 / SM 70 gates. Floating instruction types and
scalar-to-scalar B128 forms remain excluded. Other family fences and
modern 256-bit brace memory forms are unchanged. Owned validation rejoins complete
base/lane declarations and source ownership after source release/copy/move.

## Named array addresses

Dedicated address-source MOV and generic address operands accept `A[index]`,
including LDNC/LDU, atomic/reduction, cache, asynchronous-copy, barrier, tensor-map,
response-object and fabric local-memory addresses. Admission follows each typed
binding: explicit register-only async data addresses remain restricted, independently
of generic barrier-object siblings. Tensor operands, fabric handles and texture/surface
accesses retain their separate grammars. Generated source and owned checks use the
same role policy; existing target, state-space, alignment and control rules still apply.
The named form does not compose with a `.unified` suffix. Known unified storage
still requires the existing bracket-qualified LD path and remains read-only for
ST; MOV may take its address, including named indexing, without dereferencing it.

`ResolvedAddress::named_index` owns an integer index's bits/signedness or the
exact bound scalar register, optional integer displacement, written operator,
scalar byte stride and source ranges. It is mutually exclusive with the old
byte offset. `A[1]` scales by the declaration's scalar base element size:
u32, v2.u32 and multidimensional u32 arrays all use four bytes, while u64 uses
eight. Instruction data types, transfer granules and opaque object sizes do not
determine this scalar stride. Array shape comes from owned storage/parameter records keyed by SymbolId;
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

## Current validation limits

The entry point contracts below describe modeled checks, not exhaustive PTX
validation. Header/target, storage and ABI checks and their remaining boundaries
are recorded in [current coverage](converage/coverage_status.md). Complete source context is distinct
from completeness of the validator; runtime behavior remains outside this layer.

## Status and scope

This document describes the implemented Resolved PTX IR, not a future CFG,
SSA, or backend IR. The frontend's core flow is:

```text
PTX source -> Token stream -> Syntax AST -> symbol binding -> Resolved IR -> checker
```

Syntax AST preserves source spelling, modifier order, and `SourceRange`.
Resolved IR records the selected semantic instruction form, resolved operand values,
and diagnostic locations. These are distinct frontend boundaries; public API compatibility is still pre-1.0. Lexical symbol
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
independent identity for a scalar General integer/bit `.reg` indirect target or a bound
function-local `.callprototype`/`.calltargets` label. Its enclosing
`ResolvedFunction` owns the matching ordered metadata payload and normalized
ABI separately, so an operand remains compact without making the metadata
unavailable. The final `CallDirect` class now has three additional
`IndirectCall` layouts (target/metadata, target/input/metadata, and
return/target/input/metadata), each available from PTX 2.1 / SM 20; normal
module indirect calls preserve the bound target and metadata identities, then
reuse the direct-call ABI contract through metadata-indexed canonical
signatures. ABI comparison does not create a second indirect-call model.

The operand-level `ResolvedIndirectCallee` `std::variant` additionally contains
`ResolvedCallTableRef`, whose ID is the actual
storage variable, not a metadata label. `ResolvedModule::call_tables` owns only
consumed tables, including scope/owner, declaration provenance, ordered explicit
slots with byte offsets and bound/canonical function IDs, exact initializer and
target-token ranges, and the full common signature. Sparse omitted tails do not
create slots. Owned validation reconstructs each contract from real storage,
initializer references and canonical function contracts, rejoins lexical flist
uses and prior declaration occurrences, and rejects missing/extra/stale contracts
or coherent cached-field mutations. Visitors expose the active
`ResolvedIndirectCallee` operand alternative.
No borrowed AST data or runtime pointer-membership proof is retained.

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
The explicit validation catalog includes historical `sm_10`, `sm_11`, `sm_12`,
`sm_13`, and `sm_20` with no modern capabilities or family features, allowing
legacy source contexts and the PTX 6.0 `sm_20`/`sm_30` declaration boundary to be
checked consistently; arbitrary numeric target spellings remain unrecognized.
Support is limited to catalog targets, rather than every historical GPU profile.

`ResolvedSourceTargetRegion::map_f64_to_f32` owns the effective programmer option
decoded only from the exact `map_f64_to_f32` spelling in that region's ordered
`target_options`. Every `.target` resets the option; omission means false even
after a region that enabled it. Both AST-backed and owned validation pass it to
`checker::TargetInfo::map_f64_to_f32`. Owned validation rejects contradictions
between the Boolean and retained spellings. This option is separate from hardware
capabilities and does not change f64 instruction identity, storage, or operand
widths. At PTX 1.4 or later, `div.rn.f64`, `rcp.rn.f64`, and `sqrt.rn.f64` accept
exact generic `sm_10`, `sm_11`, or `sm_12` only with this option; their existing
`sm_13` and later availability remains available without it. Other f64 forms,
directed rounding, and f32 forms keep their own gates. This frontend preserves
and validates the source option; it performs no lowering or emulation.


`RequireCompleteContext` also requires an initial `.version`, then `.target`,
then optional `.address_size`, before other module items. A version or address
size cannot appear later; subsequent `.target` directives still open valid source
regions. Header-only modules use the same rule, and an empty module lacks complete
context. `ResolvedModuleHeader::source_order` owns typed traversal events so this
check remains possible after source and AST destruction. Missing or inconsistent
order metadata cannot certify complete context. `AvailableContext` and
`resolveModuleOnly()` preserve fragment and out-of-order-header compatibility.
The AST overload checks the supplied AST's order when validating a replacement
source configuration.

Every `.target` item is classified: its first item must be an architecture
spelling, and later items may be `texmode_unified`, `texmode_independent`, `debug`,
or `map_f64_to_f32`. Unknown options and additional architecture items are
rejected during parsing and by both validators. Lexically valid unrecognized
targets remain parseable and produce `UnknownTarget` at validation. For every
explicitly cataloged `sm_<number>[a|f]` profile, `compute_<number>[a|f]` is a
synonym with the same architecture, flavor, enabled family features and
capabilities. Both lexical parsing and owned source regions preserve the written
prefix; semantic availability and explicit checker-target comparisons use
validation-feature architecture and flavor through the explicit catalog rename
correspondence. `TargetIdentity::operator==` still compares retained
source spelling as well, so source-provenance consistency checks remain exact.
Unknown numbers and unsupported flavors, including `compute_102a` and
`compute_102f`, do not acquire catalog profiles. Texture-mode
selection is checked for repetition and module conflicts. This contract adds neither
DWARF content checks nor floating-point instruction conversion. These source forms follow the archived
[PTX ISA 9.3 target directive](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target).


Header compatibility is checked in both validation policies, including header-only
modules and every later target region. `.version` requires PTX ISA 1.0 or later;
explicit `.address_size` requires 2.3, while an omitted address size retains the
32-bit default at earlier versions. Both explicit texture mode options require
1.5, and `debug` requires 3.0. `AvailableContext` defers checks that need an absent
version or target, but rejects contradictions for the context already supplied.
Replacement-AST validation uses that AST's header; owned validation rechecks
retained values, option spellings, provenance and traversal events after source
destruction. An explicit address-size event cannot become implicit merely by
changing its owned provenance.

The existing target catalog is the single owner of exact profile introduction
versions. `base::TargetProfile::minimum_ptx_version` exposes the base-owned
`PtxIsaVersion` components, independently of `checker::PtxVersion`.
`supports_map_f64_to_f32` is true only for exact generic SM10/11/12 profiles and
their compute synonyms; recognized other profiles reject that option even when
no version is supplied. This follows the archived target table and legacy option
inheritance, corroborated by CUDA 13.3 `ptxas` rejecting the option on SM13 or
higher. It does not add instruction emulation. Unknown target keys still produce
`UnknownTarget` and receive no guessed minimum or option permission.

The pinned [PTX ISA 9.3 target table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target)
sets these minima (both `sm_` and `compute_` prefixes):

| Exact architecture/flavor suffixes | Minimum PTX ISA |
| --- | --- |
| 10, 11 | 1.0 |
| 12, 13 | 1.2 |
| 20 | 2.0 |
| 30 | 3.0 |
| 35 | 3.1 |
| 32, 50 | 4.0 |
| 37, 52 | 4.1 |
| 53 | 4.2 |
| 60, 61, 62 | 5.0 |
| 70 | 6.0 |
| 72 | 6.1 |
| 75 | 6.3 |
| 80 | 7.0 |
| 86 | 7.1 |
| 87 | 7.4 |
| 89, 90 | 7.8 |
| 90a | 8.0 |
| 100, 100a | 8.6 |
| 120, 120a | 8.7 |
| 100f, 103, 103a, 103f, 120f, 121, 121a, 121f | 8.8 |
| 88, 110, 110a, 110f | 9.0 |

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
The owned symbol table additionally retains source-ordered `.file` declarations,
including exact filename and optional numeric metadata tokens and ranges, after
source/AST destruction. Both validation overloads recheck normalized index
uniqueness, first-symbol association, payload boundaries, declaration coverage,
and basic/inline `.loc` file-reference targets. Replacement source independently
rebinds its debug metadata: a valid replacement filename or index need not match
the original. Bound `.debug_str` identities also validate `.loc` metadata, but
`.file`, `.loc`, `.section`, and `.pragma` add no executable Resolved IR nodes or
instruction attachment.

A present `file_size` is one checked unsigned64 integer token, including zero
(unavailable) and UINT64_MAX. Decimal, leading-zero octal, binary, hexadecimal and
optional `u`/`U` suffixes use the existing checked integer-magnitude decoder;
overflow, signs, floating values, expressions and noninteger leaves reject without
truncation. Its raw text/range and absence remain owned; no decoded-size cache is
added. Current symbol-table sizes and supplied/rebound sizes are checked
independently before optional context gates. Invalid current data cannot be
masked by valid replacement metadata, while independently valid different `.file`
datasets retain debug-neutral source identity. Timestamp's separate `time_t`
domain and existing index, filename, optional-pair, range and reference rules are
unchanged.

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

Copies, assignments and moves own the complete vector alongside the module table.
Function-only copies do not acquire module advisory metadata. Current records are
intrinsically checked in both validation APIs. Supplied AST projection is checked
independently; its version selects the PTX 2.0 introduction, so valid neutral
replacement may remove pragmas and lower to PTX 1.5 without an old-presence veto.
AST-free availability uses the current module version. Known old versions reject
even without target context; only unknown version defers under `AvailableContext`.
Invalid current data cannot be masked by valid replacement, or vice versa. This
public layout addition requires consumer rebuild. The explicit public
`ptx_syntax_sections.hpp` value dependency supplies `AstSyntax` while `AstModule`
and `AstInstruction` remain incomplete through the module-only header boundary.

Explicit `.file` timestamp/file-size metadata (including `0, 0`) requires PTX ISA
3.2 or later; plain `.file` has no such gate. This presence-based check has no SM
or debug-option requirement and rejects a known older version even without a
recognized target. `AvailableContext` defers an absent version; strict validation
retains its existing completeness requirements. Owned validation uses the retained
header version and file records. AST-backed validation uses the supplied AST's
version and independently rebound file metadata, so replacement metadata and
versions are checked together. See [PTX ISA 9.3 §11.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-file).

`ResolvedFunction::parameter_declarations` owns validated `.param`
declarations: return formals first, input formals next, then body-local declarators
in lexical traversal order (including nested blocks). Each
`ResolvedParameterDeclaration` retains its `symbol_id`, `scope_id`, role
(`EntryInput`, `DeviceInput`, `DeviceReturn`, `BodyLocal`, or `LegacyEntryInput`), fundamental
`scalar_type`, effective byte `alignment`, `explicit_alignment`, `vector_width`,
outer-to-inner `array_extents`, checked `byte_extent`, and optional pointee
properties. Scalar shapes have no array extents; supported unsized device input
arrays retain a null outer axis and checked known inner axes, with no byte extent. The owning function and symbol
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

Complete-module direct calls require a prior declaration or definition of the actual
bound device-function SymbolId. The shared source/owned pass uses retained lexical
events and declaration occurrences at exactly one positive target range inside its
call; unknown or ambiguous ordering is rejected. Compatible prior prototypes and
modern self-recursion work. Alias visibility uses the alias identity, not an earlier
canonical target. Copied/cloned owned calls may retarget to another compatible
already-declared function at that event without rejoining diagnostic spelling;
later-only targets and malformed use locations fail. Source association remains
independent. Instruction fragments retain partial-context resolution, while indirect
register/metadata/table calls keep their separate rules.

`<ptx_frontend/semantic/ptx_function_contract.hpp>` exposes the canonical
function-signature contract without a Syntax AST dependency. Its parameter
contracts use semantic state-space and pointer-space enums, `ScalarType` and a
typed vector shape, retained invalid spelling for diagnostics, and optional
tagged numeric values: omitted, validated constants, or invalid structural
keys. Direct-call ABI validation consumes those normalized values without
reparsing alignment text or the former array-extent string protocol; invalid
structural data is never silently replaced by a default.

Call-staging adjacency follows the owned executable instruction sequence. Variable
and debug directives, metadata declarations, bare labels and lexical braces are
transparent. Transfers through a nested block may use the same still-visible outer
object; distinct shadowed declarations and out-of-scope actuals remain invalid.
Every intervening instruction interrupts adjacency, and transfers cannot be
predicated; the call itself may be predicated. Both resolution and AST-free
validation derive the same bound object/member identity from byte-address and
named-array forms, including symbolic indices. Offsets do not change which local
object the call must use. This check does not prove runtime index bounds, byte
initialization coverage or correctness across arbitrary incoming control paths.

Parameter records also retain fundamental `.f16x2` and legal `.param` v2/v4 shapes,
with lane-aware extents, natural vector alignment and AST-free padded entry limits.
The [parameter declaration contract](parameter_declarations.md) distinguishes packed
scalar dtype from explicit vector lanes and the numeric packed-literal reference gap.

The owned symbol table also retains ordered `.loc` events, without creating
executable instructions or function-local duplicate payloads. A common validator
checks both current owned records and independently rebound replacement records.
An `inlined_at` triple must match an earlier **base** triple in module lexical
order; the current base is inserted only after checking its reference. Nested
blocks, function boundaries and target regions do not reset that stream. Physical
ranges check ownership and within-record field order, never event sorting. Actual
function-definition occurrence scopes, not a canonical function's first owned
scope, associate records with bodies. Both datasets must be intrinsically valid;
different valid debug datasets remain compatible with debug-neutral source identity.
Numeric triples compare unsigned magnitudes across radices and suffixes, including
zero line/column and full uint64 values. This is frontend capacity, not a DWARF
width claim. File indices must independently bind to `.file`; function names must
bind to current typed `.debug_str` identities. Basic `.loc` requires PTX 1.0 and
inline attributes require PTX 7.2, with no architecture floor. Absent version may
defer availability under `AvailableContext`, never numeric, identity or precedence
failures. No instruction attachment, decoded line table or string-address execution
is implied.

A private iterative SCC check recomputes current canonical local-definition call
relations on each validation. Direct cycles are syntactic recursion; cycles that
need consumed finite target-set/table alternatives are statically possible
recursion. Both require independently known PTX 2.0 / SM 20 at every participating
caller's selected context; source-aware validation substitutes supplied context
while keeping the current owned graph. An original acyclic AST cannot mask a
coherent current callee edit. Copies and cloned calls retain no stale graph state.
Call identity/ABI/table joins precede the graph, and non-call instruction lanes
are not walked for edge collection. No public graph/cache is retained. Open
external/prototype/dynamic edges preclude whole-program acyclicity claims, and
`AvailableContext` deferral is not an acyclic certificate. This is no CFG or
runtime stack/linker execution model.

Current weak linkage in canonical symbols, storage occurrence records and
function contracts requires known PTX 3.1, independently of target context.
Every Weak symbol, including a symbol-only function, and storage record uses the
common module version guaranteed by the existing equality of all region versions.
Function occurrence contracts also use each exact selected region. Either retained function contract or canonical weak symbol is
sufficient, including unused prototypes. Source-aware validation checks current
facts under supplied context and independently checks every supplied weak
occurrence; weak qualifiers remain nonneutral source identity. Missing version
alone may defer under `AvailableContext`, while strict context and unknown-target
diagnostics remain. Copies retain existing typed facts; no cache-equality,
historical qualifier registry, public layout or linker simulation is added.

Current retained `.branchtargets` and `.calltargets` each require PTX 2.1 / SM 20,
including unused and nested declarations. Their availability reuses the same
minimum-only mechanism below: exact selected-region context when AST-free,
supplied active context for current records and independently supplied typed
nodes when source-aware. Known failing prerequisites reject before full context
is available. Target-list payload remains nonneutral source identity; these
checks add no CFG, execution, public layout or new member/signature policy.

Current retained call prototypes are checked even when unused or nested. Their
base requires PTX 2.1 / SM 20, `.noreturn` requires PTX 6.4 / SM 30, and either
ABI-preservation suffix requires PTX 9.0 / SM 80. AST-free validation uses exactly
the function's validated selected region. Source-aware validation checks current
prototype fields and independently supplied prototype syntax with the supplied
active context; version/target retargeting adds no historical payload veto.
These four conjunctive minimum-only rules evaluate known version and recognized
profile independently: a known failure rejects even if the other fact is absent.
Only unknown prerequisites defer under `AvailableContext`; strict missing-context
checks and unknown-target diagnostics remain. Target events clear the active
recognized profile, and no guessed version, target number or generic partial-DNF
interpretation is introduced. Ordinary function/instruction availability retains
its full-context gates. Counts, variants, identities and signatures retain their
existing contracts; this does not prove physical register preservation.

## Named memory-vector sources

Ordinary `ld`/`st`, `ld.global.nc`, and `ldu` accept a named ordinary `.reg .v2`
or `.reg .v4` data operand alongside braces. Declaration width must equal arity,
with at most 128 declared bits; memory equal-or-wider element rules remain.
Modern 256-bit vectors, asynchronous stores, atomic/reduction, matrix and texture
operands retain their separate brace restrictions. Scalar bit pack/unpack also
admits named ordinary vectors through its exact aggregate-width policy below.

At the common checker boundary, an effective scalar `Register` operand cannot
carry a whole-vector `ResolvedRegisterRef::vector_width`. An explicit component
remains scalar: its effective width is absent and its component metadata retains
the owning declaration width. Actual `Vector` operands keep their whole base and
ordered lane provenance under their dedicated checks. These rules also apply to
AST-free copies and instruction clones.

`ResolvedRegisterVector::elements` retains the lanes. Its owned `ResolvedVectorSource`
distinguishes `BraceList`/`NamedVector`, saves the full written range, and only for
named form owns the real `WithLocs<ResolvedVectorRegisterRef>` whole base. Ordered
lanes share its SymbolId, parameterized member and declared element type; each
has `NamedProjection` origin, canonical lane and actual declaration width, absent
effective `vector_width` and no selector. All base/lane locations repeat the exact
written identifier range; no `.x` text or selector range is fabricated.

Only a complete valid named container plus an opted descriptor admits projections.
Scalar/brace consumers still reject them. Reference traversal includes wholebase
and each lane; AST-free owned validation rejoins declarations and source ownership.
Actual braces always retain their full range. Legacy standalone manual braces may
omit it, but malformed supplied ranges reject; owned validation requires full
provenance and instruction/lane containment. Named provenance is always mandatory.
Data-source admission is independent of the generic array-address role policy.
No GPU execution or assembler parity is claimed.

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

`AstImmediateKind` retains the lexer's literal classification, including
`BinaryInteger` for `0b`/`0B` spellings. Decimal, octal, binary, and hex
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
convert to `F32`, `F64`, and equal-source-width `B64`; a single leading `+`
is normalized at decimal decoding, while a leading `-`, signed zero, and exponent signs retain
their normal floating semantics. Exact-F32 `0f` literals reject parentheses
and both unary signs, including lexer-compacted signed spellings. Exact-F64
`0d` literals admit unary signs through the shared numeric conversion, preserving negative zero.
`0f<8 hex>` and `0d<16 hex>` are raw IEEE bit patterns for `F32` and `F64`
respectively. Scalar data slots that already admit constants also accept
unsigned direct exact-F32 into `B32`, and decimal/exact-F64 literals or
supported binary64 expressions into `B64`. This uses source precision rather
than arbitrary floating conversion: exact-F32 into `B64`, binary64 sources into
`B32`, and floating sources into `B8`/`B16`/`B128` are rejected. Typed immediates
retain their bit destination and raw payload without integer-source provenance;
owned call literal revalidation retains lexical source kind and spelling.
Integer-only controls and register-only slots keep their role requirements.
Exact-F32 operators remain excluded, including unselected branches. Other
floating formats require explicit quantization rules and must not silently take
the integer path.

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
locations. `NamedProjection` metadata is not admitted by these source
forms and carries no fabricated written selector. Owned validation rejoins the
base identity, declaration shape/type and source ranges. Ordinary address bases
and named-array register indices accept explicit `.xyzw/.rgba` scalar selections
through this same carrier. For example, `[V.x+4]` retains a byte displacement,
while `A[V.x-1]` retains the array's scalar-element stride and written subtraction.
The array object remains a data identifier. The selected owner must be a real
General `.reg .v2/.v4` declaration, within its existing 128-bit total-width cap;
its scalar element must satisfy the existing integer/bit address role no wider
than 64 bits. Group member identity and lane identity remain independent.
Dynamic alignment, checked scaled memory offsets, and MOV's wider offset domain
are unchanged; no runtime address, index bounds, layout or allocation is inferred.

This applies ordinary scalar-element inheritance to existing address/index
roles, rather than claiming that the manual explicitly illustrates these source
spellings. The public CST/AST variants append the existing member alternative,
so public producers and consumers must rebuild together. Exact written selector
aliases retain different source identities. Coherent current owned alias/lane
edits remain valid without authenticating producer history; malformed current
metadata cannot be hidden by a valid supplied AST. Component leaf ranges must
remain inside their address and instruction, and the selected physical owner
keeps its 128-bit cap, even when target checks defer. `AvailableContext` retains
the existing deferral of full instruction role checks when target context is
missing; it is not complete validation.
Calls admit explicit ordinary selected scalar inputs, returns and indirect register targets. Fabric, opaque-resource and tensor-map
roles retain their separate semantic exclusions despite shared address-child
parsing. `NamedProjection` does not impersonate an explicit scalar selector.
Hardware components remain a distinct registry-backed domain with only its
existing selectors.
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

Predicate constants, logical `!`, and WGMMA scale-d numeric controls require an
integer result source before truth or range conversion. A `B64` conversion
carrier does not establish integer origin: decimal/exact-F64 literals and
floating-valued expressions remain invalid in these roles, even with payload
zero or one. Floating comparisons that return an integer, and `!` of that
integer result, remain valid. Equal-width floating `B64` bit-data operands keep
their separate compatibility rule. Owned predicate constants contain only a
canonical Boolean; validation does not reconstruct source syntax erased by
normalization.

`MovScalar` owns the scalar dynamic type modifier
`.b16/.u16/.s16`, `.b32/.u32/.s32/.f32`, and `.b64/.u64/.s64/.f64`, including
the established `.b16/.b32/.b64` pack and unpack layouts.
`MovB128PackUnpack` is a distinct generated public variant with fixed
`.b128` for vector pack/unpack only. `ResolvedRegisterVector` stores two or
four optional `ResolvedRegisterRef` elements; an empty element is the
destination-only `_` sink. Resolution and checking require a bit-size
instruction type, equal total vector/instruction widths, no source sink, at
least one real destination register, and no sub-byte element.
`MovB128PackUnpack` carries PTX 8.3 / SM 70 availability.

The existing aggregate layouts accept ordinary named `.reg .v2`/`.v4` vectors
alongside braces. Arity comes from the bound declaration, not an implicit MOV
modifier. Named source metadata retains the real whole base and canonical
`NamedProjection` lanes with identical identifier ranges, declaration type,
`SymbolId` and parameterized-member identity; no explicit component selectors or
synthetic brace syntax are introduced. Layout selection uses declaration shape
only at descriptor-authorized aggregate/scalar alternatives. Unknown names and
context-free inputs never imply a named aggregate; existing scalar behavior and
diagnostics remain, including declaration-free scalar symbol-address sources.
Width/type and target errors do not retry another layout. Scalar and modifier-typed ordinary vector MOV keep
their existing layouts. Source form, opcode, type and source identity survive
owned module copy and instruction cloning.

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
with partial sinks and load destinations that contain only sinks. The default register-width policy is `SameWidth`, preserving
`mov/add/sub`, immediate, and special-register behavior. `ld` destination
and `st` source register descriptors, together with applicable `cvt` data
operands, select `EqualOrWider`, so the declared
register may be at least as wide as the instruction type. After that size
check, either side being a bit type is compatible, signed/unsigned fundamental
integers are mutually compatible, floats require the exact type/size, and
integer/float combinations remain incompatible. This covers wider load
destinations, store sources (including truncation), and applicable conversion
operands through `.b128` declared bit registers, following PTX ISA 9.3 Tables
27/28. Central declaration semantics classifies `.b128` as fundamental while
rejecting instruction-only declaration formats and register vectors over 128
bits. Declaration eligibility does not bypass instruction, format, PTX-version,
or SM gates: selected `.b128` memory instructions still require PTX 8.3 / SM 70.
The unsuffixed `isspacep` address-source role instead selects `WordOrDoubleword`
with a fixed u64 use type: ordinary B/S/U32 and B/S/U64 registers are legal,
independently of module address size; wider, floating and packed containers remain
excluded. This finite address-role rule does not restrict general data carriers.
Exact `.b128` compatibility remains unchanged for existing `mov` vector consumers.
The focused [LD coverage](converage/ld_coverage.md) and [ST coverage](converage/st_coverage.md)
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
load/store vectors. Ordinary write-only load destinations also accept fully
represented brace tuples containing only sinks, with a selected `.v8` type
`.b32/.s32/.u32/.f32` or `.v4` type `.b64/.s64/.u64/.f64`. The private grant
joins actual owning null lanes, exact selected type/arity, complete brace/lane
ranges, and consistent borrowed projections. It adds no public descriptor flag
and does not infer type from null lanes. All-sink stores, legacy sinks, named
roots, MOV and texture results retain their existing restrictions. The global,
32-byte alignment and PTX 8.8/SM 100 checks still apply. Incomplete-context
`AvailableContext` success retains its existing instruction-check deferral and
is not a feature-availability guarantee. Static natural
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
by module resolution. Scalar `.b128` is rejected by the generated `MovScalar`
type domain; wider `.b128` carriers use the central register declaration rules
and the selected instruction's width policy. Legacy scalar/vector `ld`/`st` cache operators, PTX 8.8 modern memory
vectors, static memory-address alignment, and memory-consistency qualifiers are
covered here.

## Owned video operands

`ResolvedVideoOperand` owns a located `RegOrImm`, an optional located `VideoSelector` variant, a written register-negation Boolean and optional minus range. Identifier locations remain separate from the enclosing selector/negation range for binding diagnostics. Source arrays preserve written high-to-low digit order and index concatenated A+B carriers. `video_default_selector` and `video_effective_selector` expose defaults without inventing written provenance. Generated forms expose static `video_lanes`/`video_operation`. `video_mad_interpretation` derives product/input-C/final signs independently of written dtype; numeric negative constants never become register-negation controls. Public helpers are in `<ptx_frontend/resolved_ir/ptx_video.hpp>`. Checker operand views borrow owned Video payloads only for synchronous checking; module reference traversal uses the inner carrier's identifier locations.


## Conversion special-register sources

Ordinary `cvt` (PTX 9.3 section 9.7.9.22) owns `ResolvedCvtSource`, a
register/immediate/special-register union. Scalar read positions retain their
original immediate admission; destinations, vector register sources and
`cvt.pack` keep their existing contracts. Symbol and address values are excluded.
The checker validates intrinsic identity, indexed-family bounds and component
selection before consuming metadata. Presentation spelling does not determine
semantics. Special sources follow the operand's register-width policy, including
integer chops for wider declarations, while integer/float incompatibility remains.
CVT declares the same historical 16-bit thread/grid-component and 16/32-bit
grid-ID read compatibilities as MOV, using source use-width. Intrinsic identity
and current metadata remain unchanged; contextual availability applies per check.

## Historical omitted floating controls

For `1.0 <= PTX < 1.4`, thirteen exact forms preserve omitted source controls:
`div.f32`, `rcp.f32`, `sqrt.f32`, `rsqrt.f32`, `sin.f32`, `cos.f32`, `lg2.f32`,
and `ex2.f32` have `FloatingPointDefault::ApproximateFlushToZero`; `div.f64`,
`rcp.f64`, `sqrt.f64`, and `mad.f64` have `RoundNearestEven`; `rsqrt.f64` has `Approximate`
without FTZ. Their final classes expose `inline static constexpr
floating_point_default` and no source mode/rounding/FTZ members. DIV also omits
`.full`. Resolution never inserts explicit modifiers or rewrites the opcode.

FP32 forms keep the existing all-target rule. FP64 forms need SM >= 13; only
`div.f64`, `rcp.f64`, and `sqrt.f64` additionally accept exact generic `sm_10`, `sm_11`, or
`sm_12` with `map_f64_to_f32`. That option does not admit `rsqrt.f64` or `mad.f64`
on these targets. `RcpImplicitF64` is distinct from `RcpRnF64`; its RN default is a static interpretation, never a fabricated written control. Native SM13 requires a PTX1.2+ source header, while mapped SM10/11 permit PTX1.0 and SM12 requires PTX1.2. The historical CUDA 13.3.33 assembler discrepancy is evidence rather than a normative override of the pinned manual. Existing implicit `mad.f32` has its own contract.

Availability metadata uses optional `ptx_before: "1.4"` as an exclusive bound,
in plain requirements and each DNF clause. Missing bounds remain unbounded;
malformed or empty/reversed intervals are rejected. Plain upper-bound failures
report located `UnsupportedPtxVersion`; DNF failures retain
`UnsupportedAvailability`. Selection is version-independent; validation uses the
current standalone context or current source/owned/replacement module version.
Copies and clones retain the omitted identity and interpretation. Revalidation
at PTX 1.4 or later fails without converting the instruction to an explicit form.

WMMA source spellings preserve the PTX 6.3 `.aligned` boundary. The 156
FP16-input / FP16-or-FP32 accumulator forms introduced in PTX 6.0 or 6.1 have
separate `*_implicit_aligned` variants and `*ImplicitAligned` final classes:
72 loads, 36 stores, and 48 multiply-accumulates. They retain their original
PTX floor and SM70 requirement and require PTX `< 6.3`. The existing modern
classes require an explicit `.aligned` and PTX `>= 6.3` (or their later native
floor). Historical classes have no `aligned` field, alias entry, default, or
fabricated source range. Both spellings retain immutable `MatrixFamily::WMMA_LOAD`,
`WMMA_STORE`, or `WMMA_MMA` topology: these families imply uniform execution by
the participating warp. This execution requirement is distinct from byte address
alignment; fragment, layout, address, and stride checks remain unchanged.

Standalone resolution selects the written spelling without a version context.
Module validation and instruction availability checks apply the half-open version
interval using current owned or replacement context. No historical counterparts
are added for `shared::cta`, integer, subbyte/bit, BF16, TF32, FP64, or other
forms introduced in PTX 6.3 or later. The source reference is the
[PTX 9.3 WMMA description shipped with CUDA 13.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-ld).

The 96 existing FP16-input WMMA MMA forms (48 explicit `.aligned` forms and
48 implicit counterparts) expose optional Boolean `satfinite`, default `false`.
The selected `.satfinite` value requires PTX `< 6.5`, intersected with the form's
native floor and alignment interval: implicit forms use PTX 6.0/6.1 through 6.2,
explicit forms use PTX 6.3 through 6.4, all on SM70 or later. NVIDIA deprecated
floating `.satfinite` in PTX 6.4 and removed it in PTX 6.5. Omission retains each
form's existing availability, including modern PTX versions. Saturation maps
positive/negative infinity to the largest positive/negative finite value and NaN
to positive zero. Typed `satfinite.value` and source locations survive owned
resolution, copying and cloning; current context checks reject removal-era use
without changing the form identity. Integer saturation and BF16, TF32, FP64 and
other MMA families are unchanged. These are syntax and static validation
contracts; numerical GPU execution is not validated. See the
[CUDA 13.3 PTX 9.3 WMMA MMA reference](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-mma).

Header numeric parameter records retain every ordered `array_extents` axis and
checked `byte_extent`. Their own signature axes must match exactly, while ABI
comparison across declarations/calls uses total count. Name-only
`ResolvedOpaqueEntryParameter::array_extents` replaces its old `is_array` and
`array_extent`; it retains sized axes without assigning opaque bytes. Function
copies own these vectors and clone body instructions independently. See the
[public parameter migration](parameter_declarations.md).

Ordinary register-vector operands and fixed-cardinality vector/tensor tuples
contain scalar register leaves. This role is independent of whether arity comes
from a modifier, an allowed-arity list, or fixed tuple bounds, and applies to both
element and aggregate policies. An owned whole-vector register cannot replace
one leaf even when its element dtype matches. Checking reports the offending
lane range, with the operand/instruction range as fallback. Explicit scalar
components and genuine named-vector projections retain absent effective vector
width and remain legal under their existing source contracts. The enclosing
named MOV/memory vector root keeps its own whole-vector shape; that root is not
a scalar leaf. Value-vector sources retain their existing scalar-register and
immediate/provenance checks. This changes no cardinality, dtype/carrier policy,
named-source permission or immediate range.

[Block-scaled MMA scale selectors](converage/mma_scale_selector_coverage.md) use
U16 logical values with closed B16/S16/U16 register carriers. Their two scalar
leaves, byte masks, separate A/B thread limits and unsigned literal conversion
remain independent of whole-vector shape and runtime register contents.

Selected multimem data vectors, SULD destinations, and root `st.async` v2/v4 sources accept ordinary named register vectors alongside braces. A sole fixed Element arity is taken from the binding when no vector modifier supplies it. The existing whole-base identity and canonical `NamedProjection` lanes remain owned together. MM/SULD require matching element widths; root `st.async` retains its wider-carrier rule. Every named declaration remains v2/v4 and at most 128 bits; MM v8 remains brace-only. SUST value-vector sources use the source contract below.

`ResolvedValueVector` now owns `ResolvedVectorSource source` alongside ordered `RegOrImm` elements. Its default remains BraceList with no whole base; complete brace sources preserve mixed values, explicit components, and the full brace range. The 78 opted SUST v2/v4 bindings also accept ordinary named declarations. A named source has exactly one whole declaration authority in `source.whole_base`; every lane is a canonical General-register `NamedProjection` derived from that real SymbolId/member/type/width and the complete identifier range. Immediates, sinks, special registers, and independent scalar projections cannot form a named source. `valid_value_vector_source` reuses register-vector provenance validation; legacy standalone empty brace ranges remain compatible unless complete provenance is requested. Owned module validation requires the complete source and rejoins the base and each lane after syntax destruction.

Public ABI migration: rebuild consumers of `ResolvedValueVector` and `checker::OperandView`. The latter adds a borrowed `const ResolvedValueVector* value_vector`, valid only during the synchronous check and pointing to the actual owned payload. Generated projections verify arity, lane kinds, and register/immediate pointers against that payload before named permission or type checks. Copy, equality, assignment, and instruction clone include the owned source; `IReferenceObserver::value_vector` keeps its signature. Other value-vector permissions stay default false, without a blanket claim that every such ISA role forbids named syntax. No RegOrImm domain or numeric conversion rule changed.

`checker::OperandDescriptor::mbarrier_state_token_form` is now `std::optional<MbarrierStateTokenForm>` with an absent default, replacing the former unconditional Register default. Rebuild public consumers for this member type/default migration. Absence means a non-token descriptor; all 93 generated typed-token bindings explicitly engage their existing Register, RegisterOrSink, or Sink policy, including Register inputs. The existing optional register payload and observer signature are unchanged. The private common checker reconciles actual Register shape, sink flag, borrowed reference, and optional declared-type cache before enforcing presence policy. Sink-capable policies require write destinations. Other scalar type/width/component/declaration and availability rules remain independent; unknown declaration type is not made globally mandatory. Generic register/predicate/SHFL sinks with no token policy retain their existing rules. Caller-supplied absence chooses non-token checking and does not authenticate a token. Owned copies and clones enforce the same form after source destruction; neither this policy nor a B64 carrier proves runtime phase contents, progress, or token authenticity.

A scalar `Register` view claims a General register. The common checker rejects a known non-General class in either the actual borrowed reference or its class cache, and rejects known predicate type facts under that claim. The same rule applies to scalar Register leaves after valid register-vector cardinality is established, with lane-range diagnostics. Genuine named-vector whole bases are not scalar leaves; General explicit components and canonical `NamedProjection` lanes retain their existing rules. Predicate and special-register views keep their own shape contracts. Unknown General types and legacy views without actual facts still defer to existing checks: this rule adds no required SymbolId or declared type, universal type-cache equality, or general instruction-only declaration eligibility check. Contextualized numeric types remain governed by their existing conversion rules. Class validation does not prove runtime register contents.

Mbarrier register carrier typing is role-specific even though the written instruction modifier remains `.b64`. Opaque arrival/arrival-drop outputs and the opaque `pending_count` state input accept fundamental B64/S64/U64/F64 declarations of exactly 64 bits. Non-parity test/try-wait state inputs have semantic U64 type and accept B64/S64/U64, rejecting F64 and every other width. The frontend retains each actual declared type, SymbolId, member/component, and range; it performs no numeric floating conversion and proves no runtime token contents or prior operation. The aligned shared-memory `.b64` object is distinct from the register carrier. CTA RegisterOrSink and cluster mandatory Sink policies and availability remain independent.

Report-value outputs retain the frontend's qualified logical B8 model, with same-size B8/S8/U8 carriers. The pinned PTX 9.3 text does not unambiguously map this heterogeneous report operand to a normative width/type; later official uint8 examples corroborate the retained model but do not close that fixed-reference gap. Report carrier tests establish this retained model, not unconditional ISA-width certification. Opaque F64 acceptance follows the adopted fixed-reference type interpretation; supplementary assembler rejection is documented tool divergence, not a new frontend type restriction.

Scalar F16 numeric constants use IEEE binary16 conversion at the use site. Decimal and evaluated floating expressions, exact F64 bits, and bare unsigned exact F32 literals round once to nearest, ties to even. Binary64 bits convert directly to binary16; an exact F32 input widens exactly first. Signed zero, gradual subnormals, overflow to signed infinity and quiet NaNs with retained sign/high payload follow the deterministic frontend literal policy, independently of instruction rounding, FTZ or saturation. This does not simulate a CVT result. Integer-origin values remain distinct from floating constants. Exact F32 signs, parentheses and operators remain rejected; Bit16 does not inherit numeric half conversion, and F16/F16x2 data initializers remain forbidden.

Twenty fields in eleven existing generated classes migrate from `WithLocs<ResolvedRegisterRef>` to `WithLocs<RegOrImm>`; rebuild public consumers and use `std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>` on `.value`. Variant names, shared F16/BF16 add/sub domains, selector order and all width/availability rules are unchanged.

| Generated class | Migrated fields |
| --- | --- |
| `AddMixedF32`, `SubMixedF32` | `src` each |
| `FmaMixedF32F16` | `src1`, `src2` |
| `SetHalfF16F16`, `SetHalfF16F16Boolean` | `src1`, `src2` each |
| `SetHalfBf16F16`, `SetHalfBf16F16Boolean` | `src1`, `src2` each |
| `SetHalfIntegerF16`, `SetHalfIntegerF16Boolean` | `src1`, `src2` each |
| `SetpF16`, `SetpF16Boolean` | `src1`, `src2` each |

A canonical current F16/BF16 immediate is a low16 word with no integer-source provenance or integer-negative flag. Ordinary instruction IR retains the current typed word, so a coherent in-range word edit can be valid without recovering the original literal. Shared mixed BF16 fields accept numeric BF16 sources and canonical typed BF16 words under the scalar conversion contract below. Calls retain the original lexical input or preconversion F64 expression value and recompute the half word during owned validation. Every otherwise eligible scalar Reg/Param input uses that common conversion; modern narrow Register-formal ABI restrictions, vector no-splat and missing-context requirements remain independent. The four scalar F16 SETP sources additionally require an explicit raw Bit16 route in a separate typed-policy delivery; their current numeric path is not a claim that raw integers are normatively forbidden. Packed numeric construction and additional source permissions remain separate scope.

The ordinary `cvt.pack.sat` S32 inputs accept integer constants and expressions as well as same-width B32/S32/U32 registers. The existing 64-bit evaluator narrows each input to its low32 S32 word and retains original64-bit integer bits/sign metadata; `0x100000001U` therefore has use word1 without a target-range requirement. The frontend does not saturate inputs to the destination lane width or precompute the packed result. Floating-valued inputs remain incompatible with S32; an expression whose final category is Integer, including an FP comparison, follows the integer route. Independent B32 carry sources keep their existing literal/precision contract, including same-width unsigned direct0f bit data; this does not admit literals in register-only stochastic `rbits`. Carry topology, destination widths and target boundaries remain unchanged.

Public ABI migration: `src1` and `src2` in `CvtPackSatU8S32B32`, `CvtPackSat16S32` and `CvtPackSatSmallS32B32` change from `WithLocs<ResolvedRegisterRef>` to `WithLocs<RegOrImm>`. Rebuild consumers and inspect `.value` with `std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`. Six fields change in place; no class, modifier domain or selector order changes. Copied/assigned/cloned instructions retain typed words, source caches, declaration identities and locations. Ordinary current IR word/reference edits are checked against current facts; source association remains a separate retained-syntax check.

The three stochastic quad classes `CvtRsSatfiniteF8x4F32`, `CvtRsSatfiniteF4x4F32`, and `CvtRsSatfiniteF6x4F32` expose `src.value` as `ResolvedValueVector`, replacing `ResolvedRegisterVector`; consumers must rebuild and inspect each `RegOrImm` alternative. Brace sources retain the complete written range and lane order while numeric floating sources use the common F32 conversion. Integer-origin sources remain incompatible, and exact F32 constants retain the bare unsigned literal rule. This conversion computes source F32 words only; it does not execute stochastic conversion, saturation, or the packed result. Named V4 F32/B32 sources use `source.whole_base` as the sole declaration authority and canonical `NamedProjection` lanes, with the existing General class, arity, binding, and 128-bit declaration limits. Copies and clones own both provenance and lanes. A borrowed `OperandView.value_vector` must point to the actual payload and match its lane pointers, types, ranges, and descriptor permission; default-false named permission remains effective. The destination carrier policy and independent register-only `rbits` rule, including its widening policy, remain unchanged.

Ordinary register declaration eligibility uses the actual physical declaration type and the central `ScalarTypeMetadata::register_declaration_usage` classification. Known General-register reference types must be Fundamental, including when reached through explicit components, named projections, or vector register leaves; absent standalone declaration facts retain their existing deferral. Logical BF16/TF32/packed instruction formats and canonical typed immediate words are independent of physical register declarations. Contextual operand type caches do not replace the actual declaration type. Owned validation separately checks every concrete ordinary `SymbolKind::Variable` in Register state space, including unused declarations; missing or unrecognized concrete types cannot establish eligibility. Predicate declarations and native scalar F16x2 remain Fundamental. Retained source identity records historical syntax association and does not exempt a current mutated binding table from declaration legality. Copies must validate within a module that owns the relevant symbol table.

### Owning debug section values

`ResolvedModule.sections` owns source-ordered section fragments. The public
`ResolvedDebugSection` alias reuses `syntax_ast::AstSectionDirective` and its
small value-only variants: every string/range is copied, without AST/CST/source
borrows or numeric caches. Default module copy/assignment/move/swap therefore
include nonempty integer and relocation payloads. This public CST/AST/Module
migration changes source and binary layout; consumers must rebuild.

A shared checker decodes already-tokenized integer magnitudes and applies original
B8/B16/B32/B64 intervals `[-2^(w-1), 2^w-1]`. Full UINT64_MAX and negative 2^63
survive; U suffixes do not relax ranges. The manual's `.b16 -65535` example
conflicts with its explicit interval and is rejected in favor of that interval.
B32/B64 symbolic data retains unresolved single addresses, signed byte addends
(including checked `label-integer` shorthand), and differences of unique known
labels/section bases in one logical section. Equal section names join fragments;
linked addresses and relocation results remain unknown. Strings, arbitrary
expressions and mixed symbol/integer lists are not inferred productions.

Known PTX gates are section 2.0, label addend 3.2, B16 data 6.0, label definition
7.2, label difference and negative values 7.5; minus zero is mathematically zero.
Missing context follows existing validation policies. AST-free checks revalidate
current owning records; source-aware checks independently validate both current
and supplied section values with the supplied effective context and rebind its
debug labels. Valid changed section data does not cause executable source-identity
mismatch. Invalid current data cannot be hidden by valid replacement sections.
This checks the bounded source grammar and static locality, not DWARF decoding,
linked byte values, target-debug DWARF recognition or GPU execution.

### Target debug absence and recognition boundary

The file-wide `.target debug` requirement applies if any target directive selects
`debug`, including earlier or later regions. Known `debug` with zero owning modern section records AND zero legacy records
is a determinate violation, even under `AvailableContext` with
missing version information. `.file`, `.loc`, or retained debug-string symbol
identities cannot replace serialized debug records. The existing PTX 3.0 option gate and complete
context requirements remain independent. Source-aware validation applies the same
private absence rule separately to current owned options/modern sections/legacy records and supplied AST
options/modern sections/legacy records; neither dataset can conceal the other's definite absence or
invalid typed payload. Valid differing datasets retain section-neutral source
association. No presence cache, recognition enum or decoder is added.

**D-DEBUG-DWARF-PRESENCE-RECOGNITION remains a reference boundary, not a code PASS.**
The pinned [target clause](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target)
and [section grammar](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-section)
do not specify a sufficient recognition criterion for arbitrary names, empty
sections, label-only sections or nonempty typed data. A `.debug_` prefix, whitelist,
nonempty payload or mandatory `.debug_info`/`.debug_abbrev` pair would invent such a
criterion. The paired witness is one structural control, not a universal minimum
or a decoder. Validation success for nonzero structurally valid section or legacy records
therefore does not certify that actual DWARF information was found. The finite
debug-off/on boundary tests record this qualification explicitly; full recognition
and linked/binary authenticity are not claimed.

Current owned `.language` values are context-independent intrinsic data: an
absent optional denotes omission/default `ptx`; an engaged vector must be nonempty
and every raw token must satisfy the shared public semantic decoder. Multiple
values and duplicates retain exact order and spelling. Both module validation
APIs recheck current values at the owning function range, even without version or
target; legal supplied source cannot hide invalid current words. Valid current
name/code/list edits or optional removal do not require historical equality.
Replacement AST identity remains raw and ordered: changing `"ptx"` to `3` or
reordering a valid list can still produce the existing source-association
mismatch, while ordered decoded aliases remain compatible for redeclarations.
Presence requires PTX 9.3 on all targets; a known old selected/supplied version
rejects even with no target, and only missing version may defer availability under
`AvailableContext`. Domain checking never defers; strict context requirements stay
unchanged. The new decoder is an additive exported semantic function; clients
must use the matching installed header/library. No payload layout or backend
language execution guarantee is added.

Owned entry-input `.ptr` availability is derived from the existing parameter
record and checked against PTX 2.2 before requiring complete target context.
A coherent version retarget updates the selected region and function metadata;
copies and instruction clones preserve the same check after AST destruction.
Source-aware validation instead applies its supplied effective version to current
pointer presence, without treating the historical cached version as a veto.
Existing pointer type, shape, alignment and pointee-space checks remain separate;
the availability diagnostic uses the established function-range fallback.

Parameter modifier metadata uses the exact typed identity
`(MemoryStateSpace::Parameter, ParameterAddressQualifier)`. The domain and
availability descriptors append a `parameter_qualifier` member defaulting to
`Default`; `ModifierValueView` appends an optional member with an engaged
`Default`. Existing positional aggregate initializers keep their meaning, but
these public layouts change ABI: rebuild consumers with matching headers and
libraries. An unknown projection (`nullopt`), invalid enum, or qualified
non-Parameter space rejects independently of target and spelling presence.
A non-default Parameter qualifier is semantically present even after locations
are cleared. Plain Parameter therefore cannot select an 8.3 qualified row.

Generated views use the current direct address or selected direct symbol tied
to the same normalized modifier binding. Nested symbol defaults do not supply
a second written qualifier. Fixed forms use their unique canonical identity;
register alternatives need no qualifier payload. Associated faithful written
qualifiers must agree. The existing typed parameter-address preservation policy
can derive an Entry frame qualifier from a fixed plain modifier; that frame
fact is validated separately and does not change the fixed modifier identity.
Existing frame, readonly, declaration, source identity, and Shared/atomic
qualifier guards continue to apply.

Current-owned Parameter address checks run before missing version/target context
can skip instruction validation, in both `validateModule` overloads. Exact
variant/layout operand descriptors provide closed address-space domains and
current associated state-space equality. Empty domains stay unconstrained;
unknown runtime register pointees are not inferred. Device formal address
materialization still uses its existing policy and may produce Local space.
Known entry inputs cannot supply an explicitly writable operand or a Return
parameter operand. `OperandAccess::Read` describes operand value access and
never classifies memory effects; MOV address acquisition and tensor-map reads
remain legal.

`detail::IReferenceObserver::operand_context` is an appended default no-op
virtual callback. A nonnull descriptor brackets the immediately following
address-bearing top-level payload callback; nullptr resets the context.
Descriptors and spans are borrowed synchronously. Collectors copy needed
scalar/domain facts and retain no borrowed descriptor. Existing payload
callbacks and their order stay unchanged. Existing derived source observers
need no new override, but the added virtual slot changes ABI: rebuild observers
and producer libraries against matching headers.


## Historical entry body inputs

PTX 1.0–1.3 entry body `.param` declarations have the owning `LegacyEntryInput`
role in the sole `parameter_declarations` table, including scoped/group/array
identities. The function signature remains syntactic header ABI only, so an empty
header signature does not imply no host inputs. Lexical metadata order assigns
no launch addresses or packing. Resolution needs a version only for ambiguous
entry body roles; it reports `MissingValidationContext` without guessing from SM.
Typed modules retain their role when context is missing.

The optional `ResolvedSymbolRef::parameter_role` cache is derived from the actual
record while keeping structural `CallParameter`. Address/MOV/tensor projections
share input/return/call-slot directions. Both validators rejoin current records
and caches and enforce read-only entry-frame access, direct call-actual exclusion,
shape/scope and logical capacity before context skips. Legacy input LD is not
call-return staging; MOV preserves `.param`. Modern BodyLocal staging is unchanged.
The checked sum of per-member bytes times compact count has the known32764-byte
global ceiling; no double axes/vector multiplication or invented256 legacy cap.
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` remains qualified for exact historical
physical launch packing. See the [parameter contract](parameter_declarations.md)
for context windows, intrinsic guarantees and preserved limitations.


The shared current operand-domain pass classifies both actual `EntryInput` and
`LegacyEntryInput` records as read-only entry objects. It uses current typed
layout/field requirements before context skips, including non-LD/ST consumers;
MOV address acquisition stays legal, while an explicit writable operand rejects.
Legacy readonly diagnostics use their original label and instruction range;
only identical local causes merge. No opcode-specific writer classification or
new historical instruction permission is inferred.

## Exclusive numeric SM availability bound

Plain availability and each `any_of` clause accept optional `sm_before: 70`. Together with `sm: 30` this means `30 <= SM < 70`; a missing lower bound means zero and an omitted upper bound means unbounded. The field must be a strict uint32 integer greater than the minimum; zero, booleans, floats, strings, explicit null, overflow and empty/reversed intervals are rejected by typed normalization and rendering. JSON Schema applies integer/range constraints; the typed boundary also rejects integral floating values. The existing maximum of six ordered alternatives and prohibition on mixing `any_of` with plain fields remain. No target intersection solver is introduced.

`AvailabilityClause::sm_before` and `AvailabilityDescriptor::sm_before` append disengaged `std::optional<uint32_t>` members after `ptx_before`; rebuild public consumers for this aggregate layout change. The predicate compares concrete `TargetInfo::sm_version`, independently of profile spelling or flavor, while all existing exact-target, family, capability and mapping requirements remain conjunctive. Plain failures produce located `UnsupportedSmVersion` in variant, layout, modifier, operand-value, parameter-address and memory-vector paths; DNF failures retain `UnsupportedAvailability`.

Legacy SHFL uses `any_of: [{ptx: "3.0", ptx_before: "6.4", sm: 30}, {ptx: "3.0", sm: 30, sm_before: 70}]`. Source modules still require recognized profiles and legal profile/version headers. Under `AvailableContext`, missing version or recognized target continues to defer whole-instruction availability, even if another known fact appears incompatible; such success is not full ISA certification. Strict validation reports `MissingValidationContext`; an explicit unknown profile remains `UnknownTarget`. No partial DNF evaluator or opcode-specific context rule is added.

## Historical 101 source profiles and validation features

`sm_101`/`compute_101` and their A forms begin at PTX8.6; F forms begin at PTX8.8. They remain admitted at PTX9.0/9.3, with no rename-imposed upper window. The corresponding 110 source profiles still begin at PTX9.0. `TargetIdentity` retains architecture101, flavor and exact written spelling. `same_target_identity` compares source architecture/flavor, so101A and110A remain distinct.

The additive base function `target_feature_architecture(identity)` consults explicit private catalog data: the three101 keys project to feature architecture110; other keys retain their architecture. Uncataloged typed keys do not become recognized. Public `TargetProfile` and `TargetInfo` layouts are unchanged. Source/owned/semantic factories use this feature level for numeric SM, retain source identity, and pass the existing110 baseline capabilities `cluster`, `aggregate_smem`, `reserved_smem`, `graph_exec`. Generic101 has no family; 101A/F enable only canonical `sm_110f`, with no100f/103f inheritance or FP64 mapping option. This bounded correspondence follows the published rename and family relation; it is not a physical hardware/driver guarantee.

Availability singular and span exact-target requirements compare validation-feature architecture plus exact flavor through that helper. This matching relation differs from source identity and stored-key equality. A manually supplied `TargetInfo::sm_version` remains the caller's concrete feature fact and is never overwritten by the checker. Source profile introductions, instruction/PTX floors, capabilities and shape constraints remain independent.

Historical instruction data changes only the adopted231 parents:219 preserve existing100A clauses and add101A, with101F only where an original110F branch exists;12 block-selector parents append an8.8 historical101A/F clause. All854 original110 conditions and their floors remain, the DNF limit stays six, and the25 restricted cluster gather4/im2col-W parents remain unchanged. TCGEN closed rows retain kind, scaled-D and explicit scale-vector restrictions: written block16/32 historical gates require8.8, while100-only vector and scaled-D paths remain100-only. Generated operational queries first validate the exact source profile and its minimum version, then apply feature matching. Unknown words and addresses remain unresolved obligations, not facts inferred from live registers.

Both module validators independently join current function `source_region`, `source_version` and exact `source_target` to the current owned region. A valid replacement AST cannot mask contradictory caches. Intrinsically coherent101/110 contexts may be retargeted through a supplied header when the current payload is legal under that effective context; retained availability is not a historical veto. Missing full instruction context still follows the existing `AvailableContext` deferral and strict `MissingValidationContext` policy.


### Deprecated maximum-CTA hint

Entry headers accept `.maxnctapersm ncta` from PTX ISA 1.3 on every recognized
source target, including PTX 2.0 and later. Deprecation does not impose an upper
version bound. The distinct `MaxNctaPerSm` AST/owned kind is an optimization hint:
additional CTAs may still execute, so it is not a hard runtime occupancy cap.
`.maxntid` is optional, and `.minnctapersm` may coexist as a separate resource.

One complete unsigned integer token is checked before projection into the existing
`uint32_t` resource value, including zero and `UINT32_MAX`; overflowing values do
not wrap. This is the frontend's modeled resource capacity. The published grammar
does not specify this bit width, and validation does not prove hardware occupancy
or driver feasibility. CST/AST leaves retain written radix/suffix/ranges while
alive; owning contracts retain only kind, normalized values, explicit count and
range, with no parallel raw spelling field.

Both validators check current payload shape, entry placement and known range
containment. Known PTX context below 1.3 rejects even without a complete target;
missing version defers availability under `AvailableContext` while intrinsic
checks continue. The source-aware route uses the supplied effective version.
Coherent current numeric edits remain valid; changing written resource kind or
value in a replacement AST retains the existing source-association rule.

Source reference: [PTX ISA maximum-CTA directive](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#performance-tuning-directives-maxnctapersm).


### Legacy debug serialization

The case-sensitive `@@DWARF` marker introduces one closed production at module,
function-body or nested-block statement scope. It is supported for PTX ISA
**[1.2, 2.0)** on every recognized source target. Function headers do not admit it.
The forms are `.byte` unsigned hexadecimal lists (0..255), `.4byte` lists
(0..UINT32_MAX) and `.quad` lists (0..UINT64_MAX). Lists are nonempty. The two wider
forms also admit one ordinary or singly dotted symbolic label, retained unlinked;
there is no ordinary PTX symbol-resolution requirement for that label.

`0x`/`0X` with optional `U`/`u` follows existing PTX integer token inheritance;
this suffix choice is derived rather than explicit wording of the legacy syntax.
Signs, decimal/octal/binary values, mixed symbol/numeric lists, addends,
differences, label definitions and arbitrary host assembly are outside this model.
The published example's header is supported as `.section name, "", @progbits`.
Generalizing its name to an ordinary or singly dotted identifier is derived from
the example; flags remain exactly empty and the type remains exactly `progbits`.

Whitespace, comments and newlines separate tokens without ending a record. One
structurally complete payload and an optional semicolon end it. The next token is
handled by ordinary PTX grammar; malformed records preserve following markers,
blocks and functions through structural recovery. Lossless CST tokens retain
comments and source text. Owning AST values retain exact marker/word/header
spellings and within-record ranges, including separate `@` and `progbits` leaves.

`ptx_syntax_legacy_dwarf.hpp` contains only small owning values. It does not expose
module, instruction or expression AST definitions. One shared `AstLegacyDwarfRecord`
is used in module/body variants, and `ResolvedModule::legacy_dwarf_records` is the
sole ordered stream of `{value, scope_id}` wrappers. Actual declaration-occurrence
and block scopes come from the module's symbol table; a canonical function's
preferred owned scope does not replace its definition occurrence. Metadata creates
no executable node, label slot or instruction-provenance index. Module and function
source identities exclude it while ordinary program constructs remain relevant.

Both current and supplied datasets validate their own typed words, ranges, scope
ownership and format windows. Source-aware validation uses the supplied effective
version for both datasets; AST-free validation uses current module context. Known
out-of-window versions reject without a complete target. Missing version under
`AvailableContext` defers only the format window; intrinsic checks continue and
strict missing-context checks remain. Coherent current metadata edits and differing
valid datasets do not require old source-byte equality. Cross-format replacement
requires removal or reconstruction of current incompatible format data.

For `.target debug`, definite absence means **zero modern sections AND zero legacy
records**, independently in each dataset. An unknown linked label, a header or
nonempty bytes only avoid this definite-absence result. They do not certify actual
DWARF recognition, decoding, linked addresses or binary authenticity. A known full
legacy context cannot combine the pre-2.0 format with the PTX-3.0 debug option;
missing-version tests are partial-context controls only.

References: [legacy debug marker](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-atatdwarf)
and [legacy-format window](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives).


### Indirect function-address carriers

An indirect function address uses a General `.reg` effective scalar with a
fundamental signed, unsigned or bit type of 8, 16, 32 or 64 bits. Floating types,
packed/alternate instruction-only types, `.b128`, predicates, special registers
and whole vectors do not satisfy this role. A selected ordinary component uses
the same element-type rule and retains its real owner identity, selector ranges
and the 128-bit vector-declaration limit. Written call components use the same invariant as typed public register-component values.

This is a **derived** address-carrier interpretation of
[PTX §6.4.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#addresses-as-operands)
and the function-address-register requirement in
[§9.7.13.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-call),
not a claim that `call` explicitly enumerates all twelve carrier types. Address
value widths of 32/64 bits do not impose a 32-bit register minimum or require a
64-bit register under `.address_size 64`. Eligibility does not prove that a
narrow register actually contains a representable function address, a linked
code address or a member of a runtime target set.

Source resolution, direct typed checks and both module validation APIs apply
the role before missing version/target can defer it. Owned checks rejoin actual
symbol declarations and reject lying or erased type caches; valid supplied
source cannot mask bad current data. Generated views borrow the active RegRef
alternative without reinterpreting metadata/table alternatives. Completely
unknown standalone facts retain their existing unknown/context behavior.
Formal input/return ABI rules and ordinary floating call inputs remain separate.


### Selected scalar call operands

Call inputs and the single parenthesized return accept ordinary `.xyzw/.rgba`
register selections. The target accepts an identifier or an explicitly selected
indirect register; final prototype/target-set/table metadata stays identifier-only.
Anonymous brace actuals and direct function projections remain unsupported.
`ResolvedCallParameterRef::component` is a default-disengaged owning optional;
null preserves the whole-variable contract and old aggregate construction.

Source and both owned-module validation routes first join the real General
`.reg` v2/v4 declaration, SymbolId, parameterized member, element type/state,
physical 128-bit owner bound and explicit selector provenance. Only then does
the existing call ABI comparison see an effective scalar. Arrays, pointer,
size and alignment constraints are retained. Deleting or contradicting a cache
cannot hide a known declaration. The return's outer parenthesized range stays
in `WithLocs`; its selected leaf remains contained inside the group and instruction.
Copied observer values, module copies and instruction clones own the selection.

Modern register-formal minimum width remains a formal ABI rule, not a global
component minimum: matching legal subword `.param` formals use the existing
scalar actual policy. Whole named-vector actuals, constants and parameter-memory
staging retain their existing rules. This is derived scalar-element inheritance,
not pointer execution, hidden moves or synthetic scalar declarations. Coherent
current edits remain legal; written source identity is checked independently.
Public CST/AST variants and the owning call reference require matching producer
and consumer rebuilds. No instruction schema, generated layout or callback changes.

### Alternate scalar literal conversion

Scalar BF16, E4M3 and E5M2 numeric conversion uses the existing literal helper and evaluated F64 route, with integer-bit round-to-nearest, ties-to-even directly from binary64. Bare unsigned exact F32 literals widen exactly first. Signed zero and gradual subnormals are retained. BF16 overflows to signed infinity at `(2-2^-8)*2^127`; E5M2 does so at 61440. Their NaNs retain sign and high payload bits and set the quiet bit. E4M3 has no infinity: the exact 464 tie rounds to finite 448, while every larger finite magnitude and infinity becomes signed `0x7f` NaN; NaNs also become signed `0x7f`. These are adopted deterministic frontend literal choices, independent of runtime rounding, FTZ or saturation; they do not execute a GPU conversion.

The returned immediate keeps its logical format, a low16 BF16 or low8 FP8 word, and no integer-origin or integer-negative metadata. All in-width current words remain valid, including signaling NaNs; converter quieting does not impose literal history on later current-word edits. Integer-origin inputs, narrow Bit carriers and packed formats do not gain scalar floating conversion. Existing exact F32 sign, wrapper and expression restrictions remain. No public types, signatures or source permissions change.

The current source model admits BF16 numeric input in exactly five fields: mixed ADD/SUB `src`, and the source of `CvtF32Bf16`, `CvtF32Bf16Ftz` and `CvtNonF32BaseFromBf16`. The mixed result and other input stay F32; selected rounding, width and availability checks remain independent. E4M3/E5M2 have no admitted scalar opcode source here: helper and generic checker coverage does not establish source/module-clone support. Other BF16 register-only, vector, packed, call and storage permissions remain separate. Calls still recompute supported F16 values from preserved preconversion sources. Partial-target instruction deferral remains unchanged.

### Packed32 exact words and native call formals

The exact-word contract covers exactly F16x2, BF16x2, E4M3x4 and E5M2x4 through the existing literal helper. A bare unsigned direct `0f` literal without an evaluated `source_value` retains its entire 32-bit payload and selected logical type, including raw sign and signaling-NaN-looking bits. It performs no component conversion, rounding, quieting, saturation or splat. Integer origins, decimal/F64 numeric packing, signed or wrapped exact F32 expressions, and truncation to packed16 do not gain a producer. Underdefined numeric one-to-many mappings remain a reference boundary, rather than a blanket normative packed-literal ban.

An ordinary current packed word is in width, has absent integer-source metadata (including no optional zero), and has no integer-negative flag. Coherent in-width current edits do not authenticate erased literal history. A call literal differs because it already retains spelling/kind or evaluated input: an otherwise eligible scalar native F16x2 Reg/Param formal must recompute that retained direct word through the same helper. Typed-word-only or integer-origin edits reject; editing retained direct spelling and typed bits coherently may remain valid, with saved AST identity checked separately. The four existing direct/prototype/target-list/storage-function-list signature routes share this rule. Vector formals, return-actual grammar, formal ABI readiness and instruction-only BF16x2 classification are unchanged.

This private helper contract does not grant literals to the nine multimem vector fields, six adjacent scalar packed fields, or atomic fields. Existing scalar alternate RNE/NaN policies and the integer-origin distinction remain independent. Public structures, metadata and generated fields do not change. Current Debug, installed-only consumer checks and independent actual-diff acceptance are required before recording delivery completion.

### Floating value-vector source contract

Exactly nine existing `src` fields use `WithLocs<ResolvedValueVector>`: weak and ordered floating stores, and floating add reductions, at v2/v4/v8. The classes, selector/layout identities, element type domains, arities, defaults, availability and addresses remain unchanged. This is a public field/factory/observer payload migration from `ResolvedRegisterVector`; downstream consumers must rebuild with matching generated headers and libraries. Wrap each old register lane in `RegOrImm`, preserve locations and `ResolvedVectorSource`, and inspect alternatives with `std::get_if`. A former null register element is not an immediate or a newly permitted sink.

All64 selected type branches have typed value carriers. Exactly32 have one-value numeric producers (15 F16/F32 and17 BF16/E4M3/E5M2), and20 have an exact whole32-bit bare unsigned direct `0f` producer (F16x2/BF16x2/E4M3x4/E5M2x4). The12 E4M3x2/E5M2x2 branches have no exact16 lexical producer or adopted numeric packing constructor: literal-source and literal-source-lifetime positives remain Source_N_A. Their real register controls and independently constructed canonical low16 typed words remain valid evidence; source absence is not a typed-data ban. Numeric scalar conversion reuses the existing single-round binary64 policy; packed32 retains every written word bit without splat, quantization or quieting. Integer-valued source expressions remain incompatible with logical Float lanes.

Mixed braces may contain registers and immediates. The50 v2/v4 named branches retain actual General whole-base declaration identity, exact NamedProjection lanes, element SameWidth and the16-byte declaration cap. The14 v8 branches remain brace-only; the existing128-element projection buffer and256-bit generic capacity are unchanged, and this cohort needs at most8 lanes/128 bits. Immediate projection is not a user sink. Synchronous observers receive actual payloads and all lane ranges, including lane8, and must not retain callback references. Copies/assignments/clones reacquire views from their own owners.

Ordinary typed words require the selected logical type, in-width bits and absent integer-origin/negative flags; coherent current edits do not replay erased floating source history. Current payload/pointer/range/provenance checks and retained source-identity checks remain independent. Address alignment, selected type/modifier/target gates and incomplete-context deferral still apply. This contract does not widen the six scalar packed branches, atomic inputs, packed numeric constructors or load destinations, and makes no runtime multimem mapping or GPU execution claim. Current generation, Debug, fresh installed public-consumer and independent actual-diff evidence remain required before delivery completion.
