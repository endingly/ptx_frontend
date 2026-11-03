# CST and Syntax AST design

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#syntax-ast-design). This page retains technical reference material and evidence.

## Handwritten parser implementation

[SDK support: Handwritten parser implementation](../sdk_support.md#syntax-ast-design--handwritten-parser-implementation).

`PtxCstParser` remains the single owner of lexer lookahead and the token buffer.
Its member implementations are divided by grammar responsibility: token/root
handling, constant expressions and initializers, operands and control-flow source
forms, declarations and parameters, directives/functions/modules, and recovery.
`CstFile` operations are separate from parsing. Leaf parsers return typed
`std::expected` results; drivers select a form before parsing and append recovery
nodes once. Optional forms that are absent consume no tokens.

## Frontend layers

The frontend now separates concrete source representation from the syntax
model consumed by resolution:

```text
source -> lexer token buffer -> CST -> Syntax AST -> symbol binding -> Resolved IR
```

- The CST owns source fidelity: tokens, punctuation, delimiters, comments,
  whitespace, original spellings, and token ranges.
- Syntax AST owns normalized grammar shapes needed by instruction matching and
  resolution.
- Symbol binding builds module/function scopes and associates identifier
  references with declarations.
- Resolved IR owns selected variants, typed modifiers and operands, semantic
  values, and target checking metadata.

## Instruction constant expressions

[SDK support: Instruction constant expressions](../sdk_support.md#syntax-ast-design--instruction-constant-expressions).

## Named array-address syntax

[SDK support: Named array-address syntax](../sdk_support.md#syntax-ast-design--named-array-address-syntax).

## Named memory-vector syntax

[SDK support: Named memory-vector syntax](../sdk_support.md#syntax-ast-design--named-memory-vector-syntax).

## CST ownership and representation

[SDK support: CST ownership and representation](../sdk_support.md#syntax-ast-design--cst-ownership-and-representation).

Public CST headers live under `submod/cst/include`. A `syntax_cst::CstFile`
owns the complete `PtxToken` buffer. Its `CstRoot` distinguishes a standalone
instruction fragment from a `CstModule`; nodes refer to the file buffer with
`TokenId`, and composite nodes also store half-open `CstTokenRange` values.

The tree retains comma, semicolon, bracket, brace, sign, predicate, and vector
selector tokens explicitly. Each `PtxToken` retains its leading trivia, and the
EOF token retains final trivia. `CstFile::sourceText()` is the token-buffer
round-trip serializer: for an unmodified CST it reproduces parsed input
byte-for-byte. It emits the token buffer rather than CST nodes, so recovery
markers do not add source text and node mutation is not pretty printing;
internal EOF-sentinel multiplicity is not a public contract.

```cpp
PtxCstParser parser(source);
auto cst = parser.parseInstruction();
if (cst)
  assert(cst->sourceText() == source);
```

Public parser and lowering roots return `ResultWithDiagnostics<T, D>`: an
optional value plus an ordered `DiagnosticCollection<D>`. This lets module
recovery return a CST with diagnostics without another API change. Module
recovery may return both a value and diagnostics; standalone instruction
fragments remain fail-fast with no value on error.

## CST to Syntax AST lowering

`lowerSyntaxInstruction()` and `lowerSyntaxModule()` are the explicit
CST-to-AST boundaries:

```cpp
auto ast = lowerSyntaxInstruction(cst);
auto module = lowerSyntaxModule(module_cst);
```

The resulting AST does not refer to CST token IDs and remains valid after the
CST is destroyed. Leaf spellings required by resolution are copied together
with their `SourceRange`.

`PtxSyntaxParser` remains as a convenience facade. Its `parseInstruction()`
and `parseModule()` perform source -> CST -> AST for fragment and module
clients respectively, mapping CST/lowering diagnostics in order.

`AstFile` mirrors the same root distinction and `AstModule` provides typed
containers for the supported module directives and functions. `AstFunction`
contains the function kind, qualifiers, name, and an ordered body variant of
`AstVariableDeclaration`, `AstLabel`, `AstCallPrototype`, `AstCallTargets`,
`AstBranchTargets`, `AstLocDirective`, `AstPragma`, `AstBlock`, and
`AstInstruction`. `AstBlock` keeps ordered body items and its whole source
range.
`AstCallPrototype` retains its label, sink, formal return/input payloads, and
the PTX 9.3 `.noreturn` / ABI-preservation suffixes with ranges. Return and input
parameters retain state space, alignment, type, pointer attributes, array form,
name, and range. `AstConstantExpression` represents literals/symbols,
parentheses, casts, unary/binary/conditional expressions, and initializer
operators. `AstInitializer` distinguishes scalar expressions from recursive
lists. Symbol identity is not written back into the AST; a separate
`SymbolTable` associates references by source range, as described in
`symbol_binding_design.md`.

## Narrowed Syntax AST responsibility

[SDK support: Narrowed Syntax AST responsibility](../sdk_support.md#syntax-ast-design--narrowed-syntax-ast-responsibility).

Syntax AST stores normalized grammar values and ranges. Concrete punctuation
and trivia remain in the CST/token buffer.

This structure must not classify an identifier as a register, symbol, label,
or function, decode a literal without its selected scalar type, select an
instruction variant, or enforce PTX/SM availability. Those remain resolution
and checker responsibilities.

Formatting, source-preserving rewriting, and future automated fixes must use
the CST/token buffer. They must not attempt to recover source layout from
Syntax AST or Resolved IR.

## Remaining location work

[SDK support: Remaining location work](../sdk_support.md#syntax-ast-design--remaining-location-work).

## Arithmetic-negated register syntax

[SDK support: Arithmetic-negated register syntax](../sdk_support.md#syntax-ast-design--arithmetic-negated-register-syntax).

### Legacy debug serialization

[SDK support: Legacy debug serialization](../sdk_support.md#syntax-ast-design--legacy-debug-serialization).

### ASCII whitespace and physical positions

[SDK support: ASCII whitespace and physical positions](../sdk_support.md#syntax-ast-design--ascii-whitespace-and-physical-positions).
