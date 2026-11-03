# Parameter declarations

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#parameter-declarations). This page retains technical reference material and evidence.

[SDK support: Parameter declarations](../sdk_support.md#parameter-declarations--parameter-declarations).


## Coverage matrix

[SDK support: Coverage matrix](../sdk_support.md#parameter-declarations--coverage-matrix).

## Alignment, pointers, and size

[SDK support: Alignment, pointers, and size](../sdk_support.md#parameter-declarations--alignment-pointers-and-size).

## Owned metadata and remaining scope

[SDK support: Owned metadata and remaining scope](../sdk_support.md#parameter-declarations--owned-metadata-and-remaining-scope).

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

### Retained boundaries and normative classification

[SDK support: Retained boundaries and normative classification](../sdk_support.md#parameter-declarations--retained-boundaries-and-normative-classification).

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

[SDK support: Fundamental vectors and packed elements](../sdk_support.md#parameter-declarations--fundamental-vectors-and-packed-elements).

## Historical entry body inputs and typed roles

[SDK support: Historical entry body inputs and typed roles](../sdk_support.md#parameter-declarations--historical-entry-body-inputs-and-typed-roles).
