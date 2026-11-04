# Declaration Semantics Design

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#declaration-semantics-design). This page retains technical reference material and evidence.

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

## Shared numeric evaluation

[SDK support: Shared numeric evaluation](../sdk_support.md#declaration-semantics-design--shared-numeric-evaluation).

## Arrays and initializers

[SDK support: Arrays and initializers](../sdk_support.md#declaration-semantics-design--arrays-and-initializers).

## Unified UUID attributes

[SDK support: Unified UUID attributes](../sdk_support.md#declaration-semantics-design--unified-uuid-attributes).

## Redeclarations

[SDK support: Redeclarations](../sdk_support.md#declaration-semantics-design--redeclarations).

## Control-flow metadata

[SDK support: Control-flow metadata](../sdk_support.md#declaration-semantics-design--control-flow-metadata).

## Parameter declarations

[SDK support: Parameter declarations](../sdk_support.md#declaration-semantics-design--parameter-declarations).

## Entry resource constraints

[SDK support: Entry resource constraints](../sdk_support.md#declaration-semantics-design--entry-resource-constraints).

## Debug metadata boundary

[SDK support: Debug metadata boundary](../sdk_support.md#declaration-semantics-design--debug-metadata-boundary).

## Current boundary

[SDK support: Current boundary](../sdk_support.md#declaration-semantics-design--current-boundary).

## Historical entry body inputs

[SDK support: Historical entry body inputs](../sdk_support.md#declaration-semantics-design--historical-entry-body-inputs).

### Deprecated maximum-CTA hint

[SDK support: Deprecated maximum-CTA hint](../sdk_support.md#declaration-semantics-design--deprecated-maximum-cta-hint).
