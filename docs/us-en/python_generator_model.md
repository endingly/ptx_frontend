# Python Generator Model Design

## Purpose

The Python layer is the sole model boundary between YAML and generated C++. It
must not concatenate raw YAML dictionaries or reproduce C++ storage details.
It normalizes declarative PTX facts into immutable dataclasses, then derives
Syntax, Resolved, and checker artifacts from that one model.

```text
YAML files
  -> CodegenDatabase / normalized InstructionSpec
  -> SyntaxInstructionDescriptor + ResolvedInstruction
  -> generated C++ header and descriptor sources
```

## Input database

`ptx_frontend.spec.database` recursively discovers the canonical
`python/src/ptx_frontend/spec/resources/ptx_spec/**/*.yaml`,
loads them in path order, enforces one schema version, and then merges
definitions of the same opcode. The minimal stable model in `ptx_frontend.spec.model` is:

```python
InstructionSpec(opcode, variants, syntax_forms, source_categories,
                codegen_category)
VariantSpec(name, availability, modifiers, operand_layouts, rule, ..., modifier_order_aliases)
OperandLayoutSpec(name, operands)
ModifierSpec(name, kind, presence, values, value, token, default)
OperandSpec(name, kind, role, access, type_expression)
```

The model carries only fields currently consumed by the frontend generator.
YAML documentation, examples, and constraints that have no generator consumer
must not silently leak into the C++ representation.

After merging an opcode, the database validates the selector language. Active
modifier slots may share spellings only when required/fixed positions make
ordered binding unambiguous. Canonical modifier sequences and explicit
`modifier_order_aliases` must bind identically when they overlap within a
variant; accepted sequences must not overlap across variants. Slot names are
variant-local, so one spelling may bind different slots across variants. These
checks keep candidate-local C++ binding deterministic without accepting
arbitrary source order.

## Normalization

`ptx_frontend.spec.normalize` converts different legal YAML spellings into one model:

- expands `$name` references from both `type_sets` and `value_sets`, rejecting
  names defined in both namespaces;
- parses `type: {expr: modifier(type)}` into an `OperandTypeExpression`
  (`MODIFIER`, `modifier_name="type"`); a fixed scalar such as `u32` becomes
  `FIXED_SCALAR`;
- expands named `operand_patterns`;
- lifts legacy `operands` into one `OperandLayoutSpec("default", ...)`;
- rejects a variant that declares both `operands` and `operand_layouts`;
- rejects duplicate layout names in one variant.

All later code therefore consumes `variant.operand_layouts`; the normalizer is
the compatibility boundary, not the emitters.

## Syntax model

`ptx_frontend.ir.syntax_ast` builds a source-syntax descriptor model from `InstructionSpec`:

```python
SyntaxInstructionDescriptor(opcode, variants)
SyntaxVariantDescriptor(variant_id, modifiers, operand_layouts, modifier_order_aliases=())
SyntaxModifierDescriptor(kind_id, presence, allowed_spellings)
SyntaxOperandLayoutDescriptor(layout_id, kind, slots)
```

It answers only whether source can be written as a variant/layout:
variant-local modifier-slot spelling and presence, AST operand shape, and slot
count. `Flat` layouts and
the `reg`, `imm`, `reg_or_imm`, `pred`, and `pred_or_not` mappings are
implemented today; the last preserves the complemented `!%pN` spelling. A new
AST shape must first extend this model and the C++ foundation ABI.

## Resolved model

`ptx_frontend.ir.resolved_ir` maps the same `InstructionSpec` to a resolved C++ field model:

```python
ResolvedInstruction(opcode, cpp_name, variants)
ResolvedVariant(variant_id, modifier_fields, modifier_bindings,
                operand_layouts, availability, rule)
ResolvedOperandLayout(layout_id, cpp_name, fields, bindings)
ResolvedField(name, value_kind, origin, storage, ...)
ResolvedModifierBinding(source_kind_id, target_field_id, default_value)
ResolvedOperandBinding(target_field_id, type_expression, role, access, ...)
```

