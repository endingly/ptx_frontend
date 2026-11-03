# Resolved storage declarations

`ResolvedModule::storage_declarations` exposes owned metadata for `.global`,
`.const`, `.shared`, and `.local` declarators. Include
`<ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>` for the data types and
the usual resolved-IR header for `resolveModule()`. Source text and Syntax AST
may be destroyed after successful resolution.

This is a declaration-to-consumer contract, not a memory allocator or a claim
of complete PTX declaration conformance. Parameters remain separate:
`ResolvedFunction::parameter_declarations` describes sized `.param` declarations;
the `EntryInput` role identifies ordinary entry inputs. Opaque entry inputs live
in `ResolvedFunction::opaque_entry_parameters` without fabricated byte layout.
Registers and call parameters do not enter the storage list.

Module `.reg` and `.local` declarations are checked against the source-target
ABI assumption: PTX ISA 3.0 or later with a recognized active source target of
SM 20 or later requires these variables to be declared within a function.
The frontend does not model a caller-selected switch that disables ABI compilation.
Earlier PTX versions and recognized SM 1.x targets retain their historical
module placement. Target changes apply in source order; a later unknown or empty
target clears the previous placement context.

Declaration semantics checks every actual module occurrence, including unused
variables and conflicting redeclarations, and reports `InvalidDeclarationPlacement`
at the complete declaration range. Owned validation checks every variable identity
and retained occurrence against its source region, including registers with no
storage record, and reports `RuleViolation`. Both validation overloads recheck
placement after source/AST destruction or replacement source recontextualization.
`AvailableContext` defers this rule when either the version or recognized active
target is absent; strict validation reports `MissingValidationContext` at the
affected declaration. Other header and identity diagnostics remain independent.
Function-local declarations, nested blocks and valid register formals retain
their existing contracts. See the pinned PTX 9.3
[changes from PTX 1.x](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#changes-from-ptx-isa-version-1-x),
[register](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#register-state-space),
[local](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#local-state-space),
and [variable declaration](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#variable-declarations) rules.

## Absolute variable placement

Absolute fixed variable placement is not parsed or allocated. PTX ISA 9.3
§5.4.1 mentions the capability without a determinate executable production in
the referenced declaration clauses. This remains the named
[`ADOPTED_REFERENCE_GAP`](converage/syntax_coverage.md#fixed-variable-addresses-adopted-reference-gap),
not implemented fixed-address support.

| Ordinary declaration | Retained fact |
| --- | --- |
| `.global .u32 x;` | Zero initialization, 4-byte extent, natural alignment 4 |
| `.global .u32 x=0x100;` | Initializer contents 256, 4-byte extent |
| `.global .u32 x[256];` | 256 elements, 1024-byte extent |
| `.global .align 256 .u32 x;` | Alignment divisor 256, 4-byte extent |
| `.global .align 16 .u32 x[2]={0x100,0};` | Alignment 16, two elements, 8-byte extent, contents 256 and 0 |

None selects absolute address 256. The owned records describe declarations,
contents and layout inputs, with no absolute-placement field or allocator.
Implicit zero initialization does not fabricate a list of explicit initializer
values. These ordinary records can be validated and copied after source/AST
destruction; no fixed-placement source or owned case is implied.

## Identity and layout inputs

Each source declarator contributes one record in source traversal order.
`symbol_id` names its existing module symbol; `scope_id` identifies its lexical
scope, and `owner_function` is absent for module-level storage. Nested blocks
and same-spelled locals therefore remain distinguishable. Repeated compatible
external declarations share a symbol identity but retain separate occurrences.
Existing linkage and multiple-definition checks still apply; an extern
declaration followed by a same-named plain definition is rejected.

`.common` is retained as distinct `SymbolLinkage::Common` on ordinary module-scope
`.global` definitions (PTX 5.0, SM 20 or later). Scalar, vector, array, and
parameterized declarations use the existing storage layout rules. Omitted
initializers retain `Zero`; explicit zero, nonzero, and sparse initializers retain
`Explicit`. Functions, opaque objects, other state spaces, and conflicting linkage
qualifiers are rejected. Same-module repeated definitions remain errors.
Both validation overloads check eligibility and their selected version/target
context; AST-free validation rejoins declarations to ordered source target events.
The [PTX 9.3 `.common` rules](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#linking-directives-common)
select the largest definition across object files and allow only one object file
to initialize it, which must also provide the largest definition. These cross-object
rules belong to a downstream linker; the frontend does not select or merge objects.

Both `validateModule` overloads rejoin every record to its bound declaration
occurrence and require the storage list to be complete, including each compatible
extern occurrence. `SymbolTable::declarationOccurrences()` exposes a read-only
span of these owned occurrences. Validation checks lexical ownership, state space,
linkage, declaration kind, scalar type, vector width, positive array shape,
checked byte extent, alignment, and parameterized count. An unsized external first
dimension has absent byte extent; the dynamic-shared flag must agree with its
external shared declaration. Clearing the list or changing a record to zero bytes,
invalid alignment, or an unrelated identity is rejected after AST destruction.
The owned shape is checked for internal consistency; binding does not retain a
second copy of array dimensions or reconstruct source text.

| Field | Contract |
| --- | --- |
| `element_type`, `vector_width` | Typed scalar or explicitly opaque object; vector lanes are separate from array dimensions |
| `array_extents` | Outer-to-inner element counts; scalar declarations have no dimensions; an external unsized first dimension has no value |
| `byte_extent` | Checked size of one declared object; not a runtime address, frame offset, or sum over parameterized names |
| `alignment`, `explicit_alignment` | Effective byte alignment and whether the source explicitly specified it |
| `linkage`, `declaration_kind` | Source linkage and external-declaration versus definition status |
| `is_dynamic_shared` | External shared declaration whose first dimension is unsized |
| `parameterized_count` | Number of separately named objects; instruction references identify members by their parameterized index |
| `is_managed`, `unified_id` | Retained attributes. `unified_id` is optional `ResolvedUnifiedId{upper, lower}` in PTX `uuid1`/`uuid2` order, not simulated allocation policy or host/device addresses |

Opaque texture, sampler, and surface identities do not acquire invented physical
sizes or default alignments. Unknown size is represented by an absent value,
never by zero. Zero/invalid dimensions, invalid alignments, and multiplication
overflow produce diagnostics rather than wrapping a size.

## Initializers

Initialization has four explicit states: `Uninitialized` for shared/local or
opaque objects without source initialization; `Zero` for implicit ordinary global/constant initialization; `Explicit`
for a source initializer, including an empty brace list; and `External` when
this module supplies no initial contents. An explicit initializer is a sparse
list of scalar values at byte offsets within the declared object. Missing
positions in ordinary `Explicit` mode are zero-filled; the frontend does not expand a
large aggregate into a byte buffer. Recursive array/vector brace positions
determine each offset. Opaque named-member initializers use separate typed field
groups and object-index paths; they never enter this byte-offset list or imply
default values for omitted fields.

`StorageConstant` holds the normalized element bits: `bits` is the low 64-bit
word and `high_bits` is the high word, independent of host byte order. The high
word is zero for elements of at most 64 bits. `StorageRelocation`
retains the bound symbol, optional parameterized member, state-space/generic/
function address interpretation, a byte addend, and an optional raw byte mask
(`0xff` shifted by 0, 8, ..., 56 bits).
Mask callees must be integer literals; decimal, leading-zero octal, binary,
and hexadecimal spellings (including an unsigned suffix) use the common integer
decoder and select the same eight allowed mask values. For example,
`255(257)`, `0377(257)`, `0b11111111(257)`, and `0xff(257)` each store `1`.
Expression-valued or parenthesized mask callees and floating literals are rejected.
The mask selects a byte after applying the addend and places that byte in the
low eight bits. Arithmetic after extraction is rejected rather than reordered.
Symbol identity follows lexical binding, including same-named declarations in
inner scopes. Function references may be bare or byte-masked; generic conversion
and address arithmetic on function references are rejected.
For a function-address initializer, the referenced function spelling must have
an earlier declaration occurrence; a later definition found by two-pass binding
does not make the use legal. Compatible prototypes and definitions still share
their stable identity, while an alias must itself have been declared before an
initializer uses that alias spelling.
Neither unresolved external symbols nor function references receive fabricated
addresses. Downstream consumers resolve these references against their own
linking/allocation model, without reparsing initializer text.

Known source versions before PTX 3.1 retain implicit generic addressing for
global symbols. Versionless fragments use the current state-space interpretation.
Address masks require PTX 7.1 and constant-integer masks PTX 7.3 when a source
version is present. Kernel-function initializers require PTX 3.1;
a versionless fragment is not a target-validity certificate.

Only fundamental scalar declaration types enter the scalar type alternative;
instruction-only packed/alternate formats must use their bit-container types.
Integer/bit initializer elements include `.b128`, with 16-byte array strides.
Integer literals and expressions still use PTX's `.s64`/`.u64` evaluation domain;
the wider destination does not enable 128-bit literals or expression arithmetic.
For `.b128`, the frontend sign-extends a signed result and zero-extends an
unsigned result only after evaluation. Thus `-1` fills both words with ones,
whereas `-1U` fills only the low word. Precisely, after evaluating the whole
expression, `lo = source.bits` and `hi = (!source.is_unsigned && lo.bit63) ?
UINT64_MAX : 0`. Sparse aggregate positions remain zero.

Owned `StorageConstant` records erase the source expression and signedness. Both
module validator APIs check each retained constant leaf of actual `.b128`
storage against `hi == 0 || (hi == UINT64_MAX && lo.bit63)`, including unused
storage, comma declarators, and every flattened array leaf. Thus a low word with
bit 63 set permits both high zero and high all-ones; current value edits between
these forms do not recover source history. The check is independent of target
context and initialization mode. Empty and implicit-zero storage do not gain
synthetic leaves, and existing external and parameterized-group source
initializer restrictions remain. Invalid values report the owned leaf range,
falling back to the declaration range when the leaf has no usable range.

This widening is an explicit frontend policy, not a GPU-verified conformance
claim: PTX specifies conversion at initialization but does not explicitly state
the upper-half rule for `.b128`. No GPU validation was available, and observed
anomalous upper words in ptxas 13.3.33 output are not reproduced by this contract.

Floating initialization supports decimal and exact-F64 literals with signs and
parentheses, including decimal trailing-point forms such as `1.` and `1.e2`;
supported compound arithmetic and integer-condition ternaries use the shared
binary64 constant evaluator, followed by the common `.f32`/`.f64` destination
conversion. Mixed integer/floating operands promote to binary64 using integer
signedness; floating comparisons produce integer results. Exact-F32 literals
retain their raw payload only as unsigned direct initializers. Parentheses,
unary signs, arithmetic, comparisons, casts, and conditional expression uses of
exact-F32 are rejected even in unselected branches. Decimal and exact-F64 unary
signs remain valid, including negative zero. Bit destinations accept only equal
source precision: `.b32` receives a direct exact-F32 literal, while `.b64`
receives decimal/exact-F64 literals or supported binary64 expressions. Raw
payload and negative-zero bits are preserved. `.b8/.b16/.b128` and mismatched
`.b32/.b64` source precision are rejected; integer destinations remain incompatible
with floating results. Opaque object
metadata includes module-global declarations with preserved array shape,
explicit alignment, and typed named-member initialization. See the
[texture contract](converage/texture_coverage.md) for its identity and static-field boundary.
These boundaries do not turn unsupported values into zero.

## Diagnostics and boundaries

`ResolveDiagnostic::declaration_kind` and `previous_range` preserve categories
and related locations supplied by declaration semantics. Other diagnostic
stages retain their existing contract; this is not a general diagnostic API
redesign.

The normalization path reuses the existing parser, binding identities, and
constant-expression machinery. A form outside its typed initializer domain
must produce an explicit diagnostic, not an empty initializer or a guessed
value. Storage metadata does not certify every declaration-type target rule,
linker rule, launch limit, or runtime access restriction.

The normative baseline is the [PTX ISA 9.3 variable
rules](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#variables).
The executable contract is exercised by the [storage regression
tests](../../submod/resolved_ir/test/test_storage_declaration_metadata.cpp) and
the [installed consumer](../../examples/texture_consumer/main.cpp).

Allocation order, padding between distinct objects, external symbol resolution,
dynamic shared-memory size, per-CTA/per-thread instances, and runtime memory
contents remain downstream responsibilities.
