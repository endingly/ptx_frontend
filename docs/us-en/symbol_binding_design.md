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
scope whose parent is the module scope. The initial pass collects:

- module and function variable declarations;
- function input and return parameters;
- function symbols;
- labels and function-local `.callprototype`, `.calltargets`, and
  `.branchtargets` declarations.

`SymbolId` and `ScopeId` are strong index types. A `Symbol` retains its name,
kind, declaration location, and the state space/type of a variable or
parameter; a function symbol also records its `.func`/`.entry` classification.
`SymbolLinkage` directly records `.extern`, `.visible`, or `.weak`.
A function symbol points to its function scope through `owned_scope`. When a
prototype and definition coexist, each item still has a distinct scope and
`owned_scope` prefers the definition.

Lookup checks exact names first, parameterized names second, and then walks to
the parent scope. A function-local declaration therefore shadows a module
symbol.

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
prototype/signature semantics remain I05 work. See
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
