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
| Kernel resource directives | Supported subset | Entry headers retain `.maxnreg n`, `.maxntid nx[,ny[,nz]]`, `.reqntid nx[,ny[,nz]]`, `.minnctapersm ncta`, `.reqnctapercluster nx[,ny[,nz]]`, zero-argument `.explicitcluster`, and `.maxclusterrank n` with dedicated CST/AST; declaration semantics checks source `.version` minima and rejects same-entry `.maxntid` plus `.reqntid` and `.reqnctapercluster` plus `.maxclusterrank`; target/launch-time rules remain unchecked |
| Functions | Supported subset | `.entry`/`.func` definitions, `.func` prototypes, visibility/linkage qualifiers, return/input parameter lists, `.noreturn`, `.func` ABI suffixes, `.language`, and entry `.blocksareclusters` |
| Formal parameters | Supported subset | `.reg`/`.param`, alignment, scalar type, pointer space/alignment, and arrays sized by structured constant expressions |
| Variable declarations | Supported subset | Module/function scope, linkage qualifiers, `.reg`/`.param`/`.local`/`.shared`/`.global`/`.const`, narrow `.attribute(.managed/.unified)` support, alignment, vector/base type, parameterized names, multidimensional arrays, and `.global`/`.const` initializers |
| Function body | Supported subset | Variable declarations, labels, supported instruction syntax, and recursively bound nested blocks; resolution recursively flattens nested instructions in source order, with call staging confined to each lexical block |
| Constant expressions | Supported subset | Literals/symbols, parentheses, `.s64`/`.u64` casts, unary/binary/conditional operators, `generic(symbol)`, and mask initializer operators |
| Initializers | Supported subset | Scalar expressions, recursive brace lists, and an unsized first dimension; `.extern`, parameterized-name, and non-`.global`/`.const` initializers are rejected |
| Symbol binding | Supported subset | Module/function/nested-block scopes, variables/parameters/functions/labels, lexical shadowing, parameterized members, instruction/initializer/dimension/control-flow references, and isolated debug file/string metadata identity; labels and control-flow metadata remain function-local |
| Declaration semantics | Supported subset | Positive array extents, inferred first extent, initializer type/brace shape/element limits, symbol addresses, module linkage-compatible redeclarations, and the supported entry resource-version/conflict rules |
| Other directives | Partial | Same-module `.alias` canonicalizes direct-call ABI lookup; only the typed `.managed`/`.unified` attributes and documented header directives are modeled. Linker/backend behavior, UUID classes, and load/store attribute effects remain unsupported |
| Structured control syntax | Supported subset | `.callprototype`, `.calltargets`, and `.branchtargets` have dedicated function-local CST/AST syntax; binding and declaration semantics validate their labels/members/contracts. Generated `IndirectCall` layouts resolve a `.reg` target plus bound prototype/target-set metadata at PTX 2.1 / SM 20, and module resolution applies the shared call ABI contract. `brx.idx` resolves a `.u32` index and current-function `.branchtargets` identity at PTX 6.0 / SM 30; it does not expand target entries or build CFG |
| Recovery/editing | Supported subset | `parseModule()` emits ordered diagnostics plus inserted/skipped/error CST recovery nodes and resumes at bounded structural/module anchors; a partial nested block retains its valid body but has no closing-brace token. Standalone instruction parsing remains fail-fast. Recovered modules lower only valid neighboring nodes; recovery markers remain CST-only and parser diagnostics return once in source order. The installed consumer covers legal PTX 9.3 directive text, semantic directive failure, and recovered unknown directives. Round-trip serialization uses the original token buffer rather than recovery markers. An opt-in Clang lexer/CST libFuzzer target has a GTest seed smoke, but no ASan/UBSan or CI matrix yet |
| Resolved opcodes | Partial | The machine-readable manifests are authoritative for modelled opcode slices and deferred scope. The M12 common-kernel corpus validates 60 frozen forms through parse, resolve, and target-aware checking on `sm_80`, `sm_90a`, and `sm_100`; its `setmaxnreg.inc.sync.aligned.u32` occurrence is only in the `sm_90a` corpus fixture. That corpus presence is distinct from checker availability: the model accepts `sm_90a` at PTX 8.0, exact `sm_100a` at 8.6, the enabled `sm_100f` family at 8.8 (including modelled `sm_100f` and `sm_103a`/`sm_103f`), and `sm_120f` at 8.8. Uncatalogued official spellings report `UnknownTarget`; translation compatibility is not inferred. Inventory entries remain partial for implemented frozen slices with residual variants deferred after M12; simulator execution remains unsupported. |