Field origin distinguishes `MODIFIER` from `OPERAND`; storage distinguishes
per-instance `WithLocs<T>` from a fixed modifier `STATIC_CONSTANT`.
`ResolvedOperandBinding` is the semantic contract shared by the C++ resolver
and checker: target field, structured type expression, role, access, and
allowed shape. Its descriptor form is `None`, `FixedScalar(ScalarType)`, or
`ModifierField(field_id)`, so neither C++ consumer parses YAML expression text.

During model conversion, an optional modifier's YAML `default` becomes a typed
`ResolvedModifierBinding.default_value` and is emitted into the resolved
descriptor. The common resolver uses it to construct `WithLocs<bool>`,
`WithLocs<ScalarType>`, or `WithLocs<RoundingMode>`: omitted modifiers have empty `locs`, while explicit
ones use the source value and range. The syntax descriptor retains only
spelling/presence and does not duplicate the semantic default.

Layouts may reuse a field name only when its complete definition is identical.
Otherwise model construction fails instead of generating ambiguous code.

Modifier value handling is table-driven. `ir.resolved_value_kind` owns semantic
identity, and `ir.resolved_value_policy` owns modifier-kind mappings, Python value
types, optional-default support, and diagnostic labels. The C++ domain and
descriptor-member mappings live in `code_gen.resolved_value_traits`; emitters
share its value conversion and descriptor initialization helpers instead of
dispatching on C++ type-name strings. Descriptor expression maps use
`ResolvedValueKind` enum keys; C++ member-name strings are output spellings only.
`ResolvedValueKind` remains importable from
`ir.resolved_ir` for existing callers.

Normalized discriminator enums are strict `Enum` members: YAML spelling is
converted once at the normalization boundary and is not interchangeable with a
raw string downstream. `spec.semantic_domains` owns immutable modeled-PTX vocabularies
and the policy for spellable values versus optional default-only sentinels.
It validates expanded value sets, fixed values, defaults, scalar expressions,
state spaces, and special-register compatibility before Resolved IR is built.
The vocabulary intentionally includes legal PTX forms that the configured C++
backend does not yet map. Such IR is valid; a missing C++ mapping remains a
clear code-generation capability error. `ResolvedField` therefore has no C++
type or expression properties, and code-generation helpers apply those
representations only while emitting output.

## C++ emitters and artifacts

`python -m ptx_frontend.code_gen` generates the public direct-class
declarations, runtime mappings, dispatch, and category-partitioned
implementations required by Resolved IR:

For one run, `GenerationContext` owns a single ordered sequence of bindings:
each normalized `InstructionSpec` is paired with its once-lowered,
backend-projected `ResolvedInstruction`. Each binding derives one canonical C++
instruction type name from its source opcode and requires the resolved model to
carry exactly that name. Syntax emission and category selection read the source
side and binding type identity, while model, descriptor, resolver, and checker
emission read the resolved side. The resolved tuple exposed for compatibility is
derived from the bindings, so source and resolved order or C++ type identity
cannot drift independently.

Context construction performs a finite structural preflight before an emitter
can create a directory or write a file: canonical binding C++ type names must
be unique, and every resolved operand payload kind must have a module-reference
policy. Binding construction itself rejects a resolved C++ name that differs
from its source-derived type name, including direct construction and
`dataclasses.replace`. This validates the frozen snapshot, not every possible
rendering or filesystem failure.

| Output | Emitter | Contents |
| --- | --- | --- |
| `public/ptx_frontend/resolved_ir/ptx_instruction_base.gen.hpp` | `emit.resolved_model` | base `Instruction`, exact form identities, and observer contract |
| `public/ptx_frontend/resolved_ir/model/<category>/<opcode>.gen.hpp` | `emit.resolved_model` | stable per-opcode aggregate: direct classes for small opcodes, or bounded form-shard includes; descriptor getters and resolver declarations |
| `public/ptx_frontend/resolved_ir/model/<category>/<opcode>_forms_NNN.gen.hpp` | `emit.resolved_model` | final-class declarations for one canonical shard of at most 64 forms when needed |
| `public/ptx_frontend/resolved_ir/ptx_resolved_ir.gen.hpp` | `emit.resolved_model` | aggregate of all opcode headers |
| `private/resolved_value_domains.gen.hpp` | `emit.value_domains` | runtime value-domain lookup tables used by the resolver |
| `private/resolved_ir_dispatch.gen.cpp` | `emit.resolved_dispatch` | opcode-independent resolution dispatch |
| `private/resolved_ir_<category>_<opcode>.gen.cpp` | `emit.resolved_source` | stable opcode resolver, selector, and descriptor-getter entry points; unsharded methods and rows for small opcodes |
| `private/resolved_ir_<category>_<opcode>_{methods,descriptors}_NNN.gen.cpp` | `emit.resolved_source` | bounded method definitions and static descriptor rows for canonical form shards |

