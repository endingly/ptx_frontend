# Control-flow Operand Syntax Design

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
- a `brx.idx` target list names a `.branchtargets` declaration in the current
  function scope.

An indirect-call target-set operand names a function-local `.callprototype`
or `.calltargets` declaration, or a supported lexically visible call-table array.
Their labels, and `.branchtargets` labels, now
have stable function-scope symbols. Declaration semantics validates metadata
members and target-set signatures; generated instruction layout and normal
module metadata use now resolve through their descriptors.

## Descriptor and Resolved IR boundary

`OperandSyntaxShape` now provides `Group`, `CallTarget`, `CallTargetSet`,
`BranchTarget`, and `BranchTargetSet`, with matching bits in the Python descriptor model and C++
backend domain. Because `bra` has one direct label target, it legitimately uses
the existing `Flat` layout and is now part of the YAML database, unified
dispatch, and checking. During module resolution, `ResolvedBranchTarget` stores
the current function label's stable `SymbolId`; standalone resolution retains
only the source spelling. `.uni` and the execution predicate are preserved as
a generated modifier field and an opcode-common field respectively.

`brx.idx{.uni} index, tlist` is a separate PTX 6.0 / SM 30 opcode. Its index
is a `.u32` register and its `tlist` resolves as `ResolvedBranchTargetSet`,
which retains the current-function `.branchtargets` `SymbolId`; standalone
resolution retains the spelling. It does not expand target entries or build a
control-flow graph. In a module, the `.branchtargets` declaration must occur
earlier in the same function's lexical traversal; its member labels may still
refer forward. Direct `bra` targets remain forward-referenceable. `bra` remains
direct-only.

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

`ResolvedIndirectCallee` represents one indirect-call component: a
non-predicate `.reg` target, a function-local metadata label, or a call-table
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

The existing indirect-call layouts also admit a final storage name, for example
`call %fp, (1+2), table;`. `ResolvedCallTableRef` is a distinct indirect-callee
branch retaining the actual variable `SymbolId`, written name and range; storage
is not relabeled as metadata. The module owns one `ResolvedCallTableContract`
per consumed table and reuses its common full function signature for the normal
return/input ABI and formal-driven literal/expression checks.

Supported tables are initialized one-dimensional scalar `.u32/.u64` arrays in
`.global/.const`, at module scope or a visible local scope. Inferred extent is
supported, and element width need not match function-pointer width. Explicit
entries must be complete unmasked zero-addend device-function relocations.
Order and duplicate targets survive; omitted tails are zero-fill, not callable
slots. At least one callable entry is required. Numeric/mixed, masked/addended,
nonfunction, vector, multidimensional, parameter and extern-only tables remain
unsupported as flist, without invalidating unrelated ordinary data.

The table declaration must precede the call. Function references retain the
existing exact-token prior-declaration rule, including aliases and compatible
prototype/definition identity. Target signatures must agree in all normalized
ABI fields and `.noreturn`. This proves neither loaded-pointer membership nor
mutable runtime contents. Stored ptxas 13.3.33 compile-only evidence accepts
partial/duplicate tables but crashes on mixed function/numeric call controls;
the frontend diagnoses that unsupported subset, with no assembler-parity or
GPU-execution claim.

## Return uniformity

`ret{.uni};` accepts unpredicated, `@p`, and `@!p` forms in both device
functions and entries, with the existing PTX 1.0 / all-architecture availability.
The canonical `ret_bare` variant and generated `RetBare` class remain stable.
Its `WithLocs<bool> uni` field defaults to `false` with no locations; a written
`.uni` sets it to `true` and retains the exact modifier range. The execution
predicate is retained independently, including its negation and bound register
identity, and both survive AST release for owned-module validation.
`.uni` records the programmer's guarantee of uniform return behavior. The
frontend does not prove dynamic divergence or convergence. Unknown or repeated
modifiers and unexpected operands remain invalid.

## Function-local `.callprototype` syntax

The parser now retains PTX 9.3 `.callprototype` declarations as dedicated
function-body CST/AST nodes rather than a label followed by an instruction.
All four signature forms are accepted: `_`, `_ (params)`, `(return) _`, and
`(return) _ (params)`. CST preserves the label, colon, sink, parameter-list
punctuation, `.noreturn`, `.abi_preserve N`, and `.abi_preserve_control N`.
AST retains their semantic spellings and source ranges. Declaration semantics
rejects `.noreturn` with return parameters and validates array formals; parsing
at module scope is rejected. Binding owns the local label. Current indirect-call
layouts consume that bound prototype identity and reuse the shared call ABI
contract; see [Resolved IR](resolved_ir_design.md) for entry points and limits.

Current owned and supplied prototype contracts independently enforce base
PTX 2.1 / SM 20, `.noreturn` PTX 6.4 / SM 30, and both ABI suffixes PTX 9.0 /
SM 80, including independently known partial context. Nested and unused prototypes
are included; ordinary instruction availability and runtime ABI behavior are not
expanded by these checks.

## Function-local `.calltargets` syntax

