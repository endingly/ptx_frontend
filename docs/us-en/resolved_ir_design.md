# Resolved PTX IR Design

## Status

This document defines the target semantic representation produced after a PTX
Syntax AST has been parsed and resolved. It is the second and final core IR
layer of this frontend.

```text
PTX source
  -> lossless token stream
  -> Syntax AST
  -> Resolved PTX IR
```

## Scope and goals

Resolved PTX IR represents a program after name binding, instruction-form
selection, operand validation, and target validation. It must:

- contain no unresolved textual references in semantic fields;
- preserve the exact selected PTX instruction form;
- represent only well-formed instruction instances;
- retain instruction- and field-level provenance for diagnostics;
- be simple to generate from YAML as C++ structs and members;
- remain close to PTX rather than prematurely becoming CFG or SSA IR.

CFG construction, SSA construction, optimization, interpretation, and target
lowering are downstream passes, not responsibilities of this representation.

## Module and statement structure

A resolved module retains PTX's source-level linear structure. Labels remain
explicit and branch operands initially refer to `LabelId`; a later CFG pass may
replace or supplement them with `BlockId` edges.

```cpp
struct ResolvedStatement {
  std::optional<ResolvedPredicate> predicate;
  ResolvedInstruction instruction;
  SourceOrigin origin;
};

struct ResolvedFunction {
  FunctionId id;
  std::vector<ResolvedStatement> body;
};
```

The predicate belongs to `ResolvedStatement`, not to every individual
instruction type. This avoids duplicating a PTX statement property in every
generated instruction struct.

## Source provenance

Resolved IR must preserve its connection to source syntax because checkers run
on Resolved IR and must emit source diagnostics. A semantic value can arise
from one AST node or from several nodes, so a single `SourceRange` is not
always sufficient.

```cpp
struct SourceOrigin {
  SourceRange primary;
  std::vector<SourceRange> related;
};

template <typename T>
struct WithOrigin {
  T value;
  SourceOrigin origin;
};
```

`primary` identifies the location to underline by default. `related` records
other source fragments that jointly determine the value. For example, the
resolved form of `add.sat.u8x4` may use `.u8x4` as its primary location and
record `.sat` as a related location.

Every resolved statement carries statement-level provenance. Operand and
form/modifier fields that can independently fail validation carry
`WithOrigin<T>` as well. This does not require reuse of the old
`WithLoc<ParsedOp>` representation: the wrapped value is resolved semantic
data such as `RegisterId` or `ResolvedImmediate`, not an AST value.

## Identity and resolved references

The resolver allocates opaque IDs in the appropriate ownership scope:

```cpp
struct RegisterId { uint32_t value; };
struct SymbolId   { uint32_t value; };
struct LabelId    { uint32_t value; };
struct FunctionId { uint32_t value; };
```

The exact representation may use strong typedefs, index classes, or compact
integer wrappers, but distinct ID categories must not be implicitly
interchangeable.

The resolver maps textual AST references to IDs according to context. A name
that appeared as `AstIdentifierRef` can therefore resolve to a register,
variable, function, label, or other symbol without imposing that decision on
the Syntax AST.

## Resolved operands

Resolved operands carry semantic identity and value, not source spelling.
Their exact alternatives will grow with PTX coverage, but the initial model is
conceptually:

```cpp
struct ResolvedImmediate {
  ImmediateBits bits;
  ScalarType type;
};

using ResolvedValue = std::variant<RegisterId, ResolvedImmediate>;

struct ResolvedAddress {
  std::variant<RegisterId, SymbolId> base;
  int64_t offset;
  StateSpace space;
};

struct ResolvedVectorMember {
  RegisterId base;
  uint8_t member;
};
```

Immediate representation must preserve the information needed by PTX
semantics, including bit pattern and type. It must not depend on the AST's
literal spelling. Address space, vector width, and operand category are
validated during resolution rather than inferred by later consumers.

