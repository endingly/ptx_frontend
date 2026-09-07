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

`ptx_frontend.code_gen.database` recursively discovers the canonical
`python/code_gen/resources/ptx_spec/**/*.yaml` (available in source trees via
the compatibility symlink `instructions/ptx_spec`),
loads them in path order, enforces one schema version, and then merges
definitions of the same opcode. The minimal stable model in `ptx_frontend.code_gen.model` is:

```python
InstructionSpec(opcode, variants, syntax_forms, source_categories,
                codegen_category)
VariantSpec(name, availability, modifiers, operand_layouts, rule)
OperandLayoutSpec(name, operands)
ModifierSpec(name, kind, presence, values, value, token, default)
OperandSpec(name, kind, role, access, type_expression)
```

The model carries only fields currently consumed by the frontend generator.
YAML documentation, examples, and constraints that have no generator consumer
must not silently leak into the C++ representation.

After merging an opcode, the database validates the selector language. Active
modifier slots within one variant must have disjoint spelling sets, and the
unordered spelling sets accepted by different variants must not overlap. Slot
names are variant-local, so one spelling may bind different slots across
variants. These checks make candidate-local C++ binding deterministic while
remaining independent of modifier source order.

## Normalization

`ptx_frontend.code_gen.normalize` converts different legal YAML spellings into one model:

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
SyntaxVariantDescriptor(variant_id, modifiers, operand_layouts)
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
ResolvedField(name, value_cpp_type, origin, storage, ...)
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

## C++ emitters and artifacts

`python/scripts/gen_all.py` atomically generates the public declarations,
runtime mappings, dispatch, category-partitioned implementations, and
descriptors required by the Resolved IR stage:

| Output | Emitter | Contents |
| --- | --- | --- |
| `public/resolved_ir.gen.hpp` | `gen_resolved_ir.py` | all opcode structs plus explicit-specialization declarations for `resolve<T>` and `check<T>` |
| `private/resolved_value_domains.gen.hpp` | `gen_resolved_value_domains.py` | runtime value-domain lookup tables used by the resolver |
| `private/resolved_ir_dispatch.gen.cpp` | `gen_resolved_ir.py` | opcode-independent resolve/check dispatch |
| `private/resolved_ir_<category>.gen.cpp` | `gen_resolved_ir.py` | out-of-line definitions of those two specialization sets for one category |
| `private/syntax_descriptor.gen.cpp` | `gen_syntax_ast_arch.py` | source-syntax descriptors and getters |
| `private/resolved_descriptor.gen.cpp` | `gen_resolved_descriptor.py` | resolved field/binding descriptors and getters |
| `private/resolved_ir_checker_descriptor.gen.cpp` | `gen_resolved_checker_descriptor.py` | availability/rule descriptors and getters |

The generated public header remains flat under the `generated/public` include
root in the `submod/resolved_ir` build tree. `submod/resolved_ir` includes the
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

### Backend configuration boundary

`instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` and
`instructions/schemas/ptx-cpp-backend-v1.schema.yaml` are retained as a
separate C++ backend configuration layer. `ptx_frontend.code_gen.cpp_backend` normalizes its
`domains` into `DomainBackend`; Syntax, Resolved, and checker emitters use only
typed lookups for C++ spellings. Lookup APIs require a `CppDomain` enum member,
such as `CppDomain.SCALAR_TYPES`, rather than a bare string. Current domains
cover scalar types, rounding modes, resolved value types/kinds, modifier
presence, operand roles/access/shapes, type-expression kinds, and checker
modifier kinds.

A backend spec must not duplicate PTX ISA semantics from `ptx_spec` or alter
the normalized `InstructionSpec`. `DomainBackend` and `CodegenUnit` now serve
the active generation path. `InstructionBackend` and `EmitBackend` remain
reserved for future per-instruction overrides; the current `instructions`
mapping is empty and cannot alter Resolved IR variant/layout structure.
Emitters never read raw YAML dictionaries, and a missing domain/value is a
generation-time `ValueError`. The loader performs JSON Schema validation before
checking that every domain required by the active generation path exists.
CMake tracks both the backend YAML and its schema as generation dependencies,
so changing a C++ mapping regenerates all affected artifacts.

Domains that must parse PTX source suffixes at runtime declare
`runtime_lookup: ptx_suffix`. The generator emits their mappings as private
`inline constexpr std::array` tables in `resolved_value_domains.gen.hpp`.
The handwritten resolver owns one generic suffix-search algorithm and does not
repeat scalar-type or rounding-mode mapping data. Domains without this marker
remain generation-only mappings and do not produce runtime tables.

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
