# Control-flow Operand Syntax Design

## Why this is not a flat operand list

The operands of `bra` and `call` are not an ordinary comma-separated flat
list. In the current PTX ISA, direct `bra` has one label target. A `call` is
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

CST retains parentheses, commas within groups, and commas between operands.
Syntax AST drops punctuation but retains group roles and source ranges. A
return group contains exactly one identifier. An input group may be empty and
currently contains identifiers or immediates.

## Binding

Dedicated nodes map to distinct `ReferenceKind` values for call targets,
return parameters, arguments, target sets, and branch targets. Binding checks
the symbol kinds it can already determine:

- a direct callee is a function, while an indirect callee may be a `.reg`
  function pointer;
- call return/input identifiers name `.reg` or `.param` variables or formal
  parameters;
- a direct branch target names a label in the current function scope.

Complete target-set/prototype validation depends on
`.calltargets`/`.callprototype` directives entering AST and the symbol table.
For now the reference is retained, and a missing declaration produces a
specific unresolved target-set diagnostic.

## Descriptor and Resolved IR boundary

`OperandSyntaxShape` now provides `Group`, `CallTarget`, `CallTargetSet`, and
`BranchTarget`, with matching bits in the Python descriptor model and C++
backend domain. Because `bra` has one direct label target, it legitimately uses
the existing `Flat` layout and is now part of the YAML database, unified
dispatch, and checking. During module resolution, `ResolvedBranchTarget` stores
the current function label's stable `SymbolId`; standalone resolution retains
only the source spelling. `.uni` and the execution predicate are preserved as
a generated modifier field and an opcode-common field respectively.

`call` still cannot be disguised as `Flat`. It needs a layout algorithm for
groups and variadic operands, plus binding-aware resolved values for function
targets, call parameters, and target-set/prototype symbols, before it can join
unified dispatch and checking.
