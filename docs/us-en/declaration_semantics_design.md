# Declaration Semantics Design

## Position and API

Declaration semantics runs after lexical binding and before Resolved IR. Its
public entry point is:

```cpp
auto diagnostics =
    declaration_semantics::checkDeclarations(module, binding.table);
```

The pass uses the structured Syntax AST for initializer and array constraints,
and the module declaration sequence for cross-declaration compatibility.
`resolveModule()` runs both binding and this pass automatically and accumulates
their diagnostics before resolving any instruction.

## Arrays and initializers

Integer literals share decimal, leading-zero octal, and hexadecimal decoding
with instruction immediates. For example, `010` initializes the value 8 and
denotes an array extent of 8; an unsigned suffix does not change the radix.

An integer literal that cannot be decoded in the 64-bit source domain is an
invalid expression, not a deferred value. Declaration checking reports
`InvalidIntegerLiteral` at the literal's own range, and `resolveModule()`
preserves that category and location. Folding cannot hide this failure, even
when a conditional has identical branches or the literal is in an unselected
branch. Valid deferred expressions remain distinct: equal-branch folding of
a valid but unevaluated comparison can still produce a constant.

An array dimension must evaluate to a positive integer constant. The evaluator
retains a 64-bit bit pattern plus `.s64`/`.u64` signedness for every integer
subexpression. It therefore supports negative intermediate values, casts,
usual arithmetic conversions, and unary, binary, and conditional operations
without rejecting expressions such as `-1 + 2`. `WARP_SZ` is evaluated here as
well; a symbol address is not an array extent. Only the first dimension may be
omitted: an initializer can infer it from its outermost list, while an external
storage declaration may leave its first dimension unknown without an initializer.

Initializer brace nesting must match the array rank. A vector declaration adds
an innermost aggregate extent of two or four. A list may contain fewer elements
than its declared extent because PTX zero-initializes the remainder; only an
overflowing list is diagnosed.

Scalar leaves distinguish integer, floating, and symbol-address expressions.
Integer and floating expressions must match their destination type category,
while an address may initialize only `.u32` or `.u64`. An initializer symbol
must name a function or a non-opaque `.global`/`.const` variable. `.texref`,
`.samplerref`, and `.surfref` identities cannot become initializer addresses,
including through `generic()`, byte-mask, or arithmetic wrappers. `generic()`
and the mask form are treated as initializer operators rather than ordinary
calls; this restriction does not affect the permitted `mov` retrieval of an
opaque handle.

## Unified UUID attributes

For modeled `.unified(upper, lower)` attributes, declaration semantics decodes
both direct integer tokens as exact unsigned 64-bit UUID halves. Decimal,
leading-zero octal, hexadecimal, and the supported unsigned suffix share the
integer-literal rules; overflow diagnoses the offending token rather than
truncating it. This applies uniformly to eligible global variables and device
function definitions or prototypes before storage metadata or a future function
metadata backend consumes the value. Placement, PTX-version, and target checks
remain separate declaration rules.

## Redeclarations

Binding first maps same-name module items to a stable `SymbolId`; this pass then
decides whether the redeclaration is legal:

- matching `.extern` variable declarations may repeat;
- matching `.func` prototypes may be combined with at most one definition;
- variable state space, alignment, vector/base type, parameterized count, and
  array shape must be compatible;
- function kind, `.noreturn`, return/input interface, and linkage must be
  compatible;
- symbol-kind conflicts, linkage conflicts, signature changes, and multiple
  definitions report diagnostics carrying the previous source range;
- an `.extern .func` must be a prototype and cannot have a body.

Redeclaration alignment compares its effective value while retaining source
provenance and ranges on every declaration occurrence. For modeled fundamental
storage, an omitted alignment is the scalar byte size (also for scalar arrays)
or the complete `.v2`/`.v4` element width (also for arrays of vectors). An
explicit valid alignment compares by decoded integer value, so equivalent octal
and decimal spellings match. Unsupported layouts and invalid alignment syntax
do not receive an inferred default. Parameterized counts and ABI-preserve
counts likewise compare by decoded integer value.

