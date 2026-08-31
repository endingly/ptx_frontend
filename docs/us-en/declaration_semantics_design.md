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

An array dimension must evaluate to a positive integer constant. The evaluator
retains a 64-bit bit pattern plus `.s64`/`.u64` signedness for every integer
subexpression. It therefore supports negative intermediate values, casts,
usual arithmetic conversions, and unary, binary, and conditional operations
without rejecting expressions such as `-1 + 2`. `WARP_SZ` is evaluated here as
well; a symbol address is not an array extent. Only the first dimension may be
omitted, only when an initializer can infer it from its outermost list.

Initializer brace nesting must match the array rank. A vector declaration adds
an innermost aggregate extent of two or four. A list may contain fewer elements
than its declared extent because PTX zero-initializes the remainder; only an
overflowing list is diagnosed.

Scalar leaves distinguish integer, floating, and symbol-address expressions.
Integer and floating expressions must match their destination type category,
while an address may initialize only `.u32` or `.u64`. An initializer symbol
must name a function or a `.global`/`.const` variable. `generic()` and the mask
form are treated as initializer operators rather than ordinary calls.

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
symbols, and report the compact-entry range for missing or overlapping labels.

`.callprototype` rejects `.noreturn` with return parameters, applies existing
alignment/array-extent checks to its formals, and requires an array formal to
use `.param`. Binding remains the owner of duplicate declaration labels.
Module resolution converts a valid prototype to the same canonical signature
as a function and reuses the validated first `.calltargets` member signature
for indirect-call ABI checking. ABI suffix availability remains later work.

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

This pass does not perform opcode-specific instruction type checking or
link-time selection across modules. Integer constant-expression handling
covers the current AST grammar and propagates PTX `.s64`/`.u64` types; new
constant operators must extend classification, signedness propagation, and
evaluation together.
