# C++ Resolved IR Design

## Status and scope

This document describes the implemented Resolved PTX IR, not a future CFG,
SSA, or backend IR. The frontend's core flow is:

```text
PTX source -> Token stream -> Syntax AST -> symbol binding -> Resolved IR -> checker
```

Syntax AST preserves source spelling, modifier order, and `SourceRange`.
Resolved IR records the selected instruction variant, resolved operand values,
and diagnostic locations. Both are stable frontend boundaries. Lexical symbol
binding is connected to module resolution, execution predicates resolve to
declaration-aware values, and special registers, external symbols, and
genuinely undeclared references are distinct. Complete special-register
operands, address semantics, `call` groups, CFG/SSA, and target lowering remain
later work.

The generated public layer also provides an opcode-independent boundary:

```cpp
using ResolvedInstruction = std::variant<Add, Sub, Bar, Bra /* ... */>;

std::expected<ResolvedInstruction, ResolveDiagnostic>
resolveInstruction(const syntax_ast::AstInstruction& ast);

std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveModule(const syntax_ast::AstModule& ast);
```

`resolveInstruction` is generated from the instruction database and dispatches
to the existing `resolve<T>` specialization. This keeps opcode dispatch out of
callers while retaining the strongly typed per-opcode structures.
`resolveModule` first builds a `SymbolTable`, then constructs an explicit
`ResolveContext` for each function scope. The resulting `ResolvedModule` owns
that table, and each `ResolvedFunction` is identified by its function
`SymbolId`. Standalone `resolveInstruction` and `resolve<T>` remain
declaration-free for single-instruction tools. Directives, declarations, and
labels remain in the Syntax AST/symbol table instead of being copied into
Resolved IR as unresolved string fields.

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
Modifier primitives include `bool`, `ScalarType`, and `RoundingMode`; the latter
turns `.rn/.rz/.rm/.rp` into statically checkable values instead of runtime
strings. Operand primitives include `ResolvedRegisterRef`, `ResolvedImmediate`,
`ResolvedPredicate`, `ResolvedBranchTarget`, and `RegOrImm`. A `ResolvedImmediate` stores integer bits
and `ScalarType`, so the checker never has to reinterpret literal text.

`AstImmediateKind` retains the lexer's literal classification. Decimal and hex
integers, including their optional `U` suffix, are range-checked against the
target integer or bit type. Negative values are stored in `bits` as the
target-width two's-complement representation rather than being unconditionally
extended to 64 bits. Decimal floats currently convert to `F32` and `F64`, while
`0f<8 hex>` and `0d<16 hex>` are raw IEEE bit patterns for `F32` and `F64`
respectively. Other floating formats require explicit quantization rules and
must not silently take the integer path.

`ResolvedRegisterRef` owns the complete source spelling and its
`ResolvedRegisterClass`. During module resolution it also stores the
declaration `SymbolId`, optional parameterized-member index, and declared
`ScalarType`, giving named registers such as `%tmp` and `name<count>` members a
stable identity. A numbered-register index remains an optional convenience,
not an identity. The context-free standalone resolver preserves its previous
boundary: it accepts numbered registers and leaves symbol/type fields empty.
An instruction's optional execution predicate is stored as the opcode-level
common field `std::optional<WithLocs<ResolvedPredicate>>`. Module resolution
requires it to bind to a `.pred` register, while standalone resolution accepts
a numbered `%pN` guard. `ResolvedBranchTarget` follows the same two-boundary
rule: module resolution stores the current function label's `SymbolId`, while
standalone resolution retains the source spelling with no symbol identity.

## Opcode-generated structures

Every opcode generates one outer struct. `VariantType` and `std::variant`
represent the variant uniquely selected by the modifier combination:

```cpp
struct Add {
  enum class VariantType { IntegerNoSat, Sat, PackedOptionalSat };

  struct IntegerNoSat {
    ResolvedOperandLayoutTag operand_layout;
    WithLocs<ScalarType> type;
    WithLocs<ResolvedRegisterRef> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };
  using Variant = std::variant<IntegerNoSat /* ... */>;
  std::optional<WithLocs<ResolvedPredicate>> execution_predicate;
  Variant variant;
};
```

A fixed modifier is not mutable per-instance state. In the merged `Add::Sat`,
`.sat` is fixed while the type is an allowed value with its own availability,
so it generates:

```cpp
inline static constexpr bool saturate = true;
WithLocs<ScalarType> type;
```

This avoids re-inferring the fixed fact while retaining the selected type and
its source location.

One variant may have multiple named modifier slots of the same value kind. The
mixed-precision Add, for example, emits a static `result_type = F32` and a
dynamic `WithLocs<ScalarType> input_type`; its three operand type expressions
refer to `result_type`, `input_type`, and `result_type`. Slot IDs are local to a
variant, so `.f32` may bind `type` in standard Add and `result_type` in mixed
Add without becoming a global spelling-to-kind map.

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
operand slots. Syntax AST can now represent groups and call parameter lists,
but descriptors/resolution still need a new layout kind to consume them.
Variadics and call groups must not be disguised as `Flat`.

## Resolution protocol

`resolve<T>(const AstInstruction&)` and its `ResolveContext` overload share one
generated opcode-specific implementation. Shared logic performs these steps:

1. The common matcher diagnoses spellings unknown to the whole syntax
   descriptor, then binds spellings to unique active slots separately inside
   each candidate variant. Reusing one slot is a user diagnostic; one spelling
   owned by multiple active slots in a variant is a descriptor bug.
2. `selectVariant<T>` selects exactly one variant from those variant-local
   bindings. `absent`, `optional`, and `required/fixed` match by slot and
   allowed value, independent of source modifier order.
3. The selected variant chooses exactly one `OperandLayout` from AST shapes and
   arity.
4. `resolve_fields` resolves the common execution predicate and converts
   modifiers and operands through resolved bindings into location-carrying
   values. With a binding context, a guard must bind to a `.pred` register and
   ordinary registers must resolve to visible `.reg` declarations; both retain
   their `SymbolId` and declaration type, while a direct branch target must bind
   to a label in the current function.
5. The generated builder places fields in the selected struct or payload.

No matching variant/layout is a user diagnostic. Multiple matching layouts, or
a mismatch between descriptors and generated structures, is a generator bug and
uses `ResolveException`, distinct from `ResolveDiagnostic`.

`selectVariant<T>` remains a common template adapter in the handwritten public
ABI header, so every type satisfying the `PtxOperator` concept can use it
directly. It passes the descriptor to an out-of-line non-template matcher and
converts the selected variant name to the opcode's `VariantType`. One generated
`resolved_ir.gen.hpp` centralizes all opcode structs and
the explicit-specialization declarations for `resolve<T>` and `check<T>`.
Definitions of the latter two are non-inline and emitted by YAML category into
`resolved_ir_<category>.gen.cpp`, which is compiled into the library. This
boundary keeps only the small type adapter as a template while preventing every
consumer translation unit from reparsing the matcher or instantiating large
resolve builders and checker visits/lambdas, with one public include entry point.

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
- operand field identity, resolved shape, and immediate or bound-register
  declaration types from structured descriptors.

`rule_id` is reserved for typed instruction-specific rules. Register visibility
and `.reg` state space are checked during module resolution; address spaces and
cross-instruction constraints remain outside the current common checker ABI.

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
