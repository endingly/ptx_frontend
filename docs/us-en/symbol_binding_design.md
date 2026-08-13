# Symbol Binding Design

## Position in the frontend

Symbol binding sits between Syntax AST and Resolved IR/checking:

```text
source -> CST -> Syntax AST -> symbol binding -> Resolved IR/checker
```

`include/ptx_ir/bind/ptx_symbol_table.hpp` exposes the public entry point:

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
- labels.

`SymbolId` and `ScopeId` are strong index types. A `Symbol` retains its name,
kind, declaration location, and the state space/type of a variable or
parameter. A function symbol points to its function scope through
`owned_scope`.

Lookup checks exact names first, parameterized names second, and then walks to
the parent scope. A function-local declaration therefore shadows a module
symbol.

## Parameterized variable names

PTX `name<count>` denotes `name0` through `name(count-1)`. The symbol table
stores the base and count instead of expanding them. Looking up `%r2` returns
the declaration's `SymbolId` plus `2` in
`SymbolLookup::parameterized_index`, avoiding potentially large symbol lists.

Parameterized names are valid in every state space, but cannot also declare an
array or initializer. The previous `.reg`-only restriction was removed, and
the public CST/AST field is now consistently named `parameterized_count`.

## Reference binding

The pass visits:

- instruction predicates and identifiers in every operand shape;
- constant expressions used as array dimensions;
- symbol expressions in scalar and recursive initializers.

Each `SymbolReference` retains its spelling, range, reference kind, and an
optional target. A parameterized target also retains its member index.
Unresolved references remain in the table with an empty target so a later pass
can diagnose them with opcode, special-register, and linkage context. This
pass deliberately does not classify every unknown `%` name as an undeclared
ordinary register.

`generic()` is an initializer operator rather than a symbol reference; its
argument is still bound normally. A mask operator has a literal callee, so only
its argument contributes references as well.

## Current diagnostics and boundary

The pass currently accumulates same-scope duplicate-symbol and invalid/zero
parameterized-count diagnostics. Remaining semantic work includes:

- linkage-compatible redeclarations and function prototype/definition merging;
- special-register and external-symbol classification;
- initializer type, array-shape, and element-count validation;
- carrying bound register/symbol `SymbolId` values into Resolved IR and the
  checker.
