# Symbol Binding Design

## Position in the frontend

Symbol binding sits between Syntax AST and Resolved IR/checking:

```text
source -> CST -> Syntax AST -> symbol binding -> Resolved IR/checker
```

`submod/binding/include/ptx_symbol_table.hpp` exposes the public entry point:

```cpp
auto binding = binding::bindSymbols(module);
```

The result contains both a `SymbolTable` and accumulated `BindDiagnostic`
values. Both own the strings they need and do not depend on the Syntax AST
lifetime.

## Scopes and symbols

Each module has one root scope. Every `.entry`/`.func` item has a function
scope whose parent is the module scope, and every nested `AstBlock` has a
range-identified child block scope. The initial pass collects:

- module and function variable declarations;
- function input and return parameters;
- function symbols;
- labels and function-local `.callprototype`, `.calltargets`, and
  `.branchtargets` declarations into the owning function scope, even when
  written inside a nested block.

`SymbolId` and `ScopeId` are strong index types. A `Symbol` retains its name,
kind, declaration location, and the state space/type of a variable or
parameter; a function symbol also records its `.func`/`.entry` classification.
`SymbolLinkage` directly records `.extern`, `.visible`, or `.weak`.
A function symbol points to its function scope through `owned_scope`. When a
prototype and definition coexist, each item still has a distinct scope and
`owned_scope` prefers the definition.
Each function scope also owns its declaration's `SourceRange`.
`functionScope(range)` returns the unique matching occurrence (or no value for
a missing/ambiguous range), so clients need not align function and scope-vector
traversals. This occurrence identity must not be replaced by `owned_scope` when
processing a prototype's local parameters.

Lookup checks exact names first, parameterized names second, and then walks to
the parent scope. A block declaration can therefore shadow an outer or module
symbol, while sibling blocks remain invisible to each other. Labels and
control-flow metadata are deliberately function-local rather than block-local.

Debug identities use a separate module metadata namespace. `.file` indices are
normalized to `uint64_t` (decimal/octal/hex and an optional `u`/`U` suffix) and bind
as `DebugFile` symbols; repeated indices intentionally reuse the first
`SymbolId`. A `.debug_str` section binds its name and each raw `name:` payload
label as `DebugStringLabel` symbols. Ordinary `SymbolTable::lookup()` skips
both debug kinds, so a program declaration may use the same spelling. `.loc`
creates `DebugFile` references for its basic and `inlined_at` file fields, plus
a `DebugFunctionName` reference for `function_name`; the latter can resolve
only the `.debug_str` section itself or one of its labels. These metadata
identities diagnose unresolved references but never behave as PTX operands.

## Parameterized variable names

PTX `name<count>` denotes `name0` through `name(count-1)`. The symbol table
stores the base and count instead of expanding them. Looking up `%r2` returns
the declaration's `SymbolId` plus `2` in
`SymbolLookup::parameterized_index`, avoiding potentially large symbol lists.
Member suffixes use canonical decimal spelling: `%r<3>` matches `%r0` through
`%r2`, but does not match `%r02`.

Declaration collection compares the represented name sets. A parameterized
declaration that overlaps an explicit name, or another parameterized
declaration with a different base, produces a same-scope duplicate diagnostic
with the previous range. The base itself is not a generated member, so
`name<2>` and an explicit `name` remain distinct symbols.

Parameterized names are valid in every state space, but cannot also declare an
array or initializer. The previous `.reg`-only restriction was removed, and
the public CST/AST field is now consistently named `parameterized_count`.

## Lookup indexes and compact groups

The table retains its owning `symbols` vector as the source of stable
`SymbolId` values. It additionally keeps private, scope-aligned indexes with
their own string keys: ordinary exact names, parameterized exact bases, and a
prefix trie for parameterized bases. Lookup therefore checks the current
scope's ordinary exact name, then only parameterized bases that can prefix the
queried spelling, before moving to the parent scope. This preserves the
current-scope ordinary, current-scope parameterized, then parent precedence.
Debug metadata is deliberately excluded from these lexical indexes.
When a scope has just one parameterized group, lookup checks that group's
member directly after the ordinary-name probe, avoiding prefix-trie overhead
without changing bounds, canonical suffix handling, or parent fallback.

`exactDeclaration(scope, spelling, parameterized)` exposes the corresponding
same-scope exact index for consumers that associate an AST declarator with its
bound identity. It neither walks parents nor treats a generated compact member
as a declaration. Metadata remains excluded, and a legal redeclaration retains
the first stable `SymbolId`. `initializerReference(range)` similarly indexes
only initializer references by their exact source range, retaining the first
record even when it is unresolved; declaration semantics and storage lowering
therefore do not rescan all instruction and initializer references per symbol.