| M10 frozen memory/atomic subset | Partial | PTX 7.4 / SM 70 L1 eviction and PTX 7.4 / SM 80 L2 cache-hint `ld`/`st`; frozen `ldu`, `prefetch`, `membar`, `fence`, and global relaxed-CTA scalar `atom`/`red` forms. They reuse the existing memory-consistency/scope domains; other qualifiers, operations, spaces, and types are outside this subset. |
| M10 frozen warp/async/matrix subset | Partial | `activemask` (PTX 6.2 / SM 30), `vote.sync.ballot.b32` and `shfl.sync.idx.b32` (PTX 6.0 / SM 30), `cp.async.ca.shared.global` plus commit/wait forms (PTX 7.0 / SM 80), `ldmatrix.sync.aligned.m8n8.x2.shared.b16` (PTX 9.3 §9.7.15.5.15; PTX 6.5 / SM 75; destination 2×b32), and `mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32` (PTX 9.3 §9.7.15.5.14; PTX 6.5 / SM 75; D/C 4×f32, A 2×f16x2, B 1×f16x2). Only these forms resolve/check; there are no execution semantics or simulator support. |
| Frozen mixed `cvt` | Supported | Register-only `cvt.rn.f32.u32` and `cvt.rzi.u32.f32` (equal-or-wider declarations at both endpoints; PTX 1.0 / SM 0) |
| Frozen `cvta` | Supported | Register-only `cvta.global.u64` and `cvta.to.global.u64` (PTX 2.0 / SM 20); no variable-address or provenance inference |
| Frozen integer `mul` | Supported | `mul.lo.u32` with register-or-immediate sources (PTX 1.0 / SM 0) |
| Frozen floating `mul` | Supported | Register-only `mul.rn.f32` (PTX 1.0 / SM 0) |
| Frozen integer `mad` | Supported | `mad.lo.u32` with register-or-immediate sources (PTX 1.0 / SM 0) |
| Frozen `fma` | Supported | Register-only `fma.rn.f32` (PTX 2.0 / SM 20) |
| Frozen integer `div` | Supported | `div.u32` with register-or-immediate sources (PTX 1.0 / SM 0); a zero divisor remains accepted with PTX-specified unspecified behavior |

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
| `.alias` | G | T | T | Y | I | C | Same-module device-function alias only; no linker/backend aliasing |
| `.abi_preserve` | G | T | T | — | — | C | `.callprototype` and `.func` suffix; PTX 9.0 source-version check, no target rule |
| `.abi_preserve_control` | G | T | T | — | — | C | `.callprototype` and `.func` suffix; PTX 9.0 source-version check, no target rule |
| `.align` | D | E | E | Y | Y | C | Declaration/parameter alignment |
| `.attribute` | G | T | T | — | — | C | Only `.managed` and `.unified(id,id)` placement/version subset |
| `.branchtargets` | D | T | T | Y | I | C / I | Declaration rules are direct; `brx.idx` consumer is PTX 6.0 / SM 30 |
| `.callprototype` | D | T | T | Y | I | C / I | Declaration rules are direct; indirect-call availability is consumer-driven |
| `.calltargets` | D | T | T | Y | I | C / I | Declaration rules are direct; indirect-call availability is consumer-driven |
| `.common` | G | R | — | — | — | — | Unmodeled declaration directive |
| `.const` | D | E | E | Y | Y | C | Existing variable declaration |
| `.entry` | D | E | E | Y | Y | C | Existing function node |
| `.explicitcluster` | D | T | T | — | — | C | Entry-only, zero arguments, PTX 7.8 source-version minimum; target/launch rules deferred |
| `.extern` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.file` | D | T | T | Y | — | C | Decimal/hex uint64 identity; repeated ID idempotent, overflow diagnoses |
| `.func` | D | E | E | Y | Y | C | Existing function node |
| `.global` | D | E | E | Y | Y | C | Existing variable declaration |
| `.local` | D | E | E | Y | Y | C | Existing variable declaration |
| `.loc` | D | T | T | Y | — | C | Decimal/hex file ID plus `.debug_str` function-name identity; no attachment |
| `.maxclusterrank` | D | T | T | — | — | C | Entry-only, one argument, PTX 7.8 source-version minimum; conflicts with `.reqnctapercluster` |
| `.maxnctapersm` | G | R | — | — | — | — | Unmodeled deprecated resource directive |
| `.maxnreg` | D | T | T | — | — | C | Entry-only source-version minimum |
| `.maxntid` | D | T | T | — | — | C | Entry-only; conflicts with `.reqntid` |
| `.minnctapersm` | D | T | T | — | — | C | Warning/device feasibility deferred |
| `.noreturn` | D | E | E | — | — | C | Device `.func`/`.callprototype`; return-parameter conflict and PTX 6.4 source-version checked; target rule deferred |
| `.param` | D | E | E | Y | Y | C | Existing variable/formal/call-parameter declaration |
| `.pragma` | D | T | T | — | — | — | Backend string interpretation intentionally absent |
| `.reg` | D | E | E | Y | Y | C | Existing variable/formal declaration |
| `.reqnctapercluster` | D | T | T | — | — | C | Entry-only, one to three arguments, PTX 7.8 source-version minimum; conflicts with `.maxclusterrank` |
| `.reqntid` | D | T | T | — | — | C | Entry-only; conflicts with `.maxntid` |
| `.section` | D | T | T | Y | — | C | Only `.debug_str` plus raw `name:` labels bind; payload stays raw |
| `.shared` | D | E | E | Y | Y | C | Existing variable declaration |
| `.sreg` | G | R | — | — | — | — | Unmodeled special-register declaration |
| `.target` | D | T | T | — | — | — | Module syntax retained; not checker context |
| `.tex` | G | R | — | — | — | — | Unmodeled declaration directive |
| `.version` | D | T | T | — | — | S | Supplies supported resource source-version checks |
| `.visible` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.weak` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.blocksareclusters` | G | T | T | — | — | C | Zero-argument entry marker, PTX 9.0; requires `.reqntid` + `.reqnctapercluster`; target/launch rules deferred |
| `.language` | G | T | T | — | — | C | Nonempty official string/integer list, PTX 9.3; retained syntax only |

## Implementation priority

The [project roadmap](../../.agents/project_roadmap.v2.md) is the sole authority
for implementation status, dependencies, and priority. This matrix records
capability boundaries only and intentionally does not repeat that ordering.

The PTX ISA variable-declaration overview mentions an optional fixed address,
but the current specification provides no separate grammar, constraints, or
examples. The frontend will not invent syntax from that sentence; a node will
be added only when normative grammar or verifiable `ptxas` behavior is
available.
