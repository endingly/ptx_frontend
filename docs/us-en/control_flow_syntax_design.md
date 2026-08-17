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
backend domain. Existing YAML opcodes still use `Flat` layouts; `call` and
`bra` have not been added to the generated database as incomplete flat
instructions.

The next phase needs a layout algorithm for call groups and variadic operands,
plus binding-aware Resolved IR for execution predicates, function/label
targets, and call parameters, before control-flow opcodes join unified dispatch
and checking.