Parameterized overlap checking uses a sparse 32-bit member-range trie keyed
by canonical spelling decompositions. It identifies existing explicit names,
existing group bases, and group first-member spellings relevant to the new
base/count, then retains the first stored overlapping identity for the
diagnostic. The existing name-set-overlap predicate remains authoritative.
The indexes store no logical members: a declaration with a very large count
uses storage proportional to its spelling and the fixed 32-bit trie paths,
not to its count. Index keys and trie storage are owned by the table, so
`symbols` vector growth and supported table copies or moves cannot leave
borrowed name keys dangling.

The [scaling benchmark](../../submod/resolved_ir/benchmark/README.md) records
separate parser, binding, direct-lookup, and module-resolution measurements,
including compact-group controls and a generated PTX corpus input.

## Reference binding

The pass visits:

- instruction predicates and identifiers in every operand shape;
- constant expressions used as array dimensions;
- symbol expressions in scalar and recursive initializers;
- call targets/returns/arguments/target sets and direct branch targets.

Each `SymbolReference` retains its spelling, range, reference kind, optional
target, and an explicit `ReferenceClassification`:

- `DeclaredSymbol` for an ordinary declaration in this module;
- `ExternalSymbol` for a reference bound to an explicit `.extern` declaration;
- `SpecialRegister` for a predefined PTX special register that needs no user
  declaration;
- `Unresolved` when none of the above matches.

A parameterized declaration target also retains its member index. Special
registers are recognized through the independent `special_registers` semantic
registry. It also records current element types, vector widths, and minimum
PTX/SM targets, making it the single source of truth for name classification
and Resolved IR checks. Bounded families
such as `%envreg<32>`, `%pm<8>`, `%pm0_64..%pm7_64`, and
`%reserved_smem_offset_<2>` are range-checked rather than approximated by a
generic `%` prefix. Vector members such as `%tid.x` bind their `%tid` AST base.
`WARP_SZ` is already an immediate kind in the lexer and does not enter the
symbol-reference path.

`.extern` says that a declaration is defined in another module; it does not
make undeclared names legal. An external reference therefore has a normal
`SymbolId` target plus explicit external classification and linkage.

`generic()` is an initializer operator rather than a symbol reference; its
argument is still bound normally. A mask operator has a literal callee, so only
its argument contributes references as well.

Dedicated call/branch AST nodes produce distinct reference kinds. Binding now
checks that a callee is a function or `.reg` function pointer, call parameters
belong to `.reg`/`.param`, a direct branch target is a label in the current
function, and an indirect target-set operand is a `.callprototype` or
`.calltargets` symbol. The three metadata declaration kinds have stable
function-scope `SymbolId` values. Member validation, duplicate policy, and
prototype/signature semantics are checked by declaration semantics; binding
does not resolve metadata members or instruction use. See
`control_flow_syntax_design.md`.

## Current diagnostics and boundary

The pass now accumulates diagnostics for same-scope duplicates, parameterized
name-set overlaps, invalid or zero parameterized counts, conflicting linkage
qualifiers, and genuinely unresolved references. Same-name module declarations first share a
stable `SymbolId`; the declaration semantic pass then classifies them as a
legal redeclaration, signature conflict, or multiple definition. Module
resolution preserves the distinction between special and unresolved names.
The unified scalar `mov` source resolves the former to a
`ResolvedSpecialRegisterRef` carrying stable identity and component. The
semantic registry provides the current declared type and intrinsic availability
for that identity, while generated checker descriptors provide
instruction-specific historical read compatibility. Opcodes without a declared
special-register shape still report an unsupported operand rather than an
undeclared name. See `declaration_semantics_design.md` for the following pass.

32/64-bit integer/bit-size `mov d, symbol[+offset]` and `ld.u32 d, [address]`
now consume binding
identity as well. A direct symbol in the former produces a `ResolvedSymbolRef`;
its offset form embeds that representation as a `ResolvedAddress` base. Symbol
address bases in the latter use the same representation. Module resolution
retains a stable `SymbolId`, parameterized member, declaration kind, declared
state space, and effective address state space. Direct parameter memory
addresses and kernel formal-parameter `mov` addresses remain in `.param`;
device-function formal-parameter `mov` addresses are in `.local`, and the
checker applies a PTX 2.0 / SM 20 baseline to all such addresses and raises
the PTX minimum to 6.0 for a return-parameter address.
Function-local `.param` call-argument variables are bound separately from
formal parameters; direct `ld.param`/`st.param` addresses are valid, while
`mov` remains non-addressable. Standalone resolution keeps spelling only. A bare function name binds to a
`ResolvedFunctionRef` with the same stable `SymbolId` and its `.func`/`.entry`
classification. The checker requires PTX 3.1 / SM 35 for a kernel-function
address; a device-function address uses the base PTX 1.0 availability of
`mov`. Remaining work includes:

- state-space compatibility and the remaining special-register/type forms.
