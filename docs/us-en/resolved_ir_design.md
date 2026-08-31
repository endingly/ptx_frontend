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
genuinely undeclared references are distinct. The 16/32/64-bit scalar `mov`
type families accept register, immediate, and special-register sources; the
32/64-bit forms also accept data-symbol, `symbol+offset`, function-address, and
legal formal-parameter-address sources. Bit-size forms also support two/four-
element vector pack/unpack; `.b128` is vector-only. `mov.pred` reuses the declaration-aware
`ResolvedPredicate` representation. Generic and basic explicit-space scalar
plus braced-vector `ld`/`st` exercise the dereferenced-address path
for 14 bit-size, integer, and floating-point types from 8 through 64 bits.
Legacy memory-vector payloads are capped at 128 bits: `.v2` accepts modeled
types through 64 bits and `.v4` through 32 bits. PTX 8.8/SM 100 additionally
accepts exactly 256-bit `.v8` × 32-bit and `.v4` × 64-bit forms. Static natural
alignment checks bound data symbols with constant byte offsets and absolute
immediate addresses; register and standalone unresolved addresses stay unknown.
Other source forms, remaining qualifier extensions, CFG/SSA, and target
lowering remain later work. `ResolvedIndirectCallee` now provides descriptor-
independent identity for a non-predicate `.reg` indirect target or a bound
function-local `.callprototype`/`.calltargets` label; it intentionally omits
metadata payload and ABI. Generated `Call::Direct` now has three additional
`IndirectCall` layouts (target/metadata, target/input/metadata, and
return/target/input/metadata), each available from PTX 2.1 / SM 20; normal
module indirect calls preserve the bound target and metadata identities, then
reuse the direct-call ABI contract through metadata-indexed canonical
signatures. ABI comparison does not create a second indirect-call model.

The generated public layer also provides an opcode-independent boundary:

