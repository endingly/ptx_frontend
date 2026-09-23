# PTX Syntax Coverage

## Purpose

This matrix describes implemented parser behavior. It is not a claim of full
PTX ISA support. The reference grammar is NVIDIA's
[PTX ISA documentation](https://docs.nvidia.com/cuda/parallel-thread-execution/).
The [core opcode completeness audit](core_opcode_completeness_audit.md)
separates this frontend boundary from the archived PTX 9.3 and pinned-simulator
execution evidence for eleven commonly used operation names.

| Area | Status | Implemented subset |
| --- | --- | --- |
| Tokens and trivia | Partial | Identifiers, dot identifiers, literals, punctuation, comments, whitespace, and selected stable directives; unmodified `CstFile::sourceText()` round-trips its token buffer byte-for-byte |
| Instruction fragment | Partial | Predicate guard, opcode/modifiers, ordinary operands, addresses, vector members/packs, and dedicated call/branch operand shapes |
| Module header | Supported subset | `.version`, `.target`, and `.address_size` lower to ordered, AST-independent source-configuration regions. Each region owns its effective version, target-option spellings, address width, and explicit/defaulted provenance; an omitted address size owns PTX's 32-bit default rather than a host property. A recognized region supplies target-aware module validation, but is not a hardware-configuration or execution contract. |
| Debug file directive | Supported subset | Outermost `.file file_index "filename"` with optional paired `, timestamp, file_size`; decimal/octal/hex uint64 IDs bind in a debug-only namespace, repeated IDs are idempotent, and overflow diagnoses |
| Debug location directive | Supported subset | Function/nested-block `.loc file line column`, with decimal/octal/hex file IDs and paired PTX 7.2 `function_name`/`inlined_at` payload, validates bound file IDs and `.debug_str` section/label identity; it does not attach to instructions or enter Resolved IR |
| Debug section directive | Supported subset | Outermost `.section name { ... }` retains matched braces and ordered raw DWARF payload tokens; `.debug_str` and raw `name:` labels bind as debug identity, while payload widths, relocations, and offset semantics remain unsupported |
| Backend pragma directive | Supported subset | Module, `.entry` header, and function/nested-block statement `.pragma` preserve a nonempty comma-separated string list in CST/AST; pragmas neither bind nor enter Resolved IR |
| Kernel resource directives | Supported subset | Entry headers retain `.maxnreg n`, `.maxntid nx[,ny[,nz]]`, `.reqntid nx[,ny[,nz]]`, `.minnctapersm ncta`, `.reqnctapercluster nx[,ny[,nz]]`, zero-argument `.explicitcluster`, and `.maxclusterrank n` with dedicated CST/AST and owned normalized function-contract values. Declaration semantics rejects same-entry `.maxntid` plus `.reqntid` and `.reqnctapercluster` plus `.maxclusterrank`; module validation checks modeled PTX/target minima. Launch feasibility, occupancy, and physical resource allocation remain unchecked. |
| Functions | Supported subset | `.entry`/`.func` definitions, `.func` prototypes, visibility/linkage qualifiers, return/input parameter lists, `.noreturn`, `.func` ABI suffixes, `.language`, and entry `.blocksareclusters`. Resolved functions own their normalized signature, linkage/canonical identity, supported attributes, ABI suffixes, resources, and language/cluster markers independently of the AST. |
| Formal parameters | Supported subset | `.reg`/`.param`, alignment, scalar type, pointer space/alignment, and arrays sized by structured constant expressions |
| Variable declarations | Supported subset | Module/function scope, linkage qualifiers, `.reg`/`.param`/`.local`/`.shared`/`.global`/`.const`, narrow `.attribute(.managed/.unified)` support, alignment, vector/base type, parameterized names, multidimensional arrays, and `.global`/`.const` initializers |
| Function body | Supported subset | Variable declarations, labels, supported instruction syntax, and recursively bound nested blocks; resolution recursively flattens nested instructions in source order, with call staging confined to each lexical block |
| Constant expressions | Supported subset | Literals/symbols, parentheses, `.s64`/`.u64` casts, unary/binary/conditional operators, `generic(symbol)`, and mask initializer operators |
| Initializers | Supported subset | Scalar expressions, recursive brace lists, and an unsized first dimension; `.extern`, parameterized-name, and non-`.global`/`.const` initializers are rejected |
| Symbol binding | Supported subset | Module/function/nested-block scopes, variables/parameters/functions/labels, lexical shadowing, parameterized members, instruction/initializer/dimension/control-flow references, and isolated debug file/string metadata identity; labels and control-flow metadata remain function-local |
| Declaration semantics | Supported subset | Positive array extents, inferred first extent, initializer type/brace shape/element limits, symbol addresses, module linkage-compatible redeclarations, and the supported entry resource-version/conflict rules |
| Resolved storage declarations | Supported subset | Owned global/constant/shared/local declaration metadata with identity/scope, typed shape, checked byte extent, alignment, linkage, and initializer constants/relocations; external unsized shared data remains dynamic and size-unknown. See the [storage contract](storage_declarations.md) for normalization boundaries; no memory allocation or runtime instances |
| Other directives | Partial | Same-module `.alias` canonicalizes direct-call ABI lookup and is retained as an owned alias contract. Typed `.managed`/`.unified` attributes are retained as typed function/storage contracts (`.unified` owns its two numeric UUID halves); documented header directives are likewise owned. LD/ST validate unified-address and read-only contracts. Linker/backend behavior and runtime allocation remain unsupported. |
| Structured control syntax | Supported subset | `.callprototype`, `.calltargets`, and `.branchtargets` have dedicated function-local CST/AST syntax; binding and declaration semantics validate their labels/members/contracts. Resolved module contracts retain bound metadata-label identities, scope, canonical call signatures, ordered call targets, and expanded logical branch-target entries. Generated `IndirectCall` layouts resolve a `.reg` target plus bound prototype/target-set metadata at PTX 2.1 / SM 20, and module resolution applies the shared call ABI contract. `brx.idx` resolves a `.u32` index and current-function `.branchtargets` identity at PTX 6.0 / SM 30; it does not build a CFG or prove dynamic control flow. |
| Recovery/editing | Supported subset | `parseModule()` emits ordered diagnostics plus inserted/skipped/error CST recovery nodes and resumes at bounded structural/module anchors; a partial nested block retains its valid body but has no closing-brace token. Standalone instruction parsing remains fail-fast. Recovered modules lower only valid neighboring nodes; recovery markers remain CST-only and parser diagnostics return once in source order. The installed consumer covers legal PTX 9.3 directive text, semantic directive failure, and recovered unknown directives. Round-trip serialization uses the original token buffer rather than recovery markers. An opt-in Clang lexer/CST libFuzzer target has a GTest seed smoke, but no ASan/UBSan or CI matrix yet |
| Resolved opcodes | Partial | The documented supported forms and their parser/resolver/checker tests define the current opcode boundary; there is no exhaustive manual ISA ledger. The M12 common-kernel corpus validates 60 frozen forms through parse, resolve, and target-aware checking on `sm_80`, `sm_90a`, and `sm_100`; its `setmaxnreg.inc.sync.aligned.u32` occurrence is only in the `sm_90a` corpus fixture. That corpus presence is distinct from checker availability and from complete ISA coverage: the model accepts `sm_90a` at PTX 8.0, exact `sm_100a` at 8.6, the enabled `sm_100f` family at 8.8 (including modelled `sm_100f` and `sm_103a`/`sm_103f`), and `sm_120f` at 8.8. Uncatalogued official spellings report `UnknownTarget`; translation compatibility is not inferred. Implemented frozen slices remain partial, with residual variants deferred after M12; simulator execution remains unsupported. |

| M10 frozen memory/atomic subset | Partial | PTX 7.4 / SM 70 L1 eviction and PTX 7.4 / SM 80 L2 cache-hint `ld`/`st`; the historical `ldu.global.u32` and `prefetch.global.L1` seeds (expanded below); and frozen `membar`, `fence`, and global relaxed-CTA scalar `atom`/`red` forms. They reuse the existing memory-consistency/scope domains; other qualifiers, operations, spaces, and types are outside this frozen subset. |
| PTX 9.3 `ldu` | Supported | Scalar and v2/v4 uniform global loads with generic or explicit `.global` addressing, the documented type set, and target/operand checks. See [`ldu` coverage](ldu_coverage.md). |
| PTX 9.3 `prefetch` / `prefetchu` | Supported | Ordinary generic/global/local L1/L2 forms, global L2 eviction priority, generic/const/param tensor-map forms, and uniform-cache L1. See [prefetch coverage](prefetch_coverage.md). |
| PTX 9.3 `applypriority` / `discard` | Supported | Generic and explicit-global L2 forms with fixed 128-byte range, alignment, and target checks. See [cache-range coverage](applypriority_discard_coverage.md). |
| PTX 9.3 `createpolicy` | Supported | Fractional, range, and access-property conversion forms with typed priority, fraction, size, and target checks. See [`createpolicy` coverage](createpolicy_coverage.md). |
| M10 frozen warp/async/matrix subset | Partial | `activemask` (PTX 6.2 / SM 30), `vote.sync.ballot.b32` and `shfl.sync.idx.b32` (PTX 6.0 / SM 30), `cp.async.ca.shared.global` plus commit/wait forms (PTX 7.0 / SM 80), `ldmatrix.sync.aligned.m8n8.x2.shared.b16` (PTX 9.3 §9.7.15.5.15; PTX 6.5 / SM 75; destination 2×b32), and `mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32` (PTX 9.3 §9.7.15.5.14; PTX 6.5 / SM 75; D/C 4×f32, A 2×f16x2, B 1×f16x2). Only these forms resolve/check; there are no execution semantics or simulator support. |
| PTX 9.3 logic and shift | Supported | `and`/`or`/`xor`/`not` cover `.pred/.b16/.b32/.b64`; `cnot` covers `.b16/.b32/.b64`; `lop3` covers base and `.and/.or` predicate-result layouts with a checked `.u8` LUT; `shf` covers every `.l/.r` × `.clamp/.wrap` form; `shl` covers bit widths; and `shr` covers bit, unsigned, and signed widths. See [logic/shift coverage](logic_shift_coverage.md). |
| PTX 9.3 integer bit operations | Supported | `popc`, `clz`, `brev`, `bfind`, `bfe`, and `bfi` cover their documented PTX 2.0 / `sm_20` width, sign, `.shiftamt`, and control-operand forms. See [bit-operation coverage](bit_operations_coverage.md). |
| PTX 9.3 integer arithmetic | Supported | All documented §9.7.1 syntax forms are modelled through parsing, resolution, operand/type checks, and target availability. See [integer arithmetic coverage](integer_arithmetic_coverage.md). |
| Conversion and address-query forms | Supported subset | The modelled `isspacep`, `cvta`, `cvt`, `cvt.pack`, `prmt`, `mapa`, and `getctarank` syntax, operand layouts, typed modifier boundaries, and per-form PTX/target minima are defined in [conversion coverage](conversion_coverage.md). This is a frontend acceptance and validation boundary, not a claim of conversion execution or of the complete PTX conversion family. |
| Modelled `mul` | Supported | The complete PTX 9.3 integer, floating, half, and bfloat MUL forms, their modifier/operand contracts, and availability are listed in the [MUL coverage matrix](mul_coverage.md); simulator execution remains unsupported |
| Modelled `setp` | Supported | Ordinary and half/bfloat comparisons, Boolean predicate sources, destination shapes, and target boundaries are documented in [SETP coverage](setp_coverage.md); this does not execute comparisons |
| Modelled `set` | Supported | Ordinary and half/bfloat result/source types, comparison and Boolean domains, `.ftz`, operand containers, and target minima are documented in [SET coverage](set_coverage.md); comparisons are not executed |
| Ordinary `selp` | Supported | All PTX 9.3 ordinary scalar types, predicate selection operands, and the `.f64` target boundary are documented in [SELP coverage](selp_coverage.md); selection is not executed |
| Modelled `slct` | Supported | All PTX 9.3 ordinary data types, numeric selectors, `.ftz`, operand containers, and `.f64` target boundary are documented in [SLCT coverage](slct_coverage.md); selection is not executed |
| Modelled `ld`/`st` | Supported | Scalar/vector, shared sub-spaces, cache-control combinations, ordered semantics, NC loads, and unified-address validation are described in [LD coverage](ld_coverage.md) and [ST coverage](st_coverage.md); memory execution and allocation remain outside the frontend |
| Extended-precision integer | Supported | All documented §9.7.2 `add`/`addc`/`sub`/`subc`/`mad`/`madc` type, mode, and CC-effect combinations expose typed carry/borrow effects and target-aware validation; see [carry coverage](carry_coverage.md). Runtime CC state remains outside the frontend |
| Modelled `mad` | Supported subset | Integer and carry forms remain available; explicit-rounding FP32/FP64 forms, operands, target minima, and excluded legacy profiles are documented in [MAD coverage](mad_coverage.md). |
| Modelled `fma` | Supported | The 16 PTX 9.3 FMA variants, their modifier/operand contracts, and availability are listed in the [FMA coverage matrix](fma_coverage.md); simulator execution remains unsupported |
| Modelled `div` | Supported subset | Frozen integer `div.u32` (PTX 1.0 / SM 0) and explicit FP32/FP64 forms; see [DIV coverage](div_coverage.md). A zero divisor remains accepted with PTX-specified unspecified behavior. |

The conversion-family inventory is documented separately in
[conversion coverage](conversion_coverage.md). It names the modelled forms and
their deliberate exclusions without recreating the retired manual opcode ledger.

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
`Y` = retained as an owned binding/Resolved-IR contract; `I` = a consuming
instruction retains/checks its bound identity; `C` = direct binding/declaration
semantic check; `V` = the current AST-free `validateModule` traversal performs
target-aware validation. Retention alone does not imply `V`: prototype ABI/
`.noreturn` availability and storage-declaration attribute availability remain
AST/declaration-validation paths. `V` validates only modeled source-profile
requirements; it does not establish launch feasibility, simulator execution, or
hardware behavior. `—` = no support at that stage.

| Directive | Token | CST | AST | Binding | Resolved IR | Target / semantic | Explicit boundary |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `.address_size` | D | T | T | — | Y | C | Owned effective 32/64-bit source-header width with explicit/defaulted provenance; it is revalidated as header integrity, not used to construct `checker::TargetInfo` or host-address meaning |
| `.alias` | G | T | T | Y | Y / I | C / V | Owned same-module device-function alias; no linker/backend aliasing |
| `.abi_preserve` | G | T | T | — | Y | C / V (`.func` only) | Both forms own ABI metadata. PTX 9.0 availability is AST-free for `.func`; `.callprototype` availability remains AST-backed. No physical register assignment. |
| `.abi_preserve_control` | G | T | T | — | Y | C / V (`.func` only) | Both forms own ABI metadata. PTX 9.0 availability is AST-free for `.func`; `.callprototype` availability remains AST-backed. No physical register assignment. |
| `.align` | D | E | E | Y | Y | C | Declaration/parameter alignment |
| `.attribute` | G | T | T | — | Y | C / V (function only) | Only typed `.managed` and `.unified(id,id)` placement/version subset. AST-free target availability covers function attributes; retained storage attributes are currently checked through AST/declaration validation. No runtime allocation or host-address interpretation. |
| `.branchtargets` | D | T | T | Y | Y / I | C / I | Owns bound, expanded logical targets; `brx.idx` consumer is PTX 6.0 / SM 30, with no CFG/protocol proof |
| `.callprototype` | D | T | T | Y | Y / I | C / I | Owns normalized signature and supported ABI metadata. AST-free validation rechecks its identity, scope, and signature, not prototype ABI/`.noreturn` availability; indirect-call availability remains consumer-driven. |
| `.calltargets` | D | T | T | Y | Y / I | C / I | Owns ordered bound/canonical function targets and their shared signature; indirect-call availability remains consumer-driven |
| `.common` | G | R | — | — | — | — | Unmodeled declaration directive |
| `.const` | D | E | E | Y | Y | C | Existing variable declaration |
| `.entry` | D | E | E | Y | Y | C | Existing function node |
| `.explicitcluster` | D | T | T | — | Y | C / V | Entry-only, zero arguments, PTX 7.8 modeled availability; target launch feasibility deferred |
| `.extern` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.file` | D | T | T | Y | — | C | Decimal/octal/hex uint64 identity; repeated ID idempotent, overflow diagnoses |
| `.func` | D | E | E | Y | Y | C | Existing function node |
| `.global` | D | E | E | Y | Y | C | Existing variable declaration |
| `.local` | D | E | E | Y | Y | C | Existing variable declaration |
| `.loc` | D | T | T | Y | — | C | Decimal/octal/hex file ID plus `.debug_str` function-name identity; no attachment |
| `.maxclusterrank` | D | T | T | — | Y | C / V | Entry-only normalized resource, PTX 7.8 modeled availability; conflicts with `.reqnctapercluster` |
| `.maxnctapersm` | G | R | — | — | — | — | Unmodeled deprecated resource directive |
| `.maxnreg` | D | T | T | — | Y | C / V | Entry-only normalized resource with modeled availability; occupancy implication deferred |
| `.maxntid` | D | T | T | — | Y | C / V | Entry-only normalized resource; conflicts with `.reqntid`; launch feasibility deferred |
| `.minnctapersm` | D | T | T | — | Y | C / V | Entry-only normalized resource; warning/device feasibility deferred |
| `.noreturn` | D | E | E | — | Y | C / V (`.func` only) | Device `.func`/`.callprototype`; return-parameter conflict is checked, but AST-free PTX 6.4 availability is currently rechecked only for `.func`; prototype availability remains AST-backed. |
| `.param` | D | E | E | Y | Y | C | Existing variable/formal/call-parameter declaration |
| `.pragma` | D | T | T | — | — | — | Backend string interpretation intentionally absent |
| `.reg` | D | E | E | Y | Y | C | Existing variable/formal declaration |
| `.reqnctapercluster` | D | T | T | — | Y | C / V | Entry-only normalized resource, PTX 7.8 modeled availability; conflicts with `.maxclusterrank` |
| `.reqntid` | D | T | T | — | Y | C / V | Entry-only normalized resource; conflicts with `.maxntid`; launch feasibility deferred |
| `.section` | D | T | T | Y | — | C | Only `.debug_str` plus raw `name:` labels bind; payload stays raw |
| `.shared` | D | E | E | Y | Y | C | Existing variable declaration |
| `.sreg` | G | R | — | — | — | — | Unmodeled special-register declaration |
| `.target` | D | T | T | — | Y | V | Owned source-target options build `checker::TargetInfo` for recognized profiles; no physical target selection or translation guarantee |
| `.tex` | G | R | — | — | — | — | Unmodeled declaration directive |
| `.version` | D | T | T | — | Y | V | Owned source version participates in target-aware module validation |
| `.visible` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.weak` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.blocksareclusters` | G | T | T | — | Y | C / V | Owned zero-argument entry marker; PTX 9.0 modeled availability and required `.reqntid` + `.reqnctapercluster`; launch rules deferred |
| `.language` | G | T | T | — | Y | C / V | Owned nonempty official string/integer list; PTX 9.3 modeled availability, no backend-language behavior |

## Implementation priority

The [project roadmap](../../.agents/project_roadmap.v2.md) is the sole authority
for implementation status, dependencies, and priority. This matrix records
capability boundaries only and intentionally does not repeat that ordering.

The PTX ISA variable-declaration overview mentions an optional fixed address,
but the current specification provides no separate grammar, constraints, or
examples. The frontend will not invent syntax from that sentence; a node will
be added only when normative grammar or verifiable `ptxas` behavior is
available.
