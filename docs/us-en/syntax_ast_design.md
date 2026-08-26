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
identified explicitly. Entry headers additionally retain typed `.maxnreg`,
`.maxntid`, `.reqntid`, and `.minnctapersm` constraints: CST retains directive,
integer values, and commas, while AST retains kind, values, and ranges.

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

`parseInstruction()` accepts exactly one complete instruction fragment, while
`parseModule()` requires a module root. At outermost module scope, `.file`
accepts exactly `file_index "filename"` or
`file_index "filename", timestamp, file_size`; the optional numeric fields
are a required pair and their absence retains PTX's default zero without
inventing source locations. Function bodies may contain nested
blocks; CST retains their braces, ordered body items, and source ranges, while
Syntax AST retains their body items and whole source ranges. Module resolution
binds lexical block scopes and recursively resolves their instructions
into the enclosing function's source-ordered flat body; it does not introduce a
`ResolvedBlock`. Function bodies (including nested blocks) also accept `.loc`
with its basic `file line column` triple or its paired PTX 7.2
`function_name label {+ integer}` / `inlined_at file line column` payload;
CST preserves its punctuation and AST retains fields and ranges. Resolving
`.file` indices, DWARF labels, and attachment to instructions or labels remains
deferred. At outermost scope, `.section name { ... }` preserves its matched
braces and raw DWARF payload token spelling in CST and AST; section names are
syntax, not ordinary bound identifiers. DWARF payload typing, private labels,
and `.loc` offset validation remain deferred. `.pragma` preserves a nonempty
comma-separated string list at module, entry-header, and function/nested-block
statement scope; it does not enter binding or Resolved IR. Entry-header
pragmas may be interleaved with the four supported kernel-resource directives;
their concrete order remains lossless in the CST header token sequence.
`CstRecoveryNode` is a tagged CST-only model for future recovery: `Inserted`
holds an expected `TokenKind` and a zero-width range without a token-buffer
span; `Skipped` holds a nonempty span of real source tokens; and `Error` holds
either such a span or an EOF zero-width range. It can occur as a module or
function-body item, does not carry a diagnostic ID, and never creates a
synthetic `PtxToken`. `parseModule()` appends ordered diagnostics and returns a
recovered CST: it synchronizes malformed module/body items at `;`, `}`, EOF,
the next function (including qualifiers), or a supported module-only directive.
It preserves those anchors, inserts only missing `;`/`}` markers at zero width,
and otherwise records real discarded spans. `parseInstruction()` remains
fail-fast. Recovered CST is intentionally not lowered by `PtxSyntaxParser`
until C01 defines that contract; round-trip serialization uses the original
token buffer rather than recovery markers. A nested block missing its required `}`
retains its parsed body and an inserted marker, with no `right_brace` token.
The module grammar does not yet accept
other kernel-tuning directives or a token-edit API. The parser validates initializer
grammar shape and state-space/linkage constraints; the following declaration
semantics pass validates types, array dimensions, and element counts.
Unsupported constructs are not silently treated as instructions.

Public parser and lowering roots return `ResultWithDiagnostics<T, D>`: an
optional value plus an ordered `DiagnosticCollection<D>`. This permits later
recovery to return a CST with diagnostics without another API change. Module
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

Syntax AST no longer stores trivia, punctuation tokens, or reconstructed text
for composite operands. It retains only:

- opcode, modifier, identifier, literal, and selector spellings;
- lexical immediate kind;
- predicate negation;
- address base, offset operation, and bracketed grammar form;
- vector member and vector pack structure;
- call return/input parameter groups, callees, and target-set/prototype symbols;
- direct branch label targets;
- function-local `.callprototype` labels, signature payloads, and PTX 9.3
  suffix payloads;
- function-local `.calltargets` labels and ordered target identifiers;
- function-local `.branchtargets` labels and unexpanded compact target entries;
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
