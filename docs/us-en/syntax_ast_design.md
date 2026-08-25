# CST and Syntax AST design

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

## CST ownership and representation

Public CST headers live under `submod/cst/include`. A `syntax_cst::CstFile`
owns the complete `PtxToken` buffer. Its `CstRoot` distinguishes a standalone
instruction fragment from a `CstModule`; nodes refer to the file buffer with
`TokenId`, and composite nodes also store half-open `CstTokenRange` values.

`CstModule`, `CstModuleDirective`, and `CstFunction` establish module-level
ownership without duplicating token buffers. `parseModule()` currently parses
`.version`, `.target`, and `.address_size`, plus `.entry` and `.func`
definitions, `.func` prototypes, structured formal parameters, `.reg`
and other variable declarations, and labels. Function bodies contain
syntax supported by the instruction parser. Variable declarations structurally
retain linkage qualifiers, state space, optional alignment and vector type,
base type, comma-separated names, parameterized-name `<count>` syntax, and
multi-dimensional array declarators, optional equals signs, and initializers.
Array dimensions and scalar initializers use structured constant-expression
trees; brace initializers recursively retain every brace level, element, and
comma. Function qualifiers and the complete token sequence for the supported
header grammar remain in the CST; the entry/function kind and name are also
identified explicitly. An unmodeled function-header token such as `.maxntid`
is rejected at the CST boundary instead of being accepted as an opaque token
and silently discarded during AST lowering.

The tree retains comma, semicolon, bracket, brace, sign, predicate, and vector
selector tokens explicitly. Each `PtxToken` retains its leading trivia, and the
EOF token retains final trivia. Therefore `CstFile::sourceText()`
can reproduce the parsed input byte-for-byte.

```cpp
PtxCstParser parser(source);
auto cst = parser.parseInstruction();
if (cst)
  assert(cst->sourceText() == source);
```

`parseInstruction()` accepts exactly one complete instruction fragment, while
`parseModule()` requires a module root. The module grammar does not yet accept
debug or kernel-tuning directives, nested statement scopes, recovery nodes,
missing-token insertion, or a token-edit API. The parser validates initializer
grammar shape and state-space/linkage constraints; the following declaration
semantics pass validates types, array dimensions, and element counts.
Unsupported constructs are not silently treated as instructions.

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
clients respectively.

`AstFile` mirrors the same root distinction and `AstModule` provides typed
containers for the supported module directives and functions. `AstFunction`
contains the function kind, qualifiers, name, and an ordered body variant of
`AstVariableDeclaration`, `AstLabel`, and `AstInstruction`. Return and input
parameters retain state space, alignment, type, pointer attributes, array form,
name, and range. `AstConstantExpression` represents literals/symbols,
parentheses, casts, unary/binary/conditional expressions, and initializer
operators. `AstInitializer` distinguishes scalar expressions from recursive
lists. Symbol identity is not written back into the AST; a separate
`SymbolTable` associates references by source range, as described in
`symbol_binding_design.md`.

## Narrowed Syntax AST responsibility

Syntax AST no longer stores trivia, punctuation tokens, or reconstructed text
for composite operands. It retains only:

- opcode, modifier, identifier, literal, and selector spellings;
- lexical immediate kind;
- predicate negation;
- address base, offset operation, and bracketed grammar form;
- vector member and vector pack structure;
- call return/input parameter groups, callees, and target-set/prototype symbols;
- direct branch label targets;
- declaration array dimensions, constant expressions, and recursive
  initializer structure;
- source ranges for diagnostics;
- operand grammar alternatives required by generated layout descriptors.

This structure must not classify an identifier as a register, symbol, label,
or function, decode a literal without its selected scalar type, select an
instruction variant, or enforce PTX/SM availability. Those remain resolution
and checker responsibilities.

Formatting, source-preserving rewriting, and future automated fixes must use
the CST/token buffer. They must not attempt to recover source layout from
Syntax AST or Resolved IR.

## Remaining location work

`SourceRange` currently stores line and column only. A future multi-file CST
and robust edit system should extend locations with a source identity and byte
offsets. This does not require widening the Syntax AST responsibility.
