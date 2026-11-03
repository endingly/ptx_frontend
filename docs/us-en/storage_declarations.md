# Resolved storage declarations

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#storage-declarations). This page retains technical reference material and evidence.

[SDK support: Resolved storage declarations](../sdk_support.md#storage-declarations--resolved-storage-declarations).

`ResolvedModule::storage_declarations` exposes owned metadata for `.global`,
`.const`, `.shared`, and `.local` declarators. Include
`<ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>` for the data types and
the usual resolved-IR header for `resolveModule()`. Source text and Syntax AST
may be destroyed after successful resolution.

## Absolute variable placement

[SDK support: Absolute variable placement](../sdk_support.md#storage-declarations--absolute-variable-placement).

## Identity and layout inputs

[SDK support: Identity and layout inputs](../sdk_support.md#storage-declarations--identity-and-layout-inputs).

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

[SDK support: Initializers](../sdk_support.md#storage-declarations--initializers).

## Diagnostics and boundaries

[SDK support: Diagnostics and boundaries](../sdk_support.md#storage-declarations--diagnostics-and-boundaries).