Operands that can be diagnosed independently are stored with their provenance,
for example `WithOrigin<RegisterId>` and `WithOrigin<ResolvedValue>`.

## Instructions and forms

`ResolvedInstruction` is a generated global variant whose alternatives are
organized by opcode:

```cpp
using ResolvedInstruction = std::variant<Add, Atom, Ld, St, Bra /* ... */>;
```

An opcode struct records the selected PTX form in the smallest representation
that preserves semantic distinctions.

### Forms with one operand layout

When all forms of an opcode share the same resolved operand layout, use a form
tag plus one shared operand struct:

```cpp
struct Add {
  enum class Form { U32, SatS32, U16x2, SatU8x4 };

  struct Operands {
    WithOrigin<RegisterId> dst;
    WithOrigin<RegOrImmediate> src1;
    WithOrigin<RegOrImmediate> src2;
  };

  Form form;
  Operands operands;
};
```

The `Form` determines fixed modifiers such as `.sat` and `.s32`; those facts
are not redundantly stored as independent fields.

### Forms with different operand layouts

When legal forms differ structurally, use an internal semantic variant. Shared
instruction data remains outside it:

```cpp
struct Atom {
  struct Common {
    StateSpace space;
  };

  struct Basic {
    WithOrigin<RegisterId> dst;
    WithOrigin<ResolvedAddress> addr;
    WithOrigin<ResolvedValue> value;
  };

  struct CompareAndSwap {
    RegisterId dst;
    ResolvedAddress addr;
    ResolvedValue compare;
    ResolvedValue replacement;
  };

  Common common;
  std::variant<Basic, CompareAndSwap> operands;
};
```

This internal variant is part of the semantic model: each alternative has a
different PTX operand layout. It is not an arbitrary code-generation layout
option.

## Generated C++ model

YAML generates the following resolved-IR artifacts:

- opcode structs;
- nested common/operand structs where the semantic model requires them;
- form enums and internal form variants;
- category and global `ResolvedInstruction` variants;
- static descriptors used by the resolver.

Every generated member must correspond to a resolved PTX fact. The generator
must not expose generic layout modes such as `direct`, `sub_struct`, or
`sub_variant` as user-selected backend mechanisms. The schema describes form
structure; templates choose the mechanically necessary C++ syntax.

Form names are generated from a stable machine-readable YAML form identifier.
They are not hand-written API obligations. Readable generated names are useful,
but their generation must be deterministic and collision-checked.

## Resolver contract

The resolver consumes `syntax_ast::AstInstruction` together with a resolution
context that contains symbol tables and an optional PTX target. It must:

1. resolve identifier references to typed IDs;
2. match opcode, modifier sequence, and complete AST operand shapes;
3. select exactly one legal semantic form;
4. validate types, state spaces, arity, and instruction-specific constraints;
5. validate PTX/SM/family availability, deprecation, and removal;
6. emit the corresponding generated opcode struct with provenance, or a
   diagnostic tied to AST source ranges.

This is where candidates with the same opcode and modifiers but different
operand layouts are disambiguated.

## YAML requirements

The instruction specification must provide stable form identifiers and enough
information to generate resolved fields and resolver descriptors:

```yaml
forms:
  - id: sat_s32
    requires: { sat: true, type: s32 }
    availability: { ptx: "1.0" }
    operands:
      - { name: dst, resolved_type: RegisterId }
      - { name: src1, resolved_type: RegOrImmediate }
      - { name: src2, resolved_type: RegOrImmediate }
```

The normalized Python model must distinguish:

- shared fields of an opcode;
- form tags with a common operand layout;
- structurally different form payloads.

## Implementation sequence

1. Define core IDs, resolved operands, statements, and source-origin policy.
2. Add a hand-written `Add` resolved instruction and resolver pilot.
3. Extend YAML/schema/normalization with stable form IDs and resolved field
   descriptors.
4. Generate one instruction category and its resolver descriptors.
5. Migrate categories incrementally, with resolver tests for valid forms,
   operand-layout ambiguity, symbols, and target availability.