Verification note: CUDA 13.1 `ptxas` V13.1.115 accepted both declaration orders
for `.version 8.0` / `.target sm_80` / `.address_size 64` fixtures containing
matching implicit and explicit scalar, scalar-array, byte-array, `.v2 .u32`,
and `.v4 .u32` external declarations. For the `g` scalar fixture, whole-program
compilation emitted exactly `ptxas warning : Unresolved extern variable 'g' in
whole program compilation, ignoring extern qualifier`; the comparison is
therefore successful with that warning, not warning-free. The same assembler
rejected `.align 3` with `Alignment must be a power of two`.

Every function prototype and definition still owns a lexical scope. The
function symbol's `owned_scope` prefers the definition scope, so module
resolution uses the parameters and locals belonging to the body it resolves.

## Control-flow metadata

The same pass checks function-local indirect-control metadata. A
`.calltargets` member must be a previously declared device `.func`, duplicate
members are rejected with both member ranges, and every valid member must have
the same canonical `FunctionSignature`. `.branchtargets` members must be labels
in the owning function; forward labels are valid. Compact entries such as
`N<5>` are checked against the existing local labels without creating synthetic
symbols, and report the compact-entry range for missing labels. The branch
table is an ordered index sequence: repeated explicit destinations and overlaps
with or between compact entries are valid and are not deduplicated.

`.callprototype` rejects `.noreturn` with return parameters, applies existing
alignment/array-extent checks to its formals, and requires an array formal to
use `.param`. Binding remains the owner of duplicate declaration labels.
Module resolution converts a valid prototype to the same canonical signature
as a function and reuses the validated first `.calltargets` member signature
for indirect-call ABI checking. ABI suffix availability remains later work.

## Parameter declarations

Parameters have a context-aware [coverage and validation contract](parameter_declarations.md),
including supported types, unsized-array placement, pointer attributes, and
statically decidable ISA version/target and entry-size limits. This applies to
entry/device headers, call-prototype formals, and body-local `.param`
declarations; retained type spelling or array syntax alone does not establish validity.

## Entry resource constraints

For the supported entry-header `.maxnreg`, `.maxntid`, `.reqntid`, and
`.minnctapersm` directives, this pass compares a declared module `.version`
with their PTX minima (1.3, 1.3, 2.1, and 2.0 respectively). All four are
supported on every SM, so `.target` does not add a check here. `.reqntid` and `.maxntid`
are mutually exclusive within one entry and diagnose the later directive with
the earlier range as context. A lone `.minnctapersm` is a PTX warning rather
than an error; warning severity, backend resource feasibility, and numerical
limits remain outside this pass.

## Debug metadata boundary

Binding owns the `.file`/`.loc` and `.debug_str` identity table: duplicate file
indices are idempotent, `.loc` file references must bind, and
`function_name` must identify `.debug_str` itself or one of its raw labels.
This declaration pass intentionally adds no DWARF payload-expression, source
attachment, or resource-feasibility semantics.

## Current boundary

After declaration checking, module resolution projects the supported storage
forms into [owned storage metadata](storage_declarations.md), including checked
byte extents and typed initializer values/references. This additional
normalization can diagnose a form outside its representable domain even when
the declaration checker alone accepts its expression category. Declaration
categories and related ranges survive through `ResolveDiagnostic`.

This pass does not perform opcode-specific instruction type checking or
link-time selection across modules. Integer constant-expression handling
covers the current AST grammar and propagates PTX `.s64`/`.u64` types; new
constant operators must extend classification, signedness propagation, and
evaluation together.

Binary bitwise operators follow the detailed PTX usual-conversion rule; see
[the signedness decision and reproducible assembler evidence](bitwise_constant_policy.md)
for the conflicting summary-table wording and tool-version limitations.