The generated public headers are under
`generated/public/ptx_frontend/resolved_ir` in the `submod/resolved_ir` build
tree and install under the same path relative to `include`. Private generated
sources and support headers remain under `generated/private` and are not
installed. `submod/resolved_ir/CMakeLists.txt` uses the Python codegen CLI's
`--list-outputs` mode to discover artifacts, then generates them before compiling
private sources into `resolved_ir`. The generation rule depends on both schemas,
the backend mapping, specification files, and generator Python sources.
The CLI defaults to six concurrent artifact writers (`--jobs 6`); `--jobs 1`
retains serial emission. The CMake source build passes
`PTX_FRONTEND_CODEGEN_JOBS` (default `6`) to the CLI. The plan and output listing
remain ordered, and the manifest is written only after all selected artifacts
succeed. Each artifact uses a sibling candidate and atomic replacement; a failed
run may leave successfully written artifacts, but does not publish a new manifest.

Syntax descriptor storage supplies per-opcode free getters consumed by variant
selection and resolution. Unsharded opcodes keep their syntax, resolved, and
checker descriptor rows together; sharded opcodes keep the public getters in
the stable entry-point source and their rows in private descriptor shards.

The direct-class path keeps the YAML schema and normalized instruction model.
Each semantic form is a final subclass of `Instruction`, with common fields as
direct members and layout-specific fields as typed optionals. Resolution returns
`std::unique_ptr<Instruction>`. The generated opcode source and, when needed,
its method shards provide out-of-line resolution, checking, clone, and
reference visitation definitions.
The central dispatch selects a per-op resolver without an instruction union.

The public opcode headers contain no generated resolver or checker bodies. The
selection adapter lives in handwritten `ptx_resolved_ir_selection.hpp`;
opcode model headers remain usable with an incomplete syntax AST. The installed
`ptx_resolved_ir.hpp` includes the generated aggregate and module resolution API.
Generation uses the
normalized `codegen_category`, which is separate from PTX documentation
`source_categories`. Every definition of one opcode must use the same
`codegen_category`. The generator keeps one stable public aggregate and
private entry-point source per opcode. Above 64 forms it partitions
declarations, methods, and descriptor rows into deterministic shards of at
most 64 forms, compiled into the `resolved_ir` library. Consumers retain the
same opcode include entry point. A shard does not promise that adding a form
has no compile fanout.

The public syntax, resolved, and checker descriptor getters return `const&`
and are `noexcept`. For a sharded opcode, each getter uses a bounded one-time
function-local `static const std::array` to concatenate canonical static
shard rows into contiguous, stable-lifetime storage without heap allocation.
The public aggregate and exact final-class identities remain the compatibility
boundary; no mutable logical-form tag or opcode wrapper is introduced.

The generator formats a sibling candidate before comparing bytes with an
existing artifact. Identical formatted output, including the output manifest,
keeps its modification time. Consumers can include the aggregate or a single
opcode header.

The comparison and selection spec owns the generated
`comparison_and_selection` category. Narrow consumers use individual headers
such as `model/comparison_and_selection/set.gen.hpp`; the aggregate exposes all
forms.

Each generated file opens its outer namespace once. Private storage shares one
anonymous or `generated_detail` namespace; getters are in
`ptx_frontend::resolved_ir`. Checker specialization declarations share one
`checker` namespace in the public header, and each opcode implementation
likewise opens it only once.

Emitters obtain C++ types and expressions for semantic values from normalized
C++ backend domains. Generated files do not
embed wall-clock time by default. If the build environment provides the
standard `SOURCE_DATE_EPOCH`, the warning uses that deterministic UTC time;
otherwise it explicitly marks the time as omitted. Identical specs and
backend specs and generator inputs therefore produce byte-identical content.

