# PTX Frontend IR Design

## Status

This document defines the target architecture for the PTX frontend IR. It
supersedes the current design in which the parser directly produces generated
instruction structures parameterized by `ParsedOp`.

The design has exactly two first-class representations:

```text
PTX source
  -> Syntax AST
  -> Resolved PTX IR
```

Control-flow graphs, SSA, and target-specific lowering are optional derived
representations. They are not part of the parser's core IR contract.

## Goals

- Preserve the source form and source ranges needed for diagnostics,
  formatting, and error recovery.
- Keep parsing independent of symbol resolution, target selection, and
  instruction semantic selection.
- Represent every semantically selected PTX instruction form with a simple,
  generated C++ struct.
- Make all identifiers in the resolved representation explicit IDs rather than
  unchecked strings.
- Keep YAML declarative and make it describe PTX facts, not C++ storage-layout
  choices.

## 1. Syntax AST

The Syntax AST is a handwritten, generic, source-faithful representation. It
does not have one generated C++ type per opcode. Parsing only establishes that
the token sequence is structurally valid PTX syntax; it must not decide which
semantic instruction form is selected.

An instruction has the following conceptual shape:

```cpp
struct AstInstruction {
  AstOpcode opcode;
  std::vector<AstModifier> modifiers;
  std::vector<AstOperand> operands;
  std::optional<AstPredicate> predicate;
  SourceRange range;
};
```

`AstOpcode`, `AstModifier`, and every alternative of `AstOperand` retain their
original spelling and `SourceRange`. `AstOperand` is a syntax union, for
example:

```cpp
using AstOperand = std::variant<
    AstIdentifierRef,
    AstIdentifierRef,
    AstImmediate,
    AstAddress,
    AstVectorMember,
    AstVectorPack>;
```

At this stage `%r1`, `foo`, and `target` are textual references. In
particular, `%r1` and `foo` are both `AstIdentifierRef`: the parser does not
infer whether they name a register, a variable, a function, or a label.
`AstAddress`, `AstImmediate`, and vector forms record grammar shape only.
Likewise, modifier order is recorded exactly as written rather than encoded as
a generated instruction variant.

The AST contains declarations, directives, labels, functions, and modules in
the same source-faithful style. It is the only representation that needs
per-token locations.

## 2. Resolved PTX IR

The semantic analyzer consumes a complete Syntax AST plus a symbol table,
instruction database, and optional PTX target. It chooses an instruction form
only after it has examined the complete modifier and operand sequence.

The output is Resolved PTX IR. It contains no unresolved identifier strings:

- registers use `RegisterId`;
- variables, functions, and parameters use `SymbolId`;
- branch destinations use `LabelId` or `BlockId`;
- operands use resolved address-space, type, and immediate information.

Resolved IR preserves source provenance for diagnostics. Every statement has
an instruction-level origin, and independently diagnosable fields may carry a
`WithOrigin<T>` that records one primary source range plus related ranges. The
wrapped value is resolved semantic data, not `WithLoc<ParsedOp>` from the old
mixed representation.

Each semantic PTX instruction form is one flat generated struct. For example:

```cpp
struct AddU32 {
  RegisterId dst;
  RegOrImmediate src1;
  RegOrImmediate src2;
};

struct AddSatS32 {
  RegisterId dst;
  RegOrImmediate src1;
  RegOrImmediate src2;
};

using ResolvedInstruction = std::variant<AddU32, AddSatS32>;
```

Two forms may intentionally have identical fields. Keeping them as distinct
structs retains their selected PTX semantics and prevents later passes from
reconstructing that semantic decision from modifier fields.

## Resolution responsibilities

Resolution owns the following work:

1. Resolve names and construct IDs.
2. Check modifier names, duplication, and declared slot order.
3. Match an opcode against candidate forms using both modifiers and complete
   operand shapes.
4. Check operand classes, types, state spaces, arity, and instruction-specific
   constraints.
5. Check PTX version, SM version, target family, deprecation, and removal.
6. Produce a `ResolvedInstruction` or diagnostics tied to Syntax AST ranges.

Consequently, an ambiguity between forms with the same opcode and modifiers but
different operand layouts is resolved in this pass, not rejected prematurely
by the parser.

## Generated C++ IR rules

The generator emits simple data declarations only:

- one `struct` for every resolved instruction form;
- one member for every resolved field;
- category and global `std::variant` aliases;
- instruction descriptors needed by resolution.

Generated IR must not use backend layout strategies such as `direct`,
`sub_struct`, or `sub_variant`. Those choices describe C++ storage mechanics,
not PTX semantics, and must be removed from the backend schema, normalization
model, and templates.

The existing generic `PtxInstruction<Operand>` / `ParsedOp` model is not the
new IR boundary. It mixes unresolved textual operands with selected semantic
instruction shapes, while labels, predicates, and declarations still contain
raw identifiers outside the operand template parameter.

## YAML model

YAML remains the source of generated resolved forms, but it has two distinct
semantic sections:

- `syntax`: opcode spelling, modifier slots, operand grammar categories,
  optionality, and source-level forms;
- `forms`: semantic constraints, availability, resolved field definitions, and
  operand resolution rules.

Conceptually:

```yaml
instructions:
  add:
    syntax:
      modifiers: [sat, type]
      operands: [dst, src1, src2]
    forms:
      - name: AddSatS32
        requires:
          sat: true
          type: s32
        availability: { ptx: "1.0" }
        fields:
          - { name: dst, type: RegisterId }
          - { name: src1, type: RegOrImmediate }
          - { name: src2, type: RegOrImmediate }
```

The schema must validate this model. It must not expose unsupported backend
hooks or C++ layout alternatives as if they were PTX capabilities.

## Optional downstream representations

A client that needs control-flow analysis, optimization, interpretation, or
translation may build a CFG and optionally an SSA form from Resolved PTX IR.
These are separate passes with explicit ownership; they do not alter the
Syntax AST or the resolved instruction-form contract.

## Migration plan

1. Introduce Syntax AST types and change the parser API to return an AST
   module/instruction.
2. Introduce ID and resolved operand types, plus a resolver that emits one
   hand-written pilot resolved form.
3. Replace the generator backend schema with flat `forms` and generate the
   resolved instruction structs and descriptors.
4. Migrate one instruction category at a time; add AST parser tests and
   resolver tests for every migrated form.
5. Remove `ParsedOp`, `PtxInstruction<Operand>`, `emit.kind`, and templates
   whose only purpose was to support the old mixed representation.
