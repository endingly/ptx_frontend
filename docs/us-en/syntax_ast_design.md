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

## Instruction constant expressions

Instruction numeric positions reuse the declaration constant-expression grammar:
parentheses, precedence, integer unary/binary/conditional operators and
`(.s64)`/`(.u64)` casts, and homogeneous f64 arithmetic/comparisons with an
integer condition for floating ternaries. Single literal leaves retain their
existing representation. `CstConstantOperand` and `AstConstantOperand` own the
operation tree and full/subexpression ranges; copying either wrapper deep-copies
its tree. Expression operands match the existing Immediate syntax descriptor.
Call inputs, source tuples, predicate constants and address constant portions
use this shared grammar; register-only positions retain their descriptor limits.
Every expression entry shares the depth-128 budget and ordinary recovery rules.
Symbol addresses and initializer-only operators retain their separate semantics.
In bracketed addresses, top-level `+`/`-` still separate base and offset;
parenthesize a base expression containing additive or lower-precedence operators.
Exact `0f` single-precision bit patterns remain valid literal operands, but
cannot participate in constant expressions.

## Named array-address syntax

`A[index]` is one unbracketed address expression, distinct from `[A+offset]`.
The CST and AST own a separate named-index payload with bracket, index,
operator and displacement ranges. The index is an integer constant expression,
a bound scalar integer/bit register, or that register followed by `+`/`-` and
an integer constant expression. Register names need not begin with `%`.
`A[idx-1]`, `A[idx - 1]` and `A[idx+-1]` preserve their written operations;
constant expression trees are deep-copied and included in source identity.
Only one bracket pair is accepted: nested `[A[index]]`, repeated `A[i][j]`
and dynamic arithmetic such as `A[i+j]` or `A[i*2]` are unsupported.
Parsing this shape does not grant instruction-family admission or array binding.

## Named memory-vector syntax

Named data vectors use the existing identifier node, without synthesizing brace
lanes or selectors in CST/AST. Only opted ordinary LD/ST, LDNC, LDU and explicit
vector MOV slots admit
this shape; declaration-bound resolution records the owned whole source and
implicit lane origins described in [Resolved IR](resolved_ir_design.md#named-memory-vector-sources).
Brace syntax and the separate named-array address grammar are unchanged.

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
`.maxntid`, `.reqntid`, `.minnctapersm`, `.reqnctapercluster`,
`.explicitcluster`, and `.maxclusterrank` constraints: CST retains directive,
integer values, and commas, while AST retains kind, values, and ranges;
`.explicitcluster` has no values.

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
`.file` indices and `.debug_str` section/raw-label identities is performed in a
separate debug-metadata binding namespace, and `.loc` validates those
references. Attaching source locations to instructions or labels remains
deferred. At outermost scope, `.section name { ... }` preserves its matched
braces and raw DWARF payload token spelling in CST and AST; section names are
syntax, not ordinary bound identifiers. DWARF payload typing, private labels,
and `.loc` offset validation remain deferred. `.pragma` preserves a nonempty
comma-separated string list at module, entry-header, and function/nested-block
statement scope; it does not enter binding or Resolved IR. Entry-header
pragmas may be interleaved with the four supported kernel-resource directives;
their concrete order remains lossless in the CST header token sequence.
`CstRecoveryNode` is the tagged CST-only recovery model: `Inserted`
holds an expected `TokenKind` and a zero-width range without a token-buffer
span; `Skipped` holds a nonempty span of real source tokens; and `Error` holds
either such a span or an EOF zero-width range. It can occur as a module or
function-body item, does not carry a diagnostic ID, and never creates a
synthetic `PtxToken`. `parseModule()` appends ordered diagnostics and returns a
recovered CST: it synchronizes malformed module/body items at `;`, `}`, EOF,
the next function (including qualifiers), or a supported module-only directive.
It preserves those anchors, inserts only missing `;`/`}` markers at zero width,
and otherwise records real discarded spans. `parseInstruction()` remains
fail-fast. Recovered CST lowers only its valid neighboring nodes: recovery
markers remain CST-only, while `PtxSyntaxParser` returns the filtered AST with
the original parser diagnostics once and in source order. Round-trip
serialization uses the original token buffer rather than recovery markers. A
nested block missing its required `}`
retains its parsed body and an inserted marker, with no `right_brace` token.
The opt-in Clang `PTX_FRONTEND_BUILD_FUZZERS` target fuzzes raw lexer and CST
input; its entry point is also exercised by a small GTest seed smoke. It has no
ASan/UBSan or CI matrix yet. From the source root, run
`cmake -S . -B out/fuzz -DPTX_FRONTEND_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF`,
`cmake --build out/fuzz --target fuzz_lexer_cst`, then
`out/fuzz/submod/cst/fuzz_lexer_cst submod/cst/fuzz/corpus`.
The module grammar does not yet accept
other kernel-tuning directives or a token-edit API. The parser validates initializer
grammar shape and state-space/linkage constraints; the following declaration
semantics pass validates types, array dimensions, and element counts.
Unsupported constructs are not silently treated as instructions.

Source constant-expression and initializer trees share a maximum depth of 128
(`PtxCstParser::maxConstantTreeDepth`). A literal or symbol has depth 1; each
unary, cast, parenthesized, call, binary, conditional, or brace-list node adds
one to its deepest child. The scalar-initializer wrapper adds no level. This
is a frontend resource limit, not a PTX language limit; shallow lists may have
more than 128 elements. Recursive parsing checks the remaining depth before
descending, and iterative binary/postfix construction checks tree height before
adding a parent. Over-limit input produces a source-located parse diagnostic
and follows normal lossless recovery, keeping partial-tree cleanup and later
lowering, checking, and destruction bounded. This source-parser guarantee does
not validate arbitrarily deep CST/AST trees manually constructed by callers.

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

Syntax AST no longer stores trivia, punctuation tokens, or reconstructed text
for composite operands. It retains only:

- opcode, modifier, identifier, literal, and selector spellings;
- lexical immediate kind;
- predicate negation;
- address base, offset operation, and bracketed grammar form;
- vector member and vector pack structure, including ordinary `.xyzw/.rgba` members in brace scalar lanes;
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

## Arithmetic-negated register syntax

`CstNegatedRegisterOperand` retains the minus token and a child limited to one identifier or vector member. `AstNegatedRegisterOperand` owns the minus range, child range and full range. The parser recognizes `-Ident[.selector]` before signed-numeric immediate parsing; it does not introduce recursive unary operands. The distinct `ArithmeticNegatedRegister` syntax shape is enabled only for approved `vmad` source slots. Binding, lowering, source identity and layout classification traverse this child explicitly. Predicate `!` negation remains separate, and `!-%r` is rejected.
