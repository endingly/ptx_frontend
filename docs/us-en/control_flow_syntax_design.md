# Control-flow Operand Syntax Design

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#control-flow-syntax-design). This page retains technical reference material and evidence.

## Why this is not a flat operand list

The operands of `bra`, `brx.idx`, and `call` are not an ordinary comma-separated flat
list. In the current PTX ISA, direct `bra` has one label target, `brx.idx` has
a register index plus target-list declaration, and a `call` is
formed from an optional return-parameter group, a callee, an optional input
parameter group, and an optional target-set or prototype symbol for an
indirect call.

The frontend therefore no longer disguises parenthesized call parameters as a
vector pack or leaves a branch label as an untyped identifier. CST and Syntax
AST expose dedicated nodes:

- `CstCallParameterList` / `AstCallParameterList`, with `Return` and `Input`
  roles;
- `CstCallTarget` / `AstCallTarget`;
- `CstCallTargetSet` / `AstCallTargetSet`;
- `CstBranchTarget` / `AstBranchTarget`.
- `CstBranchTargetSet` / `AstBranchTargetSet`.

CST retains parentheses, commas within groups, and commas between operands.
Syntax AST drops punctuation but retains group roles and source ranges. A
return group contains exactly one identifier or explicit ordinary register member.
An input group may be empty and contains identifiers, selected members or existing
numeric operands.

## Binding

Dedicated nodes map to distinct `ReferenceKind` values for call targets,
return parameters, arguments, target sets, and branch targets. Binding checks
the symbol kinds it can already determine:

- a direct callee is a function, while an indirect callee may be a `.reg`
  function pointer;
- call return/input identifiers name `.reg` or `.param` variables or formal
  parameters;
- a direct branch target names a label in the current function scope.
- a `brx.idx` target list names a `.branchtargets` declaration in the current
  function scope.

An indirect-call target-set operand names a function-local `.callprototype`
or `.calltargets` declaration, or a supported lexically visible call-table array.
Their labels, and `.branchtargets` labels, now
have stable function-scope symbols. Declaration semantics validates metadata
members and target-set signatures; generated instruction layout and normal
module metadata use now resolve through their descriptors.

## Descriptor and Resolved IR boundary

[SDK support: Descriptor and Resolved IR boundary](../sdk_support.md#control-flow-syntax-design--descriptor-and-resolved-ir-boundary).

`OperandSyntaxShape` now provides `Group`, `CallTarget`, `CallTargetSet`,
`BranchTarget`, and `BranchTargetSet`, with matching bits in the Python descriptor model and C++
backend domain. Because `bra` has one direct label target, it legitimately uses
the existing `Flat` layout and is now part of the YAML database, unified
dispatch, and checking. During module resolution, `ResolvedBranchTarget` stores
the current function label's stable `SymbolId`; standalone resolution retains
only the source spelling. `.uni` and the execution predicate are preserved as
a generated modifier field and an opcode-common field respectively.

`call` now uses the non-`Flat` `Call` layout algorithm. One generated direct
variant has exactly three fixed payload layouts: target only, target plus the
variadic input group, and return group plus target plus input group. The group
role is checked during layout selection, so a return group cannot match the
input position. `ResolvedFunctionRef` preserves a bound direct target,
`ResolvedCallParameterRef` preserves each `.reg`/`.param` identity, type, and
state space, and `ResolvedCallArguments` owns per-element ranges. Literals are
retained untyped by standalone instruction resolution, which deliberately has
no callee declaration context.

For a direct named call in a module, resolution looks up the callee's canonical
prototype/definition signature. It compares return and input arity and order,
then reuses the call-argument compatibility contract for `.reg/.param` type
and vector shape, `.param .b8` array extent/alignment, and pointer
state-space/alignment. Each input literal is typed against its corresponding
formal and reports literal-kind or overflow errors at that literal. The check
belongs to module resolution, not the generated single-instruction checker.

`ResolvedIndirectCallee` represents one indirect-call component: a
scalar General integer/bit `.reg` target, a function-local metadata label, or a call-table
reference described below. In a module, a metadata label retains its `SymbolId`
and whether it names `.callprototype` or
`.calltargets`; standalone resolution retains only its spelling. It carries no
signature or member list. Module resolution indexes each function-local
metadata `SymbolId` to the canonical signature: `.callprototype` converts its
own return/input contract (including `.noreturn`), while `.calltargets` reuses
the first member signature already validated by declaration semantics. Direct
and indirect calls then share the same arity, literal typing, and
argument-compatibility check; the latter reports the metadata label. The
generic descriptor layout diagnostic handles malformed metadata-bearing call
syntax that matches no descriptor.

## Call-table flist arrays

[SDK support: Call-table flist arrays](../sdk_support.md#control-flow-syntax-design--call-table-flist-arrays).

## Return uniformity

[SDK support: Return uniformity](../sdk_support.md#control-flow-syntax-design--return-uniformity).

## Function-local `.callprototype` syntax

[SDK support: Function-local `.callprototype` syntax](../sdk_support.md#control-flow-syntax-design--function-local-callprototype-syntax).

## Function-local `.calltargets` syntax

[SDK support: Function-local `.calltargets` syntax](../sdk_support.md#control-flow-syntax-design--function-local-calltargets-syntax).

## Function-local `.branchtargets` syntax

[SDK support: Function-local `.branchtargets` syntax](../sdk_support.md#control-flow-syntax-design--function-local-branchtargets-syntax).

## PTX 9.3 call parameter context

[SDK support: PTX 9.3 call parameter context](../sdk_support.md#control-flow-syntax-design--ptx-93-call-parameter-context).

## Static recursion boundary

[SDK support: Static recursion boundary](../sdk_support.md#control-flow-syntax-design--static-recursion-boundary).

## Historical entry body inputs

[SDK support: Historical entry body inputs](../sdk_support.md#control-flow-syntax-design--historical-entry-body-inputs).

### Indirect function-address carriers

[SDK support: Indirect function-address carriers](../sdk_support.md#control-flow-syntax-design--indirect-function-address-carriers).

### Selected scalar call operands

[SDK support: Selected scalar call operands](../sdk_support.md#control-flow-syntax-design--selected-scalar-call-operands).
