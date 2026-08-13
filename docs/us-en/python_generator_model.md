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

`code_gen.database` recursively discovers `instructions/ptx_spec/**/*.yaml`,
loads them in path order, and enforces one schema version and globally unique
opcodes. The minimal stable model in `code_gen.model` is:

```python
InstructionSpec(opcode, syntax, variants)
VariantSpec(name, availability, modifiers, operand_layouts, rule)
OperandLayoutSpec(name, operands)
ModifierSpec(name, kind, presence, values, value, token, default)
OperandSpec(name, kind, role, access, type_expression)
```

The model carries only fields currently consumed by the frontend generator.
YAML documentation, examples, and constraints that have no generator consumer
must not silently leak into the C++ representation.

## Normalization

`code_gen.normalize` converts different legal YAML spellings into one model:

- expands `$name` type-set references;
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

`ir.syntax_ast` builds a source-syntax descriptor model from `InstructionSpec`:

```python
SyntaxInstructionDescriptor(opcode, variants)
SyntaxVariantDescriptor(variant_id, modifiers, operand_layouts)
SyntaxModifierDescriptor(kind_id, presence, allowed_spellings)
SyntaxOperandLayoutDescriptor(layout_id, kind, slots)
```

It answers only whether source can be written as a variant/layout: modifier
spelling and presence, AST operand shape, and slot count. `Flat` layouts and
the `reg`, `imm`, `reg_or_imm`, `pred`, and `pred_or_not` mappings are
implemented today; the last preserves the complemented `!%pN` spelling. A new
AST shape must first extend this model and the C++ foundation ABI.

## Resolved model

`ir.resolved_ir` maps the same `InstructionSpec` to a resolved C++ field model:

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
descriptor. The common resolver uses it to construct `WithLocs<bool>` or
`WithLocs<ScalarType>`: omitted modifiers have empty `locs`, while explicit
ones use the source value and range. The syntax descriptor retains only
spelling/presence and does not duplicate the semantic default.

Layouts may reuse a field name only when its complete definition is identical.
Otherwise model construction fails instead of generating ambiguous code.

## C++ emitters and artifacts

`python/scripts/gen_all.py` coordinates four fixed outputs:

| Output | Emitter | Contents |
| --- | --- | --- |
| `public/resolved_ir.gen.hpp` | `gen_resolved_ir.py` | opcode structs plus `resolve<T>` and `check<T>` specializations |
| `private/syntax_descriptor.gen.cpp` | `gen_syntax_ast_arch.py` | source-syntax descriptors and getters |
| `private/resolved_descriptor.gen.cpp` | `gen_resolved_descriptor.py` | resolved field/binding descriptors and getters |
| `private/resolved_ir_checker_descriptor.gen.cpp` | `gen_resolved_checker_descriptor.py` | availability/rule descriptors and getters |

The generated public header remains flat under the build-tree `public` include
root. CMake installs that specific file as
`include/ptx_ir/resolved/resolved_ir.gen.hpp`.

Each generated file opens its outer namespace once. Private storage shares one
anonymous or `generated_detail` namespace; getters are in
`ptx_frontend::resolved_ir`; generated checker specializations share one
`checker` namespace.

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
