# PTX Syntax Coverage

## Purpose

This matrix describes implemented parser behavior. It is not a claim of full
PTX ISA support. The reference grammar is NVIDIA's
[PTX ISA documentation](https://docs.nvidia.com/cuda/parallel-thread-execution/).

| Area | Status | Implemented subset |
| --- | --- | --- |
| Tokens and trivia | Partial | Identifiers, dot identifiers, literals, punctuation, comments, whitespace, and selected stable directives; unmodified `CstFile::sourceText()` round-trips its token buffer byte-for-byte |
| Instruction fragment | Partial | Predicate guard, opcode/modifiers, ordinary operands, addresses, vector members/packs, and dedicated call/branch operand shapes |
| Module header | Supported subset | `.version`, `.target`, `.address_size` |
| Debug file directive | Supported subset | Outermost `.file file_index "filename"` with optional paired `, timestamp, file_size`; decimal/hex uint64 IDs bind in a debug-only namespace, repeated IDs are idempotent, and overflow diagnoses |
| Debug location directive | Supported subset | Function/nested-block `.loc file line column`, with decimal/hex file IDs and paired PTX 7.2 `function_name`/`inlined_at` payload, validates bound file IDs and `.debug_str` section/label identity; it does not attach to instructions or enter Resolved IR |
| Debug section directive | Supported subset | Outermost `.section name { ... }` retains matched braces and ordered raw DWARF payload tokens; `.debug_str` and raw `name:` labels bind as debug identity, while payload widths, relocations, and offset semantics remain unsupported |
| Backend pragma directive | Supported subset | Module, `.entry` header, and function/nested-block statement `.pragma` preserve a nonempty comma-separated string list in CST/AST; pragmas neither bind nor enter Resolved IR |
| Kernel resource directives | Supported subset | Entry headers retain `.maxnreg n`, `.maxntid nx[,ny[,nz]]`, `.reqntid nx[,ny[,nz]]`, and `.minnctapersm ncta` with dedicated CST/AST; declaration semantics checks their source `.version` minima and rejects same-entry `.maxntid` plus `.reqntid` |
| Functions | Supported subset | `.entry`/`.func` definitions, `.func` prototypes, visibility/linkage qualifiers, return and input parameter lists, `.noreturn` |
| Formal parameters | Supported subset | `.reg`/`.param`, alignment, scalar type, pointer space/alignment, and arrays sized by structured constant expressions |
| Variable declarations | Supported subset | Module/function scope, linkage qualifiers, `.reg`/`.param`/`.local`/`.shared`/`.global`/`.const`, alignment, vector/base type, parameterized names, multidimensional arrays, and `.global`/`.const` initializers |
| Function body | Supported subset | Variable declarations, labels, supported instruction syntax, and recursively bound nested blocks; resolution recursively flattens nested instructions in source order, with call staging confined to each lexical block |
| Constant expressions | Supported subset | Literals/symbols, parentheses, `.s64`/`.u64` casts, unary/binary/conditional operators, `generic(symbol)`, and mask initializer operators |
| Initializers | Supported subset | Scalar expressions, recursive brace lists, and an unsized first dimension; `.extern`, parameterized-name, and non-`.global`/`.const` initializers are rejected |
| Symbol binding | Supported subset | Module/function/nested-block scopes, variables/parameters/functions/labels, lexical shadowing, parameterized members, instruction/initializer/dimension/control-flow references, and isolated debug file/string metadata identity; labels and control-flow metadata remain function-local |
| Declaration semantics | Supported subset | Positive array extents, inferred first extent, initializer type/brace shape/element limits, symbol addresses, module linkage-compatible redeclarations, and the supported entry resource-version/conflict rules |
| Other directives | Not supported (rejected) | Module variable and other structured kernel-tuning directives, including official-but-unmodeled `.language`, produce a recovery diagnostic rather than silently entering the AST |
| Structured control syntax | Supported subset | `.callprototype`, `.calltargets`, and `.branchtargets` have dedicated function-local CST/AST syntax; binding and declaration semantics validate their labels/members/contracts. Generated `IndirectCall` layouts resolve a `.reg` target plus bound prototype/target-set metadata at PTX 2.1 / SM 20, and module resolution applies the shared call ABI contract. `brx.idx` resolves a `.u32` index and current-function `.branchtargets` identity at PTX 6.0 / SM 30; it does not expand target entries or build CFG |
| Recovery/editing | Supported subset | `parseModule()` emits ordered diagnostics plus inserted/skipped/error CST recovery nodes and resumes at bounded structural/module anchors; a partial nested block retains its valid body but has no closing-brace token. Standalone instruction parsing remains fail-fast. Recovered modules lower only valid neighboring nodes; recovery markers remain CST-only and parser diagnostics return once in source order. The installed consumer covers legal PTX 9.3 directive text, semantic directive failure, and recovered unknown directives. Round-trip serialization uses the original token buffer rather than recovery markers. An opt-in Clang lexer/CST libFuzzer target has a GTest seed smoke, but no ASan/UBSan or CI matrix yet |
| Resolved opcodes | Partial | Only opcodes present in the YAML database; currently bare `ret`, `exit`, and `trap` (PTX 1.0 / all SM, no modifiers or operands; ordinary predicate guards are accepted), `add`, `sub`, `bar`, direct `bra`, indexed `brx.idx`, direct and descriptor-backed indirect `call`, selected scalar/vector `mov`, and generic/basic-explicit scalar plus braced-vector `ld`/`st` for `.b8/.b16/.b32/.b64`, `.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`, and `.f32/.f64`; module-resolved direct and indirect `call` share canonical return/input ABI checking for arity/order, `.reg/.param` type/shape, `.param .b8` arrays, pointers, and formal-typed literals. Indirect signatures come from the local prototype or validated target set and retain no call-table support. `brx.idx` retains target-list identity without metadata expansion. PTX 8.8/SM 100 modern vectors accept only `.v8` × 32-bit or `.v4` × 64-bit 256-bit payloads, require global space when known, and allow partial `_` sinks. Legacy vectors remain at most 128 bits and reject sinks. Static natural alignment checks bound data symbols plus constant byte offsets and absolute immediate addresses; register and standalone unresolved addresses remain unknown. `ld/st` support omitted/explicit `.weak`, `.volatile`, scoped relaxed/acquire/release, and PTX 8.2 scalar `.mmio.relaxed.sys`, with generated cross-modifier checks; generic loads accept known `.const/.global/.local/.shared` spaces (`.const` requires PTX 3.1), generic stores accept `.global/.local/.shared`, and explicit forms require an exact runtime-modifier match; bound `.param` loads require input parameters and stores require return parameters, with function-context PTX/SM checks; load destination/store source registers and vector elements may be wider under the bit/integer/float kind rules, while other typed operands remain same-width and unknown address identity is not inferred |

The lexer may tokenize source outside this matrix, and Syntax AST may retain an
unknown opcode as text. Neither behavior means that the construct can be
lowered to Resolved IR.

## PTX 9.3 directive registry

This is the per-spelling registry for the 35 directives in PTX ISA Table 1,
plus five directives omitted by that table: `.attribute` from 5.4.8,
`.abi_preserve` and `.abi_preserve_control` from 11.4, and
`.blocksareclusters` and `.language` from 11.8. It answers the coverage
matrix's six pipeline questions. Legacy non-dot `@@dwarf` and attributes such
as `.ptr` are intentionally outside this dot-directive registry.

Legend: `D` = dedicated lexer token; `G` = generic `DotIdent` (still tokenized,
but not CST support). `T` = typed directive CST/AST; `E` = represented by an
existing declaration/function node; `R` = explicitly rejected by the parser.
`Y` = retained or used directly at the binding/Resolved IR stage; `I` = only a
consuming instruction indirectly retains/checks the identity; `C` = direct
binding/declaration semantic check; `S` = supplies source `.version` to a
current semantic check, not `checker::TargetInfo`; `—` = no support at that
stage.

| Directive | Token | CST | AST | Binding | Resolved IR | Target / semantic | Explicit boundary |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `.address_size` | D | T | T | — | — | — | Module syntax only |
| `.alias` | G | R | — | — | — | — | Unmodeled module directive |
| `.abi_preserve` | G | T | T | — | — | — | Only `.callprototype` suffix; `.func` header form and PTX 9.0 / SM 80 availability remain unsupported |
| `.abi_preserve_control` | G | T | T | — | — | — | Only `.callprototype` suffix; `.func` header form and PTX 9.0 / SM 80 availability remain unsupported |
| `.align` | D | E | E | Y | Y | C | Declaration/parameter alignment |
| `.attribute` | G | R | — | — | — | — | Unmodeled variable/function directive |
| `.branchtargets` | D | T | T | Y | I | C / I | Declaration rules are direct; `brx.idx` consumer is PTX 6.0 / SM 30 |
| `.callprototype` | D | T | T | Y | I | C / I | Declaration rules are direct; indirect-call availability is consumer-driven |
| `.calltargets` | D | T | T | Y | I | C / I | Declaration rules are direct; indirect-call availability is consumer-driven |
| `.common` | G | R | — | — | — | — | Unmodeled declaration directive |
| `.const` | D | E | E | Y | Y | C | Existing variable declaration |
| `.entry` | D | E | E | Y | Y | C | Existing function node |
| `.explicitcluster` | G | R | — | — | — | — | Unmodeled entry-header directive |
| `.extern` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.file` | D | T | T | Y | — | C | Decimal/hex uint64 identity; repeated ID idempotent, overflow diagnoses |
| `.func` | D | E | E | Y | Y | C | Existing function node |
| `.global` | D | E | E | Y | Y | C | Existing variable declaration |
| `.local` | D | E | E | Y | Y | C | Existing variable declaration |
| `.loc` | D | T | T | Y | — | C | Decimal/hex file ID plus `.debug_str` function-name identity; no attachment |
| `.maxclusterrank` | G | R | — | — | — | — | Unmodeled entry-header directive |
| `.maxnctapersm` | G | R | — | — | — | — | Unmodeled deprecated resource directive |
| `.maxnreg` | D | T | T | — | — | C | Entry-only source-version minimum |
| `.maxntid` | D | T | T | — | — | C | Entry-only; conflicts with `.reqntid` |
| `.minnctapersm` | D | T | T | — | — | C | Warning/device feasibility deferred |
| `.noreturn` | D | E | E | — | — | C | `.func`/`.callprototype` supported; return-parameter conflict checked; PTX 6.4 / SM 30 availability unchecked |
| `.param` | D | E | E | Y | Y | C | Existing variable/formal/call-parameter declaration |
| `.pragma` | D | T | T | — | — | — | Backend string interpretation intentionally absent |
| `.reg` | D | E | E | Y | Y | C | Existing variable/formal declaration |
| `.reqnctapercluster` | G | R | — | — | — | — | Unmodeled entry-header directive |
| `.reqntid` | D | T | T | — | — | C | Entry-only; conflicts with `.maxntid` |
| `.section` | D | T | T | Y | — | C | Only `.debug_str` plus raw `name:` labels bind; payload stays raw |
| `.shared` | D | E | E | Y | Y | C | Existing variable declaration |
| `.sreg` | G | R | — | — | — | — | Unmodeled special-register declaration |
| `.target` | D | T | T | — | — | — | Module syntax retained; not checker context |
| `.tex` | G | R | — | — | — | — | Unmodeled declaration directive |
| `.version` | D | T | T | — | — | S | Supplies supported resource source-version checks |
| `.visible` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.weak` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.blocksareclusters` | G | R | — | — | — | — | Table-external PTX 9.0 entry / SM 90 directive |
| `.language` | G | R | — | — | — | — | Table-external PTX 9.3 entry/function directive |

## Implementation priority

The [project roadmap](../../.agents/project_roadmap.md) is the sole authority
for implementation status, dependencies, and priority. This matrix records
capability boundaries only and intentionally does not repeat that ordering.

The PTX ISA variable-declaration overview mentions an optional fixed address,
but the current specification provides no separate grammar, constraints, or
examples. The frontend will not invent syntax from that sentence; a node will
be added only when normative grammar or verifiable `ptxas` behavior is
available.