Backend lookup helpers require an explicit `CodegenUnit`, supplied by the
context or the emitting call. The generator has no process-global active
backend, configuration step, or backend cache, so independent generation
snapshots cannot select each other's C++ spelling.

### Backend configuration boundary

`python/src/ptx_frontend/spec/resources/ptx_cpp_backend_spec/ptx_frontend.yaml` and
`python/src/ptx_frontend/spec/resources/ptx-cpp-backend-v2.schema.yaml` form a separate C++ backend
mapping layer. `ptx_frontend.code_gen.cpp_backend` normalizes its
`domains` into `DomainBackend`; Syntax, Resolved, and checker emitters use only
typed lookups for C++ spellings. Lookup APIs require a `CppDomain` enum member,
such as `CppDomain.SCALAR_TYPES`, rather than a bare string. Current domains
cover scalar types, rounding modes, resolved value types/kinds, modifier
presence, operand roles/access/shapes, type-expression kinds, and checker
modifier kinds.

A backend spec must not duplicate PTX ISA semantics from `ptx_spec` or alter
the normalized `InstructionSpec`. Its only generated inputs are the ISA schema
version, backend schema version, and closed set of C++ mapping domains;
per-instruction layout, emit, namespace, include, and category policy are not
accepted. `CodegenUnit` contains only those inputs. Emitters never read raw YAML
dictionaries, and a missing, unknown, or unmapped domain value is a
generation-time `ValueError`. The loader rejects retired
`ptx-cpp-backend/v1` files with a migration diagnostic before v2 schema
validation; consumers must migrate imports and construction to the narrowed
`CodegenUnit(spec_schema, backend_schema, domains)` contract. CMake tracks both
the backend YAML and its schema as generation dependencies, so changing a C++
mapping regenerates all affected artifacts.

To migrate a backend file, change its schema tag and YAML-language-server header
from v1 to v2, then remove `target`, `category`, `namespace`, `includes`,
`common`, `emit_kinds`, and `instructions`. Remove the retired
`modifier_value_cpp_types` and `operand_value_cpp_types` domains and any value
`token` or `aliases` entries. `Emit*`, `InstructionBackend`, `ModifierBackend`,
and `OperandBackend` are no longer importable; use `DomainBackend` and the
narrowed `CodegenUnit` instead. PTX ISA files remain `ptx-instr/v1`.

Domains that must parse PTX source suffixes at runtime declare
`runtime_lookup: ptx_suffix`. The generator emits their mappings as private
`inline constexpr std::array` tables in `resolved_value_domains.gen.hpp`.
The handwritten resolver owns one generic suffix-search algorithm and does not
repeat scalar-type or rounding-mode mapping data. A marked domain's `cpp_type`
sets the generated table value type. Without `runtime_lookup`, `cpp_type` is
type annotation only; it does not choose an instruction field type, which comes
from the `resolved_value_cpp_types` mapping. Domains without this marker remain
generation-only mappings and do not produce runtime tables.

## Generation rules

- YAML identifiers deterministically become PascalCase C++ names; collisions
  are errors.
- Each final form stores common operand fields directly and fields present
  only in some layouts as typed optionals. Fixed small operand domains may
  themselves use typed variants; the opcode owner remains `unique_ptr<Instruction>`.
- `ResolvedOperandLayoutTag` always indexes the matching syntax/resolved
  descriptor layout.
- Emitters choose only mechanically necessary C++ syntax. They never re-add
  old backend options such as `direct` or `sub_variant` to the model.
- A type, role, access mode, or shape without C++ support must raise a Python
  `ValueError` during model construction, not produce partially correct C++.

## Tests and change process

`python/tests/ir` tests YAML -> normalized model -> descriptor/emitted-source
structure. A new model field needs normalization, IR-model, and emitted-ABI
coverage. C++ tests cover the real parser/resolver/checker path.

Extend schema and normalized dataclasses first, then Syntax/Resolved models,
then emitters and tests. Do not make an emitter read a new raw YAML field: that
bypasses the consistency boundary.
