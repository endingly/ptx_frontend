# Resolved storage declarations

`ResolvedModule::storage_declarations` exposes owned metadata for `.global`,
`.const`, `.shared`, and `.local` declarators. Include
`<ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>` for the data types and
the usual resolved-IR header for `resolveModule()`. Source text and Syntax AST
may be destroyed after successful resolution.

This is a declaration-to-consumer contract, not a memory allocator or a claim
of complete PTX declaration conformance. Parameters remain separate:
`ResolvedFunction::parameter_declarations` describes `.param` declarations;
the `EntryInput` role identifies entry inputs.
Registers and call parameters do not enter the storage list.

## Identity and layout inputs

Each source declarator contributes one record in source traversal order.
`symbol_id` names its existing module symbol; `scope_id` identifies its lexical
scope, and `owner_function` is absent for module-level storage. Nested blocks
and same-spelled locals therefore remain distinguishable. Repeated compatible
external declarations share a symbol identity but retain separate occurrences.
Existing linkage and multiple-definition checks still apply; an extern
declaration followed by a same-named plain definition is rejected.

| Field | Contract |
| --- | --- |
| `element_type`, `vector_width` | Typed scalar or explicitly opaque object; vector lanes are separate from array dimensions |
| `array_extents` | Outer-to-inner element counts; scalar declarations have no dimensions; an external unsized first dimension has no value |
| `byte_extent` | Checked size of one declared object; not a runtime address, frame offset, or sum over parameterized names |
| `alignment`, `explicit_alignment` | Effective byte alignment and whether the source explicitly specified it |
| `linkage`, `declaration_kind` | Source linkage and external-declaration versus definition status |
| `is_dynamic_shared` | External shared declaration whose first dimension is unsized |
| `parameterized_count` | Number of separately named objects; instruction references identify members by their parameterized index |
| `is_managed`, `unified_id` | Retained attributes, not simulated allocation policy or host/device addresses |

Opaque texture, sampler, and surface identities do not acquire invented physical
sizes or default alignments. Unknown size is represented by an absent value,
never by zero. Zero/invalid dimensions, invalid alignments, and multiplication
overflow produce diagnostics rather than wrapping a size.

## Initializers

Initialization has four explicit states: `Uninitialized` for shared/local or
opaque objects; `Zero` for implicit global/constant initialization; `Explicit`
for a source initializer, including an empty brace list; and `External` when
this module supplies no initial contents. An explicit initializer is a sparse
list of scalar values at byte offsets within the declared object. Missing
positions in `Explicit` mode are zero-filled; the frontend does not expand a
large aggregate into a byte buffer. Recursive array/vector brace positions
determine each offset.

`StorageConstant` holds the normalized element bits: `bits` is the low 64-bit
word and `high_bits` is the high word, independent of host byte order. The high
word is zero for elements of at most 64 bits. `StorageRelocation`
retains the bound symbol, optional parameterized member, state-space/generic/
function address interpretation, a byte addend, and an optional raw byte mask
(`0xff` shifted by 0, 8, ..., 56 bits).
The mask selects a byte after applying the addend and places that byte in the
low eight bits. Arithmetic after extraction is rejected rather than reordered.
Symbol identity follows lexical binding, including same-named declarations in
inner scopes. Function references may be bare or byte-masked; generic conversion
and address arithmetic on function references are rejected.
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
whereas `-1U` fills only the low word. Sparse aggregate positions remain zero.

This widening is an explicit frontend policy, not a GPU-verified conformance
claim: PTX specifies conversion at initialization but does not explicitly state
the upper-half rule for `.b128`. No GPU validation was available, and observed
anomalous upper words in ptxas 13.3.33 output are not reproduced by this contract.

Floating initialization supports `.f32`/`.f64` literals with signs and
parentheses; compound floating arithmetic is not normalized. Opaque object
metadata is limited to module-level scalar `.global` declarations;
field-assignment initializers remain outside the supported parser/normalizer
domain. These boundaries do not turn unsupported values into zero.

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
the [installed consumer](../../submod/resolved_ir/test/package_consumer/main.cpp).

Allocation order, padding between distinct objects, external symbol resolution,
dynamic shared-memory size, per-CTA/per-thread instances, and runtime memory
contents remain downstream responsibilities.
