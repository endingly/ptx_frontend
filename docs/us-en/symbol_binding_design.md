# Symbol Binding Design

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#symbol-binding-design). This page retains technical reference material and evidence.

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

[SDK support: Scopes and symbols](../sdk_support.md#symbol-binding-design--scopes-and-symbols).

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
`SymbolLinkage` directly records `.extern`, `.visible`, `.weak`, or `.common`.
`Common` remains distinct from `Weak` and `Visible`; it denotes an ordinary
module-scope global definition, with no cross-object selection in binding.
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

`SymbolTable::debugLocations()` borrows the table's owning, module-ordered
`DebugLocationDeclaration` records. Each retains its lexical `ScopeId`, base
`DebugLocationSourceTriple`, optional `DebugLocationInlineContext`, and physical
ranges. All leaves reuse owning `DebugFileSourceLeaf` strings; there are no AST
borrows or independently mutable decoded coordinates. Copying or assigning a
table copies these values; assignment, move and destruction invalidate borrowed
spans. The added table layout requires rebuilding binary consumers. Binding
collects events in source order across functions and depth-first nested blocks;
common module validation, rather than binding alone, enforces inline precedence.

## Parameterized variable names

[SDK support: Parameterized variable names](../sdk_support.md#symbol-binding-design--parameterized-variable-names).

PTX `name<count>` denotes `name0` through `name(count-1)`. The symbol table
stores the base and count instead of expanding them. Looking up `%r2` returns
the declaration's `SymbolId` plus `2` in
`SymbolLookup::parameterized_index`, avoiding potentially large symbol lists.
Member suffixes use canonical decimal spelling: `%r<3>` matches `%r0` through
`%r2`, but does not match `%r02`.

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
by canonical spelling decompositions. It identifies existing explicit names
and nonempty group first-member spellings relevant to the new base/count, then
retains the first stored overlapping identity for the diagnostic. Group bases
are excluded because they are not members; the existing name-set-overlap
predicate remains authoritative.
The indexes store no logical members: a declaration with a very large count
uses storage proportional to its spelling and the fixed 32-bit trie paths,
not to its count. Index keys and trie storage are owned by the table, so
`symbols` vector growth and supported table copies or moves cannot leave
borrowed name keys dangling.

The [scaling benchmark](../../submod/resolved_ir/benchmark/README.md) records
separate parser, binding, direct-lookup, and module-resolution measurements,
including compact-group controls and a generated PTX corpus input.

## Reference binding

[SDK support: Reference binding](../sdk_support.md#symbol-binding-design--reference-binding).

The pass visits:

- instruction predicates and identifiers in every operand shape;
- constant expressions used as array dimensions;
- symbol expressions in scalar and recursive initializers;
- call targets/returns/arguments/target sets and direct branch targets.

Each `SymbolReference` retains its spelling, range, reference kind, optional
target, and an explicit `ReferenceClassification`:

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
`.calltargets` symbol, or an actual storage variable consumed as a call-table
flist. Table eligibility remains module resolution's responsibility. Variable
declarator names and flist names participate in the existing lexical-order index;
owned variable occurrences retain their declarator ranges for AST-free joins.
The three metadata declaration kinds have stable
function-scope `SymbolId` values. Member validation, duplicate policy, and
prototype/signature semantics are checked by declaration semantics; binding
does not resolve metadata members or instruction use. See
`control_flow_syntax_design.md`.

## Current diagnostics and boundary

[SDK support: Current diagnostics and boundary](../sdk_support.md#symbol-binding-design--current-diagnostics-and-boundary).
