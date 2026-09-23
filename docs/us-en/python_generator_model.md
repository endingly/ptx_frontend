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

`python/scripts/gen_all.py` atomically generates the public declarations,
runtime mappings, dispatch, category-partitioned implementations, and
descriptors required by the Resolved IR stage:

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
| `public/ptx_frontend/resolved_ir/model/<category>.gen.hpp` | `emit.resolved_model` | one category's opcode structs and module-reference visitors |
| `public/ptx_frontend/resolved_ir/resolved_instruction_union.gen.hpp` | `emit.resolved_model` | the complete canonical-order `ResolvedInstruction` union |
| `public/ptx_frontend/resolved_ir/resolved_ir.gen.hpp` | `emit.resolved_model` | aggregate compatibility header for all category model headers and the union |
| `public/ptx_frontend/resolved_ir/{resolution,checker}/<category>.gen.hpp` | `emit.resolved_resolver` / `emit.resolved_checker` | self-contained category specialization declarations |
| `public/ptx_frontend/resolved_ir/resolved_ir_resolution.gen.hpp` / `public/ptx_frontend/resolved_ir/resolved_ir_checker.gen.hpp` | resolver / checker emitters | aggregate compatibility wrappers for whole-model consumers |
| `private/resolved_value_domains.gen.hpp` | `emit.value_domains` | runtime value-domain lookup tables used by the resolver |
| `private/resolved_ir_dispatch.gen.cpp` | `emit.resolved_dispatch` | opcode-independent resolution dispatch |
| `private/resolved_ir_<category>.gen.cpp` | `emit.category_source` | out-of-line resolver and checker specialization definitions for one category |
| `private/{syntax_descriptor,resolved_descriptor,resolved_ir_checker_descriptor}_<category>.gen.cpp` | descriptor emitters | category-owned descriptor storage and getters |

The generated public headers are under
`generated/public/ptx_frontend/resolved_ir` in the `submod/resolved_ir` build
tree and install under the same path relative to `include`. Private generated
sources and support headers remain under `generated/private` and are not
installed. `submod/resolved_ir` includes the
project-level `cmake/generate_ptx_frontend.cmake` helper, which invokes
`gen_all.py` atomically to list and generate all outputs before compiling them
into `resolved_ir`. The top level only orchestrates submodules and provides the
facade target.

Although `syntax_descriptor.gen.cpp` describes source syntax, it implements
getters on generated Resolved IR opcode types and is consumed by variant
selection and resolution. Until that generator dependency boundary changes, it
belongs to `resolved_ir` with the other atomic `gen_all.py` outputs rather than
to the `syntax` submodule by filename alone.

The public header contains no generated function bodies. Generation uses the
normalized `codegen_category`, which is separate from PTX documentation
`source_categories`. Every definition of one opcode must use the same
`codegen_category`. The generator uses that value to create stable category
sources, which CMake compiles into the `resolved_ir` library. Consumers retain
one include entry point, while the complex `std::visit` code, lambdas, and
resolve builders are compiled only once inside the library.

The generator formats a sibling candidate before comparing bytes with an
existing artifact. Identical formatted output, including the output manifest,
keeps its modification time. Whole-module APIs continue to include the
aggregate model and complete union; category-local consumers include only their
category model and resolver/checker declaration headers.

The comparison and selection spec now owns the generated
`comparison_and_selection` category. Code using `Set`, `Setp`, `Selp`, or `Slct`
through category-local headers must include
`model/comparison_and_selection.gen.hpp` and the matching `resolution/` and
`checker/` headers in place of their former `arithmetic.gen.hpp` paths. The
installed aggregate headers still expose the complete instruction model.

Each generated file opens its outer namespace once. Private storage shares one
anonymous or `generated_detail` namespace; getters are in
`ptx_frontend::resolved_ir`. Checker specialization declarations share one
`checker` namespace in the public header, and each category implementation
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

`instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` and
`instructions/ptx-cpp-backend-v2.schema.yaml` form a separate C++ backend
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
- A variant with one layout stores operand fields directly. Multiple layouts
  generate nested `*Operands` structs and a `std::variant` payload.
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
