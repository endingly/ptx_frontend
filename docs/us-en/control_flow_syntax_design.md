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

An indirect-call target-set operand must name a function-local `.callprototype`
or `.calltargets` declaration. Their labels, and `.branchtargets` labels, now
have stable function-scope symbols. Declaration semantics validates metadata
members and target-set signatures; generated instruction layout and normal
module metadata use now resolve through the call descriptor, while branch
integration remains C02.

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

Indirect forms use separate `IndirectCall` layouts rather than reusing either
the direct `Call` layouts or `Flat`: target plus metadata, target plus input
group plus metadata, and return group plus target plus input group plus
metadata. Both the register target and the final metadata operand resolve as
`ResolvedIndirectCallee`; layout slot shapes distinguish `CallTarget` from
`CallTargetSet`. These layouts require PTX 2.1 and SM 20. The public modifier
variant remains `call_direct` for compatibility.

For a direct named call in a module, resolution looks up the callee's canonical
prototype/definition signature. It compares return and input arity and order,
then reuses the call-argument compatibility contract for `.reg/.param` type
and vector shape, `.param .b8` array extent/alignment, and pointer
state-space/alignment. Each input literal is typed against its corresponding
formal and reports literal-kind or overflow errors at that literal. The check
belongs to module resolution, not the generated single-instruction checker.

`ResolvedIndirectCallee` now represents one indirect-call component: either a
non-predicate `.reg` target or a function-local metadata label. In a module,
the latter retains its `SymbolId` and whether it names `.callprototype` or
`.calltargets`; standalone resolution retains only its spelling. It carries no
signature, member list, or ABI fact. Generated `call` layouts and normal module
resolution use this value. It does not establish the indirect-call ABI; that
remains C01. The temporary C03 fallback stays only for malformed
metadata-bearing call syntax that matches no descriptor.

## Function-local `.callprototype` syntax

The parser now retains PTX 9.3 `.callprototype` declarations as dedicated
function-body CST/AST nodes rather than a label followed by an instruction.
All four signature forms are accepted: `_`, `_ (params)`, `(return) _`, and
`(return) _ (params)`. CST preserves the label, colon, sink, parameter-list
punctuation, `.noreturn`, `.abi_preserve N`, and `.abi_preserve_control N`.
AST retains their semantic spellings and source ranges. Declaration semantics
rejects `.noreturn` with return parameters and validates array formals; parsing
at module scope is rejected. Binding owns the local label; I06 can retain its
resolved identity, while instruction layout and ABI use remain later work.

## Function-local `.calltargets` syntax

PTX 9.3 `.calltargets` is likewise a dedicated function-body CST/AST node.
It retains its label, colon, directive, non-empty ordered function-name list,
commas, semicolon, and whole/member source ranges. The parser rejects an empty
list, a trailing comma, and use without a local label or at module scope.
Declaration semantics requires every member to be a prior device `.func`,
diagnoses duplicates using individual member ranges, and requires equal
canonical signatures.

## Function-local `.branchtargets` syntax

`.branchtargets` now has its own function-body CST/AST node. Its non-empty
ordered list preserves ordinary labels and compact entries such as `N<5>`;
the latter retain name, count, angle punctuation, and one entry range without
expanding into synthetic labels. Declaration semantics checks local-label
membership, compact overlap, and count validity without adding symbols. It
does not connect the declaration to an instruction: PTX 9.3 uses it with
`brx.idx`, and that integration remains out of scope.

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
