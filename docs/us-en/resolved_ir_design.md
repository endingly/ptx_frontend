# C++ Resolved IR Design

## Status and scope

This document describes the implemented Resolved PTX IR, not a future CFG,
SSA, or backend IR. The frontend's core flow is:

```text
PTX source -> Token stream -> Syntax AST -> Resolved IR -> checker
```

Syntax AST preserves source spelling, modifier order, and `SourceRange`.
Resolved IR records the selected instruction variant, resolved operand values,
and diagnostic locations. Both are stable frontend boundaries. CFG/SSA,
complete symbol binding, and target lowering are later passes.

## Locations and primitive values

Every independently diagnosable resolved value uses:

```cpp
template <typename T>
struct WithLocs {
  T value;
  std::vector<SourceRange> locs;
};
```

`locs` can associate one semantic value with several source fragments; an empty
list denotes no direct source location, such as a compile-time fixed modifier
or an instance value injected from an optional modifier's YAML `default`. The
latter remains a `WithLocs<T>`: `value` holds the semantic default and empty
`locs` means it was not written explicitly.
Current primitives include `ResolvedRegisterRef`, `ResolvedImmediate`, and
`RegOrImm`. A `ResolvedImmediate` stores integer bits and `ScalarType`, so the
checker never has to reinterpret literal text.

`AstImmediateKind` retains the lexer's literal classification. Decimal and hex
integers, including their optional `U` suffix, are range-checked against the
target integer or bit type. Negative values are stored in `bits` as the
target-width two's-complement representation rather than being unconditionally
extended to 64 bits. Decimal floats currently convert to `F32` and `F64`, while
`0f<8 hex>` and `0d<16 hex>` are raw IEEE bit patterns for `F32` and `F64`
respectively. Other floating formats require explicit quantization rules and
must not silently take the integer path.

Until symbol-table declaration binding is available, `ResolvedRegisterRef`
owns the register's complete source spelling and also records its
`ResolvedRegisterClass` and numbered-register index. The index is only a
convenience, not an identity: `%r1` and `%rd1` both have index 1 but retain
different spellings. The current resolver supports the numbered-register
subset and distinguishes general registers from `%pN` predicates. A future
symbol-table pass should augment or replace this lexical reference with a
declaration `SymbolId`.

## Opcode-generated structures

Every opcode generates one outer struct. `VariantType` and `std::variant`
represent the variant uniquely selected by the modifier combination:

```cpp
struct Add {
  enum class VariantType { IntegerNoSat, SatS32 /* ... */ };

  struct IntegerNoSat {
    ResolvedOperandLayoutTag operand_layout;
    WithLocs<ScalarType> type;
    WithLocs<ResolvedRegisterRef> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };
  using Variant = std::variant<IntegerNoSat /* ... */>;
  Variant variant;
};
```

A fixed modifier is not mutable per-instance state. For example,
`add.sat.s32` generates:

```cpp
inline static constexpr bool saturate = true;
inline static constexpr ScalarType type = ScalarType::S32;
```

This retains selected semantics without forcing later passes to infer the same
fact again.

## Multiple operand layouts in one variant

The same modifier combination can admit different operand shapes. It must not
be split into artificial modifier variants. Instead, generate a layout tag and
a nested payload variant. `bar.sync a{, b}` is represented as:

```cpp
struct Bar::Sync {
  ResolvedOperandLayoutTag operand_layout;
  inline static constexpr bool sync = true;
  struct BarrierOperands { WithLocs<RegOrImm> barrier; };
  struct BarrierAndThreadCountOperands {
    WithLocs<RegOrImm> barrier;
    WithLocs<RegOrImm> thread_count;
  };
  using Operands = std::variant<BarrierOperands,
                                BarrierAndThreadCountOperands>;
  Operands operands;
};
```

`ResolvedOperandLayoutTag` is the index of the layout in generated descriptors.
The checker verifies tag validity, tag/payload-alternative agreement, and every
operand binding. A disagreement is corrupted Resolved IR and produces
`OperandLayoutPayloadMismatch`.

The only implemented layout algorithm is `Flat`: comma-separated positional
operand slots. Groups, variadics, and call argument lists require a Syntax AST
extension followed by a new layout kind; they must not be disguised as `Flat`.

## Resolution protocol

`resolve<T>(const AstInstruction&)` is a generated opcode-specific thin
wrapper. Shared logic performs the following steps:

1. `collect_actual_modifiers` maps source spellings to descriptor `kind_id`s
   and diagnoses unknown spellings and duplicate kinds.
2. `selectVariant<T>` selects exactly one variant from modifier slots only.
   `absent`, `optional`, and `required/fixed` match by kind and allowed value,
   never by modifier-list index.
3. The selected variant chooses exactly one `OperandLayout` from AST shapes and
   arity.
4. `resolve_fields` converts modifiers and operands through resolved bindings
   into location-carrying values.
5. The generated builder places fields in the selected struct or payload.

No matching variant/layout is a user diagnostic. Multiple matching layouts, or
a mismatch between descriptors and generated structures, is a generator bug and
uses `ResolveException`, distinct from `ResolveDiagnostic`.

`selectVariant<T>` remains a common template implementation in the handwritten
public ABI header, so every type satisfying the `PtxOperator` concept can use it
directly. One generated `resolved_ir.gen.hpp` centralizes all opcode structs and
the explicit-specialization declarations for `resolve<T>` and `check<T>`.
Definitions of the latter two are non-inline and emitted by YAML category into
`resolved_ir_<category>.gen.cpp`, which is compiled into the library. This
boundary keeps the small generic variant matcher as a template while preventing
every consumer translation unit from reparsing and instantiating large resolve
builders and checker visits/lambdas, with one public include entry point.

## Three descriptors

One YAML specification generates three static descriptors with distinct roles:

| Descriptor | Responsibility |
| --- | --- |
| Syntax descriptor | modifier spellings/presence, AST operand shapes, and layout slots |
| Resolved descriptor | resolved field kinds, modifier/operand bindings, structured type expressions, roles, and access |
| Checker descriptor | variant/layout PTX/SM/family availability and rule ID |

They must not duplicate each other: syntax descriptors do not store resolved C++
types, resolved descriptors do not recognize modifier spellings, and checker
descriptors do not redo resolve bindings.

## Checker contract

Each generated `checker::check<T>` wrapper uses common checking for:

- minimum PTX version, SM version, and target family for the variant, selected operand layout, and actual modifier value;
- layout-tag bounds;
- layout-tag/payload agreement;
- operand field identity, resolved shape, and immediate types from structured descriptors.

`rule_id` is reserved for typed instruction-specific rules. Register declaration
types, symbol visibility, address spaces, and cross-instruction constraints need
a complete symbol table and are outside the current common checker ABI.

## Extension rules

- A YAML semantic variant is defined by its modifier combination; never add a
  fake variant merely for C++ layout convenience.
- Every generated member represents a resolved PTX fact or its provenance; do
  not expose `direct` or `sub_struct` as backend layout switches.
- Add a Syntax AST shape and syntax descriptor before adding its resolved value.
- A new multi-layout instruction needs tests for normal resolution, an invalid
  layout tag, and a tag/payload mismatch.

Implementation entry points are `include/ptx_ir/resolved/ptx_resolved_ir.hpp`,
`include/ptx_ir/ptx_resolved_ir_checker.hpp`, and generated
`resolved_ir.gen.hpp`.