PTX 9.3 `.calltargets` is likewise a dedicated function-body CST/AST node.
It retains its label, colon, directive, non-empty ordered function-name list,
commas, semicolon, and whole/member source ranges. The parser rejects an empty
list, a trailing comma, and use without a local label or at module scope.
Declaration semantics requires every member to be a prior device `.func`,
diagnoses duplicates using individual member ranges, and requires equal
canonical signatures.

`.branchtargets` and `.calltargets` each require PTX 2.1 and SM 20, including
unused declarations in nested blocks. AST-free validation uses the function's
selected owned region; source-aware validation checks both current records and
supplied typed declarations with the supplied active context. Known version and
recognized profile minima are checked independently before full-context gates;
only unknown prerequisites defer under `AvailableContext`. Strict context and
unknown-target diagnostics, nonneutral payload identity, member binding,
signatures and declaration-order checks retain their existing contracts.

## Function-local `.branchtargets` syntax

`.branchtargets` now has its own function-body CST/AST node. Its non-empty
ordered list preserves ordinary labels and compact entries such as `N<5>`;
the latter retain name, count, angle punctuation, and one entry range without
expanding into synthetic labels. Declaration semantics checks local-label
membership and count validity without adding symbols. Repeated destinations,
including overlapping compact entries, retain their order and multiplicity;
each expanded occurrence denotes a separate index slot.
`brx.idx` consumes the declaration by stable local identity without expanding
its entries.

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
the same variable. Nonexecuting declarations, labels, metadata, and lexical
braces are transparent to instruction adjacency. Intervening executable
instructions break a block; binding and scope rules still require the same
visible parameter object/member. These local checks do not infer runtime path
initialization.
`call` itself may be predicated. `.uni` is retained as the programmer's
uniformity assertion, so the frontend does not attempt unprovable static
uniformity analysis or reject a predicated direct `call.uni`.

Register-state device and call-prototype formals have a separate default-ABI
minimum: when known PTX >=2.0 and the active recognized target is SM20+, the whole
scalar/vector value must be at least32bits. This measures explicit vector total
width, not each lane, and preserves source type/shape. It applies to unused input
and return formals too, with the supplied AST context for source-aware retargeting
and owned regions after AST death. Known legacy contexts disable the minimum;
missing context defers it under partial checking. Ordinary small/predicate registers
and compatible small actuals to `.param` formals remain legal. See
[parameter declarations](parameter_declarations.md) for the complete boundary.

A local `ResolvedCallPrototypeContract` describes a device-call interface, so its
owned `signature.is_entry` must remain false. AST-free module validation checks
this role even when the prototype is unused, including return-only interfaces
whose types and widths are otherwise valid. Module/function copies and instruction
clones preserve prototype identity. This metadata-integrity rule is independent
of formal ABI width, feature availability and supplied-source association; no
source spelling/history is reconstructed to validate the flag.

## Static recursion boundary

Known local call cycles require PTX 2.0 and SM 20. Acyclic device functions remain
legal on supported older contexts. Both module validators recompute a private
current typed may-call graph over canonical local device definitions, including
unused definitions. Actual direct calls create exact edges; only consumed,
validated finite `.calltargets` or call-table alternatives create candidate edges.
Unused metadata and implicit zero table tails create no edges. A cycle requiring
a finite alternative is statically possible recursion, not proof it executes.
Predicated calls remain may-call edges; no CFG, predicate feasibility, stack
execution or dataflow analysis is performed.

Each participating caller uses its selected region, or supplied active context
in the source-aware API. Known old PTX or recognized low SM rejects independently
of the other missing fact; unknown prerequisites alone may defer in
`AvailableContext`. Strict context and unknown-target handling remain unchanged.
Prototype-only/external callees and dynamic signature-only calls are open
boundaries. They gain no fabricated return edges and no blanket strict rejection;
success means modeled checks passed, not whole-program acyclicity across them.


## Historical entry body inputs

PTX 1.0–1.3 entry body `.param` declarations have the owning `LegacyEntryInput`
role in the sole `parameter_declarations` table, including scoped/group/array
identities. The function signature remains syntactic header ABI only, so an empty
header signature does not imply no host inputs. Lexical metadata order assigns
no launch addresses or packing. Resolution needs a version only for ambiguous
entry body roles; it reports `MissingValidationContext` without guessing from SM.
Typed modules retain their role when context is missing.

The optional `ResolvedSymbolRef::parameter_role` cache is derived from the actual
record while keeping structural `CallParameter`. Address/MOV/tensor projections
share input/return/call-slot directions. Both validators rejoin current records
and caches and enforce read-only entry-frame access, direct call-actual exclusion,
shape/scope and logical capacity before context skips. Legacy input LD is not
call-return staging; MOV preserves `.param`. Modern BodyLocal staging is unchanged.
The checked sum of per-member bytes times compact count has the known32764-byte
global ceiling; no double axes/vector multiplication or invented256 legacy cap.
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` remains qualified for exact historical
physical launch packing. See the [parameter contract](parameter_declarations.md)
for context windows, intrinsic guarantees and preserved limitations.