```cpp
using ResolvedInstruction =
    std::variant<Add, Sub, Bar, Bra, Call, Mov, Ld /* ... */>;

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
Resolved IR as unresolved string fields. Bound `.file` and `.debug_str`
identities validate `.loc` metadata there, but `.loc`, `.section`, and
`.pragma` do not add Resolved IR nodes or instruction attachment.

Module resolution additionally performs direct and metadata-backed indirect
call ABI and call-context work
that cannot live in the generated single-instruction checker: it obtains the
canonical prototype/definition signature, checks return/input actuals and
formal-typed literals, and enforces function-local `.param` qualification,
predication, and staging adjacency. The generated checker remains responsible
for one resolved instruction and target-aware descriptor rules.

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
`ResolvedPredicate`, `ResolvedBranchTarget`, `ResolvedSpecialRegisterRef`,
`ResolvedFunctionRef`, `ResolvedSymbolRef`, `ResolvedAddress`, `ResolvedMovSource`, and `RegOrImm`. A `ResolvedImmediate` stores integer bits
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

`ResolvedSpecialRegisterRef` retains the exact spelling, a stable
`SpecialRegisterId`, and an optional vector component. It does not store an
effective type that depends on an instruction or target. The independent
special-register semantic registry is the single source of truth for names,
stable identities, current declared element types, vector widths, and intrinsic
minimum PTX/SM targets; binding only reuses it for classification. A scalar
operand accepts a scalar special register or a component such as `%tid.x`, but
not an unselected vector base.

Read forms widened by the ISA are instruction semantics, not properties of the
register itself. The `mov` variant declares the special-register identity,
instruction width, effective type, and minimum PTX/SM in YAML
`operand_type_compatibilities`, which is generated into the checker descriptor.
The checker selects effective metadata only for that check and never mutates
Resolved IR. Current rules allow 16-bit `%tid`/`%ntid`/`%ctaid`/`%nctaid`
component reads from PTX 1.0 and 16/32-bit `%gridid` reads from PTX 1.0/1.3;
other uses retain the registry's current declared type and intrinsic
availability.

A single scalar variant carries a dynamic type modifier covering
`.b16/.u16/.s16`, `.b32/.u32/.s32/.f32`, and `.b64/.u64/.s64/.f64`. The checker applies PTX
fundamental-type compatibility: a same-width bit type agrees with any
fundamental type, signed and unsigned integers agree, and integer/float mixes
remain invalid. The `.f64` value additionally carries its SM 13 requirement.

`mov.pred` has a separate variant because both fields are
`ResolvedPredicate`, structurally unlike the classified scalar source. Module
resolution requires unnegated `.pred` registers for source and destination and
retains stable `SymbolId` values; standalone resolution continues to accept
numbered predicate registers without declaration context.

Scalar and vector `mov` share one dynamic type-modifier variant because their
`.b16/.b32/.b64` modifier forms are identical. Three operand layouts represent
scalar, pack, and unpack forms without duplicate variants. `ResolvedRegisterVector`
stores two or four optional `ResolvedRegisterRef` elements; an empty element is
the destination-only `_` sink. Resolution and checking require a bit-size
instruction type, equal total vector/instruction widths, no source sink, at
least one real destination register, and no sub-byte element. `.b128` is
accepted only by pack/unpack layouts and carries PTX 8.3 / SM 70 modifier-value
availability.

`ResolvedFunctionRef` retains source spelling, a stable function `SymbolId`,
and the `.func`/`.entry` classification. A device-function address uses the
base PTX 1.0 availability of `mov`; a kernel-function address carries the PTX
3.1 / SM 35 requirement for target checking. Only a bare function name is
accepted; an offset form continues through data-symbol address resolution and
is rejected.

`ResolvedSymbolRef` retains source spelling. Module resolution also records the
declaration `SymbolId`, parameterized member, declaration kind, declared state
space, effective address state space, and representable declaration scalar
type. The two state spaces are identical for ordinary data variables. Direct
parameter memory addresses and kernel formal parameters used by `mov` retain
a `.param` address, while taking a device-function formal parameter address
with `mov` materializes it on the stack and produces a `.local` address. A
device-function formal-parameter `mov` address carries a PTX 2.0 / SM 20
baseline; a return-parameter address raises the PTX minimum to 6.0. A
function-local `.param` call-argument variable is a distinct bound symbol:
direct `ld.param`/`st.param` addresses retain `.param`, satisfy either parameter
direction, and require PTX 2.0 / SM 20; it remains non-addressable through `mov`.
Standalone resolution cannot
perform lexical binding, so it leaves identity and state-space fields empty as
it does for branch targets. `ResolvedMovSource` classifies registers,
immediates, special registers, data symbols, and address expressions after
binding, avoiding identifier-shape ambiguity during variant/layout selection.
Standalone resolution cannot tell whether an unbound name denotes data or a
function, so it remains a `ResolvedSymbolRef` with no identity.

A `ResolvedAddress` base is a variant of `ResolvedRegisterRef`,
`ResolvedImmediate`, and `ResolvedSymbolRef`. Its optional offset retains the
add/subtract operator and a parsed signed 64-bit value.
A 32/64-bit integer or bit-size `mov d, symbol+offset` uses an unbracketed
address value restricted to an addressable data-symbol or formal-parameter
base. Scalar and braced-vector `ld`/`st` require bracketed dereference and
cover register, immediate, and bound-symbol bases. Each opcode uses
`GenericScalar`, `ExplicitScalar`, `GenericVector`, and `ExplicitVector`
variants. Their runtime type field accepts `.b8/.b16/.b32/.b64`,
`.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`, and `.f32/.f64`; `.b128` is not a
memory type in the current model. Vector variants add a required runtime
`.v2/.v4/.v8` field, and the register-vector operand descriptor links its expected
element count to that field rather than duplicating variants per arity. Memory
vectors use element type policy: each register element is checked against the
instruction type, with `EqualOrWider` register width accepted. Legacy payloads
are capped at 128 bits; the generated cross constraint adds only exact 256-bit
`.v8` × 32-bit and `.v4` × 64-bit forms at PTX 8.8/SM 100, global when known,
with partial sinks. The default register-width policy is `SameWidth`, preserving
`mov/add/sub`, immediate, and special-register behavior. Only `ld` destination
and `st` source register descriptors select `EqualOrWider`, so the declared
register may be at least as wide as the instruction type. After that size
check, either side being a bit type is compatible, signed/unsigned fundamental
integers are mutually compatible, floats require the exact type/size, and
integer/float combinations remain incompatible. This covers wider load
destinations and store sources through 64-bit declared registers, including
store truncation. A wider actual `.b128` register is deliberately rejected
until declaration-type target availability is represented and checked; exact
`.b128` compatibility remains unchanged for its existing `mov` vector
consumers.

Explicit loads accept `.const/.global/.local/.param/.shared`, while stores
accept `.global/.local/.param/.shared`. `WithLocs` retains both runtime
state-space/type modifiers and their source ranges. Explicit `.f64` adds SM 13
through modifier-value availability; generic `.f64` needs no separate SM rule
because the generic variant already requires SM 20. Generated operand views
translate a bound symbol's effective address space, rather than its declaration
spelling, and the checker reports `AddressStateSpaceMismatch` when it differs
from the runtime field. Generic operand descriptors carry a static bound-space
allowlist with per-entry availability: loads accept known
`.const/.global/.local/.shared` addresses, with `.const` requiring PTX 3.1,
while stores accept `.global/.local/.shared` and reject known `.const` or
`.param` addresses. Explicit `ld.const` itself remains in the PTX 1.0 basic
explicit baseline.

Legacy cache operators are modeled on the same scalar/vector `ld/st` variants
rather than by duplicating variants per cache spelling. Loads accept
runtime `.ca/.cg/.cs/.lu/.cv`, stores accept runtime `.wb/.cg/.cs/.wt`, and
every explicit cache spelling carries PTX 2.0 / SM 20 modifier-value
availability. When source omits cache, Resolved IR stores
`CacheOperator::Unspecified` with empty `locs`. That sentinel is intentional
provenance metadata, not a claim that PTX lacks an effective hardware default:
the ISA still makes omitted `ld` behave as `.ca` and omitted `st` behave as
`.wb`, while the IR preserves `Unspecified` so omitted source does not masquerade
as an explicit modifier and does not trigger cache-value availability checks.

Memory consistency is a generated cross-modifier descriptor, not a Cartesian
set of `ld/st` variants. `MemoryConsistency::Omitted` (empty `locs`) remains
distinct from explicit `.weak`; `.volatile`, `.relaxed`, `.acquire`, and
`.release` retain their modifier locations. The checker requires scope only
for relaxed/acquire/release, rejects cache with volatile/ordered/mmio forms,
uses known address-space identity without guessing unknown generic addresses,
and applies the global/shared, `volatile.local` PTX 9.1, and scalar
`.mmio.relaxed.sys` rules. A generated `memory_vector` cross constraint detects
arity > 4, payload > 128, or sinks; it requires a 256-bit payload, global space
when known, and PTX 8.8/SM 100. Partial sinks are valid only for those modern
load/store vectors; all-sink and legacy sinks remain invalid. Static natural
alignment covers scalar, legacy `.v2/.v4`, and modern 256-bit total access sizes
when the address is known.

`ResolvedAddress` separately records the enclosing function kind. Generated
address views derive an optional parameter direction only from a bound
`InputParameter`, `ReturnParameter`, or a function-local call argument; they do
not infer it from spelling.
For explicit `.param`, the generated operand constraint requires input
parameters for `ld` and return parameters for `st`. A known wrong direction
reports `ParameterDirectionMismatch` without stacking target diagnostics. A
device-function `ld.param`, and every `st.param`, apply the YAML-provided PTX
2.0 / SM 20 function-availability constraint. A kernel input `ld.param`
retains the explicit-form baseline, while an address with unknown identity is
not assigned a direction. Its known device-function provenance still triggers
the load constraint; an unknown standalone load context does not. This
contextual rule does not change `ResolvedSymbolRef::address_availability`,
which continues to describe address-value semantics such as `mov`.

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

`Flat` handles comma-separated positional slots. `Call` is the only other
implemented layout algorithm: it recognizes the three direct-call group
arrangements and resolves the input group as one variadic field. It is a fixed
algorithm, not a general repeat DSL; variadics and call groups still must not
be disguised as `Flat`.

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
| Resolved descriptor | resolved field kinds, modifier/operand bindings, structured type/state-space expressions, static state-space allowlists with per-entry availability, roles, and access |
| Checker descriptor | variant/layout/value availability, operand type compatibility, and rule ID |

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
- special-register intrinsic metadata and contextual type/availability selected
  by the current instruction width; this creates only a temporary checking view
  and does not change Resolved IR.
- static generic state-space allowlists and explicit modifier-derived
  constraints against known effective bound-symbol address spaces, including
  per-entry target availability; unknown register/immediate/standalone bases
  are not inferred.
- explicit `.param` input/return direction and function-context availability
  from the generated operand constraint; direction mismatches take precedence
  over that contextual availability.

`rule_id` is reserved for typed instruction-specific rules. Register visibility
and `.reg` state space are checked during module resolution; the common checker
handles generated address-space constraints, while cross-instruction
constraints remain outside its ABI.

## Extension rules

- A YAML semantic variant is defined by its modifier combination; never add a
  fake variant merely for C++ layout convenience.
- Every generated member represents a resolved PTX fact or its provenance; do
  not expose `direct` or `sub_struct` as backend layout switches.
- Add a Syntax AST shape and syntax descriptor before adding its resolved value.
- A new multi-layout instruction needs tests for normal resolution, an invalid
  layout tag, and a tag/payload mismatch.

Implementation entry points are `submod/resolved_ir/include/ptx_resolved_ir.hpp`,
`submod/resolved_ir/include/ptx_resolved_ir_checker.hpp`, and generated
`resolved_ir.gen.hpp`.

Direct/indirect-call ABI plus function-local call-argument `.param` memory, qualified
`::entry`/`::func` forms, and call adjacency/predication constraints are covered
by module resolution. Scalar `.b128` and
declaration-type availability for wider `.b128` registers remain outside this
slice. Legacy scalar/vector `ld`/`st` cache operators, PTX 8.8 modern memory
vectors, static memory-address alignment, and memory-consistency qualifiers are
covered here.
