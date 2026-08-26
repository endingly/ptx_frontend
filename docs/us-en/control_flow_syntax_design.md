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

Only direct named-function calls are resolved in this slice. A `.reg` target
or a `CallTargetSet` fourth operand is rejected clearly: indirect calls still
need the unmodeled `.calltargets`/`.callprototype` metadata.

## PTX 9.3 call parameter context

`ld` accepts `.param`, `.param::entry`, and `.param::func`; `st` accepts
`.param` and `.param::func`. `::entry` addresses only an entry's formal input.
`::func` addresses a device-function parameter or a function-local call
parameter. The unqualified spelling defaults to entry parameters in an entry,
to function parameters in a device function, and to function-local call
parameters in either context.

Only function-local `.param` variables are call staging. Their `st.param` input
stores must be unpredicated and form the contiguous block immediately before a
call that passes the same variable; their `ld.param` return loads must be
unpredicated and form the contiguous block immediately after a call returning
the same variable. Labels, declarations, and other instructions break a block.
`call` itself may be predicated. `.uni` is retained as the programmer's
uniformity assertion, so the frontend does not attempt unprovable static
uniformity analysis or reject a predicated direct `call.uni`.
