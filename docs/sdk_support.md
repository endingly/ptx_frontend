# SDK supported scope and retained limitations

This is the single maintained English authority for the current SDK's supported
source syntax, operation forms, type and target restrictions, and retained
limitations. README and both language reference trees defer to
this document for those claims. Design/API pages retain representation,
ownership, algorithms, rationale and test evidence; historical records describe
their pinned revisions. A planning checkbox, opcode inventory, successful parse,
or tool observation does not independently enlarge this contract.

<a id="package-version"></a>

The current C++ package and Python wheel versions are both `0.8.0`. CMake package
compatibility remains `SameMinorVersion`.

The SDK remains pre-1.0. Generated C++ layouts, enum ordinals and binary ABI are
not frozen; consumers must rebuild against matching headers and libraries when
public members change.

## Reading this contract

The baseline is the archived [CUDA 13.3 / PTX ISA 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html),
the default PTX ABI, the exact target catalog below, and the published reference
interpretations retained here. This is a static frontend contract: lossless CST,
Syntax AST, lexical binding, declaration checking, generated typed Resolved IR,
and modeled checking. It is not an assembler, linker, translator, or executor.

The detailed sections preserve exact accepted/excluded alternatives and their
conditions. An instruction-family heading does not admit every cross-product of
its modifiers, operands, types, shapes and profiles. The source-producer sections below enumerate the current numeric and exact-word
permissions for each listed member and preserve its register-carrier conditions. Tool and test observations are evidence for their stated fixtures,
not new acceptance rules.

- [Validation APIs and missing context](#validation-and-context)
- [Exact target profiles and historical windows](#target-profiles)
- [Source grammar and directives](#syntax-coverage)
- [Lexical forms](#lexer-design), [CST/AST syntax](#syntax-ast-design), [binding](#symbol-binding-design)
- [Declarations and expressions](#declaration-semantics-design), [parameters](#parameter-declarations), [storage](#storage-declarations)
- [Control flow and calls](#control-flow-syntax-design), [register compatibility](#register-type-policy)
- [Resolved forms, operands and current source producers](#resolved-ir-design)
- [Instruction-family navigation](#operation-families)
- [Caller facts and retained reference boundaries](#retained-limitations)
- [Evidence and reproducible checks](#evidence)

<a id="validation-and-context"></a>

## Validation and context

`resolveModuleOnly` performs binding, declaration semantics, resolution and call
ABI/staging checks. It does not run the final instruction/directive checker.
`resolveAndValidateModule` requires recognized version/target context and final
checking for each checked region. `resolveModule` retains compatibility behavior,
checking where context is available. `validateModule(ast, module, policy)` checks
supplied-source correspondence; `validateModule(module, policy)` checks current
owned records after source/AST release. Mutated or constructed public IR needs
owned validation before consumption. Exact success contracts are in the
[entry-point matrix](#resolved-ir-design--status-and-scope).

`RequireCompleteContext` diagnoses missing context with
`MissingValidationContext`; `AvailableContext` defers context-dependent checks
when required facts are absent and still rejects independently known malformed
facts. Whole-instruction availability deferral does not implement a partial DNF
solver. Unknown explicit profiles produce `UnknownTarget`. Source-backed and
owned checks have distinct evidence: coherent current edits do not authenticate
erased source history, and valid current payloads can still mismatch a supplied
AST. Owned/copy/assignment/clone and mutation guarantees are the finite public
contracts recorded in these sections; they are not a guarantee for every future
program or every malformed public object.

<a id="target-profiles"></a>

## Exact target profiles and historical windows

Each listed suffix accepts both `sm_` and `compute_` prefixes, preserving written
source spelling. The original accepted scope contains the following **40 exact
architecture/flavor profiles**, including legacy profiles; it is not forty
modern GPU models. The separately adopted historical 101 trio is additional.
Unknown numeric suffixes and unlisted flavors are not inferred from an ordering
of SM numbers.

| Exact suffix | Minimum PTX ISA |
| --- | --- |
| `10`, `11` | 1.0 |
| `12`, `13` | 1.2 |
| `20` | 2.0 |
| `30` | 3.0 |
| `35` | 3.1 |
| `32`, `50` | 4.0 |
| `37`, `52` | 4.1 |
| `53` | 4.2 |
| `60`, `61`, `62` | 5.0 |
| `70` | 6.0 |
| `72` | 6.1 |
| `75` | 6.3 |
| `80` | 7.0 |
| `86` | 7.1 |
| `87` | 7.4 |
| `89`, `90` | 7.8 |
| `90a` | 8.0 |
| `100`, `100a` | 8.6 |
| `120`, `120a` | 8.7 |
| `100f`, `103`, `103a`, `103f`, `120f`, `121`, `121a`, `121f` | 8.8 |
| `88`, `110`, `110a`, `110f` | 9.0 |
| Additional historical `101`, `101a` | 8.6 |
| Additional historical `101f` | 8.8 |

The 101 spellings remain legal at PTX 9.0/9.3 and retain source architecture 101.
Their explicit catalog correspondence uses validation-feature architecture 110;
source identity remains different from 110. Generic101 has no family feature;
101A/F expose `sm_110f`, without 100f/103f inheritance. Exact source introduction,
flavor, instruction floors, capabilities and shape restrictions remain separate.
A caller's explicit `TargetInfo::sm_version` is not overwritten. See the
[precise 101 restrictions](#resolved-ir-design--historical-101-source-profiles-and-validation-features).

Historical support uses explicit windows, not a promise for all retired GPUs.
These include PTX 1.0–1.3 entry-body inputs with a 32764-byte logical ceiling;
header parameters at PTX 1.4+; device/body call slots at PTX 2.0+/SM20; the
`@@DWARF` source production at PTX [1.2, 2.0); legacy vote/shuffle availability;
implicit historical floating controls; and the exact
`map_f64_to_f32` option on generic10/11/12. Detailed operation windows and
conjunctions remain in their family sections. The source ABI does not certify
historical physical launch packing or external nondefault translator modes.

<a id="current-source-producers"></a>

## Current source producers and packed values

Source permission is role-specific. A common converter or a compatible current
IR word does not automatically create source grammar for every role. Integer
and floating origin, logical type, physical carrier, exact-word versus numeric
conversion, declaration provenance, and supplied-source identity remain distinct.

The scalar F16/BF16, packed32 and vector source sections enumerate current
permissions for exactly their listed members. In
particular, [packed SETP](#resolved-ir-design--packed-setp-raw-and-exact-words),
[BF16 FMA/SET](#resolved-ir-design--ordinary-bf16-fmaset-source-members),
[scalar F16 atomics](#resolved-ir-design--scalar-f16-atomic-source-members),
[scalar BF16 atomics](#resolved-ir-design--scalar-bf16-atomic-source-members),
[packed atomics](#resolved-ir-design--packed32-atomic-source-members),
[scalar packed32 roles](#resolved-ir-design--scalar-packed32-source-members),
and [atomic vectors](#resolved-ir-design--atomic-vector-value-sources) specify
actual members and retain unchanged destination/modifier/target rules.

A bare unsigned direct `0f` can preserve a whole 32-bit word for exactly F16x2,
BF16x2, E4M3x4 and E5M2x4 in admitted roles. It does not perform component
conversion, splat, rounding, quieting or saturation. Decimal/F64 numeric
scalar-to-native-F16x2 construction remains reference-qualified: no unambiguous
one-to-many numeric packing constructor is adopted. Integer origins, signed or
wrapped exact-F32 expressions and truncated packed16 producers gain no permission.
E4M3x2/E5M2x2 have no adopted exact 16-bit lexical or numeric packing producer; valid
current low 16-bit typed words and register controls are a separate API fact.
See [exact packed words](#resolved-ir-design--packed32-exact-words-and-native-call-formals)
and [value-vector producers](#resolved-ir-design--floating-value-vector-source-contract).

<a id="retained-limitations"></a>

## Retained limitations and interpretation boundaries

| Boundary | Current contract |
| --- | --- |
| Absolute fixed variable placement | `FIXED-ADDRESS-PRODUCTION` remains an `ADOPTED_REFERENCE_GAP`: the fixed manual mentions placement without a determinate executable source production. No absolute-placement parser or allocator is claimed. Invented `.fixed`, `.address`, `@` and colon tails are illustrations, not official negative grammar oracles. [Exact boundary](#syntax-coverage--fixed-variable-addresses-adopted-reference-gap). |
| Packed numeric construction | Whole32-bit exact words are separate from scalar numeric packing; native F16x2 has no adopted ambiguous scalar-to-pair constructor. [Producer rules](#current-source-producers). |
| mbarrier report values | Qualified logical B8 model with same-size B8/S8/U8 carriers. The pinned manual does not settle a normative heterogeneous report width/type; later uint8 examples corroborate without closing the fixed-reference gap. [Token/report rules](#resolved-ir-design--mbarrier-carriers-and-reports). |
| `.target debug` | Zero modern section records **and** zero legacy records is a definite error. Nonzero records only pass absence checking. `D-DEBUG-DWARF-PRESENCE-RECOGNITION` remains a reference boundary; names, emptiness, labels or typed bytes do not certify actual DWARF. [Recognition boundary](#resolved-ir-design--target-debug-absence-and-recognition-boundary). |
| Optional tensor-map relations | MC-FACT17–21 (`STORE_BOUNDS`, `IM2COL_SHAPE`, `IM2COL_INFO`, `W_PROFILE`, `GATHER_SCATTER`) remain conservatively `Unresolved` after local checks even with every relevant supported caller fact known. These are not five complete relation solvers. [Exact local checks and missing relationships](#tensor-map-known-facts--conservative-whole-relation-boundary). |
| Matrix/manual interpretations | K96 encoded IDs versus physical layout, sparse-WS metadata placement, low-bit packing/transpose, type carriers and other fixed-reference conflicts keep their exact published restrictions and obligations. [Interpretations](#matrix-reference-interpretations). |
| Opaque descriptors and protocol | Caller facts are conditional assertions; no live descriptor-byte authentication, Tensor Memory allocation, collector history, source stability, dynamic protocol completion or GPU results follows. [TCGEN family](#tcgen-mma-family-coverage), [tensor facts](#tensor-map-known-facts). |

Outside the static frontend contract are runtime register/memory values, GPU
numerical results, dynamic whole-CFG or cross-thread protocol proofs, memory
allocation and physical addresses, launch feasibility/occupancy/physical ABI
packing, cross-object linking/relocation and external nondefault translator
behavior. Static address alignment or a normalized ABI record proves only its
stated static predicate. Call tables do not prove runtime pointer membership;
finite local may-call cycle checks do not build a complete dynamic CFG.

<a id="operation-families"></a>

## Instruction-family navigation

The sections below own the accepted forms, exclusions and target/type/shape
conditions; original family paths preserve links for existing readers.

- [Bitwise constant-expression signedness](#bitwise-constant-policy)
- [Control-flow Operand Syntax Design](#control-flow-syntax-design)
- [`abs` and `neg` floating-point coverage](#abs-neg-coverage)
- [Floating and mixed `add`/`sub` coverage](#add-sub-coverage)
- [PTX 9.3 `applypriority` and `discard` coverage](#applypriority-discard-coverage)
- [PTX 9.3 atomic and reduction coverage](#atomic-reduction-coverage)
- [CTA `barrier.sync`, `barrier.arrive`, and `barrier.red` coverage](#barrier-sync-coverage)
- [Integer Bit-Operation Coverage](#bit-operations-coverage)
- [Non-tensor bulk asynchronous data movement](#bulk-async-coverage)
- [Extended-precision integer coverage](#carry-coverage)
- [Conversion and Address-Query Coverage](#conversion-coverage)
- [PTX 9.3 non-bulk `cp.async` coverage](#cp-async-coverage)
- [PTX 9.3 `createpolicy` coverage](#createpolicy-coverage)
- [Floating `div` coverage](#div-coverage)
- [Fabric/CFT coverage](#fabric-coverage)
- [Ordinary `fence` coverage](#fence-coverage)
- [FMA Coverage](#fma-coverage)
- [Integer Arithmetic Coverage](#integer-arithmetic-coverage)
- [LD Coverage](#ld-coverage)
- [PTX 9.3 `ldu` coverage](#ldu-coverage)
- [Logic and Shift Coverage](#logic-shift-coverage)
- [Floating `mad` coverage](#mad-coverage)
- [Matrix fragment coverage](#matrix-coverage)
- [Matrix reference interpretations](#matrix-reference-interpretations)
- [`mbarrier.test_wait` qualifier coverage](#mbarrier-test-wait-coverage)
- [`mbarrier.try_wait` qualifier coverage](#mbarrier-try-wait-coverage)
- [`membar` coverage](#membar-coverage)
- [Floating `min`/`max` coverage](#min-max-coverage)
- [PTX 9.3 miscellaneous instruction coverage](#miscellaneous-coverage)
- [Block-scaled MMA selector operands](#mma-scale-selector-coverage)
- [MUL Coverage](#mul-coverage)
- [Multimem frontend coverage (PTX ISA 9.3)](#multimem-coverage)
- [PTX 9.3 `prefetch` and `prefetchu` coverage](#prefetch-coverage)
- [SELP Coverage](#selp-coverage)
- [SET Coverage](#set-coverage)
- [SETP Coverage](#setp-coverage)
- [SLCT Coverage](#slct-coverage)
- [ST Coverage](#st-coverage)
- [Stack manipulation coverage](#stack-manipulation-coverage)
- [Surface instruction coverage](#surface-coverage)
- [PTX Syntax Coverage](#syntax-coverage)
- [TCGEN descriptor field and layout coverage](#tcgen-descriptor-coverage)
- [Non-WS activation collector and ashift](#tcgen-mma-a-collector-coverage)
- [TCGEN dense f16 MMA coverage](#tcgen-mma-coverage)
- [Dense unscaled f8f6f4 TCGEN MMA coverage](#tcgen-mma-f8f6f4-coverage)
- [TCGEN MMA frontend family coverage](#tcgen-mma-family-coverage)
- [Dense i8 TCGEN MMA coverage](#tcgen-mma-i8-coverage)
- [Dense MX4 block-scaled TCGEN MMA](#tcgen-mma-mx4-coverage)
- [Dense MX8 TCGEN MMA coverage](#tcgen-mma-mx8-coverage)
- [MX activation collector](#tcgen-mma-mx-a-collector-coverage)
- [Dense MX NV four-bit block-scaled TCGEN MMA](#tcgen-mma-mxnv-coverage)
- [Ordinary sparse TCGEN MMA](#tcgen-mma-sparse-coverage)
- [Sparse block-scaled TCGEN MMA](#tcgen-mma-sparse-mx-coverage)
- [TCGEN dense tf32 MMA coverage](#tcgen-mma-tf32-coverage)
- [Dense weight-stationary TCGEN MMA](#tcgen-mma-ws-dense-coverage)
- [Sparse weight-stationary TCGEN MMA](#tcgen-mma-ws-sparse-coverage)
- [TCGEN specialized synchronization](#tcgen-sync-coverage)
- [Tensor-map and TMA frontend closeout matrix](#tensor-async-closeout)
- [Tiled tensor asynchronous data movement](#tensor-async-coverage)
- [Tensor Memory allocation coverage](#tensor-memory-allocation-coverage)
- [Tensor Memory copy and shift](#tensor-memory-copy-shift-coverage)
- [Tensor Memory named-mask boundary](#tensor-memory-coverage)
- [Tensor Memory register transfers](#tensor-memory-transfer-coverage)
- [`testp` and `copysign` coverage](#testp-copysign-coverage)
- [Texture and opaque resource coverage](#texture-coverage)
- [Floating transcendental coverage](#transcendental-coverage)
- [Floating reciprocal and square-root coverage](#unary-float-coverage)
- [PTX 9.3 video instruction coverage](#video-coverage)
- [PTX 9.3 shuffle and vote coverage](#warp-sync-coverage)
- [PTX 9.3 WGMMA coverage](#wgmma-coverage)
- [Declaration Semantics Design](#declaration-semantics-design)
- [PTX Lexer Design](#lexer-design)
- [Parameter declarations](#parameter-declarations)
- [Python Generator Model Design](#python-generator-model)
- [Register declaration compatibility](#register-type-policy)
- [C++ Resolved IR Design](#resolved-ir-design)
- [Resolved storage declarations](#storage-declarations)
- [Symbol Binding Design](#symbol-binding-design)
- [CST and Syntax AST design](#syntax-ast-design)
- [Tensor async cache controls](#tensor-cache-controls)
- [Caller-known tensor-map facts](#tensor-map-known-facts)
- [YAML Instruction Spec: Structure and Principles](#yaml-instruction-spec)

<a id="bitwise-constant-policy"></a>

## Bitwise constant-expression signedness

Technical reference and evidence: [bitwise_constant_policy.md](us-en/bitwise_constant_policy.md).

<a id="bitwise-constant-policy--specification-conflict-and-decision"></a>

### Specification conflict and decision

[PTX ISA 9.3 section 4.5.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constant-expression-evaluation)
specifies usual arithmetic conversions for binary bitwise operators, whereas
[the summary table in section 4.5.6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#summary-of-constant-expression-evaluation-rules)
lists unsigned operands/results. The frontend follows the detailed prose,
supported by the offline assembler observations below, rather than the table.
This is an explicit interpretation of inconsistent documentation, not a claim
that NVIDIA has issued a specification correction.


<a id="bitwise-constant-policy--evaluation-and-use-conversion"></a>

### Evaluation and use conversion

The evaluator retains both the 64-bit result and its signedness until the
consumer applies its existing destination conversion. Mixed inputs follow the
same rule in either operand order. For example, `(-4 & -1) >> 1` is signed and
produces `fffffffffffffffe`, while `(-4U & -1) >> 1` is unsigned and produces
`7ffffffffffffffe`. Unary `~0` is unsigned even though `(-1 | 0)` is signed;
both have the same all-ones payload.

`constantIntegerValue` and `numericConstantValue` expose this expression type
before use conversion. Equal-width `.u64` storage and `mov.u64` can preserve the
same bits from either signed or unsigned expressions. Storage words alone do
not recover the expression type. The normalized immediate's `is_negative` and
`integer_source_bits` describe the evaluated source sign and bits; they are not
a complete expression-type cache. No destination type changes how an earlier
binary expression is evaluated. Opcode `and`, `or`, and `xor` operand rules are
independent of this constant-expression policy.

The semantic regression asserts exact bits and type for all signedness pairs,
comparison, arithmetic and right-shift observers, casts, and unary complement.
The resolved regression compares evaluator, instruction and storage values,
then validates copies, assignments and cloned instructions after the module
source and AST have been destroyed. Existing source-sign rejection tests remain
separate controls. These frontend tests do not require a new assembler run.


<a id="control-flow-syntax-design"></a>

## Control-flow Operand Syntax Design

Technical reference and evidence: [control_flow_syntax_design.md](us-en/control_flow_syntax_design.md).

<a id="control-flow-syntax-design--descriptor-and-resolved-ir-boundary"></a>

### Descriptor and Resolved IR boundary

`brx.idx{.uni} index, tlist` is a separate PTX 6.0 / SM 30 opcode. Its index
is a `.u32` register and its `tlist` resolves as `ResolvedBranchTargetSet`,
which retains the current-function `.branchtargets` `SymbolId`; standalone
resolution retains the spelling. It does not expand target entries or build a
control-flow graph. In a module, the `.branchtargets` declaration must occur
earlier in the same function's lexical traversal; its member labels may still
refer forward. Direct `bra` targets remain forward-referenceable. `bra` remains
direct-only.

Indirect forms use separate `IndirectCall` layouts rather than reusing either
the direct `Call` layouts or `Flat`: target plus metadata, target plus input
group plus metadata, and return group plus target plus input group plus
metadata. Both the register target and the final metadata operand resolve as
`ResolvedIndirectCallee`; layout slot shapes distinguish `CallTarget` from
`CallTargetSet`. These layouts require PTX 2.1 and SM 20. The public modifier
variant remains `call_direct` for compatibility.


<a id="control-flow-syntax-design--call-table-flist-arrays"></a>

### Call-table flist arrays

The existing indirect-call layouts also admit a final storage name, for example
`call %fp, (1+2), table;`. `ResolvedCallTableRef` is a distinct indirect-callee
branch retaining the actual variable `SymbolId`, written name and range; storage
is not relabeled as metadata. The module owns one `ResolvedCallTableContract`
per consumed table and reuses its common full function signature for the normal
return/input ABI and formal-driven literal/expression checks.

Supported tables are initialized one-dimensional scalar `.u32/.u64` arrays in
`.global/.const`, at module scope or a visible local scope. Inferred extent is
supported, and element width need not match function-pointer width. Explicit
entries must be complete unmasked zero-addend device-function relocations.
Order and duplicate targets survive; omitted tails are zero-fill, not callable
slots. At least one callable entry is required. Numeric/mixed, masked/addended,
nonfunction, vector, multidimensional, parameter and extern-only tables remain
unsupported as flist, without invalidating unrelated ordinary data.

The table declaration must precede the call. Function references retain the
existing exact-token prior-declaration rule, including aliases and compatible
prototype/definition identity. Target signatures must agree in all normalized
ABI fields and `.noreturn`. This proves neither loaded-pointer membership nor
mutable runtime contents. Stored ptxas 13.3.33 compile-only evidence accepts
partial/duplicate tables but crashes on mixed function/numeric call controls;
the frontend diagnoses that unsupported subset, with no assembler-parity or
GPU-execution claim.


<a id="control-flow-syntax-design--return-uniformity"></a>

### Return uniformity

`ret{.uni};` accepts unpredicated, `@p`, and `@!p` forms in both device
functions and entries, with the existing PTX 1.0 / all-architecture availability.
The canonical `ret_bare` variant and generated `RetBare` class remain stable.
Its `WithLocs<bool> uni` field defaults to `false` with no locations; a written
`.uni` sets it to `true` and retains the exact modifier range. The execution
predicate is retained independently, including its negation and bound register
identity, and both survive AST release for owned-module validation.
`.uni` records the programmer's guarantee of uniform return behavior. The
frontend does not prove dynamic divergence or convergence. Unknown or repeated
modifiers and unexpected operands remain invalid.


<a id="control-flow-syntax-design--function-local-callprototype-syntax"></a>

### Function-local `.callprototype` syntax

The parser now retains PTX 9.3 `.callprototype` declarations as dedicated
function-body CST/AST nodes rather than a label followed by an instruction.
All four signature forms are accepted: `_`, `_ (params)`, `(return) _`, and
`(return) _ (params)`. CST preserves the label, colon, sink, parameter-list
punctuation, `.noreturn`, `.abi_preserve N`, and `.abi_preserve_control N`.
AST retains their semantic spellings and source ranges. Declaration semantics
rejects `.noreturn` with return parameters and validates array formals; parsing
at module scope is rejected. Binding owns the local label. Current indirect-call
layouts consume that bound prototype identity and reuse the shared call ABI
contract; see [Resolved IR](#resolved-ir-design) for entry points and limits.

Current owned and supplied prototype contracts independently enforce base
PTX 2.1 / SM 20, `.noreturn` PTX 6.4 / SM 30, and both ABI suffixes PTX 9.0 /
SM 80, including independently known partial context. Nested and unused prototypes
are included; ordinary instruction availability and runtime ABI behavior are not
expanded by these checks.


<a id="control-flow-syntax-design--function-local-calltargets-syntax"></a>

### Function-local `.calltargets` syntax

PTX 9.3 `.calltargets` is likewise a dedicated function-body CST/AST node.
It retains its label, colon, directive, non-empty ordered function-name list,
commas, semicolon, and whole/member source ranges. The parser rejects an empty
list, a trailing comma, and use without a local label or at module scope.
Declaration semantics requires every member to be a prior device `.func`,
diagnoses duplicates using individual member ranges, and requires equal
canonical signatures.

`.branchtargets` and `.calltargets` each require PTX 2.1 and SM 20, including
unused declarations in nested blocks. AST-free validation uses the function's
selected owned region; source-aware validation checks both current records and
supplied typed declarations with the supplied active context. Known version and
recognized profile minima are checked independently before full-context gates;
only unknown prerequisites defer under `AvailableContext`. Strict context and
unknown-target diagnostics, nonneutral payload identity, member binding,
signatures and declaration-order checks retain their existing contracts.


<a id="control-flow-syntax-design--function-local-branchtargets-syntax"></a>

### Function-local `.branchtargets` syntax

`.branchtargets` now has its own function-body CST/AST node. Its non-empty
ordered list preserves ordinary labels and compact entries such as `N<5>`;
the latter retain name, count, angle punctuation, and one entry range without
expanding into synthetic labels. Declaration semantics checks local-label
membership and count validity without adding symbols. Repeated destinations,
including overlapping compact entries, retain their order and multiplicity;
each expanded occurrence denotes a separate index slot.
`brx.idx` consumes the declaration by stable local identity without expanding
its entries.


<a id="control-flow-syntax-design--ptx-93-call-parameter-context"></a>

### PTX 9.3 call parameter context

`ld` accepts `.param`, `.param::entry`, and `.param::func`; `st` accepts
`.param` and `.param::func`. `::entry` addresses only an entry's formal input.
`::func` addresses a device-function parameter or a function-local call
parameter. The unqualified spelling defaults to entry parameters in an entry,
to function parameters in a device function, and to function-local call
parameters in either context.

Only function-local `.param` variables are call staging. Their `st.param` input
stores must be unpredicated and form the contiguous block immediately before a
call that passes the same variable; their `ld.param` return loads must be
unpredicated and form the contiguous block immediately after a call returning
the same variable. Nonexecuting declarations, labels, metadata, and lexical
braces are transparent to instruction adjacency. Intervening executable
instructions break a block; binding and scope rules still require the same
visible parameter object/member. These local checks do not infer runtime path
initialization.
`call` itself may be predicated. `.uni` is retained as the programmer's
uniformity assertion, so the frontend does not attempt unprovable static
uniformity analysis or reject a predicated direct `call.uni`.

Register-state device and call-prototype formals have a separate default-ABI
minimum: when known PTX >=2.0 and the active recognized target is SM20+, the whole
scalar/vector value must be at least32bits. This measures explicit vector total
width, not each lane, and preserves source type/shape. It applies to unused input
and return formals too, with the supplied AST context for source-aware retargeting
and owned regions after AST death. Known legacy contexts disable the minimum;
missing context defers it under partial checking. Ordinary small/predicate registers
and compatible small actuals to `.param` formals remain legal. See
[parameter declarations](#parameter-declarations) for the complete boundary.

A local `ResolvedCallPrototypeContract` describes a device-call interface, so its
owned `signature.is_entry` must remain false. AST-free module validation checks
this role even when the prototype is unused, including return-only interfaces
whose types and widths are otherwise valid. Module/function copies and instruction
clones preserve prototype identity. This metadata-integrity rule is independent
of formal ABI width, feature availability and supplied-source association; no
source spelling/history is reconstructed to validate the flag.


<a id="control-flow-syntax-design--static-recursion-boundary"></a>

### Static recursion boundary

Known local call cycles require PTX 2.0 and SM 20. Acyclic device functions remain
legal on supported older contexts. Both module validators recompute a private
current typed may-call graph over canonical local device definitions, including
unused definitions. Actual direct calls create exact edges; only consumed,
validated finite `.calltargets` or call-table alternatives create candidate edges.
Unused metadata and implicit zero table tails create no edges. A cycle requiring
a finite alternative is statically possible recursion, not proof it executes.
Predicated calls remain may-call edges; no CFG, predicate feasibility, stack
execution or dataflow analysis is performed.

Each participating caller uses its selected region, or supplied active context
in the source-aware API. Known old PTX or recognized low SM rejects independently
of the other missing fact; unknown prerequisites alone may defer in
`AvailableContext`. Strict context and unknown-target handling remain unchanged.
Prototype-only/external callees and dynamic signature-only calls are open
boundaries. They gain no fabricated return edges and no blanket strict rejection;
success means modeled checks passed, not whole-program acyclicity across them.


<a id="control-flow-syntax-design--historical-entry-body-inputs"></a>

### Historical entry body inputs

PTX 1.0–1.3 entry body `.param` declarations have the owning `LegacyEntryInput`
role in the sole `parameter_declarations` table, including scoped/group/array
identities. The function signature remains syntactic header ABI only, so an empty
header signature does not imply no host inputs. Lexical metadata order assigns
no launch addresses or packing. Resolution needs a version only for ambiguous
entry body roles; it reports `MissingValidationContext` without guessing from SM.
Typed modules retain their role when context is missing.

The optional `ResolvedSymbolRef::parameter_role` cache is derived from the actual
record while keeping structural `CallParameter`. Address/MOV/tensor projections
share input/return/call-slot directions. Both validators rejoin current records
and caches and enforce read-only entry-frame access, direct call-actual exclusion,
shape/scope and logical capacity before context skips. Legacy input LD is not
call-return staging; MOV preserves `.param`. Modern BodyLocal staging is unchanged.
The checked sum of per-member bytes times compact count has the known32764-byte
global ceiling; no double axes/vector multiplication or invented256 legacy cap.
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` remains qualified for exact historical
physical launch packing. See the [parameter contract](#parameter-declarations)
for context windows, intrinsic guarantees and preserved limitations.


<a id="control-flow-syntax-design--indirect-function-address-carriers"></a>

### Indirect function-address carriers

An indirect function address uses a General `.reg` effective scalar with a
fundamental signed, unsigned or bit type of 8, 16, 32 or 64 bits. Floating types,
packed/alternate instruction-only types, `.b128`, predicates, special registers
and whole vectors do not satisfy this role. A selected ordinary component uses
the same element-type rule and retains its real owner identity, selector ranges
and the 128-bit vector-declaration limit. Written call components use the same invariant as typed public register-component values.

This is a **derived** address-carrier interpretation of
[PTX §6.4.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#addresses-as-operands)
and the function-address-register requirement in
[§9.7.13.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-call),
not a claim that `call` explicitly enumerates all twelve carrier types. Address
value widths of 32/64 bits do not impose a 32-bit register minimum or require a
64-bit register under `.address_size 64`. Eligibility does not prove that a
narrow register actually contains a representable function address, a linked
code address or a member of a runtime target set.

Source resolution, direct typed checks and both module validation APIs apply
the role before missing version/target can defer it. Owned checks rejoin actual
symbol declarations and reject lying or erased type caches; valid supplied
source cannot mask bad current data. Generated views borrow the active RegRef
alternative without reinterpreting metadata/table alternatives. Completely
unknown standalone facts retain their existing unknown/context behavior.
Formal input/return ABI rules and ordinary floating call inputs remain separate.


<a id="control-flow-syntax-design--selected-scalar-call-operands"></a>

### Selected scalar call operands

Call inputs and the single parenthesized return accept ordinary `.xyzw/.rgba`
register selections. The target accepts an identifier or an explicitly selected
indirect register; final prototype/target-set/table metadata stays identifier-only.
Anonymous brace actuals and direct function projections remain unsupported.
`ResolvedCallParameterRef::component` is a default-disengaged owning optional;
null preserves the whole-variable contract and old aggregate construction.

Source and both owned-module validation routes first join the real General
`.reg` v2/v4 declaration, SymbolId, parameterized member, element type/state,
physical 128-bit owner bound and explicit selector provenance. Only then does
the existing call ABI comparison see an effective scalar. Arrays, pointer,
size and alignment constraints are retained. Deleting or contradicting a cache
cannot hide a known declaration. The return's outer parenthesized range stays
in `WithLocs`; its selected leaf remains contained inside the group and instruction.
Copied observer values, module copies and instruction clones own the selection.

Modern register-formal minimum width remains a formal ABI rule, not a global
component minimum: matching legal subword `.param` formals use the existing
scalar actual policy. Whole named-vector actuals, constants and parameter-memory
staging retain their existing rules. This is derived scalar-element inheritance,
not pointer execution, hidden moves or synthetic scalar declarations. Coherent
current edits remain legal; written source identity is checked independently.
Public CST/AST variants and the owning call reference require matching producer
and consumer rebuilds. No instruction schema, generated layout or callback changes.


<a id="abs-neg-coverage"></a>

## `abs` and `neg` floating-point coverage

Technical reference and evidence: [abs_neg_coverage.md](us-en/converage/abs_neg_coverage.md).

<a id="abs-neg-coverage--abs-and-neg-floating-point-coverage"></a>

### `abs` and `neg` floating-point coverage

This slice models the floating forms of [PTX 9.3 `abs`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-abs), [half/bfloat `abs`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-abs), [PTX 9.3 `neg`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-neg), and [half/bfloat `neg`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-neg). They use the generated YAML, typed Resolved IR, and target-aware checker path; the frontend does not execute the operations.

| Form | Minimum | Source and storage contract |
| --- | --- | --- |
| `abs{.ftz}.f32`, `neg{.ftz}.f32` | PTX 1.0 / all target architectures; `.ftz` PTX 1.4 | A floating literal or a same-width `.f32`/`.b32` register source; the destination is a same-width `.f32`/`.b32` register. |
| `abs.f64`, `neg.f64` | PTX 1.0 / SM 13 | A floating literal or a same-width `.f64`/`.b64` register source; the destination is a same-width `.f64`/`.b64` register. |
| `abs{.ftz}.f16`, `abs{.ftz}.f16x2` | PTX 6.5 / SM 53 | Register-only. Scalar `.f16` accepts `.f16` or `.b16`; packed `abs.f16x2` accepts `.f16x2` or `.b32`. |
| `neg{.ftz}.f16`, `neg{.ftz}.f16x2` | PTX 6.0 / SM 53 | Scalar `.f16` retains its register role. Packed `neg.f16x2` accepts native `.f16x2` or `.b32` registers and the exact-word source in [scalar packed roles](#resolved-ir-design--scalar-packed32-source-members). |
| `abs`/`neg` `.bf16`, `.bf16x2` | PTX 7.0 / SM 80 | Register-only and no `.ftz`; scalar requires exact `.b16`, packed requires exact `.b32`. |

Integer literals, wrong-width or integer register containers, unsupported modifiers, extra operands, and destination sinks are rejected. The checker keeps the base FP32 instruction minimum at PTX 1.0 and applies the PTX 1.4 requirement only when the optional `.ftz` value is present. CUDA 13.3.73 `ptxas` directly reports that `.ftz` needs PTX 1.4 at `.version 1.3`; that assembler cannot demonstrate the historical SM minima, which are checked by the frontend target tests.

The recorded CUDA 13.3.73 `sm_90` probe command was `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`. Its 146 modules cover modern positive forms, FP literals, physical containers, forbidden modifiers, source/destination mismatches, and sink rejection. Table 9 explicitly admits native `.f16x2` or `.b32` register declarations; instruction descriptions of bit storage do not narrow that closed carrier set. Other same-width types and alternate formats remain excluded.

[Explicit floating MAD](#mad-coverage), [DIV](#div-coverage), [reciprocal/square-root forms](#unary-float-coverage), [floating transcendentals](#transcendental-coverage), and [floating MIN/MAX](#min-max-coverage) are now modelled separately. [Floating and mixed ADD/SUB](#add-sub-coverage) reconciles the remaining §9.7.3–§9.7.5 cohorts. None of those operations gains execution semantics here.


<a id="add-sub-coverage"></a>

## Floating and mixed `add`/`sub` coverage

Technical reference and evidence: [add_sub_coverage.md](us-en/converage/add_sub_coverage.md).

<a id="add-sub-coverage--floating-and-mixed-addsub-coverage"></a>

### Floating and mixed `add`/`sub` coverage

This slice is the [PTX 9.3 §9.7.3.3/§9.7.3.4, §9.7.4.1/§9.7.4.2 and §9.7.5.1/§9.7.5.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions) reconciliation for [`add`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-add) and [`sub`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sub). It **audits** the existing scalar, packed, half/bfloat, and mixed-precision cohorts against assembler evidence rather than adding a second implementation. Only one confirmed contract gap was found, and it is fixed here.

| Form | Audited contract |
| --- | --- |
| `add{.rnd}{.ftz}{.sat}.f32 d, a, b` | Destination register; both sources admit a floating literal. `.sat` legal, no `.ftz` or `.sat` on the FP64 sibling. `.rn`/`.rz` ungated; `.rm`/`.rp` PTX 1.0 / SM 20. |
| `add{.rnd}{.ftz}.f32x2 d, a, b` | PTX 8.6 / SM 100. Operands are **exactly `.b64`**; no `.sat`, no immediates. |
| `add{.rnd}.f64 d, a, b` | PTX 1.0 / SM 13. Both sources admit a floating literal; no `.ftz` or `.sat`. |
| `add{.rn}{.ftz}{.sat}.{f16,f16x2} d, a, b` | PTX 4.2 / SM 53. RN-only; `.f16` accepts `.f16` or `.b16`, `.f16x2` accepts `.f16x2` or `.b32`. |
| `add{.rn}.{bf16,bf16x2} d, a, b` | PTX 7.8 / SM 90. RN-only, no `.ftz` and no `.sat`; binds the exact `.b16`/`.b32` bit containers. |
| `add{.rnd}{.sat}.f32.{f16,bf16} d, a, c` | PTX 8.6 / SM 100. The **narrow source sits in the middle position** and accepts F16/BF16 numeric constants as well as registers; the FP32 addend is immediate-capable. |

`sub` carries the mirrored table.


<a id="add-sub-coverage--confirmed-gap-and-fix"></a>

### Confirmed gap and fix

The mixed-precision **FP32 addend/subtrahend** was declared register-only, rejecting legal spellings such as `add.f32.f16 %f0, %h1, 1.0;` and `sub.f32.bf16 %f0, %b1, 1.0;`. The assembler accepts them, so this was a false reject in a source position the manual leaves immediate-capable. The FP32 operand now admits a floating literal while the destination stays a register. The narrow source now follows the scalar format conversion contract below.


<a id="add-sub-coverage--audited-and-confirmed-already-correct"></a>

### Audited and confirmed already correct

These were the likely defect sites, and probing showed the model already agrees with the assembler. Recording them matters as much as the fix, because they are the parts a future reader would otherwise re-suspect:

- The shared `$binary_float_register` pattern is **not** merely width-sensitive. BF16 rejects `.f16` and BF16x2 rejects `.f16x2`, matching the assembler's `Arguments mismatch`.
- `f32x2` binds exactly `.b64`. `.f64` and `.u64` are rejected by both the assembler and the frontend, so the semantic packed suffix does not admit arbitrary same-width registers. `.reg .f32x2` is not a declaration format at all.
- Scalar FP32/FP64 admit floating literals in both source positions and reject literal destinations.
- Mixed operand order is enforced: the narrow source must be the middle operand.
- Half is RN-only but admits `.ftz` and `.sat`; BF16 admits neither and is RN-only; FP64 and `f32x2` admit no saturation.


<a id="add-sub-coverage--installed-surface-change"></a>

### Installed-surface change

Opening the addend to immediates changes the installed generated payload of two public members: `Add::MixedF32::addend` and `Sub::MixedF32::subtrahend` move from a register reference to the existing register-or-immediate representation. Consumers read them differently:

```c
mixed.addend.value.spelling;                              // before
std::get<ResolvedRegisterRef>(mixed.addend.value).spelling;  // after, register spelling
std::get<ResolvedImmediate>(mixed.addend.value).bits;        // after, immediate payload
```

These public member changes introduced an explicit API break in the installed C++ package; see the [current package version](#package-version). Consumers must migrate to the register-or-immediate variant access shown above. No compatibility shim or parallel representation was introduced: the change reuses the same typed register/immediate path that scalar `add`/`sub` and the DIV cohorts already use. The destination keeps its register payload; the narrow source now uses RegOrImm as described below.


<a id="add-sub-coverage--evidence"></a>

### Evidence

CUDA 13.3.73 `ptxas` evidence used `/usr/local/cuda/bin/ptxas -arch=<arch> <module>.ptx -o <temporary>.o`. The 49-module matrix had 27 expected accepts, 22 rejects, and no assembler divergence. Version floors were confirmed in both directions: `add.bf16` rejects `.version 7.5` with `Feature 'add.bf16' requires PTX ISA .version 7.8 or later`, and `add.f32x2` and the mixed form reject `.version 8.5` with the corresponding `requires PTX ISA .version 8.6 or later`. Target floors were confirmed at `sm_90`, where both the packed and mixed cohorts report `requires .target sm_100 or higher`.

One floor is **not** assembler-verifiable: this `ptxas` does not define `sm_53` as a `-arch` value at all, so the half cohort's PTX 4.2 / SM 53 minimum rests on the manual and on frontend checker tests rather than on assembler evidence. That is a toolchain limitation, not a frontend contract difference.

The existing MUL and FMA regression, including mixed FMA, `.f32x2`, ReLU/OOB and the documented assembler-compatibility exceptions, is unchanged.

The Issue 142 reconciliation now covers every cohort in §9.7.1 through §9.7.5 that this issue scoped. No operation here gains execution semantics.

The narrow F16 source also accepts floating constants through binary16 RNE conversion; the destination remains a register and the wide addend/subtrahend keeps its F32 conversion. Shared F16/BF16 classes and modifier domains remain intact. The shared narrow BF16 source now accepts decimal, evaluated F64, exact F64 and bare unsigned exact F32 numeric inputs through direct binary64-to-BF16 RNE conversion. Its typed word `1.5 -> 0x3fc0` is independent of runtime rounding and saturation; the other input remains F32 (`1.5 -> 0x3fc00000`). No opcode source permissions or generated fields change.


<a id="applypriority-discard-coverage"></a>

## PTX 9.3 `applypriority` and `discard` coverage

Technical reference and evidence: [applypriority_discard_coverage.md](us-en/converage/applypriority_discard_coverage.md).

<a id="applypriority-discard-coverage--ptx-93-applypriority-and-discard-coverage"></a>

### PTX 9.3 `applypriority` and `discard` coverage

The frontend supports both documented spellings of the L2 cache-range
operations in PTX ISA 9.3 §§9.7.9.17–18. Both require PTX 7.4, SM 80, a
128-byte-aligned address, and the immediate size `128`.

| Operation | Generic spelling | Explicit-global spelling |
| --- | --- | --- |
| Normal eviction priority | `applypriority.L2::evict_normal [a], 128` | `applypriority.global.L2::evict_normal [a], 128` |
| Discard range | `discard.L2 [a], 128` | `discard.global.L2 [a], 128` |

The omitted `.global` suffix uses generic addressing. Its runtime address must
fall in the global window; otherwise PTX defines the behavior as undefined.
The frontend accepts an address register with unknown provenance and rejects a
known non-global symbol. Both spellings require provable 128-byte alignment
when the address is statically known. These instructions express cache behavior;
the frontend checks their syntax and typed operands without executing them.


<a id="atomic-reduction-coverage"></a>

## PTX 9.3 atomic and reduction coverage

Technical reference and evidence: [atomic_reduction_coverage.md](us-en/converage/atomic_reduction_coverage.md).

<a id="atomic-reduction-coverage--ptx-93-atomic-and-reduction-coverage"></a>

### PTX 9.3 atomic and reduction coverage

The frontend parses, resolves, and checks synchronous scalar and vector `atom` and `red`, plus two `red.async` modes,
operation/type pairs from [PTX ISA 9.3 §9.7.14.5–6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-atom).
It checks operand shape and type, known address provenance, natural alignment,
and target availability. It does not execute atomic operations.

| Operation/type pairs | `atom` | `red` | Base PTX / SM floor |
| --- | --- | --- | --- |
| `add/min/max.{u32,s32}`, `inc/dec.u32`, `and/or/xor.b32` | Yes | Yes | `atom.global`: 1.1 / 11; `red.global`: 1.2 / 11 |
| `exch/cas.b32` | Yes | No | 1.1 / 11 |
| `add.u64` | Yes | Yes | 1.2 / 12 |
| `exch/cas.b64` | Yes | No | 1.2 / 12 |
| `min/max.{u64,s64}`, `and/or/xor.b64` | Yes | Yes | 3.1 / 32 |
| `add.f32` | Yes | Yes | 2.0 / 20 |
| `add.f64` | Yes | Yes | 5.0 / 60 |
| `cas.b16` | Yes | No | 6.3 / 70 |
| `cas/exch.b128` | Yes | No | 8.3 / 90; explicit `.sys` requires PTX 8.4 |
| `add.noftz.f16x2` | Yes | Yes | 6.2 / 60 |
| `add.noftz.f16` | Yes | Yes | 6.3 / 70 |
| `add.noftz.{bf16,bf16x2}` | Yes | Yes | 7.8 / 90 |
| `v2/v4/v8.{f16,bf16}.{add,min,max}.noftz` | Yes | Yes | 8.1 / 90 |
| `v2/v4.{f16x2,bf16x2}.{add,min,max}.noftz` | Yes | Yes | 8.1 / 90 |
| `v2/v4.f32.add` | Yes | Yes | 8.1 / 90 |

Each scalar pair and vector operation/type cohort has one public variant; vector width is a typed modifier. Optional suffixes are independent: `atom`
accepts `.relaxed`, `.acquire`, `.release`, or `.acq_rel`; synchronous `red` accepts
`.relaxed` or `.release`. Both accept `.cta`, `.cluster`, `.gpu`, or `.sys`
scope. Omitted semantics and scope remain `MemoryConsistency::Omitted` and
`MemoryScope::None` in owned IR; their effective ISA defaults are relaxed and
GPU. Both qualifier orders used by the earlier explicit global forms remain
accepted. Explicit scope requires PTX 5.0 / SM 60, explicit semantics PTX 6.0 /
SM 70, and `.cluster` scope PTX 7.8 / SM 90. These floors apply independently,
including scope-only and semantics-only forms.

The written address qualifier is omitted (generic), `.global`, `.shared`,
`.shared::cta`, or `.shared::cluster`. `Atom::address_qualifier` and
`Red::address_qualifier` retain these five spellings as a typed
`AtomicAddressQualifier`, separately from the address operand's effective
state-space provenance. Generic addressing requires PTX 2.0 / SM 20 and a
known address must refer to global or shared memory. Ordinary `.shared` has a
PTX 1.2 / SM 12 floor; 64-bit shared `add` and atomic `exch/cas` require PTX
2.0 / SM 20. Written `::cta` requires PTX 7.8 / SM 30, and `::cluster` requires
PTX 7.8 / SM 90. An unqualified `.shared` suffix has effective `::cta` address
subspace, but retains a different written qualifier. Address subspace and
memory scope are independent; for example `atom.shared::cluster.cta.add.u32`
is accepted. Known address provenance must match an explicit global/shared
qualifier, and the checker revalidates owned IR after mutation.
The checker derives the allowed written qualifiers from each variant's
state-space modifier: synchronous vectors and async release reductions admit
generic/global, while shared-completion `red.async` admits generic or
`.shared::cluster`. Invalid enum values and out-of-domain mutations are rejected.
All `atom`/`red` bracketed address offsets, including the `red.async` barrier
address, use the signed 32-bit PTX source domain. The checker revalidates that
range in owned IR. Wider `mov` address relocations remain separate.

Eligible scalar and vector `atom` and `red` operations also accept
`.L2::cache_hint`. Scalar forms place it immediately before the type (after
`.noftz` for half and bfloat add); the ISA vector spelling places it before
`.vN.type`, as in `atom.global.add.noftz.L2::cache_hint.v2.f16`. The suffix requires a final 64-bit `cache_policy` register and
PTX 7.4 / SM 80. The owned IR keeps the written hint and selects a separate
typed operand layout for its policy. Cache hints allow explicit `.global` or
generic addressing. A known global address is accepted; an unknown-provenance
generic register address is accepted with the runtime obligation to point to
global memory. Explicit shared and known shared generic addresses are rejected.
`atom.cas` has no cache-hint form. A policy operand without the suffix, or the
suffix without its policy, is rejected.

Every scalar `atom` destination accepts either a compatible register or the `_` bit
bucket. The owned IR retains the bit bucket as an absent
`ResolvedRegisterOrSink::register_ref`; it remains an `Atom` instruction.
Integer and bitwise destinations otherwise use a compatible register.
Sources accept a compatible register or an integer immediate with ordinary
narrow conversion. `cas` takes compare and swap sources. Float `add` accepts
native `.f32`/`.f64` or equal-width `.b32`/`.b64` registers, decimal floating
literals, and `0f`/`0d` bit-pattern literals; integer literals are rejected.
Half and bfloat `add` require the written `.noftz` suffix. Scalar `.f16`
accepts `.f16` or `.b16` registers, and packed `.f16x2` accepts `.f16x2`
or `.b32`. BF16 register carriers remain `.b16`/`.b32` physical storage; the packed BF16x2 source has the logical format described below.
The `.b16` CAS form accepts compatible same-width registers for its destination
and register sources; `.b128` CAS/exchange use exact `.b128` registers. CAS always has four
operands and no cache hint. Addresses require natural two-, four-, eight-, or
sixteen-byte alignment. Float addition rounds
to nearest even. Global `.f32` atomics flush subnormal inputs and results to
sign-preserving zero; shared `.f32` and `.f64` atomics do not. These last rules
are ISA behavior notes, not simulated execution.

Vector forms require global memory: explicit `.global` or generic addressing. A
known shared address is rejected; an unknown generic register address is
accepted with the runtime obligation to point to global memory. `atom` has
brace-enclosed destination and source vectors; `red` has a brace-enclosed
source vector. Atom destination and source vectors have the same exact arity.
The PTX syntax places the operation, optional `.noftz`, and optional
`.L2::cache_hint` before `.vN.type`, for example
`atom.global.add.noftz.L2::cache_hint.v2.f16`. The existing
`.vN.type.operation` spelling remains accepted.
`.f16` lanes accept `.b16`, `.f16`, `.u16`,
or `.s16` registers; `.f32` lanes accept the corresponding 32-bit register
types. `.bf16` lanes require `.b16`; packed `.f16x2` lanes accept `.f16x2`
or `.b32`, while `.bf16x2` lanes require `.b32`.
The [PTX ISA 9.3 fundamental-type rule](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fundamental-types)
is narrower for floating scalar operands; the vector `atom`/`red` sections do
not explicitly define lane declaration types. The integer-lane allowance here
is scoped to vector `.f16`/`.f32`: CUDA 13.3 `ptxas` V13.3.73 with
`-O0 -arch=sm_90` assembled all eight `atom`/`red` combinations of `.f16`
with `.u16`/`.s16` and `.f32` with `.u32`/`.s32` in the
[reproducible PTX source](../examples/atomic_vector_register_types.ptx).
This establishes assembler acceptance, not GPU execution behavior.
Among register lanes, bit types are neutral, while integer and floating
register families cannot mix; immediate lanes are excluded from that test. The destination and source vectors are checked independently.
Without declarations, standalone resolution retains unknown lane types;
declaration-bound module resolution checks the stated register-type domains.
A destination lane may be `_`, while source lanes may be admitted registers or floating immediates and an
all-sink destination is invalid. Written lanes in a destination vector must
name distinct registers, including when parameterized declarations bind them;
read-only source vectors may repeat lanes. The
whole access requires vector length times element width alignment: for example
`v8.f16` needs 16 bytes. Atomicity applies to each scalar element, not to the
vector as one aggregate transaction. The half/bfloat vectors require `.noftz`;
`v2/v4.f32.add` does not accept it. Packed and `.f32` forms have no `v8` form.
A cache hint keeps the same final 64-bit policy layout and global-address rule.

`red.async` adds two disjoint typed modes under `Red::Async...` alternatives:

| Mode | Legal operation/type pairs | Form and target floor |
| --- | --- | --- |
| Shared completion | `inc/dec.u32`, `min/max.{u32,s32}`, `and/or/xor.b32`, `add.{u32,s32,u64}` | `red.async.relaxed.cluster{.shared::cluster}{.mbarrier::complete_tx::bytes}.{op}.{type} [a], b, [mbar]`; PTX 8.1 / SM 90 |
| Global release | `add.{u32,s32,u64,s64}` | `red.async{.mmio}.release.{gpu\|sys}{.global}.add.{type} [a], b`; PTX 8.7 / SM 100 |

The optional address suffix is retained independently as `Red::address_qualifier`:
omitted generic or `.shared::cluster` for shared completion, and omitted generic
or `.global` for global release. The destination `a` must use a register base,
optionally followed by a signed 32-bit offset. Direct symbol and immediate
bases are rejected for both the destination and mbarrier addresses. Known
shared-mode destination and mbarrier addresses must be shared; unknown generic
register addresses carry a runtime shared-cluster obligation. Known release-mode
destinations must be global; unknown generic register addresses carry a runtime
global obligation. Destinations require natural 4- or 8-byte alignment, and
the mbarrier address requires 8-byte alignment. `.mmio` is available only with
release `.sys`; release `.gpu` has no MMIO form.

For relaxed mode, the specific PTX Conditions clause supplies
`.mbarrier::complete_tx::bytes` when completion is omitted; it is also the only
permitted explicit mechanism. The third `[mbar]` operand remains required in
both forms. This specific default takes precedence over the schematic syntax
that prints completion as mandatory and the generic Qualifiers prose describing
end-CTA completion on omission. Release mode remains a separate two-operand
form and forbids explicit completion. Historical assembler rejection of relaxed
omission is supplemental tool divergence, rather than the frontend grammar.
The frontend cannot prove that the shared destination and initialized barrier
are in the same remote CTA, or execute reduction/transaction/synchronization
semantics; those remain runtime obligations.

Other synchronous scalar operation/type pairs, including `.add.s64`, 64-bit `.inc/.dec`,
typed 64-bit bitwise suffixes, scalar float `.min/.max`, and `red.cas`/`red.exch`,
remain unsupported. Multimem forms are outside this coverage.

For the [current package version](#package-version), consumers must rebuild
against matching installed headers and library. The 83 synchronous operation/type and vector-cohort alternatives replace the
former 92 legacy/`.relaxed.cta` alternatives. For example, both
`atom.global.add.u32` and `atom.relaxed.cta.global.add.u32` now use
`Atom::GlobalAddU32`; inspect its `semantics`, `scope`, and `state_space`
fields together with `Atom::address_qualifier` to recover the written form.
Non-CAS synchronous scalar and vector variants now expose `NoHintOperands` and `WithPolicyOperands`
through their `operands` field; the selected layout keeps the final policy
register distinct from the ordinary source. CAS fields remain direct. Asynchronous
reductions have separate `Red::AsyncShared...` and `Red::AsyncRelease...`
alternatives with three and two operands respectively; inspect the retained
address qualifier, completion, scope, and MMIO fields instead of assuming a
synchronous layout. The 16 asynchronous alternatives are appended under `Red`.
The Python wheel includes the matching packaged YAML and generated model
contract.

The twelve shared classes (`RedAsyncSharedIncU32`, `RedAsyncSharedDecU32`,
`RedAsyncSharedMinU32`, `RedAsyncSharedMinS32`, `RedAsyncSharedMaxU32`,
`RedAsyncSharedMaxS32`, `RedAsyncSharedAndB32`, `RedAsyncSharedOrB32`,
`RedAsyncSharedXorB32`, `RedAsyncSharedAddU32`, `RedAsyncSharedAddS32`,
`RedAsyncSharedAddU64`) now expose instance `WithLocs<bool> completion` instead
of a static constant. Read `object.completion.value` for the effective mechanism
(always true), and `.locs` for written presence: omission has empty locations,
explicit spelling retains its token. False is outside this singleton domain,
even without a token. Ordinary two-valued optional flags retain their existing
value/token consistency rule. Generated C++ layouts change and require consumer
rebuilding; the four release classes and all three shared operand payloads stay
unchanged. Clearing a true completion's locations is a valid current IR edit;
association with a supplied AST independently checks retained source identity.

[Default tests](../submod/resolved_ir/test/test_red_async_default.cpp)
cover all 48 shared modifier products and 64 release candidates (24 allowed,
40 rejected), target/version boundaries, singleton-domain mutations and actual
AST-dead copies/clones. The [installed consumer](../examples/resolved_ir_consumer/main.cpp)
reads all twelve migrated fields and retains a release control without source
storage. These checks do not prove initialized barriers or runtime completion.


<a id="atomic-reduction-coverage--atomic-vector-value-sources"></a>

### Atomic vector value sources

The 26 existing `AtomVector*`/`RedVector*` classes expose `src` as
`WithLocs<ResolvedValueVector>` in both preserved layouts. Consumers must rebuild
against matching headers/libraries and inspect each `RegOrImm` lane with
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`. Destinations
remain register vectors; optional B64 cache policies and exact class identities,
field names, arity/type domains, qualifiers and target gates retain their contracts.
The source clauses remain brace-only: no whole named vector or source sink grant.

All 128 type/arity/layout branches accept floating immediate lanes, mixed with
existing physical register carriers where appropriate. The 80 F16/BF16/F32 numeric
branches reuse the existing logical converter; the 48 F16x2/BF16x2 branches accept
bare unsigned direct `0f` exact32 words. Packed words retain every bit, including
encoded sign and NaN payloads, with no quantization, splat or quieting. Integer
literals/results remain excluded for all floating source lanes. Packed decimal,
F64, unary/wrapped/operator or pair constructors remain reference-qualified
unavailable; typed current-word capacity is not a numeric source constructor.

Logical immediate type is separate from physical register type. Explicit Element
allowlists retain B16/F16/U16/S16 for F16, B16 for BF16, B32/F16x2 for F16x2,
B32 for BF16x2 and B32/F32/U32/S32 for F32. Membership cannot waive physical lane
width, General/scalar class, intrinsic declaration eligibility, component, owner
or provenance checks. Without an explicit list, existing compatibility applies.
Bit register lanes are neutral; integer and floating register families cannot mix,
and immediate Float lanes do not participate in that register-family test.
Scalar operand compatibility is unchanged.

Canonical current logical Float lanes are checked for in-width bits, absent
integer origin (optional zero still counts as present), and clean negative metadata.
This does not decide current Bit16/Bit32 arms for other logical formats from a
scalar F16 rule; those cases need their own format-specific evidence. Coherent canonical edits need no erased floating lexical history.
Source association separately checks provided AST identity. Actual ValueVector
owner pointers and all shape/type/register/immediate/bit/range mirrors must agree;
synchronous observers borrow that actual owner only during traversal. Real AST
release, copy/assignment/function copy/clone preserve ownership. Register sinks,
whole containers and stale mirrors cannot substitute for read-only scalar lanes.
These frontend contracts do not execute atomic arithmetic, ordering, cache effects
or runtime global-memory behavior.


<a id="atomic-reduction-coverage--scalar-f16-atomic-source-members"></a>

### Scalar F16 atomic source members

`AtomGlobalAddNoftzF16::src` and `RedGlobalAddNoftzF16::src` use
`WithLocs<RegOrImm>` in both the ordinary and cache-policy layouts. Rebuild
consumers and select `ResolvedRegisterRef` or `ResolvedImmediate` with `get_if`
or `visit`; the existing class identities, field locations and layout order do
not change. The cache policy remains a separate B64 register operand, and the
atomic destination retains its register-or-sink contract.

Only these four scalar F16 source roles inherit floating constant use. They keep
logical F16 and SameWidth: actual B16/F16 general registers and their existing
legal components remain supported. Numeric floating expressions use the common
binary16 converter (`1.5` produces F16 word `0x3e00`); an integer-origin source
is not a floating permission. Current canonical F16 words need no floating
lexical history, but require low16 bits, no integer-origin cache and no floating
negative metadata. IEEE sign and NaN payload bits remain data. This is scalar
numeric conversion, not a packed raw-word, pair or splat constructor.

The existing same-width Bit compatibility rule also accepts a canonical current
B16 word such as `0x3e00` without integer-origin or negative metadata. This is
inherited current-data compatibility, independent of the original AST spelling;
it adds no Bit literal-source permission and does not change the F16 producer or
its integer-source exclusion. Other Bit flag and packed-format rules are unchanged.

The ordinary form keeps PTX6.3/SM70 availability; the global cache-policy layout
keeps PTX7.4/SM80 availability. Address, alignment, ordering, scope, `noftz`,
source identity and existing AvailableContext/RequireCompleteContext rules remain
independent. Packed F16x2, BF16/BF16x2, atomic vectors, compare/swap and all other
source permissions remain unchanged. Frontend checks do not certify GPU runtime
behavior or opaque cache-policy contents.


<a id="atomic-reduction-coverage--scalar-bf16-atomic-source-members"></a>

### Scalar BF16 atomic source members

`AtomGlobalAddNoftzBf16::src` and `RedGlobalAddNoftzBf16::src` use
`WithLocs<RegOrImm>` in both `no_hint` and `with_policy` layouts. Consumers
must rebuild with matching headers/libraries and inspect `.value` through
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`.
The two existing class identities and member names remain. Atomic destinations
remain B16/Exact register-or-sink values; cache policies remain B64 registers.
Addresses, alignment, qualifiers, modifier order and target floors are independent
of this source permission. Scalar F16, packed and vector atomic cohorts retain
their separately defined contracts.

The four source keys use logical BF16 and SameWidth with the existing converter.
Numeric Float sources `1.5`, `1.0+0.5`, `0d3ff8000000000000` and
`0f3fc00000` produce the existing BF16 `0x3fc0` golden. Actual register sources
still require the ordinary scalar B16 declaration under intrinsic type and
bit-compatibility rules; SameWidth does not admit F16/U16/S16, standalone BF16,
special registers or whole vectors. Integer-origin literals/expressions are
excluded; no integer-immediate policy or new conversion path is introduced.

The producer stores logical BF16. Logical BF16 current words require low sixteen
bits and clean origin/negative flags; this grant does not change pre-existing
current Bit-carrier compatibility. Floating lexical history is unnecessary, so a coherent canonical word can
validate after AST destruction, copy, assignment and clone. Stale high bits,
type/origin/class/provenance or range facts remain errors. Source association
checks the supplied AST against retained syntax independently of current-word
retargeting. Synchronous observers borrow the actual member and ranges only for
the traversal. These scalar numeric rules add no packed pair/splat/truncation
permission. The frontend validates representation and availability; it does not
execute an atomic update, memory ordering, cache behavior or BF16 arithmetic.

Current family references retain a real declaration/member identity: erasing a
required family index or contradicting its current spelling is invalid. Coherent
current spelling/member retargets and erasing only the numbered convenience
cache remain valid; validation does not authenticate old operand spelling.
Known reference contradictions are checked under partial context, while the
existing whole-instruction deferral remains. Required delivery evidence includes
actual generated views and real borrowed callbacks, not a manual projection.


<a id="atomic-reduction-coverage--packed32-source-words-and-member-migration"></a>

### Packed32 source words and member migration

The scalar `atom` and `red` `add.noftz.f16x2` / `add.noftz.bf16x2` source slots accept compatible registers or a **bare unsigned direct `0f` word** in both `no_hint` and `with_policy` layouts. The eight source keys share four existing public `src` members. For example, `0f3e003e00` supplies the complete F16x2 word `0x3e003e00`, and `0f3fc03fc0` supplies the complete BF16x2 word `0x3fc03fc0`. All 32 bits are data, including encoded signs and NaNs: the frontend does not split, splat, quantize or quiet the word.

This grant reuses the existing packed-literal helper. Integer literals and Integer-result expressions are incompatible with logical Float sources. Decimal numbers, numeric pairs/splats, unary `+`/`-`, parenthesized/wrapped/expression forms, new `0h` constructors and truncation from `0d` are not packed source constructors. Numeric packed construction remains **Source_N_A / reference-qualified**; it is not a positive producer or a reason to add a converter. The source converter produces the selected logical F16x2/BF16x2 type. Canonical current-word positives use that selected type, only low32 bits, no `integer_source_bits`, and false floating-negative metadata, without retained floating lexical history. Existing current Bit-carrier compatibility remains unchanged; these examples do not narrow the inherited `SameWidth` policy or establish a new B32 rule.

The BF16x2 **source** is logically BF16x2 with `SameWidth`, replacing its previous B32/`Exact` projection. Its lawful physical register carrier remains B32: declaring a standalone `.bf16x2` register remains invalid. F16x2 sources retain their existing native F16x2/B32 carriers and logical `SameWidth` policy. General class, intrinsic declaration validity, scalar/component provenance, exact physical width and SymbolId binding remain checked. This is not permission for F32/U32/S32, whole vectors or predicate carriers. BF16x2 atomic destinations remain B32/`Exact`; the additional cache-policy operand remains a B64 register. Address qualifiers/alignment, modifier order, availability and scalar-half/vector/CAS/EXCH cohorts are unchanged.

`AtomGlobalAddNoftzF16x2`, `AtomGlobalAddNoftzBf16x2`, `RedGlobalAddNoftzF16x2` and `RedGlobalAddNoftzBf16x2` keep their names and identities, but `src` changes from `WithLocs<ResolvedRegisterRef>` to `WithLocs<RegOrImm>`. Consumers must rebuild with matching generated headers and libraries and use `std::get_if<ResolvedRegisterRef>` / `std::get_if<ResolvedImmediate>` on `.value`. See the [IR migration](#resolved-ir-design--packed32-atomic-source-members). Views and observer callbacks borrow the actual owner only synchronously and must be reacquired after copying or cloning. Source association remains separate from current-word validity. These frontend checks do not execute atomics or certify runtime memory, ordering or floating results.


<a id="barrier-sync-coverage"></a>

## CTA `barrier.sync`, `barrier.arrive`, and `barrier.red` coverage

Technical reference and evidence: [barrier_sync_coverage.md](us-en/converage/barrier_sync_coverage.md).

<a id="barrier-sync-coverage--cta-barriersync-barrierarrive-and-barrierred-coverage"></a>

### CTA `barrier.sync`, `barrier.arrive`, and `barrier.red` coverage

The frontend models `barrier{.cta}.sync{.aligned} a{, b}`,
`barrier{.cta}.arrive{.aligned} a, b`, and the three
`barrier{.cta}.red` operations of [PTX ISA
9.3 §9.7.14.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar).
This is a separate `barrier` opcode, not an alias for `bar.sync` or
`barrier.cluster`.

`a` is a `.u32` immediate or register barrier identifier. An immediate must be
in `0..15`; the value of a register identifier remains a runtime obligation.
For `sync`, optional `b` is a `.u32` immediate or register participant count.
An immediate count must be a multiple of 32; omitting it means all threads in
the CTA participate. The same optional count applies to `red`. For `arrive`,
`b` is required and an immediate must be positive and a multiple of 32.
Register counts cannot be checked statically.

`barrier{.cta}.red.popc{.aligned}.u32 d, a{, b}, {!}c` writes the
population count to a `.u32` register. The
`barrier{.cta}.red.{and|or}{.aligned}.pred p, a{, b}, {!}c` forms write
the conjunction or disjunction to a predicate register. `c` is a predicate
register with optional negation. These reductions wait for the participating
warps and write the result to each waiting thread. The frontend preserves
the predicate's typed, owned negation; it does not compute the result.
The frontend preserves whether `.cta` and `.aligned` were written, and retains the location of an
explicit `.aligned`. It does not prove CTA convergence or model barrier state.
On `sm_6x` and older targets, a `barrier` instruction without `.aligned` has
the aligned runtime restriction described by the ISA, while its resolved `.aligned` field
still records the source spelling as absent.

Unqualified `barrier.sync`, `barrier.arrive`, and `barrier.red` require PTX 6.0 and `sm_30`;
their `.cta` forms require PTX 7.8 and `sm_30`. The `.cta` qualifier does not
change CTA barrier semantics. `arrive` does not wait for other participating
warps to arrive.
The existing `bar{.cta}` forms and `barrier.cluster` forms retain their distinct
contracts. The ISA warns against mixing `red` with `sync` or `arrive`
on the same active barrier; the frontend does not prove that runtime
protocol. The remaining `mbarrier` wait qualifier combinations are outside this slice.


<a id="bit-operations-coverage"></a>

## Integer Bit-Operation Coverage

Technical reference and evidence: [bit_operations_coverage.md](us-en/converage/bit_operations_coverage.md).

<a id="bit-operations-coverage--integer-bit-operation-coverage"></a>

### Integer Bit-Operation Coverage

This document records the PTX 9.3 bit-operation contract modelled by the
frontend. The canonical source is
`python/src/ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml`; syntax descriptors,
Resolved IR, and target-aware checking derive from it. This is frontend
validation, not simulator or physical-GPU execution.

The normative source is NVIDIA's archived PTX ISA 9.3: [popc
§9.7.1.15](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-popc),
[clz §9.7.1.16](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-clz),
[bfind §9.7.1.17](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfind),
[brev §9.7.1.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-brev),
[bfe §9.7.1.20](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfe),
and [bfi §9.7.1.21](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-bfi).

| Opcode | Modelled forms | Operand contract |
| --- | --- | --- |
| `popc` | `.b32`, `.b64` | Source is register-or-immediate at the selected bit width; the count destination is `.u32`. |
| `clz` | `.b32`, `.b64` | Source is register-or-immediate at the selected bit width; the leading-zero count destination is `.u32`. |
| `bfind` | plain and `.shiftamt` forms for `.u32/.u64/.s32/.s64` | Source is register-or-immediate at the selected signed or unsigned width; the position or shift-amount destination is `.u32`. |
| `brev` | `.b32`, `.b64` | Source is register-or-immediate and the destination uses the selected bit width. |
| `bfe` | `.u32/.u64/.s32/.s64` | The extracted source and offset/width controls are register-or-immediate; destination uses the selected type and controls are `.u32`. |
| `bfi` | `.b32`, `.b64` | Inserted source, base, offset, and width are register-or-immediate; destination uses the selected bit width and controls are `.u32`. |

Every form requires PTX 2.0 and `sm_20`. Fundamental integer and same-width
bit register declarations remain compatible with the documented operand
widths, consistent with the frontend's [register type policy](#register-type-policy).
The bit-container source contracts of `popc`, `clz`, `brev`, and `bfi` also
admit same-width floating register storage; the signed and unsigned `bfind` and
`bfe` forms remain typed integer contracts.

For `bfe` and `bfi`, an immediate offset or width is checked inclusively in
`0..255`. Register controls remain legal because their values are dynamic; the
frontend checks their `.u32` width but cannot establish the runtime range. The
ISA pseudocode masks each control to eight bits after stating that legal values
are restricted to this range, so it does not permit out-of-range immediates.

The `bfe.b32` example printed in the archive conflicts with that section's
explicit `.u32/.u64/.s32/.s64` syntax list and is not accepted. Existing public
variant names such as `PopcB32`, `BfindShiftamtU32`, `BfeU32`, and
`BfiB32` remain available while the additional forms are generated alongside
them.

Focused [POPC](../submod/resolved_ir/test/test_select_variant_popc.cpp),
[BFE](../submod/resolved_ir/test/test_select_variant_bfe.cpp) and adjacent
per-opcode resolver/checker tests exercise the expanded public forms. The former
installed bit-operation harness is no longer present. They establish parsing, resolution, type,
range, and target contracts only.


<a id="bulk-async-coverage"></a>

## Non-tensor bulk asynchronous data movement

Technical reference and evidence: [bulk_async_coverage.md](us-en/converage/bulk_async_coverage.md).

<a id="bulk-async-coverage--non-tensor-bulk-asynchronous-data-movement"></a>

### Non-tensor bulk asynchronous data movement

The frontend models the non-tensor, non-multimem forms in [PTX ISA 9.3
§9.7.9.12, .14, .26.4, and .26.6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
They resolve to owned, typed `Cp` or `St` variants and pass through target-aware
checking. Ordinary non-bulk `cp.async` retains its existing three-operand
public shape. `red.async`, tensor-map transfers, and multimem transfers have
separate contracts.

| Form | Completion and operands | Earliest PTX / target |
| --- | --- | --- |
| `cp.async.bulk.shared::cluster.global.mbarrier::complete_tx::bytes` | Shared destination, global source, 32-bit byte size, shared mbarrier object address; optional multicast 16-bit `ctaMask` and/or L2 cache-hint 64-bit policy | 8.0 / SM 90 |
| `cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes` | Same base operands; optional L2 policy and/or `.ignore_oob` with two 32-bit ignore-byte operands | 8.6 / SM 90; `.ignore_oob` 9.2 |
| `cp.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes` | Shared destination and source, byte size, shared mbarrier object address | 8.0 / SM 90 |
| `cp.async.bulk.global.shared::cta.bulk_group` | Global destination, shared source, byte size; optional L2 policy and/or `.cp_mask` with 16-bit `byteMask` | 8.0 / SM 90; `.cp_mask` 8.6 / SM 100 |
| `cp.async.bulk.commit_group`, `wait_group{.read} N` | Independent bulk-group identity; `N` is an integer constant and `.read` waits for source reads | 8.0 / SM 90 |
| `cp.reduce.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes` | Shared reduction with byte size and mbarrier object address | 8.0 / SM 90 |
| `cp.reduce.async.bulk.global.shared::cta.bulk_group` | Global reduction with byte size; optional L2 cache-hint 64-bit policy | 8.0 / SM 90 |
| `cp.async.bulk.prefetch.L2.global` | Global address, 32-bit byte size; optional L2 cache-hint 64-bit policy | 8.0 / SM 90 |
| `st.async[.weak][.shared::cluster].mbarrier::complete_tx::bytes` | Scalar or v2/v4 register data and shared mbarrier object address | 8.1 / SM 90; scalar `.b128` 9.2 |
| `st.async[.mmio].release.{gpu,sys}[.global]` | Global scalar data, no mbarrier; `.mmio` requires `.sys` | 8.7 / SM 100 |
| `st.bulk[.weak][.shared::cta]` | Zero-fill size in u32/u64 or an immediate and literal init value `0` | 8.6 / SM 100; u32 size register 9.0 |

The shared reduction permits `add` with `u32/s32/u64`, `min/max` with
`u32/s32`, `inc/dec` with `u32`, and `and/or/xor` with `b32`. The global
reduction permits `add` with `u32/s32/u64/f32/f64`, `min/max` with
`u32/s32/u64/s64/f16/bf16`, `inc/dec` with `u32`, and `and/or/xor` with
`b32/b64`. Global `add.f16` and `add.bf16` require `.noftz`.

PTX 9.3 accepts explicit `.weak` on bulk copies and
`.relaxed.{cta,cluster,gpu,sys}...b128` on copies touching global memory.
Shared-to-shared relaxed copies permit only `.cta` or `.cluster`. These copy
qualifiers require `sm_90a`, a member of the `sm_100f` family at or above
SM 100, or a member of the `sm_110f` family at or above SM 110. The older
unqualified weak copy forms remain available on base SM 90. Reduction
`.relaxed.scope` is a separate PTX 9.3 addition available on base SM 90;
shared reductions permit `.cta/.cluster`, global reductions all four scopes.
When omitted, reduction scope follows the ISA's default `.relaxed.sys`.
The multicast architecture list is a performance recommendation, not an
additional legality gate.

For bulk copy, global reduction, and prefetch forms, `.L2::cache_hint` accepts
the ordinary operands with or without a final 64-bit `cache_policy` register.
The policy operand requires the hint. `st.async` destinations require a register
base; a symbol base or an immediate-only address is rejected.

Root `st.async` inherits the `st` wider-source-register rule from PTX 9.4.1.
Each scalar or explicit vector lane may use an otherwise compatible equal or
wider register, including B64/B128 Bit carriers; narrower registers reject.
Integer/Bit compatibility and floating-format restrictions remain independent:
F32/F64 data requires its matching native floating type or a sufficiently wide
Bit carrier, so F64 does not substitute for F32 data. Bit-typed data retains
its ordinary fundamental compatibility. Sources remain register-only.

The instruction type and written arity define transfer size, static alignment
and mbarrier completion bytes, irrespective of actual carrier widths. Shared v2
permits instruction elements through 64 bits; v4 remains limited to 32-bit
elements. Four B128 scalar carriers in a v4.u32 brace list therefore retain a
16-byte logical transfer, without declaring an oversized ordinary vector.
Scalar instruction `.b128` keeps its PTX 9.2 / SM 90 gate independently of a
wider carrier used for another logical type. Address/barrier and completion
contracts above are unchanged. Distinct
[`multimem.st.async` / `multimem.red.async`](#multimem-coverage) data remains
same-width; sharing suffix words does not make them root `st` instructions.
This is static source/type validation, not execution of chopping or a runtime
complete-tx protocol proof.

The checker enforces known address spaces, static alignment (16 bytes for
bulk copy/reduce/prefetch, 8 bytes for a barrier), modifier/operand coupling,
supported type tuples, immediate divisibility, and target availability.
`st.bulk` size must be an 8-byte multiple no larger than 16,777,216 bytes.
Owned module validation compares a register size's cached type with its bound
declaration; standalone instruction checking defers that declaration check.
For register sizes and addresses, runtime values, remote-CTA provenance,
group/phase completion protocol, and cache behavior remain consumer
obligations. This frontend does not execute instructions.

Root shared `st.async` v2/v4 data sources accept ordinary named register vectors alongside scalar brace lanes. The declaration itself must remain at most 128 bits even when each carrier is wider than the logical element. Completion bytes and transfer alignment still derive from logical type times arity. Named data does not relax the independent register-base address requirement.


<a id="carry-coverage"></a>

## Extended-precision integer coverage

Technical reference and evidence: [carry_coverage.md](us-en/converage/carry_coverage.md).

<a id="carry-coverage--extended-precision-integer-coverage"></a>

### Extended-precision integer coverage

The canonical arithmetic YAML models all 48 documented §9.7.2 type/mode/effect
combinations: `add.cc`, `addc{.cc}`, `sub.cc`, `subc{.cc}`, `mad.{hi|lo}.cc`,
and `madc.{hi|lo}{.cc}` for `.u32/.s32/.u64/.s64`, following the
[PTX 9.3 extended-precision arithmetic specification](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions).
The existing add/sub 32-bit forms require PTX 1.2; their 64-bit forms require
PTX 4.3 and SM 20. `mad` and `madc` 32-bit forms require PTX 3.0 and SM 20;
their 64-bit forms require PTX 4.3 and SM 20. Their `.hi` or `.lo` mode is
required: no implicit default mode is modeled. Rounding, saturation, wide, and
16-bit forms are not legal. All three multiply-add sources accept registers or
integer immediates under the existing same-width integer policy.

Every generated variant exposes a static `condition_code_effect` of type
`ConditionCodeEffect`; the corresponding `ResolvedVariantDescriptor` carries
the same typed value. Ordinary arithmetic defaults to `None`.

| Form | Effect |
| --- | --- |
| `add.cc` | `CarryOut` |
| `addc` | `CarryIn` |
| `addc.cc` | `CarryInOut` |
| `sub.cc` | `BorrowOut` |
| `subc` | `BorrowIn` |
| `subc.cc` | `BorrowInOut` |
| `mad.hi.cc` / `mad.lo.cc` | `CarryOut` |
| `madc.hi` / `madc.lo` | `CarryIn` |
| `madc.hi.cc` / `madc.lo.cc` | `CarryInOut` |

Carry and borrow refer to the same implicit architectural `CC.CF` bit. An
execution predicate gates both explicit results and this implicit effect.
The bit is not preserved across calls. The frontend retains those semantic
contracts; it does not execute arithmetic or maintain simulator state.
Consumers must not infer effects from opcode spelling or assume the incoming
bit is initialized. The effect is fixed by generated variant identity, rather
than mutable instruction metadata.

The checked-in [CUDA 13.1 `ptxas` evidence](extended_precision_ptxas.json)
contains 32 self-contained cases: 11 accepted, 14 contract rejects, 3
omitted-mode observations rejected, 2 version boundaries, 1 target boundary,
and 1 tool-unavailable case. It also records two assembler-accepted duplicate
`.cc` observations; the frontend continues to require each modifier at most
once.

The dedicated C++ tests cover all types and effects, predication retention,
target boundaries, required modes, invalid modifiers/layouts, and owned typed
IR. The installed-package consumer reads typed `Mad` and `Madc` effects and
revalidates a modified type without depending on an AST or source-spelling
dispatch.


<a id="carry-coverage--generated-scale-and-local-checks"></a>

### Generated scale and local checks

The canonical database grows from 81 opcodes and 476 variants to 82 and 488.
The 24 documented new type/mode/effect forms are represented by 12 canonical
variants: four appended `Mad` carry-out variants and eight `Madc` carry-in or
carry-in/out variants. Existing `Mad` variant names and indices remain stable.
The generated resolved-IR subtree remains 13 files and 9 C++ sources; it grows
from 18,444,809 to 18,785,864 bytes and from 381,098 to 388,329 C++ lines
(+341,055 bytes and +7,231 lines).

The final Debug build ran 75 Ninja actions in 57 seconds using the existing
configured, cache-enabled `out/build/ci-linux-gcc-debug` tree; it was not a
clean build. Full Python discovery passed 222 tests in 55.392 seconds. Ordinary
Debug CTest excluding the separately run consumer passed 886/886 in 11.58
seconds, and the installed-package consumer passed 1/1 in 190.27 seconds.
Release validation remains with CI.


<a id="conversion-coverage"></a>

## Conversion and Address-Query Coverage

Technical reference and evidence: [conversion_coverage.md](us-en/converage/conversion_coverage.md).

<a id="conversion-coverage--conversion-and-address-query-coverage"></a>

### Conversion and Address-Query Coverage

This document defines the PTX 9.3 syntax accepted and target-validated by the
frontend for `isspacep`, `cvta`, `cvt`, `cvt.pack`, `prmt`, `mapa`, and
`getctarank`. The machine-readable authority is
`python/src/ptx_frontend/spec/resources/ptx_spec/data_movement_and_conversion.yaml`.
It is a source-acceptance and Resolved-IR validation contract. It does not
implement conversion results, address mapping, CTA-rank lookup, assembler
output, or GPU execution.

The normative source is NVIDIA's [PTX ISA 9.3 data-movement and conversion
chapter](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions).
Target minima below are part of the accepted source profile. They do not infer
binary translation compatibility between target spellings.
Family-qualified availability is evaluated against the source profile's enabled
feature families, rather than only its literal spelling. For example,
`sm_121a` enables the `sm_120f` family.


<a id="conversion-coverage--address-space-predicates-and-conversion"></a>

### Address-space predicates and conversion

`isspacep` accepts a predicate destination and a register generic address in
one of these spellings:

```text
isspacep.{global|local|shared|const|param} p, a
isspacep.shared::{cta|cluster} p, a
isspacep.param::entry p, a
```

The instruction has no size suffix. Its source is a 32-bit or 64-bit
fundamental integer/bit container under the frontend's equal-or-wider register
contract; it is not a floating, sub-word, packed, or `.b128` operand. The
unqualified forms mean PTX's default `.shared::cta` and `.param::entry`, rather
than an inferred runtime address provenance. Global, local, and shared are PTX
2.0 / SM 20; constant is PTX 3.1 / SM 20; parameter is PTX 7.7 / SM 70;
explicit `shared::cta` is PTX 7.8 / SM 30; explicit `shared::cluster` is PTX
7.8 / SM 90; and explicit `param::entry` is PTX 8.3 / SM 70. The slice accepts
register operands only, so variable addresses and variable-plus-offset forms
are outside it.

`cvta` and `cvta.to` cover unqualified `.global`, `.local`, `.shared`,
`.const`, and `.param` spellings, plus `.shared::cta`, `.shared::cluster`, and
`.param::entry`, with `.u32` and `.u64` in both directions. Global/local/shared
require PTX 2.0 / SM 20, constant PTX 3.1 / SM 20, parameter PTX 7.7 / SM 70,
explicit shared forms PTX 7.8 (SM 30 for `::cta`, SM 90 for `::cluster`), and
`param::entry` PTX 8.3 / SM 70. The retained public `GlobalU64` and
`ToGlobalU64` variant names remain stable.

Forward `cvta.space` accepts a same-width register, an addressable declaration
in the selected space, or that declaration plus an immediate offset. Its public
source field is therefore the existing `ResolvedMovSource` sum type. This
changes the API type from `ResolvedRegisterRef` for existing forward variants;
callers inspect a register alternative with `std::get` or `std::get_if`.
Variant names are preserved. `cvta.to` remains register-only. Declaration-bound
addresses retain their declared space
through owned resolution; a known wrong symbol space is rejected, and
`.param{::entry}` symbol sources must be kernel input parameters. Register
sources remain accepted without attempting to infer runtime pointer provenance.
Owned validation also cross-checks direct and offset symbol identity against its
cached declaration space, parameter role, and function context. Generic MOV
address-taking retains its distinct device-parameter-to-local materialization
rule.

`mapa{.shared::cluster}.{u32|u64}` and
`getctarank{.shared::cluster}.{u32|u64}`, together with their generic shared
spellings, remain the existing PTX 7.8 / SM 90 cluster-capability forms. They
validate syntax, physical operands, and target availability only; they do not
prove an address belongs to a live cluster mapping or calculate a CTA rank.


<a id="conversion-coverage--prmt"></a>

### `prmt`

All seven `prmt.b32` forms are modelled: the unqualified generic form and
`.f4e`, `.b4e`, `.rc8`, `.ecl`, `.ecr`, and `.rc16`. Every form is PTX 2.0 /
SM 20. Destination and all three inputs use the 32-bit bit-container contract;
each input is register-or-immediate. The frontend intentionally does not impose
a 16-bit immediate range on the selector: generic mode consumes its low 16
bits and specialized modes consume their documented low selector bits.


<a id="conversion-coverage--cvt-scalar-and-packed-families"></a>

### `cvt` scalar and packed families




<a id="conversion-coverage--ordinary-scalar-conversions"></a>

### Ordinary scalar conversions

The ordinary scalar domain is the 12-by-12 `.u8/.u16/.u32/.u64`,
`.s8/.s16/.s32/.s64`, `.bf16`, `.f16`, `.f32`, and `.f64` type grid. It uses the
PTX integer (`rni/rzi/rmi/rpi`) and floating (`rn/rz/rm/rp`) rounding classes,
plus the applicable `.ftz` and `.sat` flags. Typed checking rejects an
otherwise syntactic pair when the rounding class, loss direction, saturation,
or FTZ endpoint is invalid. Scalar source operands are register-or-immediate;
register endpoints retain the PTX width contract. `.f64` directions require
SM 13 or newer.

For the two16-bit instruction formats F16 and BF16, the adopted equal-format-size integer-valued conversion rule permits `.rni/.rzi/.rmi/.rpi` in both directions: eight F16-destination forms with optional `.sat`, and four BF16-destination forms without `.sat`. Neither direction admits `.ftz`, `.rna`, or `.rs` through this rule. Ordinary F16-to-BF16 conversion loses significand precision and requires explicit floating rounding; stored default `Rn` does not satisfy an omitted source modifier. The adopted reverse BF16-to-F16 omission remains permitted, with optional F16 `.sat`; explicit `.rn/.rz/.rm/.rp` remain valid in both directions. These cross-half forms require PTX7.8/SM90. The equality concerns instruction-format size, not physical carrier width: wider legal Bit carriers at the F16 endpoint coexist with exact Bit16 storage at the BF16 endpoint.

Generated presence preserves the existing `nonempty locations OR value differs from default` rule. Thus current nondefault integer-rounding values remain present even after their locations are cleared; default `Rn` with empty locations is absent. A manually supplied modifier view with `is_present=false` ignores its stored mode. Legal current typed edits are validated independently of retained lexical history. Rounding selects the requested GPU operation; the frontend does not execute it or saturation. A numeric F16 input `1.5` remains source word `0x3e00` in every mode. This bounded overlapping-clause interpretation does not broaden other format pairs, qualifiers, converter permissions, or availability.

The half/bfloat/tf32 cohorts retain their distinct syntax and availability:

- Plain scalar `f16 <- f32` accepts the four floating rounding spellings
  `.rn/.rz/.rm/.rp`. The special `.relu` and `.satfinite` spellings use
  `.rn/.rz`: `.relu` begins at PTX 7.0 / SM 80, and `.satfinite` begins at
  PTX 8.1 without an additional SM minimum in the PTX availability note.
- `bf16 <- f32` and the packed `bf16x2 <- f32,f32` cohort begin at PTX 7.0 /
  SM 80. `f32 <- bf16` begins at PTX 7.1 / SM 80; its `.ftz` form begins at
  PTX 7.8 / SM 90. The remaining ordinary BF16 directions begin at PTX 7.8 /
  SM 90.
- `f16x2 <- f32,f32` and `bf16x2 <- f32,f32` use two scalar `.f32` sources
  and a packed physical destination. Their stochastic `.rs` forms add a
  required logical `.b32` value `rbits` in a scalar register and are limited to PTX 8.7 on exact
  `sm_100a` or `sm_103a`.
- `tf32 <- f32` preserves separate `.rna` and `.rn/.rz` forms. Its base
  destination is PTX 7.0 / SM 80; the directed ReLU cohort is PTX 7.8 / SM
  90. `.satfinite` has its own later availability, including PTX 8.6 / SM 100
  for the directed `.rn/.rz` form.


<a id="conversion-coverage--logical-formats-and-register-carriers"></a>

### Logical formats and register carriers

Instruction-only floating/fixed formats require Bit register declarations,
including BF16, TF32, packed low-bit formats, UE8M0, and S2F6. The selected
logical format is checked even when the physical operand model uses B8/B16/B32.
Native fundamental F16x2 instead permits its native declaration or a Bit carrier;
arbitrary same-width signed, unsigned, and F32 registers do not join that set.
Ordinary F16/F32/F64 and integer conversions keep their existing compatibility.

Carrier kind and width are independent: converted data admits compatible wider
Bit registers through B128, except BF16/BF16x2 sources and destinations and TF32
destinations, which require exact width. Intrinsic sources use their canonical
kind and width, so a B32 intrinsic cannot supply an exact-width BF16 source.
Explicit vector components retain their effective element dtype and declaration
identity; a whole vector cannot supply a scalar data role.

A scale factor independently encodes UE8M0x2 and requires Bit storage. The
`rbits` operand is random-bit data, so its ordinary compatible S32/U32/F32
carriers remain legal. Neither auxiliary inherits the converted-data format.
The adopted reading of the unqualified CVT source-widening note includes both
auxiliaries: a factor uses the low 16 bits of a Bit scalar register of at least
16 bits; `rbits` uses the low 32 bits of an otherwise compatible scalar register
of at least 32 bits. B128 carriers remain eligible. Wider carriers add no factor
lanes or random-bit fields: the two factor bytes and stochastic partitions stay
inside those logical payloads. BF16/BF16x2 converted-data exact-width exceptions
do not constrain an independent auxiliary. This is an inclusive interpretation
of the broad source note, rather than a separately printed auxiliary-specific
permission. Checking retains logical operand types and actual declared widths;
it does not execute chopping, inspect runtime register values, or construct
packed numeric literals.


<a id="conversion-coverage--fp8-low-bit-ue8m0-s2f6-stochastic-and-scaled-forms"></a>

### FP8, low-bit, UE8M0, S2F6, stochastic, and scaled forms

The modern forms retain their physical packing rather than treating
instruction-only type spellings as declaration types:

- FP8 x2 forms use `e4m3x2`/`e5m2x2`, a `.b16` destination or source container
  as directed by the syntax, mandatory `.rn.satfinite` on FP8 destinations,
  and optional `.relu` where PTX specifies it. The f32 and packed `f16x2`
  source forms retain the original PTX 7.8 / SM 90 or PTX 8.1 / SM 89 paths
  independently of the packed `bf16x2` source extension, which is PTX 9.1 on
  the family-specific
  `sm_100f`, `sm_110f`, or `sm_120f` lines; BF16 x2 destinations from FP8 are
  PTX 9.2 on those same family lines.
- FP4 (`e2m1x2`) and FP6 (`e2m3x2`/`e3m2x2`) x2 forms use their `.b8` or `.b16`
  physical containers. Their mandatory `.rn.satfinite` destination forms and
  the reverse `f16x2` forms use the documented A/F target alternatives,
  beginning with PTX 8.6 `sm_100a` or the family-specific PTX 8.8 `sm_100f`
  path. Packed-source FP4 accepts its `.b8` destination under the normal
  equal-or-wider register policy, while FP6 retains its `.b16` minimum. The
  packed-source and BF16-destination extensions retain their PTX
  9.1/9.2 gates on the `sm_100f`, `sm_110f`, and `sm_120f` families.
- The x4 FP8, FP4, and FP6 forms are `.rs` conversions from a four-element
  logical `.f32` inputs. Braces may mix scalar registers with floating constants or
  expressions; a real named `.v4.f32` or `.v4.b32` declaration, including a compact
  member, supplies the same four ordered lanes. They require a separate logical `.b32` `rbits` value in a scalar register,
  mandatory `.satfinite`, and exact PTX 8.7 `sm_100a` or `sm_103a` targets.
- `ue8m0x2` accepts its `.rz/.rp` f32 and packed-BF16 inputs and its `.rn`
  BF16 output form with the specified low-bit target alternatives.
- `s2f6x2` is PTX 9.1. Its f32 and BF16 x2 directions retain mandatory
  `.rn.satfinite`; its BF16 x2 result direction retains the applicable optional
  flags. These forms are limited to `sm_100a`, `sm_103a`, `sm_110a`,
  `sm_120a`, or `sm_121a`.
- A `.scaled::n2::ue8m0` spelling selects the matching optional extra logical `.b16`
  scale-factor register operand. Without that spelling, the shorter physical operand
  layout is used. The scale factor is never a substitute for a register
  declaration of an instruction-only scalar type.

`ScalarType` and `RoundingMode` additions append to their existing public
domains. The low-bit/modern identities used only by instruction syntax are
instruction-only: they are modifiers, not legal `.reg` declaration types.


<a id="conversion-coverage--cvtpack"></a>

### `cvt.pack`

`cvt.pack.sat.{u16|s16}.s32 d,a,b` has three operands. The four-operand form
is `cvt.pack.sat.{u2|s2|u4|s4|u8|s8}.s32.b32 d,a,b,c`. Physical `d` is `.u32`,
`a` and `b` are `.s32`, and four-operand `c` is `.b32` and may be a register
or immediate. The base forms require PTX 6.5 / SM 72; the `.u2/.s2/.u4/.s4`
values require SM 75. The existing fixed public names and constants are kept
while new dynamic modifier values select the added forms.


<a id="conversion-coverage--scope-and-tool-evidence"></a>

### Scope and tool evidence

The standalone [installed consumer](../examples/conversion_consumer/README.md)
exercises the public conversion API and validates an owned module after its
source and syntax AST are destroyed.

The supported conversion scope is the set of forms listed above. When any
kernel input has a `.ptr.const` attribute, owned module validation rejects every
forward `cvta.const.{u32|u64}` in that module, including a register source; it
does not reject `cvta.to.const`. This is a module-local declaration check. It
does not infer runtime pointer provenance or external/link-time declarations.
Runtime conversion, address-mapping, and CTA-rank semantics are not executed
by this frontend.

The recorded probes use CUDA 13.1 `ptxas` V13.1.115, whose highest accepted PTX
version is 9.1; it rejects a PTX 9.3 profile. A 64-bit forward-source probe at
`sm_120` passed across the supported spaces, symbol-plus-offset forms,
sub-qualifiers, and `.to` u64 forms. The 32-bit forward and minimum-profile
probes were blocked by the tool's unsupported 32-bit compilation/ABI path, so
they are not semantic evidence. A device-formal `.param` probe was accepted by
the assembler even though this frontend rejects it for CVTA's kernel-parameter
contract; a device-formal `.local` probe was rejected for the expected space
mismatch. The probes are reproducible with `ptxas --version` and
`ptxas -arch=<target> <fixture>.ptx -o <fixture>.cubin`. The ordinary matrix
recorded 140 accepted cases and four 8-bit integer-width discrepancies. Those
observations are compiler-tool evidence, not a replacement for PTX 9.3, and
cannot validate PTX 9.2 forms with this tool version.

Scalar F16 numeric sources now convert through the common binary64-to-binary16 RNE path: decimal/F64 expressions and bare unsigned exact F32 are supported, with no binary32 intermediate. Literal conversion preserves signed zero and subnormals independently of instruction rounding/FTZ/saturation. Existing CVT source kinds and rounding rules are unchanged: four modeled F16-source templates remain without a legal positive under those independent rounding checks. Integer-to-F16 literal reinterpretation is not added; Bit16 precision rules and the exact F32 expression ban remain.

The ordinary `cvt.pack.sat` S32 inputs accept integer constants and expressions as well as same-width B32/S32/U32 registers. The existing 64-bit evaluator narrows each input to its low32 S32 word and retains original64-bit integer bits/sign metadata; `0x100000001U` therefore has use word1 without a target-range requirement. The frontend does not saturate inputs to the destination lane width or precompute the packed result. Floating-valued inputs remain incompatible with S32; an expression whose final category is Integer, including an FP comparison, follows the integer route. Independent B32 carry sources keep their existing literal/precision contract, including same-width unsigned direct0f bit data; this does not admit literals in register-only stochastic `rbits`. Carry topology, destination widths and target boundaries remain unchanged.

Public ABI migration: `src1` and `src2` in `CvtPackSatU8S32B32`, `CvtPackSat16S32` and `CvtPackSatSmallS32B32` change from `WithLocs<ResolvedRegisterRef>` to `WithLocs<RegOrImm>`. Rebuild consumers and inspect `.value` with `std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`. Six fields change in place; no class, modifier domain or selector order changes. Copied/assigned/cloned instructions retain typed words, source caches, declaration identities and locations. Ordinary current IR word/reference edits are checked against current facts; source association remains a separate retained-syntax check.

BF16 scalar numeric sources use the existing helper conversion in the three already-admitted CVT source fields (`CvtF32Bf16`, `CvtF32Bf16Ftz`, `CvtNonF32BaseFromBf16`). Compile-time source quantization is direct binary64 RNE, independent of runtime rounding, FTZ and saturation; it does not calculate the CVT destination. Each selected destination/modifier tuple still requires its independent legal register control. E4M3/E5M2 helper capability does not admit new scalar CVT sources, and packed/raw-word, vector, call and storage permissions remain unchanged.


<a id="cp-async-coverage"></a>

## PTX 9.3 non-bulk `cp.async` coverage

Technical reference and evidence: [cp_async_coverage.md](us-en/converage/cp_async_coverage.md).

<a id="cp-async-coverage--ptx-93-non-bulk-cpasync-coverage"></a>

### PTX 9.3 non-bulk `cp.async` coverage

The frontend models PTX ISA 9.3 §9.7.9.26.3 and the legacy mbarrier bridge in
§9.7.14.16.18. Copy completion remains a programmer obligation; the frontend
does not execute asynchronous copies or prove thread scheduling.

| Form | Contract | Availability |
| --- | --- | --- |
| `cp.async.ca.shared{::cta}.global` | `cp-size` is 4, 8, or 16 bytes | PTX 7.0 / SM 80; `::cta` PTX 7.8 |
| `cp.async.cg.shared{::cta}.global` | `cp-size` is 16 bytes | PTX 7.0 / SM 80; `::cta` PTX 7.8 |
| Either copy form with `src-size` | `.u32`, `.s32`, or `.b32` register, or unsigned immediate; known immediate must be less than `cp-size`; remaining destination bytes are zero-filled | Base copy availability |
| Either copy form with `ignore-src` | Predicate register selects full zero fill | PTX 7.5 / SM 80 |
| Either copy form with `.L2::64B/128B/256B` | Typed prefetch-size hint | PTX 7.4 / SM 80 |
| Either copy form with `.L2::cache_hint` | Optional `.b64`, `.u64`, or `.s64` cache-policy register; a supplied policy requires the qualifier | PTX 7.4 / SM 80 |
| `cp.async.commit_group`, `cp.async.wait_group`, `cp.async.wait_all` | Existing per-thread group completion syntax | PTX 7.0 / SM 80 |
| `cp.async.mbarrier.arrive{.noinc}{.shared{::cta}}.b64` | Existing CTA-shared mbarrier address and pending-count form | PTX 7.0 / SM 80; `::cta` PTX 7.8 |

The two addresses must resolve to shared destination and global source when
their provenance is known, each aligned to `cp-size`. `src-size` supplied in a
register retains the runtime requirement to be smaller than `cp-size`. The
fourth copy operand retains its meaning as a typed source size, ignore-source
predicate, or cache policy; a cache policy may also follow source control as a
fifth operand. The original three-operand `.ca.shared.global` public variant
retains its `dst`, `src`, and `cp_size` fields.

Without declarations, standalone resolution keeps an ordinary fourth register
without a cache hint as source size, a fourth predicate as `ignore-src`, and a
fifth register after `.L2::cache_hint` as cache policy. Their declaration types
remain unknown until binding. A fourth ordinary register with
`.L2::cache_hint` requires a declaration: it could be either a 32-bit source
size or a 64-bit policy. Owned-module validation compares the cached control
type and selected role with the bound register declaration.

The `.ca` and `.cg` cache modes and L2 qualifiers are source controls and
performance hints. The frontend checks their shape and availability, not cache
behavior. `cp.async.bulk` and its completion model are separate coverage.

The PTX 9.3 syntax permits `.L2::cache_hint` without a `cache_policy` operand:
it only requires the qualifier when a policy is supplied. `ptxas` 13.3.73
nevertheless rejects
`cp.async.ca.shared.global.L2::cache_hint [dst], [src], 4;` with
`Arguments mismatch for instruction 'cp.async'`. The same assembler accepts
the form with a b64 policy operand. The frontend follows the published syntax;
bare-hint code may need a policy operand for this assembler version.


<a id="createpolicy-coverage"></a>

## PTX 9.3 `createpolicy` coverage

Technical reference and evidence: [createpolicy_coverage.md](us-en/converage/createpolicy_coverage.md).

<a id="createpolicy-coverage--ptx-93-createpolicy-coverage"></a>

### PTX 9.3 `createpolicy` coverage

The frontend models the documented fractional, range, and access-property
conversion forms in PTX ISA 9.3 §9.7.9.19. Every form requires PTX 7.4 and
SM 80, and writes an opaque `.b64` cache policy to a 64-bit register.

| Form | Operands and source controls |
| --- | --- |
| `createpolicy.fractional.L2::primary{.L2::secondary}.b64` | Destination and optional `.f32` fraction; omitted fraction defaults to `1.0` |
| `createpolicy.range{.global}.L2::primary{.L2::secondary}.b64` | Destination, global address, 32-bit primary size, and 32-bit total size |
| `createpolicy.cvt.L2.b64` | Destination and 64-bit access-property register |

Primary priority is `evict_last`, `evict_normal`, `evict_first`, or
`evict_unchanged`. An explicit secondary priority is `evict_first` or
`evict_unchanged`; omission defaults to `evict_unchanged`. The resolved form
retains whether the secondary modifier and fraction operand appeared in source.

An immediate fraction must be finite and in `(0.0, 1.0]`. The frontend accepts
a dynamic `.f32` register fraction and cannot prove its runtime value. If both
range sizes are immediate, the primary size must not exceed the total size;
each immediate is independently checked as a 32-bit value even when the other
size is a register. Dynamic sizes retain the ordering runtime precondition.
The range form without `.global` uses generic addressing, but its runtime
address must be global.
Known non-global symbols are rejected, while an unresolved address register
is accepted. Source integer literals for each size must fit the 32-bit operand;
the frontend rejects `4294967296` and `4294967297` instead of adopting
`ptxas` 13.3.73's observed modulo-32-bit truncation. This source contract does
not change the ISA's advertised 4 GB semantic maximum for the total range.
Representable negative spellings such as `-1` retain their 32-bit bit pattern
(`0xffffffff`) for the unsigned size comparison; larger negative magnitudes
are rejected.
The frontend checks the policy representation and operands; it does not
execute cache operations or prove cache behavior.


<a id="div-coverage"></a>

## Floating `div` coverage

Technical reference and evidence: [div_coverage.md](us-en/converage/div_coverage.md).

<a id="div-coverage--floating-div-coverage"></a>

### Floating `div` coverage

`div.f32` and `div.f64` additionally support PTX 1.0–1.3 as exact omitted-control forms. The former means approximate+FTZ; the latter means round-to-nearest-even without FTZ and accepts SM >=13 or exact generic SM10/11/12 with `map_f64_to_f32`. Both reject PTX >=1.4 and omitted mode combined with written `.ftz`. Source omission and typed interpretation are separate; see [the public contract](#resolved-ir-design--historical-omitted-floating-controls).

The generated model preserves the existing integer and round-to-nearest floating
division forms, then extends them with the explicit modes in [PTX 9.3 §9.7.3.8](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-div).
Each mode is a typed Resolved IR variant, so `.approx`, `.full`, and `.rnd` cannot
be combined as unchecked spelling flags. The frontend models parsing, owned
resolution, and target validation; it does not execute division.

| Form | Minimum | Contract |
| --- | --- | --- |
| `div.approx{.ftz}.f32`, `div.full{.ftz}.f32` | PTX 1.4 / all target architectures | Fixed typed `.approx` or `.full`; optional `.ftz`; no rounding modifier. |
| `div.{rn,rz,rm,rp}{.ftz}.f32` | PTX 1.4 / SM 20 | Required typed rounding; optional `.ftz`. |
| `div.rn.f64` | PTX 1.4 / SM >=13, or exact generic SM10/11/12 with `map_f64_to_f32` | Fixed round-to-nearest; no `.ftz`. |
| `div.{rz,rm,rp}.f64` | PTX 1.4 / SM 20 | Required directed rounding; no `.ftz`. |

Every floating source accepts a floating literal or a same-width native floating
or bit register container; the destination accepts the corresponding native or
bit register container. Integer literals and integer register containers, wrong
widths, sinks, omitted modes at PTX >=1.4, mixed `.approx`/`.full`/rounding forms,
and FP64 `.ftz` are rejected.

Historical CUDA 13.3.73 evidence used `/usr/local/cuda/bin/ptxas -arch=sm_90
<module>.ptx -o <temporary>.o`. Its 60-module matrix had 39 expected accepts,
20 rejects, and one permissive assembler acceptance: `rcp.rn.ftz.f64`. That
unrelated noncanonical spelling remains rejected by the frontend. The DIV rows
confirm all four rounded directions, both source positions, literals, and bit
containers.

[Explicit reciprocal and square-root forms](#unary-float-coverage), [floating transcendentals](#transcendental-coverage), and [floating MIN/MAX](#min-max-coverage) are now modelled separately. [Floating and mixed ADD/SUB](#add-sub-coverage) completes the reconciliation; see that document for the ADD/SUB contracts.

The implemented mapped-FP64 availability alternative
([#287](https://github.com/endingly/ptx_frontend/issues/287)) is narrowly limited
to the explicit `div.rn.f64`, `rcp.rn.f64`, and `sqrt.rn.f64` forms at PTX >=1.4
on exact generic SM10/11/12 with `map_f64_to_f32`. Native availability remains
SM >=13 without requiring that option. Directed rounding, approximate-FTZ
forms, and other FP64 instruction families do not gain this alternative.


<a id="fabric-coverage"></a>

## Fabric/CFT coverage

Technical reference and evidence: [fabric_coverage.md](us-en/converage/fabric_coverage.md).

<a id="fabric-coverage--fabriccft-coverage"></a>

### Fabric/CFT coverage

The frontend recognizes the six device-side Fabric Transport source families and
the six fabric-proxy fence forms in the fixed [PTX ISA 9.3 (CUDA 13.3)](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fabric-instructions).
They require PTX 9.3 and SM 100 or newer. One canonical `fabric` opcode produces
exact, owned instruction classes. The bracketed CFT handle is a typed value with
named `endpoint` (32 bits), `data_offset` (64 bits), and, for counted forms,
`counter_offset` (64 bits). Each component keeps its register binding and source
location. Current support accepts integer/bit registers of the required width;
the fixed manual does not define a literal domain for handle components.

| Family | Source contract | Completion and topology |
| --- | --- | --- |
| `fabric.try_get` | `.async.shared::cta.relaxed.sys.b128`; `[dst]`, two-field source handle, `size:u32`, `[bar]` | Unicast; `mbarrier::complete_tx::bytes.mbarrier::report::fabric`; writes CTA shared |
| `fabric.try_put` | `.async[.multimem].shared::cta.relaxed.sys.b128`; two-field destination handle, shared source, size, barrier; optional final `.cp_mask` `bytemask:u16`, or counted three-field destination handle | Unicast or multicast; `mbarrier::complete_tx::16B.mbarrier::report::fabric`; reads CTA shared |
| `fabric.try_red` | Same direction and handle choices as put, with `and/or/xor`, `min/max`, or `add` and their exact PTX type sets | Unicast or multicast; `mbarrier::complete_tx::16B.mbarrier::report::fabric`; reads CTA shared |
| `fabric.try_pullred` | `.async.multimem.shared::cta.relaxed.sys.<op>.<type>.sync`; shared destination, two-field source handle, size, barrier, immediate `0xffffffff` member mask | Multicast; `mbarrier::complete_tx::bytes.mbarrier::report::fabric`; writes CTA shared |
| `fabric.submit` | Zero operands; optional `.op_restrict::fetching` | Submits this thread's issued operations; fetching restricts to get/pullred |
| `fabric.wait` | `.sync_restrict::reads`, zero operands | Partial completion of submitted CTA-shared reads, not whole-operation completion |

For `try_red`, bitwise operations allow `b32/b64`; min/max allow
`u32/s32/u64/s64/f16/bf16`; add allows `u32/u64/f16/bf16/f32/f64`.
`try_pullred` has the same bitwise set, min/max also allows `e4m3/e5m2`,
and ordinary add allows `u32/u64/f16/bf16/f32`. Its `.add.acc::f16`
allows `e4m3/e5m2`, while `.add.acc::f32` allows `f16/bf16`. The FP8
min/max and accumulated add forms additionally require the repository's
SM 100f/110f family capability or exact SM 120a/121a targets.
Counted put/red forms cannot combine with `.cp_mask`.
The documented early-counted spelling of unicast `try_put`
(`.async.counted::bytes.shared::cta`) resolves to a second exact source-order
class with the same CFT contract as the canonical counted spelling. There are
43 semantic rows and 44 final source-form classes for the six families.
The manual's shortened `try_red` example omits the required `.relaxed.sys`
qualifiers; the frontend follows the family Syntax definition.

The frontend checks exact suffix and operand presence, widths, register-only
handle fields, source locations, CTA-shared pointer state space, known shared
pointer alignment, immediate size multiples of 16, and the required pullred
member mask. Public `fabric_contract` metadata on each final class records
operation, endpoint topology, shared access direction, completion identity,
fabric reporting, counted mode, and required mbarrier layout v1. The completion
identity is distinct from the status report: mbarrier wait/report handling uses
the existing `mbarrier.try_wait.phase_type::primary` report form; a try
instruction has no status-result operand. A successful operation leaves the
report unchanged; failure can set its predicate and opaque report value.
For get/pullred, `complete_tx::bytes` contributes `size` bytes; for put/red,
`complete_tx::16B` contributes `size / 16` transactions.

At runtime, the data pointer and CFT data offset must be 16-byte aligned;
size is a multiple of 16 and both ranges must be in bounds. A counted
counter is an 8-byte endpoint resource at a 256-byte-aligned offset and must
not overlap destination data. A live local CTA-shared mbarrier must have been
initialized with layout v1. Endpoint existence, multicast membership, counted
and pull-reduction capabilities, operation failure reports, warp agreement,
submit-before-phase-advance ordering, final completion before grid exit, and
resource bounds are runtime obligations. Mixing fabric reports with another
reporting mechanism on one barrier has undefined behavior. Register values and protocol history
are not inferred by the static checker.

`fence.proxy.{generic::fabric,fabric::generic,fabric::fabric}.alias.{acquire,release}.sys`
has no operands. Its ordered proxy pair and acquire/release direction are
retained as typed modifiers. A fence orders proxy accesses; it does not wait
for a Fabric operation or report its status. Host-side logical-endpoint setup
and GPU execution are outside this frontend's coverage.

The [Python contract test](../python/tests/spec/test_fabric_contract.py),
[C++ coverage test](../submod/resolved_ir/test/test_fabric_coverage.cpp), and
[installed consumer](../examples/fabric_consumer/main.cpp) exercise the
source and public static contracts without GPU execution.


<a id="fence-coverage"></a>

## Ordinary `fence` coverage

Technical reference and evidence: [fence_coverage.md](us-en/converage/fence_coverage.md).

<a id="fence-coverage--ordinary-fence-coverage"></a>

### Ordinary `fence` coverage

The frontend models the thread-fence syntax in
[PTX ISA 9.3 §9.7.14.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-membar-fence):
`fence{.sem}.scope;`. A scope is required. The supported scopes are `.cta`,
`.gpu`, `.sys`, and `.cluster`; the supported explicit semantics are `.sc`,
`.acq_rel`, `.acquire`, and `.release`. The frontend also accepts
`fence.scope.sem;`, as used in the ISA's cluster example and accepted by CUDA
13.3 `ptxas`. Both modifier orders resolve to the same typed variant and values.

| Form | Minimum PTX | Minimum target | Extra capability |
| --- | --- | --- | --- |
| Omitted semantics, `.sc`, or `.acq_rel` with CTA/GPU/SYS scope | 6.0 | `sm_70` | None |
| Omitted semantics, `.sc`, or `.acq_rel` with cluster scope | 7.8 | `sm_90` | `cluster` |
| `.acquire` or `.release` with CTA/GPU/SYS scope | 8.6 | `sm_90` | None |
| `.acquire` or `.release` with cluster scope | 8.6 | `sm_90` | `cluster` |

When `.sem` is omitted, owned IR retains `MemoryConsistency::Omitted` with no
semantic-qualifier source location; its effective ISA behavior is `.acq_rel`.
Explicit `.sc` uses `MemoryConsistency::Sc`. Explicit `fence.acq_rel.cta` and
`fence.cta.acq_rel` retain the existing `Fence::AcqRelCta` variant. Other
ordinary forms use disjoint CTA, GPU/SYS, and cluster variants. No form has
operands. The checker enforces the target and capability limits on owned IR;
the frontend does not execute fences.

The separate, operand-free `fence.mbarrier_init.release.cluster;` variant
requires PTX 8.0, `sm_90`, and the `cluster` capability. It restricts the
fence's release effect to prior `mbarrier.init` operations on objects in
`.shared::cta` state space, as specified by the ISA. Its `.mbarrier_init`,
`.release`, and `.cluster` controls are fixed typed fields in owned IR.
Omitting or reordering them, changing the semantics or scope, or supplying an
operand is rejected. The ordinary `fence.release.cluster;` form remains a
different variant with a PTX 8.6 floor.

Two further operand-free, non-proxy forms require PTX 8.6, `sm_90`, and the
`cluster` capability: `fence.acquire.sync_restrict::shared::cluster.cluster;`
and `fence.release.sync_restrict::shared::cta.cluster;`. The acquire form
restricts ordering to operations on objects in `.shared::cluster`; the release
form restricts it to `.shared::cta`. Each has fixed typed semantics, restriction,
and cluster scope in owned IR. Swapping the semantics or restricted state space,
changing the scope, reordering qualifiers, or adding an operand is rejected.
These variants are distinct from the existing
`fence.proxy.async::generic.*.sync_restrict` forms.

The operand-free `fence.proxy.alias;` form is a separate fixed variant available
from PTX 7.5 on `sm_70` and later. It orders accesses through the generic and
virtual-alias proxies in both directions. Owned IR retains fixed `.proxy` and
`.alias` controls; an added scope, operand, or reordered modifier is rejected.
It is distinct from `membar.proxy.alias;`, whose target floor is `sm_60`, and
from the async and tensormap proxy fence variants.


<a id="fma-coverage"></a>

## FMA Coverage

Technical reference and evidence: [fma_coverage.md](us-en/converage/fma_coverage.md).

<a id="fma-coverage--fma-coverage"></a>

### FMA Coverage

This document states the complete PTX 9.3 `fma` contract modelled by the
frontend. It supplements the [syntax coverage matrix](#syntax-coverage); it
is not a claim that every PTX opcode or simulator execution is supported. The
machine-readable instruction specification is
`python/src/ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml`.

The normative reference is NVIDIA's PTX ISA 9.3 archive: [floating FMA
§9.7.3.6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-fma),
[half/bfloat FMA
§9.7.4.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-fma),
and [mixed-precision FMA
§9.7.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-fma).

All rows below are modelled through syntax, Resolved IR, and target-aware
checking. None has simulator execution semantics.

| Variants | PTX / target minimum | Modifiers | Operand contract |
| --- | --- | --- | --- |
| `fma_rn_f32`, `fma_directed_f32` | 2.0 / `sm_20` | `.rn`, `.rz`, `.rm`, or `.rp` is required; `.ftz` and `.sat` are independently optional | `.f32` destination and sources; every source is floating register-or-immediate and a same-width bit register is also accepted |
| `fma_rn_f64`, `fma_directed_f64` | 1.4 / `sm_13` | one of all four rounding modes is required; no flags | `.f64` destination and sources; every source is floating register-or-immediate and a same-width bit register is also accepted |
| `fma_f32x2` | 8.6 / `sm_100` | one of all four rounding modes is required; `.ftz` optional | all operands are exact `.b64` registers |
| `fma_rn_f16`, `fma_rn_f16x2` | 4.2 / `sm_53` | optional `.rn` (default RN); `.ftz` and `.sat` independently optional | register-only: `.f16`/`.b16` for scalar, `.f16x2`/`.b32` for packed |
| `fma_half_relu` | 7.0 / `sm_80` | optional `.rn` (default RN); `.relu` required; `.ftz` optional | register-only `.f16`/`.b16`, or `.f16x2`/`.b32` packed values |
| `fma_half_oob`, `fma_half_oob_relu` | 8.1 / `sm_90` | optional `.rn` (default RN); `.oob` required; the latter requires `.relu`; neither permits `.ftz` or `.sat` | register-only `.f16`/`.b16`, or `.f16x2`/`.b32` packed values |
| `fma_bf16`, `fma_bf16x2` | 7.0 / `sm_80` | optional `.rn` (default RN); `.relu` optional; `.ftz` and `.sat` are not allowed | exact `.b16` scalar or `.b32` packed registers only |
| `fma_bf16_oob`, `fma_bf16x2_oob` | 8.1 / `sm_90` | optional `.rn` (default RN); `.oob` required; `.relu` optional; `.ftz` and `.sat` are not allowed | exact `.b16` scalar or `.b32` packed registers only |
| `fma_mixed_f32_f16`, `fma_mixed_f32_bf16` | 8.6 / `sm_100` | one of all four rounding modes required; `.sat` optional | destination and accumulator `c` are `.f32` or `.b32`; F16 multiplicands `a`/`b` accept `.f16`/`.b16` registers or numeric half constants; BF16 multiplicands accept their existing `.b16` registers or numeric BF16 constants; `c` may be a floating immediate |


<a id="fma-coverage--deliberate-compatibility-boundaries"></a>

### Deliberate compatibility boundaries

The half/bfloat section defines RN as the default, including OOB forms. The
frontend accepts omitted or explicit `.rn`; other rounding modes remain invalid.
OOB syntax admits optional `.relu`, and excludes `.sat` and `.ftz`.
Historical `ptxas` 13.3.33 rejection of omitted `.rn` and acceptance of half
OOB `.sat` are supplemental tool divergences, rather than frontend grammar rules.

The mixed-precision manual example contains a repeated/trailing `.sat`. The
model accepts only the canonical modifier sequence represented by its variants:
one rounding modifier, an optional single `.sat`, then `.f32` and `.f16` or
`.bf16`.

For floating `.f32` and `.f64` sources, cross-width hexadecimal bit literals
(`0f` for `.f64`, `0d` for `.f32`) are accepted and converted from their lexical
precision. Decimal literals are evaluated as binary64 before conversion to the
operand type, including finite decimal values that narrow to `.f32` infinity.
Narrowing uses round-to-nearest, ties-to-even, independently of the instruction's
rounding or `.ftz`. Same-width bit literals retain their exact payload. Across
precisions, the frontend quiets NaNs and retains their sign and high payload bits;
this is a deterministic representation policy, not an ISA guarantee of payload
conversion.

The public C++ IDs `FmaRnF32`, `FmaRnF64`, and `FmaRnF16` remain available.
The three source fields of `RnF32` and `RnF64` now hold `WithLocs<RegOrImm>`;
consumers must distinguish register references from typed immediates.

The nine half/bfloat classes (`FmaRnF16`, `FmaRnF16x2`, `FmaHalfRelu`,
`FmaHalfOob`, `FmaHalfOobRelu`, `FmaBf16`, `FmaBf16x2`, `FmaBf16Oob`,
`FmaBf16x2Oob`) now expose instance `WithLocs<RoundingMode> rounding`, replacing
static rounding constants. Read `value.rounding.value` and `.locs`: omitted RN
has value `Rn` and empty locations; explicit RN retains its token location.
Coherent current RN presence edits remain valid; supplied AST association checks
the retained syntax identity independently. `FmaHalfOob::saturate` is removed;
there is no SAT field or implicit SAT permission. These generated C++ layout
changes require rebuilding consumers. The 26 register-only source roles and
all carrier/type/availability contracts remain unchanged; modifier validation
does not execute FMA arithmetic, FTZ, saturation, ReLU or OOB semantics.


<a id="fma-coverage--verification"></a>

### Verification

[C++ resolution tests](../submod/resolved_ir/test/test_select_variant_fma.cpp)
exercise all 92 canonical modifier combinations: 48 half/bfloat products and
44 ordinary/mixed products. Invalid combinations and floating-literal conversion
boundaries remain covered.
[Availability tests](../submod/resolved_ir/test/test_select_variant_fma.cpp)
check each of the 16 variants at its minimum PTX/SM and independently below each
minimum. [Module tests](../submod/resolved_ir/test/test_resolved_module_instruction_types.cpp)
check declared operand types, exact bit containers, and each mixed operand
position. [Owned-control tests](../submod/resolved_ir/test/test_half_fma_modifiers.cpp)
check all 48 half products, default presence, invalid modes, source association,
availability, and independent copies/clones after AST death. The installed
[consumer](../examples/resolved_ir_consumer/main.cpp) reads all nine
instance rounding fields and validates copies/clones without source storage.

The shared Python tests check PTX-section taxonomy in
`python/tests/code_gen/test_ptx_spec_taxonomy.py` and generated Resolved-IR
operand layouts in `python/tests/ir/test_resolved_ir.py`. The installed-wheel
smoke test, `python/tests/code_gen/wheel_smoke.py`, checks the 16 variants and
the independent mixed-precision operand types.

The coverage stated here is defined by the three official PTX 9.3 FMA sections
and the implementation/tests above, not a manual coverage ledger. The older
common-kernel corpus and actual opcode coverage are distinct: corpus presence
is not used as evidence of this closure.

Mixed F32/F16 multiplicands now use RegOrImm and accept numeric floating constants converted once to binary16. The F32 accumulator keeps its existing numeric path. Explicit half-arithmetic register-only forms retain their separate contracts; literal conversion does not execute the FMA rounding/saturation result.


<a id="fma-coverage--ordinary-bf16-fmaset-source-members"></a>

### Ordinary BF16 FMA/SET source members

`FmaMixedF32Bf16`, `SetHalfIntegerBf16`, and
`SetHalfIntegerBf16Boolean` expose `src1` and `src2` as
`WithLocs<RegOrImm>`. Existing class names, identities and field order remain;
C++ consumers must rebuild against matching headers and libraries and use
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>` on `.value`.

These six sources accept numeric Float literals and Float-result expressions.
The existing logical BF16 converter maps `1.5`, `1.0+0.5`,
`0d3ff8000000000000` and `0f3fc00000` to type BF16 and word `0x3fc0`.
FMA uses its fixed `input_type` modifier; SET uses fixed logical BF16.
Integer literals and Integer-result expressions remain excluded, including an
optional integer-origin value of zero. This grant has no integer-immediate policy.
The accepted actual register declaration remains the ordinary scalar `.b16`
carrier; SameWidth does not admit `.f16/.u16/.s16`, special registers, whole
vectors or standalone instruction-only `.bf16` declarations.

Owned validation reads current logical type, low sixteen bits and flags. It
requires no floating lexical history; coherent canonical current-word edits can
validate after source/AST destruction, copying, assignment or cloning. Retained
bad type, high bits, integer origin or negative metadata remain errors. Source
association still checks the provided AST against retained syntax identity.
This scalar numeric conversion adds no packed pair/splat or truncation rule;
packed exact-word and constructor policies remain separate. Instruction rounding,
saturation, comparison, predicate combination and execution are not evaluated.
Targets, destinations, other sources and all packed SET forms keep their contracts.


<a id="integer-arithmetic-coverage"></a>

## Integer Arithmetic Coverage

Technical reference and evidence: [integer_arithmetic_coverage.md](us-en/converage/integer_arithmetic_coverage.md).

<a id="integer-arithmetic-coverage--integer-arithmetic-coverage"></a>

### Integer Arithmetic Coverage

The frontend models all 161 documented type/mode combinations in PTX ISA 9.3
§9.7.1:
`add`, `sub`, `mul`, `mad`, `clmad`, `mul24`, `mad24`, `sad`, `div`, `rem`,
`abs`, `neg`, `min`, `max`, `popc`, `clz`, `bfind`, `fns`, `brev`, `bfe`,
`bfi`, `szext`, `bmsk`, `dp4a`, and `dp2a`. The canonical source is
`python/src/ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml`; parser descriptors,
Resolved IR, and target-aware checking are generated from it.

The documented `min.s16x2.relu` example is accepted as a modifier-order alias
of canonical `min.relu.s16x2`; it does not add a form or canonical variant.

The normative reference is the [archived CUDA 13.3 PTX ISA 9.3
manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
This is frontend syntax and validation coverage, not execution semantics. Form
counts are: add 16, sub 11, mul 16, mad 17, clmad 2, mul24 4, mad24 5, sad 6,
div 6, rem 6, abs 3, neg 4, min 13, max 13, popc 2, clz 2, bfind 8, fns 1,
brev 2, bfe 4, bfi 2, szext 4, bmsk 2, dp4a 4, and dp2a 8.

The model retains the existing full MUL and bit-operation public variants and
appends the remaining forms. `clmad` requires PTX 9.3 and `sm_80`; `fns`
requires PTX 6.0 and `sm_30`; `szext` and `bmsk` require PTX 7.6 and `sm_70`;
and `dp2a`/`dp4a` require PTX 5.0 and `sm_61`. Packed add/sub/min/max/neg
forms retain their documented `sm_90` and `sm_120f` availability.

`fns` checks immediate `base` in `0..31`; register values remain dynamic.
`bmsk` accepts the full 32-bit position and width domain because the manual
defines clamp and wrap behavior for larger values. `szext` likewise accepts a
full unsigned width control. The dot-product forms keep each packed input's
signedness and make destination/accumulator `.u32` only when both inputs are
unsigned, otherwise `.s32`.

The canonical database grows from 72 opcodes and 406 variants to 81 opcodes
and 476 variants. All names and indices of the preceding 72 opcode variants
remain stable. The reused Debug generated subtree measures 13 files, including
9 C++ sources, at 18,444,254 bytes and 381,078 C++ lines; this is an increase
of 1,611,840 bytes and 20,801 C++ lines over the retained baseline. Direct
regeneration into a temporary output completed in 49.885 seconds.

The final Debug build of `test_resolved_ir` with `-j 4` completed 75 Ninja
actions in 69.733 seconds, using an existing configured tree and compiler
cache; this is not a clean-build benchmark. Ordinary Debug CTest passed
884/884 in 11.96 seconds; the installed public package consumer passed 1/1
in 85.75 seconds. Full Python discovery passed 221 tests in 56.646 seconds.
Release validation remains assigned to CI and was not run locally for this change.

The checked-in [CUDA 13.1 `ptxas` evidence](integer_arithmetic_ptxas.json)
exercised 25 modules: 11 accepted, 10 rejected at the intended contract
boundary, and 4 unavailable because that toolchain supports PTX only through
9.1. Normative PTX 9.3 text governs the FNS immediate bound even where the
older assembler accepts an undefined-behavior input.


<a id="ld-coverage"></a>

## LD Coverage

Technical reference and evidence: [ld_coverage.md](us-en/converage/ld_coverage.md).

<a id="ld-coverage--ld-coverage"></a>

### LD Coverage

Ordinary named v2/v4 data registers (at most 128 declared bits) are accepted alongside braces for ordinary LD/ST, LDNC and LDU. Modern 256-bit forms remain brace-only. Selected root `st.async` v2/v4 sources also admit named declarations under the [named source rules](#resolved-ir-design--named-data-vectors); other async forms retain their own roles; see [owned source contract](#resolved-ir-design--named-memory-vector-sources).

This document records the PTX 9.3 `ld` and `ld.global.nc` forms accepted by
the frontend. The machine-readable authority is
`python/src/ptx_frontend/spec/resources/ptx_spec/data_movement_and_conversion.yaml`; this
document describes its public boundary, not simulator execution or GPU
conformance.

The normative sources are NVIDIA's PTX ISA 9.3 [load instruction](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld)
and [non-coherent global load](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld-global-nc).


<a id="ld-coverage--contract"></a>

### Contract

`ld` accepts generic and explicit `.const`, `.global`, `.local`, `.param`, and
`.shared` addressing, including the documented `.param::entry` and
`.param::func` direction/context rules. Scalar types include the integer,
bit-size, and floating forms through `.f64`, plus `.b128` at PTX 8.3 / SM 70.
The `.sys` scope with `.b128` requires PTX 8.4. Destinations retain PTX's
equal-or-wider register rule; alignment is checked whenever the address is
statically known.

The qualified shared forms `.shared::cta` and `.shared::cluster` retain their
state-space identity. They require PTX 7.8; `::cta` requires SM 30 and
`::cluster` requires SM 90 with cluster capability. Omitting the shared
sub-qualifier keeps PTX's `::cta` default without fabricating a cluster
address.

Weak (including omission), volatile, relaxed, and acquire forms are distinct
semantic alternatives. Relaxed/acquire require a scope and permit known global
or shared addresses; volatile additionally permits local addresses at PTX 9.1.
MMIO requires a global address and `.sys` scope; its legal semantic alternatives
and target minima are represented by the generated memory-consistency
descriptor rather than an opcode-specific checker branch. Cache operators and
cache-control hints have distinct semantic restrictions, described below.

Global cache forms cover the documented cache operators, L1 eviction priorities,
L2 eviction priorities (alone and combined with L1), L2 prefetch sizes, and
`L2::cache_hint` with an optional trailing `b64` cache-policy operand. L1 eviction
requires PTX 7.4 / SM 70; L2 prefetch requires PTX 7.4 / SM 75 (256B requires
SM 80); cache policy requires PTX 7.4 / SM 80; and L2 eviction priority requires
PTX 8.8 / SM 100 with a 256-bit `.v8` 32-bit or `.v4` 64-bit vector. Other
vector forms retain legacy 128-bit layouts and the target-qualified modern
256-bit layouts; only modern layouts admit partial-load sinks.
The same controls are available for a generic spelling when declaration
provenance is unknown or known-global; known local, shared, constant, or
parameter addresses are rejected. Eviction, cache-hint, and prefetch controls
admit weak, relaxed, and acquire load semantics. Prefetch without eviction or
cache hint additionally admits volatile. A `.ca`/`.cg`/`.cs`/`.lu`/`.cv` cache
operator is a separate weak-only branch:
it may combine with cache hint and prefetch where PTX permits, but it may not
combine with eviction controls or ordered/volatile/MMIO semantics.

`ld.global.nc` is explicit-global only and covers scalar/vector cache operators,
L1 eviction, L2 prefetch, `L2::cache_hint`, and L1+L2 eviction combinations.
Its base scalar/vector forms require PTX 3.1 / SM 32. L2 eviction is
modern-vector-only; the other families retain their scalar/vector forms and
`.b128` gates. It does not silently fall back to coherent `ld.global`.
Its cache-operator branch is ordered before `.nc` and is intentionally
disjoint from its L1/L2 eviction branch. Every cache-hint form, including one
combined with prefetch, accepts the cache-policy operand either supplied or
omitted; prefetch alone takes no such operand.
Ordinary LD accepts canonical L1-before-L2 ordering and the L2-before-L1 alias
shown in the PTX examples. NC accepts only L1-before-L2 ordering.

An address referencing a declaration with PTX `.attribute(.unified(...))` must
spell `[address].unified` for an admitted ordinary `ld` form. The suffix itself
requires PTX 8.0 / SM 90 and a global or generic address. The qualifier is
typed through CST, Syntax AST, Resolved IR, and checker operand views; validation
uses owned declaration identity after source and AST release. Conversely,
`.unified` is rejected for non-unified known declarations and for LD forms whose
syntax does not admit it.


<a id="ld-coverage--optional-cache-policy-operand"></a>

### Optional cache-policy operand

LD, LDNC and ST accept both supplied and omitted trailing cache-policy operands
with `.L2::cache_hint`, as implemented for
[#281](https://github.com/endingly/ptx_frontend/issues/281). A supplied operand
must be a 64-bit register (logical `b64` operand type); omission retains the
cache-hint form without creating a policy value. The qualifier remains required to supply the operand, and
omission preserves the same memory-space, type, target and vector-layout
restrictions. Earlier CUDA 13.1 PTX 8.0 / SM 90 assembler rejection probes are
historical compiler evidence and do not override the fixed PTX 9.3 syntax.

This is source acceptance and target-aware validation only. It does not allocate
unified memory, implement ordering, predict cache behavior, or simulate load
execution. The dedicated C++ LD test, Python model test, and installed-package
consumer cover accepted and rejected forms through public frontend contracts.


<a id="ldu-coverage"></a>

## PTX 9.3 `ldu` coverage

Technical reference and evidence: [ldu_coverage.md](us-en/converage/ldu_coverage.md).

<a id="ldu-coverage--ptx-93-ldu-coverage"></a>

### PTX 9.3 `ldu` coverage

Ordinary named v2/v4 data registers (at most 128 declared bits) are accepted alongside braces for ordinary LD/ST, LDNC and LDU. Modern 256-bit forms remain brace-only. Selected root `st.async` v2/v4 sources also admit named declarations under the [named source rules](#resolved-ir-design--named-data-vectors); other async forms retain their own roles; see [owned source contract](#resolved-ir-design--named-memory-vector-sources).

The frontend models the documented supported forms in PTX ISA 9.3 §9.7.9.10. `ldu` loads
read-only global data from an address uniform across the warp. The frontend
checks instruction shape and address provenance; it does not prove that the
address is uniform or that the data is read-only at runtime.

| Form | State space | Destination | Availability |
| --- | --- | --- | --- |
| `ldu.type d, [a]` | Omitted suffix uses generic addressing to global data | Scalar register | PTX 2.0 / SM 20 |
| `ldu.global.type d, [a]` | Explicit global | Scalar register | PTX 2.0 |
| `ldu.v2/v4.type d, [a]` | Omitted suffix uses generic addressing to global data | Two or four registers | PTX 2.0 / SM 20 |
| `ldu.global.v2/v4.type d, [a]` | Explicit global | Two or four registers | PTX 2.0 |

Scalar forms accept `.b/.u/.s` at 8, 16, 32, and 64 bits, plus `.f32`,
`.f64`, and `.b128`. Vector loads are limited to 128 total bits: v2 accepts
elements through 64 bits, including `.f64`; v4 accepts elements through
32 bits. `.b128` is scalar only. `.f64` requires SM 13; `.b128` requires PTX
8.3 and SM 70. The generic-addressing minimum also applies when the type has
a lower minimum.
Destination register elements must be at least as wide as the load type. The
checker enforces type-compatible registers, vector arity, alignment, and known
global address provenance. An unknown address register may represent a global
generic pointer. Vector destination placeholders (`_`), `.v8`, cache controls,
and state spaces other than `.global` are outside the documented frontend
contract. Some `ptxas` versions accept `_` vector elements, but PTX 9.3 does
not document them for `ldu`.


<a id="logic-shift-coverage"></a>

## Logic and Shift Coverage

Technical reference and evidence: [logic_shift_coverage.md](us-en/converage/logic_shift_coverage.md).

<a id="logic-shift-coverage--logic-and-shift-coverage"></a>

### Logic and Shift Coverage

The generated [PTX 9.3 §9.7.8 logic-and-shift
contract](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#logic-and-shift-instructions)
resolves and target-checks the whole named logic-and-shift family: 38 concrete
spellings represented by 37 canonical variants (the `lop3` Boolean-result
variant covers both `.and` and `.or`). This is frontend validation only; it
does not provide execution semantics or simulator support.

`and`, `or`, `xor`, and `not` accept the documented `.pred`, `.b16`, `.b32`,
and `.b64` types. Predicate sources retain PTX predicate constants and
negation. `cnot` accepts `.b16`, `.b32`, and `.b64`.

`lop3.b32` requires a truth-table immediate in `0..255`. Its `.and` and `.or`
layouts, available from PTX 8.2 / SM 70, retain a `d|p` output and predicate
control; only those layouts permit `_` for the data output. Base `lop3` remains
PTX 4.3 / SM 50.

`shf` supports each official `.l/.r` and `.clamp/.wrap` combination at PTX 3.1
/ SM 32. Its count remains a full unsigned 32-bit operand; the frontend does
not incorrectly limit it to the mode's runtime effective range. `shl` supports
`.b16/.b32/.b64`, while `shr` supports the bit, unsigned, and signed 16-, 32-,
and 64-bit spellings. Shift counts are always 32-bit and destination/source
widths must match.


<a id="mad-coverage"></a>

## Floating `mad` coverage

Technical reference and evidence: [mad_coverage.md](us-en/converage/mad_coverage.md).

<a id="mad-coverage--floating-mad-coverage"></a>

### Floating `mad` coverage

`mad.f64` additionally accepts PTX 1.0–1.3 on SM >=13 as `MadImplicitF64`, with round-to-nearest-even interpretation and no rounding field. It does not inherit `map_f64_to_f32`; PTX >=1.4 requires explicit rounding. Source omission and typed interpretation are separate; see [the public contract](#resolved-ir-design--historical-omitted-floating-controls).

This slice models distinct implicit and explicit rounding forms through the canonical YAML, typed Resolved IR, and target-aware checker. It covers the forms below from [PTX 9.3 §9.7.3.7](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mad), retains integer and carry `mad` variants and the source opcode, and does not add arithmetic execution.

| Form | Minimum | Contract |
| --- | --- | --- |
| `mad{.ftz}{.sat}.f32` | PTX 1.0 / exact generic `sm_10`, `sm_11`, `sm_12`, `sm_13` | Rounding is absent. `.ftz` and `.sat` are independently optional in that order. Same FP32 ternary operand contracts as the explicit form. |
| `mad.{rn,rz,rm,rp}{.ftz}{.sat}.f32` | PTX 2.0 / SM 20 | Rounding is required. `.ftz` and `.sat` are independently optional. Each source admits a typed floating literal or same-width `.f32`/`.b32` register; the destination is a same-width `.f32`/`.b32` register. |
| `mad.{rn,rz,rm,rp}.f64` | PTX 1.0 / SM 13 | Rounding is required. No `.ftz` or `.sat`. Each source admits a typed floating literal or same-width `.f64`/`.b64` register; the destination is a same-width `.f64`/`.b64` register. |

Integer literals or integer register containers, wrong widths, destination sinks, unsupported FP64 flags, and the wrong operand count are rejected. FP64 rounding is required from PTX 1.4. The documented PTX 1.4 requirement makes rounding mandatory for `mad.f64`; it is not the introduction floor for explicit rounded FP64 forms. CUDA 13.3.73 `ptxas` accepts explicit `.rn` and `.rz` FP64 forms at PTX 1.3 with an `sm_90` assembler override, consistent with the PTX 1.0 model floor. That historical probe establishes syntax and contract evidence only, not old-GPU code generation.

The omitted-rounding FP32 form resolves as `MadImplicitF32` without a rounding field, preserving source modifier ranges and 32-bit operands. It is never rewritten to `.rn` or `fma`. Availability is limited to the four supported catalog profiles above, not every historical GPU profile; it rejects SM 20 and modern or flavored targets. Explicit-rounding FP32 forms retain their SM 20 minimum and reject those sm1x targets. PTX 3.0/3.1 errata/default behavior remains excluded. No contemporary `ptxas` legacy oracle is required or claimed for the implicit form.

CUDA 13.3.73 `ptxas` evidence used `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`. Its 32-module modern matrix accepted all four rounding forms for FP32 and FP64, floating immediates, and matching bit containers; it rejected 14 invalid flag, type, shape, and sink cases. [Explicit floating DIV](#div-coverage), [reciprocal and square-root forms](#unary-float-coverage), [floating transcendentals](#transcendental-coverage), and [floating MIN/MAX](#min-max-coverage) are now modelled separately. [Floating and mixed ADD/SUB](#add-sub-coverage) completes the reconciliation; see that document for the ADD/SUB contracts.


<a id="matrix-coverage"></a>

## Matrix fragment coverage

Technical reference and evidence: [matrix_coverage.md](us-en/converage/matrix_coverage.md).

<a id="matrix-coverage--matrix-fragment-coverage"></a>

### Matrix fragment coverage




<a id="matrix-coverage--named-matrix-fragments"></a>

### Named matrix fragments

Eligible MMA fragments, WGMMA destinations/register-A fragments, and WMMA MMA
A/B/C fragments accept an ordinary declared `.reg.v2` or `.reg.v4` whole
identifier where the exact role has two or four physical registers. Element
policy checks each physical lane type; logical matrix element types and existing
cardinality remain independent. The complete declaration must fit within 128 bits.
This does not add named v1/v8/v128 syntax or a legal `.v4.f64` declaration.

Owned IR retains the whole declaration SymbolId and each ordered NamedProjection,
including source ranges. Current validation checks those facts after source/AST
release and after copy, assignment, or clone. A permission flag alone cannot
replace provenance. Existing legal large brace fragments remain supported.

When WGMMA register-A and shared-descriptor forms share identifier syntax, the
private resolver uses actual lexical declaration shape with paired typed
descriptors: whole vectors choose the authorized fragment path and scalar
registers choose the descriptor path. Wrong vector count/type remains an error;
unknown declarations do not prove scalarity. The public syntax-only selector
keeps its context-free contract and can report ambiguity. Selection never chooses
by target availability or by trial resolution. WMMA load/store operands and WMMA MMA destinations,
address/descriptors, scale IDs, sparse metadata, and other excluded roles retain
their existing brace/scalar/tuple rules. GPU scheduling and runtime resource
validity remain outside frontend validation.

The specification loader admits declaration-separable overlap only when every
layout cross-pair proves separation. The added proof uses an IdentifierRef-only
intersection, explicit named Element permission, an exact existing two/four-lane
count and a scalar SharedMatrixDescriptor competitor. Missing facts, Aggregate
policy, generic scalar roles or another shared syntax alternative supply no proof.
Targets and defaults do not resolve overlap. This validation proof preserves the
public selector's exact ambiguity diagnostic at the instruction range.


<a id="matrix-reference-interpretations"></a>

## Matrix reference interpretations

Technical reference and evidence: [matrix_reference_interpretations.md](us-en/converage/matrix_reference_interpretations.md).

<a id="matrix-reference-interpretations--matrix-reference-interpretations"></a>

### Matrix reference interpretations

This note records bounded frontend interpretations of conflicting clauses in the
pinned [PTX ISA 9.3 reference](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
These are project engineering interpretations, not NVIDIA-issued errata. Each
interpretation states its own scope; it does not establish whole-matrix coverage
or hardware execution conformance. [Chinese technical reference](zh-han/converage/matrix_reference_interpretations.md)


<a id="matrix-reference-interpretations--wmma-m8n8k4-f64-fragments"></a>

### WMMA `m8n8k4` F64 fragments

For `wmma.load`, `wmma.mma` and `wmma.store` on this shape, each thread has one
64-bit register for A and B, and two 64-bit registers for C and D. The
[fragment table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-fragment)
lists a conflicting single-register F64 C/D entry. We adopt two: the
[shape definition](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)
makes C/D an 8×8 matrix with 64 F64 elements, requiring two elements per thread
across 32 threads. A is 8×4 and B is 4×8, requiring one element per thread.
The [store example](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-st)
and [MMA example](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-mma)
use two C/D registers and agree with this capacity argument. This resolves this
particular inconsistency; it is not a general precedence rule for examples.

```ptx
wmma.load.c.sync.aligned.m8n8k4.col.global.f64 {c0,c1}, [p], 8;
wmma.mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {d0,d1}, {a}, {b}, {c0,c1};
wmma.store.d.sync.aligned.m8n8k4.row.global.f64 [p], {d0,d1}, 8;
```

C/D fragments occupy 16 bytes and require 16-byte memory alignment. Leading
dimensions remain layout-dependent element strides, independently checked for
minimum and divisibility. Both row/column layouts, existing memory spaces,
rounding default `.rn` and explicit `.rn/.rz/.rm/.rp` remain available. The base
floor is PTX 7.0 / `sm_80`; `shared::cta` retains its separate PTX 7.8 floor.
Ordinary scalar F64 and compatible B64 data carriers retain their existing rules.
Physical element distribution across lanes/registers remains architecture-dependent.

The canonical rules already express this interpretation. Focused regressions in
[`test_wmma_coverage.cpp`](../submod/resolved_ir/test/test_wmma_coverage.cpp)
and [`test_wmma_coverage.py`](../python/tests/spec/test_wmma_coverage.py)
check the exact 36 static identities separately from source programs, reject
A/B pairs and C/D singletons/triples, and check independently mutated owned
fragments after syntax owners die. Parser acceptance of a brace pack alone does
not imply semantic acceptance. Owned copies/clones, supplied-source validation,
alignment, scalar width, target and stride controls remain separate checks.


<a id="matrix-reference-interpretations--standard-f64-mma-shape-spelling"></a>

### Standard F64 MMA shape spelling

The accepted dense F64 shape is `.m8n8k4`. The `.m8n84` spelling in the F64
[Syntax subsection](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-mma)
is treated as an editorial typo and is not a compatibility alias. The
[shape table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape),
dedicated [F64 fragment subsection](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-fragment-mma-884-f64),
ISA notes and examples consistently identify the full M/N/K shape `m8n8k4`.
The project retains this well-formed spelling without source rewriting or a lexer
special case. This is a bounded engineering interpretation, not a vendor erratum.

```ptx
mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {d0,d1}, {a}, {b}, {c0,c1};
```

This form has `MatrixShape{8,8,4}`, row/column A/B layouts, F64 scalar types,
and D/A/B/C register counts 2/1/1/2. Its minimum is PTX 7.0 / `sm_80`, with
default `.rn` and explicit `.rn/.rz/.rm/.rp` rounding. The separate F64
`m16n8k4`, `m16n8k8` and `m16n8k16` forms retain PTX 7.8 / `sm_90` floors.
They are adjacent controls, not aliases for this shape.

The canonical rules already implement this interpretation. Focused tests in
[`test_warp_matrix_mma_coverage.cpp`](../submod/resolved_ir/test/test_warp_matrix_mma_coverage.cpp)
and [`test_warp_matrix_mma_coverage.py`](../python/tests/spec/test_warp_matrix_mma_coverage.py)
join the exact raw/normalized/IR identity and distinguish generic CST/AST
acceptance from semantic resolution: `.m8n84`, `.m8n8`, `.m8n8k` and `.m8n8k5`
are parsed but rejected as unknown modifiers during resolution. No owned typo
variant exists. Its absence is checked through source selection; generated
shape/suffix metadata is immutable. Separate tests mutate actual owned fragments,
layout tags and coherently bound F32 references, retaining diagnostic ranges,
source association, AST-free copies/clones and independent target gates.
These checks do not authenticate live register contents or physical fragment placement.


<a id="matrix-reference-interpretations--ordered-sparse-low-bit-kind-separator"></a>

### Ordered sparse low-bit kind separator

The accepted low-bit suffix is `.kind::f8f6f4`, including its leading dot, in the
`mma.sp::ordered_metadata.sync.aligned.m16n8k64` family. The missing dot in the
kind-domain definition `{kind::f8f6f4}` in the [sparse MMA subsection](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-sparse-mma)
is interpreted as damaged metalanguage, not an alternate source spelling.
The suffix separator and the subsection's fully written examples agree on
`.kind::f8f6f4`; the [shape table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)
identifies the ordered low-bit shape/type family. This is a project engineering
interpretation, not a vendor-issued erratum or a new modifier-order restriction.

```ptx
mma.sp::ordered_metadata.sync.aligned.m16n8k64.row.col.kind::f8f6f4.f32.e3m2.e2m3.f32
  {d0,d1,d2,d3}, {a0,a1,a2,a3}, {b0,b1,b2,b3}, {c0,c1,c2,c3}, meta, 0;
```

The existing catalogue contains 50 identities: each A/B type independently ranges
over `e4m3`, `e5m2`, `e3m2`, `e2m3`, `e2m1`, with equal F16/F16 or F32/F32
D/C types. A/B each use four B32 containers. F16 D/C each use two F16x2
registers; F32 D/C each use four F32 registers. Ordered metadata uses a B32
register and selector zero. Existing availability remains PTX 8.7 / `sm_120a`
or PTX 8.8 / the qualified `sm_120f` family; a generic `sm_120` profile does
not provide that qualification.

The canonical rules already express this interpretation. Focused tests in
[`test_warp_matrix_mma_coverage.cpp`](../submod/resolved_ir/test/test_warp_matrix_mma_coverage.cpp)
and [`test_warp_matrix_mma_coverage.py`](../python/tests/spec/test_warp_matrix_mma_coverage.py)
check the 50 static identities separately from five representative source tuples.
Missing separators and unknown kinds reach resolution and reject as unknown
modifiers. Duplicate dots produce parser diagnostics even when recovery returns
CST/AST values. Native `.sp`, unequal D/C types and a block-scaled kind without
its separate syntax reject variant selection. Selector values 1–3 fail the range
check independently in source and coherently mutated owned state. No accepted
alias or mutable generated kind/order field is introduced.

Owned fragment counts, physical scalar widths, metadata binding identity, source
association, copies/clones after AST destruction, and qualified targets remain
separate controls. Checking a container's type or a metadata register's identity
does not authenticate live low-bit packing or ordered metadata contents. Those
runtime obligations remain outside this spelling interpretation.


<a id="matrix-reference-interpretations--dense-integer-wgmma-n-boundary"></a>

### Dense integer WGMMA N boundary

Dense integer WGMMA includes `m64n240k32` and `m64n256k32`. The integer Syntax
list in the [instruction subsection](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-instructions-wgmma-mma)
ends at N=224, while the [shape table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-shape)
explicitly includes N=240/256 and the [integer fragment contract](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-register-fragment-wgmma-64n32)
consistently requires N/2 S32 D registers. We adopt the dedicated shape and
fragment contract over this incomplete Syntax enumeration. This is a bounded
engineering interpretation, not a vendor-issued erratum or a blanket table
precedence rule. See also [WGMMA coverage](#wgmma-coverage).

The integer N grid is `8,16,24,32,48,64,...,256`, with step 16 after 32.
N=240 uses 120 scalar S32 D registers and N=256 uses 128. Integer N=232 and
248 are off-grid despite being multiples of eight; N=264 is outside the range.
Dense integer K remains 32. Floating and sparse families retain separate grids
and selector domains.

The two boundary sizes retain all four S8/U8 A/B type pairs, both A placements
(shared descriptor or four B32 register lanes), and plain/satfinite forms:
32 distinct canonical identities. B is a B64 shared descriptor in both placements.
D is read/write. The 128-lane accumulator is an explicit scalar brace pack;
it grants no ordinary named-vector permission. Same-sign forms retain PTX 8.0,
mixed-sign forms PTX 8.4, and all require exact `sm_90a`. Generic `sm_90` or
higher numeric SM profiles do not inherit that target qualification.

The canonical rules already include these identities. Focused C++ regressions in
[`test_wgmma_coverage.cpp`](../submod/resolved_ir/test/test_wgmma_coverage.cpp)
run all 32 source templates, inspect exact classes and static topology, and
separately mutate real owned fragment counts, layout tags and descriptor carriers
after parser/AST destruction. Source cardinalities 119/121 and 127/129 reject;
the independent current 129-lane check remains within guarded view access.
Copies/clones, supplied-source association and matched target/version controls
remain separate evidence. The standard normalization harness independently checks
the exact 32-key projection. Fence/commit/wait identities remain unchanged;
these checks do not authenticate descriptor contents, physical fragment packing,
dynamic protocol completion or GPU results.


<a id="matrix-reference-interpretations--k96-encoded-scale-ids-and-physical-layout-facts"></a>

### K96 encoded scale IDs and physical layout facts

For dense MX4 and MX NV K96, the adopted Table 47 instruction-word domain for
scale A/B IDs is **{0,2}**, rejecting **1/3**. A occupies bits 29–30 and B bits
4–5, each a two-bit dimensionless selection code; bit 31 is the dense K choice.
Deep K96 figures label `SFA_ID`/`SFB_ID` positions 0/1/2/3. Their conflicting
1/3 labels do not widen the serialized field domain. The project gives the
instruction-descriptor table precedence for encodings and uses the deep clauses
for physical layout facts. This engineering interpretation retains the reference
discrepancy; it is not a NVIDIA-issued erratum. See [MX4](#tcgen-mma-mx4-coverage),
[MX NV](#tcgen-mma-mxnv-coverage) and the [family summary](#tcgen-mma-family-coverage).

Three quantities remain distinct:

- **Encoded ID:** a dimensionless word-field code, independently checked as 0/2.
- **Illustrated physical layout index:** a figure's placement label, without an
  established numeric conversion from the encoded ID.
- **Alignment guarantee:** a caller claim in bytes about the selected sub-column.

K96 block32 has three factors with A `Mx3` and B `3xN`. NV block16 has six
factors with A `Mx6` and B `6xN`. Both require a four-byte sub-column alignment
guarantee and retain `LAYOUT_DEFINED_PLACEMENT`. Stronger positive power-of-two
guarantees, such as 8 or 16 bytes, are allowed. Encoded ID 2 with alignment 4 or
8 is valid: do **not** compute `ID % alignment`, multiply the ID into a byte
offset, equate encoded and physical indices, or normalize forbidden 1/3 into 0/2.
No new encoded-to-physical mapper or allocation rule is introduced.

Supplied alignment 0, 2 or 3 is a known invalid guarantee. Omitted role, layout
identity or alignment remains an explicit missing obligation, not an implicit
zero or complete validity. Reports may accept every supplied fact while retaining
missing layout/live-content obligations. Correct bits and caller layout facts do
not authenticate opaque source `idesc` contents, actual packing, scale contents,
allocation or completion. Word/fact value copies and owned-source copies remain independent.

Known dense K96 retains group 2, M256 and exact `sm_103a` from PTX 8.8, with
existing scale-type restrictions. Known K96 plus written `.scale_vec::2X` or
`.scale_vec::4X` retains the existing empty target intersection with those vector
forms' exact `sm_100a` requirement and unresolved physical layout-rule obligations.
Table 59 factor counts do not create missing physical rows. Source vector forms
can remain legal on `sm_100a` while K is unknown. This interpretation adds no
selector spelling, target permission, word authentication or optional Tensor solver.

Existing code already expresses this decision. Focused C++/Python regressions
check both field masks, all three block rows, ID/alignment combinations, wrong
layouts, missing facts and source/known-word separation. Live contents and GPU
execution remain outside these checks.


<a id="matrix-reference-interpretations--logical-descriptor-carriers-and-fundamental-bit-data"></a>

### Logical descriptor carriers and fundamental bit data

The pinned PTX 9.3 [§6.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#operand-type-information) and [§9.4/Table 26](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#type-information-for-instructions-and-operands) describe generic fundamental data compatibility. The [WGMMA matrix descriptor](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-shared-memory-layout-matrix-descriptor) and [TCGEN shared descriptor](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tcgen05-shared-memory-descriptor) instead encode independently defined address, stride and layout/control fields. The manual does not explicitly enumerate every permitted declared carrier for these roles. Giving the specific encoded control/address role priority here is a disclosed engineering interpretation, not a NVIDIA-issued erratum; opacity by itself is not the rationale.

Seven opaque logical roles require scalar General registers with an exact width: WGMMA shared A/B, TCGEN copy `s_desc`, TCGEN MMA shared A/B and TCGEN WS `zero_column_desc` use 64 bits and accept B64/U64/S64; TCGEN MMA `idesc` uses 32 bits and accepts B32/U32/S32. `idesc` is an **instruction descriptor**, not a TMA im2col descriptor. These sets describe declared carriers, not merely the internal `.b64`/`.b32` storage tag. Same-width F64/F32 declarations are rejected for these roles. Wrong width, an ordinary whole `v2` register, predicate class, immediate and inappropriate bracket-address shapes cannot supply the scalar descriptor. Bracketed 32-bit A remains the separate Tensor Memory source alternative.

For example, `mov.b64 %bits64, %float64;` with `.reg .b64 %bits64; .reg .f64 %float64;` is valid fundamental bit data, while supplying `%float64` directly as a WGMMA shared descriptor is rejected. Likewise, `mov.b32 %bits32, %float32;` accepts F32 as bit data; `%bits32` may supply `idesc`, while `%float32` cannot. Explicit bit copying into B32/B64 establishes a permitted carrier; it does not prove that the copied bits encode a valid runtime descriptor. Ordinary `add.rn.f64` and an actual `mbarrier.arrive.b64` F64 arrival-state output remain valid and are not matrix descriptor roles.

The generic `SameWidth` and `Exact` policies are unchanged. TCGEN output-lane masks retain their separate B32/U32/S32/F32 policy. Tensor Memory addresses, predicates, matrix fragments, ordinary memory data and other opaque resources keep their own contracts; descriptor opacity does not impose a global integer-carrier rule or grant component/named-vector permissions.

Owned validation rechecks actual symbol identity, declaration, scalar shape, role and source provenance after AST destruction. Coherent retargeting to a real B/U/S symbol is allowed; a forged cache cannot bless a known floating declaration. A bound symbol missing its declared type is rejected. A standalone unknown General reference may defer its type check, but this does not satisfy module binding. Copies and instruction clones own their payloads; descriptor views borrow their current owner and must be reacquired after copying.

See [WGMMA](#wgmma-coverage), [TCGEN descriptor facts](#tcgen-descriptor-coverage) and [TCGEN F8/F6/F4](#tcgen-mma-f8f6f4-coverage). Runtime descriptor bits, alignment/layout, placement and asynchronous completion remain obligations beyond these carrier checks.


<a id="mbarrier-test-wait-coverage"></a>

## `mbarrier.test_wait` qualifier coverage

Technical reference and evidence: [mbarrier_test_wait_coverage.md](us-en/converage/mbarrier_test_wait_coverage.md).

<a id="mbarrier-test-wait-coverage--mbarriertest_wait-qualifier-coverage"></a>

### `mbarrier.test_wait` qualifier coverage

The frontend models the `.sem.scope` pair for each of the ten existing
`mbarrier.test_wait` shapes in [PTX ISA 9.3 §9.7.14.16.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-mbarrier-test-wait-mbarrier-try-wait).
Both qualifiers must be written, in order, before the shared-space qualifier and
`.b64`: `.acquire|.relaxed` followed by `.cta|.cluster`. A lone qualifier, the
reverse order, and `.shared::cluster` wait objects are rejected. The scope
qualifier names the synchronization scope; `.shared::cta` names the object's
address space. They are independent.

The ten paired forms retain the existing token and parity operands for generic,
`.shared`, and `.shared::cta` addresses. Explicit `.phase_type::primary` retains
its ordinary, report-predicate, and report-value layouts. Explicit
`.phase_type::conditional` requires `.parity` and has no report operands. The
frontend checks eight-byte address alignment and immediate phase parity in
`0..1`; it preserves the explicit qualifier values and their source locations
in owned resolved IR.

The basic explicit pair starts at PTX 8.0 and `sm_80`. `.relaxed` starts at PTX
8.6 and requires `sm_90`; `.cluster` requires `sm_90` and the cluster
capability. Phase-type and report forms require PTX 9.3 and `sm_90`.
Unqualified forms remain distinct: absence is retained in the source model,
while their effective semantics are acquire at CTA scope. The frontend checks
instruction-local syntax, operands, and target availability; it does not prove
mbarrier phase progress, participating threads, or runtime memory visibility.
`mbarrier.try_wait` qualifier forms are outside this coverage.

Non-parity state inputs retain explicit Register token policy in generated descriptors and reject sink payloads during both source resolution and AST-free checking. The same common rule distinguishes CTA arrival RegisterOrSink from remote cluster Sink outputs; ordinary report-value registers are excluded. Optional descriptor absence denotes non-token policy. The public optional-member migration requires rebuilding consumers; it does not change the independent carrier-typing rules, source qualifiers, availability, or runtime token/phase obligations.

The written `.b64` suffix does not make every state role a Bit operand. Non-parity wait input typing is U64 with same-width B64/S64/U64 carriers; F64 is rejected. In contrast, opaque arrival outputs and `pending_count` inputs accept same-width B64/S64/U64/F64. Actual declarations and source provenance remain visible, and form, target, address, count, parity, and time-hint checks are unchanged. Report values retain qualified logical B8 with B8/S8/U8 carriers; the fixed PTX 9.3 report-role width mapping remains a reference gap. These checks do not authenticate runtime state or establish phase progress.


<a id="mbarrier-try-wait-coverage"></a>

## `mbarrier.try_wait` qualifier coverage

Technical reference and evidence: [mbarrier_try_wait_coverage.md](us-en/converage/mbarrier_try_wait_coverage.md).

<a id="mbarrier-try-wait-coverage--mbarriertry_wait-qualifier-coverage"></a>

### `mbarrier.try_wait` qualifier coverage

The frontend models the `.sem.scope` pair for all ten existing
`mbarrier.try_wait` shapes in [PTX ISA 9.3 §9.7.14.16.19](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-mbarrier-test-wait-mbarrier-try-wait).
Both qualifiers must be explicit and ordered: `.acquire|.relaxed` immediately
followed by `.cta|.cluster`, before the object space and `.b64`. A lone
qualifier, reversed pair, or `.shared::cluster` wait object is rejected.
The synchronization scope and the `.shared::cta` object space are distinct.

Each paired form retains its unqualified counterpart's token or parity
operand, generic/`.shared`/`.shared::cta` address choices, and optional
`timeHint` operand. `timeHint` is a `.u32` register or immediate in
nanoseconds. Explicit `.phase_type::primary` retains all six layouts: ordinary,
report-predicate, and report-value results, each with or without `timeHint`.
Explicit `.phase_type::conditional` requires `.parity` and has only the
ordinary with/without-hint layouts. Immediate parity must be `0` or `1`, and
the object address must be aligned to eight bytes. Owned resolved IR retains
the explicit qualifier values, locations, and chosen operand layout.

All `try_wait` forms require `sm_90`. The explicit pair starts at PTX 8.0;
`.relaxed` starts at PTX 8.6, and `.cluster` scope additionally requires the
cluster capability. Phase-type and report forms require PTX 9.3. Unqualified
forms remain distinct in the source model; their effective semantics default
to acquire at CTA scope. The frontend validates instruction-local contracts
but does not simulate suspension, the time limit, phase progress, or memory
visibility at runtime.

Non-parity state inputs retain explicit Register token policy in generated descriptors and reject sink payloads during both source resolution and AST-free checking. The same common rule distinguishes CTA arrival RegisterOrSink from remote cluster Sink outputs; ordinary report-value registers are excluded. Optional descriptor absence denotes non-token policy. The public optional-member migration requires rebuilding consumers; it does not change the independent carrier-typing rules, source qualifiers, availability, or runtime token/phase obligations.

The written `.b64` suffix does not make every state role a Bit operand. Non-parity wait input typing is U64 with same-width B64/S64/U64 carriers; F64 is rejected. In contrast, opaque arrival outputs and `pending_count` inputs accept same-width B64/S64/U64/F64. Actual declarations and source provenance remain visible, and form, target, address, count, parity, and time-hint checks are unchanged. Report values retain qualified logical B8 with B8/S8/U8 carriers; the fixed PTX 9.3 report-role width mapping remains a reference gap. These checks do not authenticate runtime state or establish phase progress.


<a id="membar-coverage"></a>

## `membar` coverage

Technical reference and evidence: [membar_coverage.md](us-en/converage/membar_coverage.md).

<a id="membar-coverage--membar-coverage"></a>

### `membar` coverage

The frontend models the three plain memory-barrier levels from
[PTX ISA 9.3 §9.7.14.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-membar-fence):

| Source form | Minimum PTX | Minimum target | Owned IR scope |
| --- | --- | --- | --- |
| `membar.cta;` | 1.4 | Any | `MemoryScope::Cta` |
| `membar.gl;` | 1.4 | Any | `MemoryScope::Gpu` |
| `membar.sys;` | 2.0 | `sm_20` | `MemoryScope::Sys` |

`membar.proxy.alias;` is also modeled as a distinct, operand-free variant. It
requires PTX 7.5 and `sm_60` and orders accesses through the generic and
virtual-alias proxies in both directions. Its fixed `.proxy.alias` spelling is
retained as two fixed flags.

The four async-proxy spellings are also operand-free and retain their written
space as a typed `AsyncProxyKind` in owned IR:

| Source form | Minimum PTX | Minimum target | Extra capability |
| --- | --- | --- | --- |
| `membar.proxy.async;` | 8.0 | `sm_90` | None |
| `membar.proxy.async.global;` | 8.0 | `sm_90` | None |
| `membar.proxy.async.shared::cta;` | 8.0 | `sm_90` | None |
| `membar.proxy.async.shared::cluster;` | 8.0 | `sm_90` | `cluster` |

These forms order generic-proxy and async-proxy accesses in both directions;
the written state space limits that ordering. The `sm_90` floor is a conservative
inference from the ISA's async-proxy target notes and the existing
`fence.proxy.async` contract; the ISA does not state a separate
`membar.proxy.async` target floor. CUDA 13.3 `ptxas` rejects these `membar`
spellings even at `sm_90`, so assembler acceptance is not claimed. Frontend
selection, owned IR, and target checking are the modeled contract.

Each supported spelling selects a distinct `Membar` variant with no operands. The
source `.gl` token maps to the typed GPU scope, while `.gpu` remains a
different spelling and is not accepted for `membar`. The checker enforces the
PTX and target floors on owned IR. Other suffixes and operands are outside this
coverage. The frontend does not execute barriers.


<a id="min-max-coverage"></a>

## Floating `min`/`max` coverage

Technical reference and evidence: [min_max_coverage.md](us-en/converage/min_max_coverage.md).

<a id="min-max-coverage--floating-minmax-coverage"></a>

### Floating `min`/`max` coverage

The generated model completes the floating [minimum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-min) and [maximum](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-max) forms of [PTX 9.3 §9.7.3.11–§9.7.3.12](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions) and their half/bfloat cohorts in §9.7.4.7 and §9.7.4.8. Sign/magnitude controls are typed: `.xorsign.abs` and `.abs` are distinct tokens with distinct slot identities (`xorsign_abs` and `abs`), so `.xorsign` cannot be spelled without `.abs`, and a consumer never has to infer that a paired operation is an absolute-magnitude operation from an `abs` field reading false. The frontend models parsing, owned resolution, and target validation; it does not execute comparison or selection.

| Form | Minimum | Contract |
| --- | --- | --- |
| `min{.ftz}{.NaN}{.xorsign.abs}.f32 d, a, b` | base PTX 1.0 / all target architectures; `.ftz` PTX 1.4; `.NaN` PTX 7.0 / SM 80; `.xorsign.abs` PTX 7.2 / SM 86 | Paired sign/magnitude. Register destination; both sources admit a floating literal or a same-width register. |
| `min{.ftz}{.NaN}{.abs}.f32 d, a, b, c` | PTX 8.8 / SM 100 | Three sources; `.abs` alone, `.xorsign` forbidden. Register destination; all three sources admit a floating literal or a same-width register. |
| `min.f64 d, a, b` | PTX 1.0 / SM 13 | No FP32-only flags. Register destination; both sources admit a floating literal or a same-width register. |
| `min{.ftz}{.NaN}{.xorsign.abs}.{f16,f16x2} d, a, b` | PTX 7.0 / SM 80 | `.ftz` optional; register-only. `.f16` takes `.f16` or `.b16`, `.f16x2` takes `.f16x2` or `.b32`. |
| `min{.NaN}{.xorsign.abs}.{bf16,bf16x2} d, a, b` | PTX 7.0 / SM 80 | No `.ftz`; register-only, and BF16 binds the `.b16`/`.b32` bit containers only. |

`max` carries the identical table. The two-source and three-source FP32 forms are **two operand layouts of one variant**, selected purely by operand count, and each layout declares the modifier slots it rejects. That is why `min.xorsign.abs.f32` is accepted at three operands and rejected at four, while `min.abs.f32` is the reverse: the coupled spelling belongs to the two-source topology and the three-source cohort takes `.abs` alone. Layout selection is shape-only, so this legality is enforced by the checker rather than by variant selection. Variant exclusivity stays conservative over the full syntactic modifier language the resolver matches against, because `select_variant_name` does not consult layout constraints.

Only the scalar FP32 and FP64 cohorts admit literals, and only in source positions; the half and bfloat cohorts stay register-only. Integer literals and integer register containers, wrong widths, sinks, literal destinations, `.ftz` on any BF16 cohort, `.abs` on FP64 or on any non-FP32 cohort, and every arity other than three or four for the FP32 cohort are rejected.

CUDA 13.3.73 `ptxas` evidence used `/usr/local/cuda/bin/ptxas -arch=<arch> <module>.ptx -o <temporary>.o`. Its 58-module matrix had 35 expected accepts, 23 rejects, and no assembler divergence. The rejections are corroborated by the assembler's own diagnostics, which name the same contracts the model enforces: `Modifier '.abs' requires modifier '.xorsign'` for the bare two-source spelling, `Modifier '.xorsign' requires modifier '.abs'` for the bare fragment, `Illegal modifier '.xorsign' for instruction 'min'` for the three-source form, and `Illegal modifier '.ftz' for instruction 'min'` for the BF16 cohorts. Literal admissibility is confirmed in both directions: `min.f32 %f0, 1.0, %f1;`, `min.f32 %f0, %f1, 0f00000000;` and `min.f64 %d0, %d1, 1.0;` are accepted, while `min.f32 1.0, %f1, %f2;` fails with `Result register required for instruction 'min'`, `min.f32 %f0, 1, %f1;` with `Arguments mismatch`, and every half/bfloat literal spelling with `Arguments mismatch`. The floors are confirmed in both directions where the toolchain permits: `.target sm_80` accepts `.NaN` and rejects it at `sm_75` (`Modifier '.NaN' requires .target sm_80 or higher`), `sm_86` accepts `.xorsign.abs` and rejects it at `sm_80`, and the three-source form requires `sm_100` (`Feature 'min.f32 with 3 input operands' requires .target sm_100 or higher`). Paired `.version` probes place the version floors at 7.0 and 7.2 (`Feature '.abs' requires PTX ISA .version 7.2 or later`, rejected at 7.1) and 8.8 for the three-source form.

The audit also recorded an assembler-permissive container result the model follows rather than contradicts: `min.f16x2` accepts both `.f16x2` and `.b32` operands, so the packed half cohort binds same-width containers instead of an exact bit container. The pre-existing `mul_half_x2` binds an exact `.b32` container for the same operand shape, which this probe suggests is an over-restriction; that is untouched here.


<a id="min-max-coverage--installed-surface-change"></a>

### Installed-surface change

Two generated public types change shape, deliberately and confined to the opcodes this slice rewrites:

- `Min::NanF32` / `Max::NanF32` become `Min::F32` / `Max::F32`, because the frozen `.NaN` seeds are folded into the general FP32 cohort rather than duplicated.
- `Min::F32` and `Max::F32` now carry `Operands = std::variant<BinaryOperands, TernaryOperands>`, and `ResolvedOperandLayoutTag{0}` selects the two-source layout while `{1}` selects the three-source one.

For consumers, `Min::NanF32` / `Max::NanF32` references migrate to `Min::F32` / `Max::F32`. Given a `Min::F32` value named `value`, select the variant alternative matching its operand count:

```cpp
const auto& binary_operands = std::get<Min::F32::BinaryOperands>(value.operands);  // two-source
// Or, when the instruction has three sources:
const auto& ternary_operands = std::get<Min::F32::TernaryOperands>(value.operands);
```

These public shape changes introduced an explicit API break in the installed C++ package; see the [current package version](#package-version). No compatibility shim or parallel legacy representation is provided, so consumers must migrate to the renamed cohort and operand variant. The repository has no dedicated migration or changelog document, so this migration note stays here.

Additions to shared checker structures are **appended** rather than inserted, so existing aggregate initializers keep compiling: `checker::ModifierValueView` gains `slot` as its final member, and `checker::OperandLayoutDescriptor` gains `forbidden_modifiers` alongside the new `CheckDiagnosticKind::ModifierNotAllowedForLayout`. The modifier diagnostic also reports the offending modifier's own source range when it is still retained, falling back to the context range otherwise.

[Floating and mixed ADD/SUB](#add-sub-coverage) reconciles the remaining §9.7.3–§9.7.5 cohorts. It gains no execution semantics here.


<a id="miscellaneous-coverage"></a>

## PTX 9.3 miscellaneous instruction coverage

Technical reference and evidence: [miscellaneous_coverage.md](us-en/converage/miscellaneous_coverage.md).

<a id="miscellaneous-coverage--ptx-93-miscellaneous-instruction-coverage"></a>

### PTX 9.3 miscellaneous instruction coverage

The frontend models the five opcodes in [PTX ISA 9.3 §9.7.20](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#miscellaneous-instructions). Parsing and owned resolution retain the operation, modifier, typed operand, and predicate guard; the target-aware checker applies the PTX and target gates. The frontend does not run a debugger, delay a thread, count performance events, or allocate physical registers.

| Instruction | Supported form and operand | PTX ISA floor | Target floor |
| --- | --- | --- | --- |
| `brkpt` | Bare instruction, optionally predicated | 1.0 | `sm_11` |
| `nanosleep` | `.u32` with a 32-bit register or integer immediate narrowed to 32 bits, in nanoseconds | 6.3 | `sm_70` |
| `pmevent` | Immediate event index `0..15` | 1.4 | All targets |
| `pmevent.mask` | Immediate 16-bit event mask | 3.0 | `sm_20` |
| `trap` | Bare instruction, optionally predicated | 1.0 | All targets |
| `setmaxnreg` | `.inc.sync.aligned.u32` and `.dec.sync.aligned.u32`, each with an immediate count `24..256` divisible by eight | 8.0 | See below |

The target catalog starts at `sm_13`, so `sm_11` is a documented floor for `brkpt`, not a separately modeled target. `pmevent` always requires an immediate; `.mask` selects a mask rather than an index. `nanosleep` accepts both register and immediate sources and preserves the programmer's source form. Its integer constants follow the general [PTX constant conversion rule](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constants): for example, `-1` narrows to `0xffffffff` and `4294967296` narrows to zero at the `.u32` use.

`setmaxnreg` accepts exact `sm_90a` from PTX 8.0, exact `sm_100a` from PTX 8.6, exact `sm_120a` from PTX 8.7, enabled `sm_100f` and `sm_120f` families from PTX 8.8, and the enabled `sm_110f` family from PTX 9.0. The model follows the repository's explicit target catalog; it does not infer unknown target spellings. Both action modifiers are distinct resolved variants. `.sync`, `.aligned`, and `.u32` are mandatory. The checker validates the literal count but cannot prove the runtime warpgroup participation, synchronization, register pool state, or launch configuration required for effective register adjustment.

The 16-bit `pmevent.mask` operand accepts `0..65535`, including zero; the frontend retains that literal even when no event bit is set.

No form in this section is marked deprecated by the fixed PTX 9.3 reference. `nanosleep` specifies an approximate duration, at most one millisecond, and permits wakeup changes within a warp; the frontend performs type checking, not timing prediction.

Offline cross-checks with CUDA 13.3 `ptxas` V13.3.73 assembled the documented forms, narrowed `nanosleep` constants, the `pmevent.mask` endpoints `0` and `65535`, and the enabled `setmaxnreg` targets. They rejected invalid event indices, masks outside `0..65535`, register operands for `pmevent`, invalid register counts, and generic `sm_90`/`sm_100`/`sm_110`/`sm_120` for `setmaxnreg`. One assembler behavior differs from the fixed PTX 9.3 reference: `ptxas` accepts `nanosleep` in a PTX 6.2 module although the reference says it was introduced in 6.3. The frontend retains the documented 6.3 floor. These checks verify assembly only; they do not establish GPU runtime behavior.


<a id="mma-scale-selector-coverage"></a>

## Block-scaled MMA selector operands

Technical reference and evidence: [mma_scale_selector_coverage.md](us-en/converage/mma_scale_selector_coverage.md).

<a id="mma-scale-selector-coverage--block-scaled-mma-selector-operands"></a>

### Block-scaled MMA selector operands

Dense and ordered-sparse block-scaled MMA retain separate A/B scale selectors,
each written as `{byte_id, thread_id}`. The logical values are U16. Register
leaves admit exactly fundamental `.b16`, `.s16` and `.u16` carriers under the
same-width integer/bit compatibility rule; they can be independently mixed with
each other or valid unsigned immediates. This does not cast/promote declarations
or infer runtime register contents. `.f16`, other widths, predicate/special
registers and instruction-only alternate declarations do not become compatible.
The same-width rule does not import ld/st/cvt widening.

The modeled cohort has 110 variants and 220 typed A/B selector contracts across
dense/ordered-sparse MXF8F6F4 (1X), MXF4 (2X), MXF4NVF4 (2X and 4X). These are
metadata inventory counts, not executed program or numerical coverage counts.
Each selector still requires exactly two scalar register/immediate leaves.
Explicit ordinary vector components retain their declaration SymbolId, optional
compact-member index, scalar lane and source ranges. Whole vectors cannot stand
for one leaf or replace the tuple; nested constructors and other cardinalities
remain unsupported. Genuine named vector roots in other operand families keep
their existing contracts.

Immediate conversion remains unsigned U16 with its original source bits. Byte
selector masks are `0b1111`, `0b0101`, `0b0001` for 1X/2X/4X; A thread IDs are
0–1 and B thread IDs 0–3. Negative, floating and overflowing literals remain
invalid. Signed register carrier permission is not permission for negative
immediate syntax, constant propagation or runtime bounds proof. The separate
sparse metadata selector, fragments, B32 scale-data roles, modifiers, targets and
availability stay unchanged.

Source resolution, source-aware validation and AST-free owned checking share
these typed contracts. Copied/cloned owned payloads permit coherent current
B16/S16/U16 identity retargeting, while wrong width/kind/whole-vector shape or
incoherent cached type/component/member provenance rejects. Source association
continues to check supplied syntax separately. No literal representation,
allocation, GPU execution or pending named matrix-fragment extension is implied.


<a id="mul-coverage"></a>

## MUL Coverage

Technical reference and evidence: [mul_coverage.md](us-en/converage/mul_coverage.md).

<a id="mul-coverage--mul-coverage"></a>

### MUL Coverage

This document states the complete PTX 9.3 `mul` contract modelled by the
frontend. It supplements the [syntax coverage matrix](#syntax-coverage); it
does not claim simulator execution or physical-GPU behavior. The canonical,
machine-readable source is `python/src/ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml`.

The normative sources are NVIDIA's PTX ISA 9.3 archive: [integer MUL
§9.7.1.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-mul),
[floating MUL
§9.7.3.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mul),
and [half/bfloat MUL
§9.7.4.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-mul).

All rows are modelled through syntax, Resolved IR, and target-aware checking.
They are frontend contracts only; no row supplies simulator semantics.

| Forms | Minimum PTX / target | Modifiers | Operand contract |
| --- | --- | --- | --- |
| Integer `.lo` and `.hi` for `.u16/.u32/.u64/.s16/.s32/.s64` | 1.0 / all targets | Exactly one of `.lo` or `.hi` | Destination and sources use the selected width; sources are register-or-immediate and same-width bit registers are accepted |
| Integer `.wide` for `.u16/.s16` | 1.0 / all targets | `.wide` | A 32-bit destination; 16-bit register-or-immediate sources |
| Integer `.wide` for `.u32/.s32` | 1.0 / all targets | `.wide` | A 64-bit destination; 32-bit register-or-immediate sources |
| `.f32` | 1.0 / all targets; `.rm/.rp` require `sm_20` | omitted rounding defaults to `.rn`; `.rn/.rz` universally available; `.rm/.rp`, `.ftz`, and `.sat` follow the legal form | Floating register-or-immediate sources and same-width bit-register compatibility |
| `.f32x2` | 8.6 / `sm_100` | omitted rounding defaults to `.rn`; all four roundings and `.ftz` are legal; `.sat` is not | all operands are exact `.b64` registers |
| `.f64` | 1.0 / `sm_13` | omitted rounding defaults to `.rn`; all four roundings are legal; neither `.ftz` nor `.sat` is | Floating register-or-immediate sources and same-width bit-register compatibility |
| `.f16` | 4.2 / `sm_53` | omitted rounding defaults to `.rn`; explicit `.rn`, `.ftz`, and `.sat` are independently legal | register-only `.f16` or same-width `.b16` values |
| `.f16x2` | 4.2 / `sm_53` | omitted rounding defaults to `.rn`; explicit `.rn`, `.ftz`, and `.sat` are independently legal | all data operands accept native `.f16x2` or `.b32` registers |
| `.bf16` | 7.8 / `sm_90` | omitted rounding defaults to `.rn`; only explicit `.rn` is legal; no `.ftz` or `.sat` | all operands are exact `.b16` registers |
| `.bf16x2` | 7.8 / `sm_90` | omitted rounding defaults to `.rn`; only explicit `.rn` is legal; no `.ftz` or `.sat` | all operands are exact `.b32` registers |


<a id="mul-coverage--deliberate-boundaries"></a>

### Deliberate boundaries

The archive restricts integer `.wide` to 16- and 32-bit types. Consequently,
`.wide.u64` and `.wide.s64` are rejected rather than inferred from the
same-width `.hi/.lo` families.

The floating and half/bfloat syntax makes rounding optional, with `.rn` as the
default. The YAML records that default instead of requiring a redundant `.rn`.
It still rejects directed half/bfloat rounding, `.sat` on `.f32x2` and bfloat
packed forms, and bfloat `.ftz`/`.sat`. Nonfundamental BF16x2/F32x2 packed forms retain their documented
exact bit-container widths, so a same-width-looking but wrong container is
rejected by the checker.

The long-standing public `MulRnF32`, `MulLoU32`, `MulHiU32`,
`MulWideU32`, and `MulWideS32` resolved-IR names remain available. This
coverage adds forms without creating a separate public support API.


<a id="mul-coverage--verification-sources"></a>

### Verification sources

[Dedicated C++ MUL tests](../submod/resolved_ir/test/test_mul_completeness.cpp)
exercise every family through parsing, resolution, declared-operand checking,
target minima, and invalid modifier/container cases. The former installed MUL harness is no longer present; current evidence is the
in-tree suite and Python model tests. The focused Python database test verifies the
canonical section, variants, modifier domains, availability, and operand
layouts. These sources define the frontend boundary; they do not verify a
simulator or hardware execution result.


<a id="multimem-coverage"></a>

## Multimem frontend coverage (PTX ISA 9.3)

Technical reference and evidence: [multimem_coverage.md](us-en/converage/multimem_coverage.md).

<a id="multimem-coverage--multimem-frontend-coverage-ptx-isa-93"></a>

### Multimem frontend coverage (PTX ISA 9.3)

The frontend recognizes the seven `multimem` source families in the fixed
[CUDA 13.3 PTX ISA 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html),
§§9.7.9.13, 9.7.9.15, 9.7.9.26.4.4–5, and 9.7.14.8. They share one
canonical `multimem` opcode and resolve into exact, owned, typed instruction
classes. The public `ResolvedAddress` retains the original address expression,
its source locations, and declaration binding when available. A global address
or symbol does **not** prove that the runtime address is a multimem mapping.

| Source family | Checked operands and suffixes | PTX / target floor | Completion |
| --- | --- | --- | --- |
| `multimem.ld_reduce` | Register destination, global/generic multimem address; omitted semantics or explicit `.weak` with optional scope, or `.relaxed/.acquire` with required scope; integer/float operation, type, vector, and optional accumulation precision according to the ISA tables | 8.1 / SM 90; `.acc::f32` 8.2; FP8 and `.acc::f16` 8.6 on the listed architecture families | None |
| `multimem.st` | Global/generic multimem destination and scalar or register-vector source; omitted semantics or explicit `.weak` with optional scope, or `.relaxed/.release` with required scope | 8.1 / SM 90; FP8 8.6 on the listed families | None |
| `multimem.red` | Global/generic multimem destination and scalar or register-vector source; `.relaxed/.release` and `.cta/.cluster/.gpu/.sys` are independently optional | 8.1 / SM 90 | None |
| `multimem.st.async` | Scalar source, mandatory `.release.{gpu,sys}`, optional `.global`; `b/u/s8–64`, `f32/f64` | 9.3 / SM 100 | None |
| `multimem.red.async` | Scalar source, mandatory `.release.{gpu,sys}`, optional `.global`, `.add.{u32,s32,u64}` | 9.3 / SM 100 | None |
| `multimem.cp.async.bulk` | `.global.shared::cta.bulk_group` only; 32-bit byte size, optional 16-bit `.cp_mask` operand; default weak, explicit `.weak` or `.relaxed.scope...b128` | 9.1 / SM 90; mask SM 100; explicit ordering 9.3 on SM 90a, SM 100f/110f family targets | Bulk group |
| `multimem.cp.reduce.async.bulk` | Same direction and byte-size contract; operation/type table including required `.noftz` for half/bfloat add; optional paired `.relaxed.scope` | 9.1 / SM 90; explicit ordering 9.3 | Bulk group |

For ordinary `ld_reduce` and `st`, scope is optional with omitted semantics
(effective weak) or explicit `.weak`, and accepts `.cta/.cluster/.gpu/.sys`.
It follows semantics and precedes state space, operation, and type; for example,
`multimem.ld_reduce.gpu.add.u32` and `multimem.st.weak.sys.u32` are accepted.
Every weak/default public form exposes `WithLocs<MemoryScope> scope`. Omitted
semantics remains `MemoryConsistency::Omitted`, and omitted scope remains
`MemoryScope::None`, both without source ranges. Explicit `.weak` and scope
retain their values and source ranges. Strong ordering still requires an
explicit scope; the `red`, release-async, and bulk-family defaults are separate.

The ordinary integer reduction table restricts `add` to `u32/u64/s32`,
`min/max` to `u32/u64/s32/s64`, and `and/or/xor` to `b32/b64`.
Floating `multimem.red` permits only `add`. Floating `ld_reduce` permits
`add/min/max`, but `f32/f64` only with `add`; `.acc::f32` applies to half and
bfloat types and `.acc::f16` to FP8 types. The scalar/v2/v4/v8 type lists
are the intersection of the manual's operation, vector, and accumulation
tables, not an unrestricted product. Bulk reduction permits `add` on
`u32/s32/u64/f32/f64/f16/bf16`, `min/max` on `u32/s32/u64/s64/f16/bf16`,
`inc/dec` on `u32`, and bitwise operations on `b32/b64`.

Ordinary and release-async multimem data registers have the same width as each
instruction data element; vector lanes obey that rule individually. Integer
signedness may differ and same-size Bit carriers remain eligible. Native
F16x2 permits itself or B32, while nonfundamental BF16/FP8 formats require
matching-size Bit storage; floating kinds retain their own compatibility.
Wider B64/B128 carriers cannot supply narrower data elements. The default
reference type-agreement rule applies: distinct `multimem` instructions do not
inherit the `ld`/`st`/`cvt` wider-register exception through their suffix names.
Address carriers, bulk byte-size inputs and byte masks keep their independent
contracts. Same-width data validation does not prove runtime mappings,
completion protocols or GPU effects.

The checker uses shared typed operand, modifier, and reference traversal.
For known declarations it checks global vs shared source spaces, scalar or
vector source types, byte-address alignment, and bound metadata consistency.
Scalar integer/bit store and reduction inputs accept registers or integer
literals with use-site narrowing. `f32/f64` scalar inputs accept the existing
typed floating literals. For the existing weak/default and ordered scalar FP8
`e4m3x4/e5m2x4` stores, integer literals and evaluated integer results are
rejected. Narrowing an integer to 32 bits does not make it a Bit source for a
logical floating operand. An explicit `mov.b32` preparation followed by a B32
register source remains legal; the frontend does not insert that move.

A bare unsigned direct `0f` literal preserves its entire 32-bit word, including
encoded sign and NaN-looking bits, without floating conversion, splatting or
quieting. The current packed immediate requires at most 32 bits, no
`integer_source_bits` (including optional zero), and `is_negative=false`.
Origin-free current word edits remain legal; these checks validate current
typed data rather than authenticate erased source spelling. Signed, wrapped
and operator-based exact-word sources retain their existing restrictions.
Decimal, `0d` and floating-expression packed constructors remain unsupported:
the one-number-to-four-values mapping is reference-qualified, not a blanket
manual ban on floating literals. Broader packed source permissions are separate
from this four-branch origin repair. The six adjacent scalar `f16x2/bf16x2` source branches also admit the existing bare unsigned direct `0f` whole-word producer;
the nine selected floating vector source fields follow the value-vector contract below.
Load destinations remain registers.

Direct and owned checks retain SameWidth, global-address and target/flavor
constraints. A valid supplied AST cannot mask invalid current origin when
instruction checks apply. Incomplete-context `AvailableContext` retains its
existing whole-instruction deferral; success there does not certify skipped
operand invariants.
Bulk destinations and sources need 16-byte alignment; an immediate byte size
must be divisible by 16. A register size remains a runtime obligation. The two release-async families
require a register base (with an optional immediate offset); a symbol-only
address is rejected even if declared global.
A written `.cp_mask` requires its final byte-mask operand. Release async
store/reduce forms have **no** named completion mechanism; bulk copy/reduce
uses `BulkGroup`, with group commit/wait provided by the existing bulk-group
instructions. The frontend does not prove completion ordering, memory-range
bounds, live multimem mappings, cross-device visibility, or actual GPU effects.

Three examples in the fixed manual conflict with its Syntax and legality tables.
The frontend follows the normative tables: `multimem.red...max.f64` is rejected
because floating `red` only permits `add`; the displayed bulk-reduction example
without `.shared::cta` and with an extra `[mbar]` is rejected because the
specified form has exactly three operands and `bulk_group` completion.
The displayed copy example `multimem.cp.async.bulk.relaxed.cta.global.bulk_group.b128`
also omits the required `.shared::cta` source qualifier and is rejected.
For `multimem.red`, the manual independently defaults omitted semantics to
`.relaxed` and omitted scope to `.sys`; the owned source representation retains
omission as typed `Omitted`/`None` values with no source ranges. Source with
only one qualifier is therefore accepted. The separately recorded `ptxas` 13.3.73 probe rejects
those one-qualifier spellings. Assembler behavior does not override this
frontend source contract.

The canonical inventory and negative boundary tests live in
[`test_multimem_contract.py`](../python/tests/spec/test_multimem_contract.py)
and [`test_multimem_coverage.cpp`](../submod/resolved_ir/test/test_multimem_coverage.cpp).
[`multimem_consumer`](../examples/multimem_consumer/main.cpp) exercises
installed public headers. These tests cover frontend acceptance and source
lifetime; they do not execute multimem instructions on a GPU.

The selected v2/v4 data register-vector roles accept ordinary named declarations and brace lists. Instruction-only logical formats use compatible fundamental Bit carriers. V8 remains brace-only; ordinary declarations cannot exceed 128 bits. Named form does not widen the SameWidth element contract.


<a id="multimem-coverage--floating-value-vector-source-contract"></a>

### Floating value-vector source contract

Exactly nine existing `src` fields use `WithLocs<ResolvedValueVector>`: weak and ordered floating stores, and floating add reductions, at v2/v4/v8. The classes, selector/layout identities, element type domains, arities, defaults, availability and addresses remain unchanged. This is a public field/factory/observer payload migration from `ResolvedRegisterVector`; downstream consumers must rebuild with matching generated headers and libraries. Wrap each old register lane in `RegOrImm`, preserve locations and `ResolvedVectorSource`, and inspect alternatives with `std::get_if`. A former null register element is not an immediate or a newly permitted sink.

All64 selected type branches have typed value carriers. Exactly32 have one-value numeric producers (15 F16/F32 and17 BF16/E4M3/E5M2), and20 have an exact whole32-bit bare unsigned direct `0f` producer (F16x2/BF16x2/E4M3x4/E5M2x4). The12 E4M3x2/E5M2x2 branches have no exact16 lexical producer or adopted numeric packing constructor: literal-source and literal-source-lifetime positives remain Source_N_A. Their real register controls and independently constructed canonical low16 typed words remain valid evidence; source absence is not a typed-data ban. Numeric scalar conversion reuses the existing single-round binary64 policy; packed32 retains every written word bit without splat, quantization or quieting. Integer-valued source expressions remain incompatible with logical Float lanes.

Mixed braces may contain registers and immediates. The50 v2/v4 named branches retain actual General whole-base declaration identity, exact NamedProjection lanes, element SameWidth and the16-byte declaration cap. The14 v8 branches remain brace-only; the existing128-element projection buffer and256-bit generic capacity are unchanged, and this cohort needs at most8 lanes/128 bits. Immediate projection is not a user sink. Synchronous observers receive actual payloads and all lane ranges, including lane8, and must not retain callback references. Copies/assignments/clones reacquire views from their own owners.

Ordinary typed words require the selected logical type, in-width bits and absent integer-origin/negative flags; coherent current edits do not replay erased floating source history. Current payload/pointer/range/provenance checks and retained source-identity checks remain independent. Address alignment, selected type/modifier/target gates and incomplete-context deferral still apply. The vector contract remains separate from scalar packed source permission and does not widen atomic inputs, packed numeric constructors or load destinations, and makes no runtime multimem mapping or GPU execution claim.


<a id="multimem-coverage--scalar-packed32-source-members"></a>

### Scalar packed32 source members

The existing classes `MultimemStFloatPackedNoneScalarDefaultWeak`,
`MultimemStFloatPackedNoneScalarDefaultOrdered` and
`MultimemRedFloatPackedAddScalarDefaultRed` expose `src` as
`WithLocs<RegOrImm>`. Consumers must rebuild with matching installed headers and
libraries and inspect `.value` with `std::get_if<ResolvedRegisterRef>` or
`std::get_if<ResolvedImmediate>`. These are three changed source fields, each
retaining the existing `modifier(type)` domain F16x2/BF16x2 and SameWidth policy.
Variant identities, member names, controls, address/global/alignment and target
contracts remain; the nine prior ValueVector source fields retain their API.

Each of the six branches admits a bare unsigned direct `0f` packed32 word through
the existing shared helper. All 32 bits are data, including encoded sign and
signaling/quiet NaN-looking patterns: no quantization, splat or NaN quieting occurs.
F16x2 retains ordinary B32/native F16x2 register carriers; BF16x2 retains B32.
Existing scalar component provenance rules remain. No whole vector or new tuple
source is granted. Integer-origin literals/results, unary signs, parentheses,
operators, decimal/F64 packed constructors, numeric pairs and implicit packing
remain excluded. There is no low-word `0d` truncation or new lexical format.

The source converter produces the selected logical packed type. Canonical current-word
positives use that type, at most 32 bits, absent integer origin (optional zero is still
present) and `is_negative=false`. Existing current Bit-carrier compatibility remains
unchanged; these examples do not narrow inherited `SameWidth` policy or establish a
new B32 rule. Canonical current-word edits do not authenticate erased producer history. Copy/assignment/
clone and AST-free checks retain actual member/range/provenance; supplied AST
association independently checks retained syntax identity. Synchronous observers
borrow the real owner only during traversal. The frontend does not execute
multimem mappings, stores, reductions, ordering or GPU behavior.

The scalar packed tests establish selected logical F16x2/BF16x2 canonical words. Compatibility of a Bit-typed B32 **current** word is a separate inherited-policy question and remains unproved here; scalar F16 Bit16 controls do not decide it. This qualification grants no additional source literal or constructor.


<a id="prefetch-coverage"></a>

## PTX 9.3 `prefetch` and `prefetchu` coverage

Technical reference and evidence: [prefetch_coverage.md](us-en/converage/prefetch_coverage.md).

<a id="prefetch-coverage--ptx-93-prefetch-and-prefetchu-coverage"></a>

### PTX 9.3 `prefetch` and `prefetchu` coverage

The frontend models the documented ordinary and tensor-map prefetch forms in
PTX ISA 9.3 §9.7.9.16. It resolves the address and checks known state-space
provenance and target availability. Prefetching is a performance hint; the
frontend does not execute it or prove cache behavior.

With `ptxas` 13.3.73, a direct `prefetch.L1 [shared_symbol]` probe triggers
internal error C7907. The frontend follows the ISA's documented shared no-op
behavior; representative assembler acceptance probes omit that direct-symbol
case.

| Form | Address contract | Availability |
| --- | --- | --- |
| `prefetch.L1/L2 [a]` | Generic addressing to global or local; shared is a no-op | PTX 2.0 / SM 20 |
| `prefetch.global.L1/L2 [a]` | Explicit global | PTX 2.0 / SM 20 |
| `prefetch.local.L1/L2 [a]` | Explicit local | PTX 2.0 / SM 20 |
| `prefetch.global.L2::evict_last/evict_normal [a]` | Explicit global | PTX 7.4 / SM 80 |
| `prefetch.tensormap [a]` | Generic address; known global, const and kernel entry-input parameter storage are accepted; known shared storage is a no-op | PTX 8.0 / SM 90 |
| `prefetch.const.tensormap [a]` | Explicit constant tensor map | PTX 8.0 / SM 90 |
| `prefetch.param.tensormap [a]` | Explicit input parameter tensor map | PTX 8.0 / SM 90 |
| `prefetchu.L1 [a]` | Generic address to uniform cache; known global is accepted, const/local/shared are no-ops | PTX 2.0 / SM 20 |

Known address provenance must match an explicit `.global`, `.local`, `.const`,
or `.param` suffix. The suffix-free `.tensormap` form accepts a generic address
register or a directly named global, const, shared or kernel entry-input parameter
symbol; a known shared address is a no-op. Device input, device return and
function-local call parameters are rejected by this generic tensor-map form,
as are known local addresses and explicit `.shared.tensormap`. The frontend
retains each concrete space, bound symbol identity and source range; it does not
relabel declarations as generic or prove a register's runtime provenance.
The ISA promises a prefetch cache effect only for const/param tensor maps; this
model does not infer one for generic addresses. `ptxas` 13.3.73 accepts direct
global/local/shared symbols for the suffix-free form and rejects direct
const/param symbols; these older assembler observations do not override the
documented ISA contract implemented here.
Tensor-map forms have no `.L1`/`.L2` suffix.
`prefetchu` has only `.L1` and no state-space suffix; a generic address
mapping to constant, local, or shared data performs no operation according to
the ISA. Known parameter addresses are rejected by `prefetchu.L1`. The model
rejects unsupported level, space, and eviction combinations. Unknown register
provenance remains unknown; `Generic` is never a wildcard for a known space.


<a id="selp-coverage"></a>

## SELP Coverage

Technical reference and evidence: [selp_coverage.md](us-en/converage/selp_coverage.md).

<a id="selp-coverage--selp-coverage"></a>

### SELP Coverage

The frontend models the ordinary `selp` forms in [PTX ISA 9.3 §9.7.6.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-selp). The canonical machine-readable contract is `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`. Parsing, owned resolution, and checking preserve the selected type and operands; the frontend does not execute the selection.

`selp.type d, a, b, c` accepts `.b16/.b32/.b64`, `.u16/.u32/.u64`, `.s16/.s32/.s64`, and `.f32/.f64`. The destination is a register; `a` and `b` are register-or-immediate sources checked against the selected type using ordinary PTX register compatibility. The predicate source `c` accepts a plain or complemented predicate register, or an integer predicate constant. Constants use PTX's C truth rule: zero is false, nonzero is true, and `!` complements the result. Predicate special registers and floating constants are excluded. This follows [PTX ISA 9.3 §4.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#predicate-constants) and CUDA 13.3 `ptxas` probes of `selp.s32` with `!%p` and `1`. The four operands are required, and no instruction modifier besides `.type` is admitted. The existing `selp_u32` generated public variant remains selected for `.u32`; the other types select the disjoint `selp_scalar` variant.

Every form requires PTX 1.0. The `.f64` type additionally requires `sm_13`; the other types have no extra target minimum in this instruction. A target-aware check rejects an earlier SM for `.f64`, and owned-IR revalidation rejects a mutated type outside the selected variant domain. The frontend does not evaluate the predicate or copy the selected value.

[The C++ SELP tests](../submod/resolved_ir/test/test_selp_completeness.cpp) cover all listed types in a validated module, the existing U32 variant, malformed modifiers and operands, declared-type mismatches, the `.f64` target boundary, and mutated public IR. [The focused Python specification test](../python/tests/spec/test_selp_completeness.py) checks the disjoint type domains, value availability, and operand descriptors. Related [SET](#set-coverage) and [SLCT](#slct-coverage) forms have their own coverage contracts.

The historical C++ package change from 0.1.0 to 0.2.0 introduced this API break: `Selp::U32::predicate.value` now holds `ResolvedPredicateSource` instead of `ResolvedPredicate`. Consumers reading a register predicate use `std::get<ResolvedPredicate>(value)` after checking the alternative, and must also handle `ResolvedPredicateConstant` where source text supplies an integer predicate. The `Selp::U32` variant identity remains unchanged. This is a source and binary API change; rebuild installed C++ consumers against the new headers and library.


<a id="set-coverage"></a>

## SET Coverage

Technical reference and evidence: [set_coverage.md](us-en/converage/set_coverage.md).

<a id="set-coverage--set-coverage"></a>

### SET Coverage

The frontend models ordinary scalar `set` in [PTX ISA 9.3 §9.7.6.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-set) and half/bfloat `set` in [§9.7.7.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-set). The canonical contract is `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`. Resolution preserves typed comparison, optional Boolean operation, optional flush control, and result/source types. Checking validates their combinations and operands; the frontend does not evaluate the comparison or write the result value.


<a id="set-coverage--ordinary-scalar-forms"></a>

### Ordinary scalar forms

`set.CmpOp{.BoolOp}{.ftz}.dtype.stype d, a, b{, c}` accepts result `.dtype` of `.u32/.s32/.f32`. The comparison sources `a` and `b` are register-or-immediate values of `.stype`; `d` is a register of `.dtype`. The optional Boolean source `c` is required exactly when `.BoolOp` is `.and/.or/.xor`; it accepts a plain or complemented predicate register or an integer predicate constant. Integer predicate constants follow the PTX C truth rule, including a leading `!`. Predicate special registers and floating constants are excluded.

| Source `.stype` | Comparison suffixes | `.ftz` | Minimum target |
| --- | --- | --- | --- |
| `.b16/.b32/.b64` | `.eq/.ne` | No | PTX 1.0 |
| `.s16/.s32/.s64` | `.eq/.ne/.lt/.le/.gt/.ge` | No | PTX 1.0 |
| `.u16/.u32/.u64` | Signed-family suffixes plus `.lo/.ls/.hi/.hs` | No | PTX 1.0 |
| `.f32` | Ordered suffixes, unordered `.equ/.neu/.ltu/.leu/.gtu/.geu`, `.num/.nan` | Optional | PTX 1.0 |
| `.f64` | Same floating suffixes | No | PTX 1.0, `sm_13` |

The selected `.stype`, rather than `.dtype`, determines the comparison domain and `.ftz` availability. Each source family has separate Boolean-absent and Boolean-required generated variants. The [ordinary C++ tests](../submod/resolved_ir/test/test_set_completeness.cpp) cover the families, modifier and operand negatives, target gate, and mutated typed IR.


<a id="set-coverage--half-and-bfloat-forms"></a>

### Half and bfloat forms

The half/bfloat forms use the same comparison and optional Boolean-source fields, but have distinct destination and source contracts. `set.CmpOp{.BoolOp}{.ftz}.dtype.stype d, a, b{, {!}c}` writes `.f16`, `.bf16`, `.f16x2`, `.bf16x2`, or an allowed integer result. The optional `c` uses the same predicate-source contract as ordinary `set`. The 30 generated alternatives partition 15 result/source cohorts by whether Boolean combination is present.

| Result `.dtype` | Source `.stype` | Minimum PTX / SM | `.ftz` |
| --- | --- | --- | --- |
| `.f16` | `.b16/.b32/.b64`, integer 16/32/64, `.f16/.f32/.f64` | 4.2 / 53 | Optional for every listed source |
| `.bf16` | Same source list | 7.8 / 90 | No |
| `.u16/.s16/.u32/.s32` | `.f16` | 6.5 / 53 | Optional |
| `.u16/.s16/.u32/.s32` | `.bf16` | 7.8 / 90 | No |
| `.f16x2` | `.f16x2` | 4.2 / 53 | Optional |
| `.u32/.s32` | `.f16x2` | 6.5 / 53 | Optional |
| `.bf16x2/.u32/.s32` | `.bf16x2` | 7.8 / 90 | No |

Bit-source comparison permits `.eq/.ne`; signed and unsigned integer sources permit `.eq/.ne/.lt/.le/.gt/.ge`; floating and packed sources permit those six plus `.equ/.neu/.ltu/.leu/.gtu/.geu/.num/.nan`. The ordinary unsigned aliases `.lo/.ls/.hi/.hs` do not apply here. The specific §9.7.7.1 F16-result syntax permits optional `.ftz` for the entire listed source domain, including Bit, integer and F64 sources. Ordinary §9.7.6.1 SET keeps its F32-source-only FTZ condition; BF16/BF16x2 productions still exclude FTZ. Earlier supplemental `ptxas` 13.3 samples rejected some of these F16-result forms; this is tool divergence, not the permission oracle. The frontend records the requested flag without evaluating comparisons or flushing values.

For scalar `.f16` sources, the frontend accepts `.f16` or compatible `.b16` registers and numeric floating constants; scalar `.bf16` sources for integer results accept the existing `.b16` registers or numeric BF16 floating constants. Packed `.f16x2` sources accept `.f16x2` or compatible `.b32` registers, while `.bf16x2` sources require exact `.b32`. Native `.f16x2` results accept `.f16x2` or `.b32`; native `.bf16x2` results require `.b32`. Integer results use width-compatible bit or integer register containers. For packed-source integer results, the written logical `.dtype` remains `.u32` or `.s32`: both admit exactly `.b32/.s32/.u32` destination declarations, with a fixed 32-bit packed result width. `.f32`, native `.f16x2`, narrower/wider registers, predicates, and whole vectors cannot serve as that scalar integer-result destination. This contract is separate from native floating-result SET and from the source-format carrier requirements. Scalar F16 numeric floating immediates are supported; other half/bfloat producer boundaries remain separate; the ordinary bit, integer, `.f32`, and `.f64` source cohorts retain register-or-immediate operands where valid.

The [half/bfloat C++ tests](../submod/resolved_ir/test/test_set_half_completeness.cpp) cover every generated alternative, target boundaries, invalid controls and containers, numeric immediates, owned IR, and mutated typed fields. The [Python spec test](../python/tests/spec/test_set_completeness.py) checks both ordinary and half/bfloat descriptor domains. The installed [consumer](../examples/conversion_consumer/main.cpp) exercises the public generated representation.

The historical C++ package version 0.2.0 replaced the former generated `Set::EqU32U32` and `Set::LtAndF32S32` alternatives with the typed ordinary families `Bit`, `Signed`, `Unsigned`, `Float`, `FloatF64` and their `Boolean` counterparts. Consumers should select the new alternatives and read `dtype`/`stype` fields to determine the exact type. This is a source and binary API change; rebuild installed C++ consumers with the new headers and library.

Scalar F16 source fields accept numeric floating constants converted to binary16 as well as their existing F16/B16 registers. Boolean predicate sources and all result carriers retain their contracts; scalar BF16 integer-result sources also accept numeric floating constants; packed conversion boundaries remain separate. Integer-origin constants are not numeric F16 sources.

The six existing `SetHalfF16Bit`, `SetHalfF16BitBoolean`, `SetHalfF16Integer`, `SetHalfF16IntegerBoolean`, `SetHalfF16F64`, and `SetHalfF16F64Boolean` classes now expose an instance `WithLocs<bool> ftz` in the existing modifier position. Omission stores false with empty locations; written `.ftz` stores true with its token range. A true flag without locations or a false flag with token locations is inconsistent. Copies and clones own the flag and locations after AST destruction, and coherent current typed flag edits remain independent of retained lexical history. These six generated object layouts change source/binary ABI: rebuild consumers against matching headers and libraries. Operands, comparator/Boolean domains, modifier order and PTX4.2/SM53 minimum remain unchanged.


<a id="set-coverage--ordinary-bf16-fmaset-source-members"></a>

### Ordinary BF16 FMA/SET source members

`FmaMixedF32Bf16`, `SetHalfIntegerBf16`, and
`SetHalfIntegerBf16Boolean` expose `src1` and `src2` as
`WithLocs<RegOrImm>`. Existing class names, identities and field order remain;
C++ consumers must rebuild against matching headers and libraries and use
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>` on `.value`.

These six sources accept numeric Float literals and Float-result expressions.
The existing logical BF16 converter maps `1.5`, `1.0+0.5`,
`0d3ff8000000000000` and `0f3fc00000` to type BF16 and word `0x3fc0`.
FMA uses its fixed `input_type` modifier; SET uses fixed logical BF16.
Integer literals and Integer-result expressions remain excluded, including an
optional integer-origin value of zero. This grant has no integer-immediate policy.
The accepted actual register declaration remains the ordinary scalar `.b16`
carrier; SameWidth does not admit `.f16/.u16/.s16`, special registers, whole
vectors or standalone instruction-only `.bf16` declarations.

Owned validation reads current logical type, low sixteen bits and flags. It
requires no floating lexical history; coherent canonical current-word edits can
validate after source/AST destruction, copying, assignment or cloning. Retained
bad type, high bits, integer origin or negative metadata remain errors. Source
association still checks the provided AST against retained syntax identity.
This scalar numeric conversion adds no packed pair/splat or truncation rule;
packed exact-word and constructor policies remain separate. Instruction rounding,
saturation, comparison, predicate combination and execution are not evaluated.
Targets, destinations, other sources and all packed SET forms keep their contracts.


<a id="setp-coverage"></a>

## SETP Coverage

Technical reference and evidence: [setp_coverage.md](us-en/converage/setp_coverage.md).

<a id="setp-coverage--setp-coverage"></a>

### SETP Coverage

This document records the complete PTX 9.3 `setp` contract modelled by the frontend. It supplements the [syntax coverage matrix](#syntax-coverage); it does not claim simulator execution or physical-GPU behavior. The canonical machine-readable source is `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`.

The normative sources are NVIDIA's PTX ISA 9.3 archive: [ordinary SETP §9.7.6.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) and [half/bfloat SETP §9.7.7.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp).

Every row is modelled through syntax, Resolved IR, and target-aware checking. These are frontend contracts only; no row defines simulator semantics.

| Forms | Minimum PTX / target | Comparison and Boolean modifiers | Destination and source contract |
| --- | --- | --- | --- |
| Ordinary bit `.b16/.b32/.b64` | 1.0 / all targets | `.eq/.ne`, with optional `.and/.or/.xor` | One predicate or a predicate pair; either ordinary destination position may be `_`, but a pair cannot discard both results. Sources are same-width register-or-immediate values. |
| Ordinary signed `.s16/.s32/.s64` | 1.0 / all targets | `.eq/.ne/.lt/.le/.gt/.ge`, with optional Boolean form | The ordinary single/pair sink and source rules apply. |
| Ordinary unsigned `.u16/.u32/.u64` | 1.0 / all targets | Signed-family comparisons plus `.lo/.ls/.hi/.hs`, with optional Boolean form | The ordinary single/pair sink and source rules apply. |
| Ordinary `.f32` | 1.0 / all targets | `.eq/.ne/.lt/.le/.gt/.ge/.equ/.neu/.ltu/.leu/.gtu/.geu/.num/.nan`; optional Boolean form and `.ftz` | The ordinary single/pair sink and floating register-or-immediate source rules apply. |
| Ordinary `.f64` | 1.0 / `sm_13` | The same floating comparison and optional Boolean form; `.ftz` is not legal | The ordinary single/pair sink and floating register-or-immediate source rules apply. |
| `.f16` | 4.2 / `sm_53` | All floating comparisons, optional Boolean form, optional `.ftz` | Exactly one predicate result. Sources are `.f16` or same-width `.b16` registers, or numeric F16 floating constants; explicit raw B16 integer words are also accepted. `_` is not a destination. |
| `.f16x2` | 4.2 / `sm_53` | All floating comparisons, optional Boolean form, optional `.ftz` | Exactly two predicate results; native `.f16x2` or `.b32` register sources. Raw B32 integer sources and bare unsigned direct exact logical packed words are admitted under [packed SETP rules](#resolved-ir-design--packed-setp-raw-and-exact-words); sink destinations remain illegal. |
| `.bf16` | 7.8 / `sm_90` | All floating comparisons and optional Boolean form; `.ftz` is not legal | Exactly one predicate result. Sources use actual `.b16` registers, numeric BF16 words or explicit raw B16 integer words; `_` is not a destination. |
| `.bf16x2` | 7.8 / `sm_90` | All floating comparisons and optional Boolean form; `.ftz` is not legal | Exactly two predicate results and actual `.b32` registers. Raw B32 integer sources and bare unsigned direct exact logical packed words are admitted under [packed SETP rules](#resolved-ir-design--packed-setp-raw-and-exact-words); sink destinations remain illegal. |


<a id="setp-coverage--deliberate-boundaries"></a>

### Deliberate boundaries

The ordinary syntax permits a single `_` destination or one `_` lane of a predicate pair. The frontend represents that explicitly, and checker revalidation rejects a public-IR pair edited to contain two sinks. Half and bfloat forms retain their required single or pair predicate destinations; native F16x2 register sources admit only `.f16x2` or `.b32`, while nonfundamental BF16x2 sources retain exact `.b32` containers. Arbitrary same-width types remain excluded.

The optional Boolean operand is a predicate source, not a predicate-register-only field. It accepts a plain or complemented predicate register and PTX integer predicate constants. Constants are canonicalized with C truth rules: zero is false, nonzero is true, and a leading `!` complements that truth value. Predicate special registers are not admitted by `setp`, even though `mov.pred` admits them.

The `.f32` and `.f64` forms are separate generated variants so `.ftz` remains legal only for `.f32`. This keeps the PTX modifier language disjoint without a separate capability registry. The expanded `ComparisonOperator` domain keeps the pre-existing `Eq`, `Lt`, and `Ge` numeric identities and appends the remaining normative suffixes.

As corroborating compiler evidence, CUDA Toolkit 13.1 `ptxas` probes using PTX 8.0 and `sm_90` accepted Boolean integer constants (including complemented constants), while rejecting numeric immediates as `.f16`, `.f16x2`, `.bf16`, and `.bf16x2` comparison sources. Those forms predate PTX 8.0 and PTX 9.3 documents no immediate-source extension. This older evidence is historical corroboration, not a normative blanket scalar-half literal ban or a current PTX 9.3/GPU result; the explicit scalar support below supersedes that inference.


<a id="setp-coverage--verification-sources"></a>

### Verification sources

[Dedicated C++ SETP tests](../submod/resolved_ir/test/test_setp_completeness.cpp) exercise every family through parsing, resolution, declared-operand checking, target minima, modifier/layout negatives, predicate-constant truth values, and mutated-IR sink validation. The [focused Python database test](../python/tests/spec/test_setp_completeness.py) verifies variants, comparison domains, availability, sink boundaries, predicate-source kinds, and packed containers. These sources define the frontend boundary; they do not verify simulator or hardware execution.

Scalar F16 numeric floating sources retain RegOrImm and the common binary16 RNE converter. The implementation below supplies a separate explicit Bit16 integer arm for four F16 and four BF16 fields; it reuses the accepted logical BF16 converter and does not change Boolean/predicate or packed policies. Recorded implementation evidence is revision-specific.


<a id="setp-coverage--explicit-scalar-bit16-sources"></a>

### Explicit scalar Bit16 sources

The implementation has passed full-model/default audits, normal generation, focused
Debug checks and a matching installed public consumer. The recorded implementation evidence is revision-specific. The `integer_immediate_type` descriptor member is
an optional trailing `ScalarType`, absent by default. Exactly eight existing
scalar SETP source rows engage B16: src1/src2 on the plain and Boolean F16/BF16
forms. Four BF16 source members migrate from register references to `RegOrImm`,
with logical BF16 and SameWidth; the four F16 members keep their existing kind,
logical type and width. The descriptor ABI requires rebuilding all consumers.

The source is evaluated once. An integer result chooses the explicit raw B16 arm:
`1` is word `0x0001`, and `0x10001` narrows to the same word while retaining its
64-bit integer origin. A floating result uses the existing logical converter:
`1.0` gives F16 `0x3c00` or BF16 `0x3f80`. Direct unsigned `0f` keeps its existing
root route and lexical precision; no wrapper or trial conversion classifies it.
Ordinary logical floating operands with absent policy still reject integer
origin, including the packed formats. The scalar B16 support does not itself extend atomic, storage, call or other
scalar permissions. The separate packed SETP extension is described below.

Current immediates select their arm by their actual type. Both words fit 16 bits;
numeric logical words have no integer origin or integer negative flag. B16
origin is optional; if present its mask/sign consistency remains checked, and a
cache-free word requires a false negative flag. Coherent word/arm edits do not
authenticate erased source history. Changed supplied syntax still has its
existing separate source association. Complete target context is required for
instruction-check proof; AvailableContext deferral is not a legality certificate.
Actual F16 carriers remain B16 or F16; BF16 carriers remain actual B16, with the
existing intrinsic declaration/class guards. No Base compatibility rule changes.


<a id="setp-coverage--packed-setp-raw-and-exact-words"></a>

### Packed SETP raw and exact words

The current packed SETP contract uses the existing trailing optional
`integer_immediate_type`; its finite pairs are F16/BF16 with B16 and
F16x2/BF16x2 with B32. It adds no descriptor field or immediate payload.
Exactly src1/src2 of plain and Boolean `SetpF16x2`/`SetpBf16x2` become
`WithLocs<RegOrImm>` (eight public members). The four BF16x2 bindings use
logical BF16x2 and SameWidth instead of physical B32 and Exact. F16x2 logical
type and width, predicate-pair destinations, Boolean combine operands,
comparison domains, FTZ rules and target minima remain unchanged.

A source is evaluated once: an Integer result takes raw B32 under the existing
narrowing/range policy and retains evaluated 64-bit source/sign facts. `1` is
word `0x00000001`, and narrowing `0x100000001` yields the same low word.
A bare unsigned direct `0f3fc03fc0` takes logical F16x2 or BF16x2 and preserves
all 32 bits through the shared packed-word helper. It performs no numeric
packing, splat, rounding or NaN quieting. Decimal/F64 numeric packed
constructors have no adopted producer; signed, wrapped or arithmetic `0f`,
`0h`, and F64 truncation do not gain one through this policy.

The Integer-source B32 arm retains source64/sign facts consistent with its low32 word.
The recorded evidence does not classify a cache-free current Bit32 word by analogy
with the separate scalar F16/Bit16 decision; its exact current flags and outcomes
remain unproved by this evidence. Genuine logical packed-word
current edits use independent direct-word producers. Logical packed words have
no integer-origin cache (even optional zero) or integer-negative flag. Coherent
current typed-word edits do not authenticate erased source history; supplied
source association remains independent. Actual register carriers are B32 or
native F16x2 for F16x2, and B32 for BF16x2; declaration/class/whole-container
checks remain in force. Missing-context deferral does not certify legality.
The existing eight scalar B16 policies and all absent-policy sources are unchanged.

Focused checks cover the finite source-policy pairs, source categories, current
values, declaration references, generated views, and owning copy/clone behavior.
Consumers must rebuild against matching generated headers and libraries. These
frontend checks do not establish GPU execution behavior or full PTX coverage.


<a id="slct-coverage"></a>

## SLCT Coverage

Technical reference and evidence: [slct_coverage.md](us-en/converage/slct_coverage.md).

<a id="slct-coverage--slct-coverage"></a>

### SLCT Coverage

The frontend models every ordinary `slct` form in [PTX ISA 9.3 §9.7.6.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-slct). Its canonical descriptor is `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`. Parsing, owned resolution, and checking preserve the type and operand contract; the frontend does not execute the selection.

`slct{.ftz}.dtype.stype d, a, b, c` selects `a` when `c` is nonnegative and `b` otherwise. The selected data is copied bit-for-bit without conversion. A floating selector treats negative zero as zero, so it selects `a`; a NaN selector selects `b`. `.ftz` flushes subnormal values of `c` only, and is available only when `.stype` is `.f32`.

| Field | Supported values and operand form |
| --- | --- |
| `.dtype` | `.b16/.b32/.b64`, `.u16/.u32/.u64`, `.s16/.s32/.s64`, `.f32/.f64` |
| `.stype` | `.s32` or `.f32` |
| `d` | Register compatible with `.dtype` |
| `a`, `b` | Registers or numeric immediates compatible with `.dtype` |
| `c` | Register or numeric immediate compatible with `.stype` |

All four operands are required. Register compatibility follows the nominal `.dtype`: a `.b32` data type can use same-width floating or integer registers, whereas `.u32` data rejects an `.f32` register, and `.f32` data rejects an `.u32` register. The selector uses its own `.s32` or `.f32` type with the same-width register policy. The two generated public variants are `Slct::S32` and `Slct::F32`; the latter retains a typed optional `ftz` field. Both start at PTX 1.0. `.f64` selected data requires `sm_13` or higher, regardless of selector type.

The [C++ tests](../submod/resolved_ir/test/test_slct_completeness.cpp) exercise all 22 type/selector combinations, literals, declared register containers, invalid modifiers and immediates, target boundaries, owned lifetime, and mutated public IR. The [Python spec tests](../python/tests/spec/test_slct_completeness.py) check exact type domains, operand descriptors, and the `.f64` value gate. The installed [consumer](../examples/conversion_consumer/main.cpp) uses both public variants and revalidates owned instructions.

In the historical 0.2.0 C++ package, the earlier fixed `Slct::U32S32` and `Slct::FtzU64F32` alternatives were replaced by `Slct::S32` and `Slct::F32`. Consumers should inspect `dtype.value` to distinguish selected data types and `ftz.value` in `Slct::F32`. `src_true`, `src_false`, and `selector` now carry `RegOrImm`; use `std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>` as appropriate. This changes the public source and binary contract; rebuild installed consumers with the matching headers and library.


<a id="st-coverage"></a>

## ST Coverage

Technical reference and evidence: [st_coverage.md](us-en/converage/st_coverage.md).

<a id="st-coverage--st-coverage"></a>

### ST Coverage

Ordinary named v2/v4 data registers (at most 128 declared bits) are accepted alongside braces for ordinary LD/ST, LDNC and LDU. Modern 256-bit forms remain brace-only. Selected root `st.async` v2/v4 sources also admit named declarations under the [named source rules](#resolved-ir-design--named-data-vectors); other async forms retain their own roles; see [owned source contract](#resolved-ir-design--named-memory-vector-sources).

This document records the PTX 9.3 `st` forms accepted by the frontend. The
machine-readable authority is
`python/src/ptx_frontend/spec/resources/ptx_spec/data_movement_and_conversion.yaml`; this
is a frontend contract, not simulator execution or GPU conformance.

The normative source is NVIDIA's PTX ISA 9.3 [store instruction](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st).


<a id="st-coverage--contract"></a>

### Contract

`st` accepts generic and explicit `.global`, `.local`, `.param`, and `.shared`
addressing, including `.param::func` return-address direction/context checks.
Scalar types include the integer, bit-size, and floating forms through `.f64`,
plus `.b128` at PTX 8.3 / SM 70. The `.sys` scope with `.b128` requires PTX
8.4. Sources retain PTX's equal-or-wider register rule: the checker validates
the selected instruction width; PTX store execution uses the source's low bits.
The checker also retains known
address alignment and write-permission validation.

The qualified shared forms `.shared::cta` and `.shared::cluster` retain their
state-space identity. They require PTX 7.8; `::cta` requires SM 30 and
`::cluster` requires SM 90 with cluster capability. An unqualified `.shared`
form retains PTX's `::cta` default.

Weak (including omission), volatile, relaxed, and release are distinct semantic
alternatives. Relaxed/release require a scope and permit known global or shared
addresses; volatile additionally permits local addresses at PTX 9.1. MMIO
requires a global address and `.sys` scope. The descriptor records the legal
MMIO semantic values and their target availability, including the PTX 9.3 /
SM 75 release+MMIO form, so the common checker never switches on `st`.
Cache operators and cache-control hints have distinct semantic restrictions.

Global cache forms cover cache operators, L1 eviction priorities, L2 eviction
priorities (alone and combined with L1), and `L2::cache_hint` with an optional
trailing `b64` cache-policy operand. L1 eviction requires PTX 7.4 / SM 70, cache
policy requires PTX 7.4 / SM 80, and L2 eviction requires PTX 8.8 / SM 100 with a
256-bit `.v8` 32-bit or `.v4` 64-bit vector. Other vector forms retain legacy
128-bit layouts and target-qualified modern 256-bit layouts; only modern
layouts admit partial-store sinks.
Those controls are also accepted from a generic spelling only for unknown or
known-global provenance; a known local, shared, constant, or parameter address
is rejected. L1/L2 eviction and `L2::cache_hint` admit weak, relaxed, and
release stores. A `.wb`/`.cg`/`.cs`/`.wt` cache operator is instead weak-only: it may
combine with cache hint, but not with eviction controls or ordered/volatile/MMIO
semantics. Combined eviction qualifiers use canonical L1-before-L2 ordering.

PTX 9.3 does **not** define an `[address].unified` syntax for ordinary `st`.
The frontend therefore rejects that spelling rather than inventing a store-side
qualifier from the declaration attribute. A known unified declaration is also
rejected as a read-only store address, including AST-free revalidation. It
neither allocates unified memory nor weakens normal address-space or alignment
diagnostics.


<a id="st-coverage--optional-cache-policy-operand"></a>

### Optional cache-policy operand

LD, LDNC and ST accept both supplied and omitted trailing cache-policy operands
with `.L2::cache_hint`, as implemented for
[#281](https://github.com/endingly/ptx_frontend/issues/281). A supplied operand
must be a 64-bit register (logical `b64` operand type); omission retains the
cache-hint form without creating a policy value. The qualifier remains required to supply the operand, and
omission preserves the same memory-space, type, target and vector-layout
restrictions. Earlier CUDA 13.1 PTX 8.0 / SM 90 assembler rejection probes are
historical compiler evidence and do not override the fixed PTX 9.3 syntax.

This coverage performs source acceptance and target-aware validation only. It
does not implement memory ordering, cache behavior, unified allocation, or
store execution. The dedicated C++ ST test, Python model test, and installed
consumer exercise the accepted/rejected public frontend contracts.


<a id="stack-manipulation-coverage"></a>

## Stack manipulation coverage

Technical reference and evidence: [stack_manipulation_coverage.md](us-en/converage/stack_manipulation_coverage.md).

<a id="stack-manipulation-coverage--stack-manipulation-coverage"></a>

### Stack manipulation coverage

The canonical catalogue implements PTX ISA 9.3 [§9.7.18 Stack Manipulation Instructions](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#stack-manipulation-instructions): `stacksave`, `stackrestore`, and `alloca`, each with `.u32` and `.u64`. Allocation has separate omitted and explicit `immAlign` layouts, giving eight source forms. All forms require PTX 7.3 and `sm_52`. Entry and device functions and either module address size are supported. The target registry explicitly includes generic `sm_50` and `sm_52` with `graph_exec` and no enabled family features, so module validation can distinguish the stack instruction floor from an unknown target.

`StackInstructionDescriptor` exposes the immutable operation, width, and default minimum alignment of eight bytes. `ResolvedStackToken` owns its register reference and function context; it is a runtime-opaque stack position. `ResolvedLocalAllocationResult` owns the same context plus the local address-space role. Both accept scalar integer/bit registers of the selected width. Neither operand is a generic address expression. Module validation cross-checks wrapper context and nested register bindings against the owning function after the AST has been destroyed. A context-free fragment keeps unknown function context and cannot stand in for a fully bound module. An explicit partial `ResolveContext` may retain a bound register while omitting the owning function scope. Individual checking validates the function-kind/scope pair but cannot authenticate binding identity without a symbol table; full module checking supplies that proof and rejects partial or unbound payloads.

The allocation byte count uses the ordinary typed `RegOrImm` unsigned width conversion. Zero is accepted; it does not prove a valid runtime allocation. Register and immediate sizes follow the general operand domain in [§4.3.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#operands); support for an immediate size is an inference from that general rule together with `alloca`'s unsigned-value description.

Explicit alignment is an integer constant used as `.u32`. Following [§4.5.1 Integer Constants](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constants) and the [alloca contract](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#alloca), the frontend evaluates the 64-bit source constant, converts it to the independent unsigned 32-bit alignment use, and checks that the converted value is a nonzero power of two at most `2^23`. Thus `4294967297` and `-4294967295` convert to one and are accepted; `4294967296` converts to zero and is rejected. The IR retains the original integer bits and negativity, and its checker verifies the conversion again. This is a combined interpretation of the general constant conversion rules and the instruction contract, corroborated by CUDA 13.3 `ptxas`.

Omitted alignment and an explicitly written eight retain distinct source-layout presence. Effective alignment is at least the maximum of the requested/default alignment and the runtime frame alignment; frame alignment is unknown to this frontend. Static checking does not simulate allocation, stack memory, stack overflow, token origin, token lifetime, restore validity, or CFG-dependent stack discipline.

The implementation uses canonical YAML metadata through normalization, typed lowering, generated public forms and checker projections. No raw-opcode runtime branch defines stack semantics. Tests in `test_stack_manipulation.cpp` exercise owned AST-free modules, source forms, gates, conversion, mutable payloads and context integrity. `examples/stack_consumer` checks the installed public headers and owned lifetime. Runtime execution remains downstream work; this family coverage makes no claim of complete PTX coverage.


<a id="surface-coverage"></a>

## Surface instruction coverage

Technical reference and evidence: [surface_coverage.md](us-en/converage/surface_coverage.md).

<a id="surface-coverage--surface-instruction-coverage"></a>

### Surface instruction coverage

The frontend follows the archived [CUDA 13.3 PTX ISA 9.3 surface instructions](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#surface-instructions). Canonical `surface.yaml` describes `suld`, `sust`, `sured`, and `suq`. Its 340 typed forms cover 1,864 legal written mnemonic combinations: 825 loads, 852 stores, 180 reductions, and seven queries. Type and optional cache choices share a typed form; these combination counts are not generated class counts.

| Family | Source coverage |
| --- | --- |
| `suld.b` | 1d, 2d, 3d, a1d, a2d; scalar/v2/v4 b8/b16/b32 and scalar/v2 b64; omitted/ca/cg/cs/cv cache; trap/clamp/zero. |
| `sust.b` | The same geometry, vector, and type matrix; omitted/wb/cg/cs/wt cache; trap/clamp/zero. |
| `sust.p` | 1d, 2d, 3d; scalar/v2/v4 b32; trap/clamp/zero; no cache selector. |
| `sured.b` | 1d, 2d, 3d; add u32/u64/s32, min/max u32/s32/u64/s64, and/or b32; trap/clamp/zero; scalar data and no cache selector. |
| `sured.p` | 1d, 2d, 3d; add/min/max/and/or b32 and min/max b64; trap/clamp/zero; scalar data and no cache selector. |
| `suq` | width, height, depth, channel_data_type, channel_order, array_size, memory_layout; b32 result. |

Surface access has an independent typed descriptor and owned operands. `SurfaceGeometry`, `SurfaceAddressingMode` (Byte/Sample), `SurfaceBoundaryMode`, `SurfaceReductionOperation`, and `SurfaceQuery` are closed domains. Surface operands reuse the existing opaque `Surface` identity for module-global `.surfref` objects, opaque entry input parameters, and scalar 64-bit integer/bit indirect handles. They do not inherit texture mode, sampler, cube, mipmap, offset, or residency rules. Both unified and independent texturing modules can use surface instructions. `mov.u64` keeps the resource identity; direct surface operands never become generic addresses.

Coordinates retain every lane, source value, and location. 1d accepts a scalar or singleton brace pack; 2d and a1d require two lanes; 3d and a2d require four. Spatial lanes have s32 interpretation, the first array lane has u32 interpretation, and the final 3d/a2d padding lane remains visible and ignored. Scalar 32-bit integer/bit register carriers are compatible with these interpretations. Coordinates can be typed immediates or registers. Loads use ordinary register/register-vector destinations; stores and reductions use ordinary register-or-immediate or value-vector sources, including mixed register/immediate store vectors. Unformatted load/store data registers must match the instruction element width, including b8/b16; vector total size is that width times the written arity. Same-size type compatibility remains: b8 admits B8/S8/U8 and b16 admits B16/S16/U16/F16, without a Bit-only restriction. The fixed reference default type agreement and surface transfer-size clauses apply; the wider-register exception for distinct `ld`/`st`/`cvt` instructions does not extend to `suld`/`sust`. Historical CUDA 13.3.73 `ptxas` acceptance of wider narrow-data carriers is supplementary tool divergence, not permission to widen this source contract. Destinations and source data retain their ordinary shape and type checks.

The generic [vector rule](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#vectors) limits surface data vectors to 128 bits. Consequently v4.b64 is rejected for both loads and stores; an instruction-specific `ld` extension does not apply to surfaces. Canonical normalization rejects illegal opcode/addressing/geometry/vector/type/reduction/query combinations and weakened feature gates before C++ emission. `Instruction::check()` rechecks mutable selected types, coordinate roles and tuple shapes, resource kinds and carriers, required brackets, and availability. Module validation additionally checks direct bindings and every nested coordinate/resource reference after syntax release, including replacement-source validation.

Byte loads/stores start at PTX 1.5. Cache selection and clamp/zero require PTX 2.0 / sm20; byte 3d/array access requires PTX 3.0 / sm20. Formatted stores and reductions start at PTX 2.0 / sm20, including `sust.p.3d`. All indirect resources require PTX 3.1 / sm20. 64-bit min/max reductions require PTX 8.1 / sm50. Query floors are PTX 1.5 for dimensions, 2.1 for channel properties, 4.1 for array_size, and 4.2 for memory_layout; direct queries have no additional SM floor.

The explicit generic `sm_50` target profile has no enabled-family features and has the `graph_exec` capability. Complete-module fixtures cover all 64-bit min/max forms at PTX 8.1 / sm50 and reject PTX 8.0 / sm50 and PTX 8.1 / sm30; this profile does not imply modern target capabilities.

The ISA Syntax takes precedence over inconsistent examples: the `sured.p.min.u32` example does not extend the formatted reduction type matrix, and surface array examples that omit the required boundary modifier do not authorize that omission. CUDA 13.3.73 `ptxas` rejects those spellings. Its parser also rejects a tested bare `[surface,0]` coordinate even though the documented 1d scalar coordinate rule and generic scalar constants permit it; the frontend retains that spelling alongside `[surface,{0}]`.

Validation establishes source and static frontend facts. It does not decode indirect objects, prove their runtime resource kind, infer surface configuration from named members, execute formatted conversion or reductions, or prove coordinate bounds, live-register natural alignment, format compatibility, aliasing, memory effects, or boundary behavior. Runtime resource configuration and access validity remain program obligations. The [installed surface consumer](../examples/surface_consumer/main.cpp) exercises public typed headers and validation after syntax release.

SULD v2/v4 data destinations accept ordinary named register vectors as well as brace lists, with the fixed binding arity and matching element widths. The retained whole declaration and canonical projected lanes are validated after syntax destruction. SUST v2/v4 data sources also accept named ordinary vectors with complete owned base/lane provenance. Brace sources retain mixed register/immediate values and their original ranges. SUST element widths remain SameWidth; formatted source interpretation is unchanged.


<a id="syntax-coverage"></a>

## PTX Syntax Coverage

Technical reference and evidence: [syntax_coverage.md](us-en/converage/syntax_coverage.md).

<a id="syntax-coverage--ptx-syntax-coverage"></a>

### PTX Syntax Coverage




<a id="syntax-coverage--purpose"></a>

### Purpose

This matrix describes implemented parser behavior. It is not a claim of full
PTX ISA support. The reference grammar is NVIDIA's
[PTX ISA documentation](https://docs.nvidia.com/cuda/parallel-thread-execution/).
The [core opcode completeness audit](us-en/converage/core_opcode_completeness_audit.md)
separates this frontend boundary from the archived PTX 9.3 and pinned-simulator
execution evidence for eleven commonly used operation names.

| Area | Status | Implemented subset |
| --- | --- | --- |
| Named array addresses | Supported subset | Dedicated MOV address sources and generic address roles accept one `A[index]`, with declaration scalar-element stride and preserved direct-minus/plus-negative source operations. Register-only role/qualifier exclusions and the ptxas spelling discrepancy are documented in [named address semantics](#resolved-ir-design--named-array-addresses); see [syntax and ranges](#syntax-ast-design--named-array-address-syntax). |
| PTX 9.3 video | Supported source family | All 23 scalar/SIMD2/SIMD4 opcodes with separate video types, selectors/masks, register negation, immediate provenance and target checks; see [video coverage](#video-coverage). Arithmetic execution is outside the frontend. |
| Tokens and trivia | Partial | Identifiers, dot identifiers, literals, punctuation, comments, whitespace, and selected stable directives; unmodified `CstFile::sourceText()` round-trips its token buffer byte-for-byte |
| Instruction keyword name contexts | Contextual | Opcode roots remain recognized in instruction position; explicit declaration names use the character grammar, including official `bar` object/function examples. Independent role/type/ABI/visibility rules remain; see [lexical interpretation](#lexer-design). |
| Instruction fragment | Partial | Predicate guard, opcode/modifiers, ordinary operands, addresses, ordinary `.xyzw/.rgba` scalar components and brace lanes, vector members/packs, and dedicated call/branch operand shapes; ordinary address bases and named-array register indices also accept selected scalar components under their existing type/role rules; explicit ordinary components are supported for call inputs, the single return and indirect register target |
| Module header | Supported subset | `.version`, `.target`, and `.address_size` lower to ordered, AST-independent source-configuration regions. Each region owns its effective version, target-option spellings, address width, and explicit/defaulted provenance; an omitted address size owns PTX's 32-bit default rather than a host property. A recognized region supplies target-aware module validation, but is not a hardware-configuration or execution contract. |
| Debug file directive | Supported subset | Outermost `.file file_index "filename"` with optional paired `, timestamp, file_size`; decimal/octal/hex uint64 IDs bind in a debug-only namespace, all repeated normalized IDs diagnose (including identical metadata), payloads are owned in the symbol table, and overflow diagnoses; explicit numeric metadata (including `0, 0`) requires PTX ISA >= 3.2, independently of SM/debug options |
| Debug location directive | Supported subset | Function/nested-block `.loc file line column`, with decimal/octal/hex file IDs and paired PTX 7.2 `function_name`/`inlined_at` payload, validates bound file IDs and `.debug_str` section/label identity; it retains owning SymbolTable records and checks modulewide preceding base triples, without executable instructions |
| Debug section directive | Supported subset | CST retains raw tokens/trivia and typed entries; owning AST/module integer lists, signed byte addends, single symbols and same-section label differences are checked for original range/locality/version; `.debug_str` typed labels bind in the metadata namespace; linked DWARF values remain unknown |
| Legacy debug serialization | Supported closed grammar | `@@DWARF`, PTX [1.2,2.0), unsigned hex widths/single wide label/finite section header; owning source-wide scoped stream, independent format checks; no decoder |
| Backend pragma directive | Supported subset | Module, `.entry` header, and function/nested-block statement `.pragma` preserve a nonempty comma-separated string list in CST/AST; strings create no bound names; module owns opaque advisory records with actual placement scopes |
| Kernel resource directives | Supported subset | Entry headers retain `.maxnreg n`, `.maxntid nx[,ny[,nz]]`, `.reqntid nx[,ny[,nz]]`, `.minnctapersm ncta`, `.maxnctapersm ncta`, `.reqnctapercluster nx[,ny[,nz]]`, zero-argument `.explicitcluster`, and `.maxclusterrank n` with dedicated CST/AST and owned normalized function-contract values. Declaration semantics rejects same-entry `.maxntid` plus `.reqntid` and `.reqnctapercluster` plus `.maxclusterrank`; module validation checks modeled PTX/target minima. Launch feasibility, occupancy, and physical resource allocation remain unchecked. |
| Functions | Supported subset | `.entry`/`.func` definitions, `.func` prototypes, visibility/linkage qualifiers, return/input parameter lists, `.noreturn`, `.func` ABI suffixes, `.language`, and entry `.blocksareclusters`. Resolved functions own their normalized signature, linkage/canonical identity, supported attributes, ABI suffixes, resources, and language/cluster markers independently of the AST. |
| Formal parameters | Supported subset | `.reg`/`.param`, alignment, fundamental scalar/v2/v4 types including packed scalar F16x2, pointer space/alignment, and arrays sized by structured constant expressions |
| Variable declarations | Supported subset | Module/function scope, linkage qualifiers, `.reg`/`.param`/`.local`/`.shared`/`.global`/`.const`, narrow `.attribute(.managed/.unified)` support, alignment, vector/base type, parameterized names, multidimensional arrays, and `.global`/`.const` initializers |
| Function body | Supported subset | Variable declarations, labels, supported instruction syntax, and recursively bound nested blocks; resolution recursively flattens nested instructions in source order, with call staging following the flattened instruction sequence and bound lexical visibility |
| Constant expressions | Supported subset | Shared declaration/instruction numeric trees: parentheses, integer operators/casts and homogeneous f64 arithmetic/comparison/conditional forms; numeric call/tuple/predicate/address leaves; depth 128 and shift counts 0–63. Exact `0f` remains a literal-only form. `generic(symbol)` and masks remain initializer operators. See [shared numeric evaluation](#declaration-semantics-design--shared-numeric-evaluation). |
| Initializers | Supported subset | Scalar expressions, recursive brace lists, and an unsized first dimension; `.extern`, parameterized-name, and non-`.global`/`.const` initializers are rejected |
| Symbol binding | Supported subset | Module/function/nested-block scopes, variables/parameters/functions/labels, lexical shadowing, parameterized members, instruction/initializer/dimension/control-flow references, and isolated debug file/string metadata identity; labels and control-flow metadata remain function-local |
| Declaration semantics | Supported subset | Positive array extents, inferred first extent, initializer type/brace shape/element limits, symbol addresses, module linkage-compatible redeclarations, and the supported entry resource-version/conflict rules |
| Resolved storage declarations | Supported subset | Owned global/constant/shared/local declaration metadata with identity/scope, typed shape, checked byte extent, alignment, linkage, and initializer constants/relocations; external unsized shared data remains dynamic and size-unknown. See the [storage contract](#storage-declarations) for normalization boundaries; no memory allocation or runtime instances |
| Other directives | Partial | Same-module `.alias` canonicalizes direct-call ABI lookup and is retained as an owned alias contract. Typed `.managed`/`.unified` attributes are retained as typed function/storage contracts (`.unified` owns its two numeric UUID halves); documented header directives are likewise owned. LD/ST validate unified-address and read-only contracts. Linker/backend behavior and runtime allocation remain unsupported. |
| Structured control syntax | Supported subset | `.callprototype`, `.calltargets`, and `.branchtargets` have dedicated function-local CST/AST syntax; binding and declaration semantics validate their labels/members/contracts. Resolved module contracts retain bound metadata-label identities, scope, canonical call signatures, ordered call targets, and expanded logical branch-target entries. Generated `IndirectCall` layouts resolve a `.reg` target plus bound prototype/target-set metadata at PTX 2.1 / SM 20, and module resolution applies the shared call ABI contract. `brx.idx` resolves a `.u32` index and current-function `.branchtargets` identity at PTX 6.0 / SM 30; it does not build a CFG or prove dynamic control flow. |
| Recovery/editing | Supported subset | `parseModule()` emits ordered diagnostics plus inserted/skipped/error CST recovery nodes and resumes at bounded structural/module anchors; a partial nested block retains its valid body but has no closing-brace token. Standalone instruction parsing remains fail-fast. Recovered modules lower only valid neighboring nodes; recovery markers remain CST-only and parser diagnostics return once in source order. The installed consumer covers legal PTX 9.3 directive text, semantic directive failure, and recovered unknown directives. Round-trip serialization uses the original token buffer rather than recovery markers. An opt-in Clang lexer/CST libFuzzer target has a GTest seed smoke, but no ASan/UBSan or CI matrix yet |
| Resolved opcodes | Partial | The documented supported forms and their parser/resolver/checker tests define the current opcode boundary; the [ISA-wide audit](us-en/converage/coverage_status.md) distinguishes root inventory from form completeness. The M12 common-kernel corpus validates 60 frozen forms through parse, resolve, and target-aware checking on `sm_80`, `sm_90a`, and `sm_100`; its `setmaxnreg.inc.sync.aligned.u32` occurrence is only in the `sm_90a` corpus fixture. Corpus presence is distinct from checker availability and complete ISA coverage. The current `setmaxnreg` target matrix and all five miscellaneous opcodes are described in [miscellaneous coverage](#miscellaneous-coverage). Uncatalogued official spellings report `UnknownTarget`; translation compatibility is not inferred. Other frozen slices remain partial; simulator execution remains unsupported. |
| PTX 9.3 miscellaneous instructions | Supported | `brkpt`, `nanosleep.u32`, `pmevent` index/mask, `trap`, and `setmaxnreg.inc`/`.dec` with operand and target checks. See [miscellaneous coverage](#miscellaneous-coverage). |

| Memory, atomics and fences | Supported subsets | Current source contracts are in [LD](#ld-coverage), [ST](#st-coverage), [LDU](#ldu-coverage), [prefetch](#prefetch-coverage), [atom/red](#atomic-reduction-coverage), [membar](#membar-coverage) and [fence](#fence-coverage). Exact accepted forms and exclusions are specified in the linked canonical family sections; historical gaps are not current exclusions. |
| PTX 9.3 `atom` / `red` | Supported subset | Synchronous scalar and vector operation/type cohorts with independent optional semantics and scope; vector forms require global memory and PTX 8.1 / SM 90. `red.async` has separate shared-completion (PTX 8.1 / SM 90) and global-release (PTX 8.7 / SM 100) cohorts. See [atomic and reduction coverage](#atomic-reduction-coverage) for tuples and contracts. |
| PTX 9.3 non-tensor bulk async | Supported subset | Direction-specific `cp.async.bulk` copy, `cp.reduce.async.bulk`, L2 prefetch, bulk-group completion, shared/global `st.async`, and `st.bulk` zero fill, including PTX 9.3 qualifier gates. See [bulk async coverage](#bulk-async-coverage). Tensor and multimem forms have separate contracts. |
| PTX 9.3 Fabric/CFT | Supported source families | Typed two- or three-field CFT handles, `fabric.try_get/put/red/pullred`, `submit`, `wait`, and six fabric proxy fences. See [Fabric/CFT coverage](#fabric-coverage) for completion, targets, and runtime obligations. |
| Tensor-map and TMA forms | Supported subset | Rank 1–5 tiled tensor prefetch/load/store, selected reduction, im2col, four-row gather/scatter, multicast and CTA-group routing, plus selected `tensormap.replace` and `tensormap.cp_fenceproxy`. See [tensor async coverage](#tensor-async-coverage) and [caller-known tensor-map facts](#tensor-map-known-facts). Supported TMA forms include [canonical cache controls](#tensor-cache-controls); raw 128-byte decoding is outside the #152 frontend closeout; see the [requirement matrix](#tensor-async-closeout). |
| Tensor Memory and TCGEN05 | Supported subset | Typed allocation, register transfer, copy/shift, wait, fence/commit, dense non-WS f16/tf32/i8, unscaled f8f6f4, block-scaled MX8/MX4/MX NV, and ordinary sparse f16/tf32/f8f6f4/i8 and sparse MX MMA forms ([f8f6f4 coverage](#tcgen-mma-f8f6f4-coverage), [MX8 coverage](#tcgen-mma-mx8-coverage), [MX4 coverage](#tcgen-mma-mx4-coverage), [MX NV coverage](#tcgen-mma-mxnv-coverage), [sparse coverage](#tcgen-mma-sparse-coverage), [sparse MX coverage](#tcgen-mma-sparse-mx-coverage), [A collector/ashift coverage](#tcgen-mma-a-collector-coverage)). See [allocation](#tensor-memory-allocation-coverage), [transfers](#tensor-memory-transfer-coverage), [copy/shift](#tensor-memory-copy-shift-coverage), [sync](#tcgen-sync-coverage), and [MMA](#tcgen-mma-coverage) coverage. [Dense WS](#tcgen-mma-ws-dense-coverage) and [sparse WS](#tcgen-mma-ws-sparse-coverage) source forms are also modeled, with conditional layout/history obligations in the [family contract](#tcgen-mma-family-coverage); [MX A collector controls](#tcgen-mma-mx-a-collector-coverage) are supported. |
| PTX 9.3 `ldu` | Supported | Scalar and v2/v4 uniform global loads with generic or explicit `.global` addressing, the documented type set, and target/operand checks. See [`ldu` coverage](#ldu-coverage). |
| PTX 9.3 `prefetch` / `prefetchu` | Supported subset | Ordinary generic/global/local L1/L2 forms, global L2 eviction priority, generic/const/param tensor-map forms, and uniform-cache L1. See [prefetch coverage](#prefetch-coverage). |
| PTX 9.3 `applypriority` / `discard` | Supported | Generic and explicit-global L2 forms with fixed 128-byte range, alignment, and target checks. See [cache-range coverage](#applypriority-discard-coverage). |
| PTX 9.3 `createpolicy` | Supported | Fractional, range, and access-property conversion forms with typed priority, fraction, size, and target checks. See [`createpolicy` coverage](#createpolicy-coverage). |
| Warp and non-bulk async | Supported subsets | `activemask`, [synchronized vote/shuffle](#warp-sync-coverage) and [non-bulk cp.async](#cp-async-coverage) have expanded source models. Legacy unsynchronized vote/shuffle have the exact historical version/SM windows in [warp coverage](#warp-sync-coverage); matrix families are listed below. |
| Warp and warpgroup matrix forms | Supported subset | Selected `ldmatrix`, `stmatrix`, `movmatrix`, `mma`, `mma.sp`, `wmma` load/store/MMA, and dense/sparse WGMMA plus fence/commit/wait forms resolve to exact final classes. Matrix data forms expose immutable topology; protocol-control forms have no matrix descriptor. See [WGMMA coverage](#wgmma-coverage). Dynamic warpgroup convergence, protocol completion, and simulator execution are not proved. |
| PTX 9.3 logic and shift | Supported | `and`/`or`/`xor`/`not` cover `.pred/.b16/.b32/.b64`; `cnot` covers `.b16/.b32/.b64`; `lop3` covers base and `.and/.or` predicate-result layouts with a checked `.u8` LUT; `shf` covers every `.l/.r` × `.clamp/.wrap` form; `shl` covers bit widths; and `shr` covers bit, unsigned, and signed widths. See [logic/shift coverage](#logic-shift-coverage). |
| PTX 9.3 integer bit operations | Supported | `popc`, `clz`, `brev`, `bfind`, `bfe`, and `bfi` cover their documented PTX 2.0 / `sm_20` width, sign, `.shiftamt`, and control-operand forms. See [bit-operation coverage](#bit-operations-coverage). |
| PTX 9.3 integer arithmetic | Supported | All documented §9.7.1 syntax forms are modelled through parsing, resolution, operand/type checks, and target availability. See [integer arithmetic coverage](#integer-arithmetic-coverage). |
| Conversion and address-query forms | Supported subset | The modelled `isspacep`, `cvta`, `cvt`, `cvt.pack`, `prmt`, `mapa`, and `getctarank` syntax, operand layouts, typed modifier boundaries, and per-form PTX/target minima are defined in [conversion coverage](#conversion-coverage). This is a frontend acceptance and validation boundary, not a claim of conversion execution or of the complete PTX conversion family. |
| Modelled `mul` | Supported | The complete PTX 9.3 integer, floating, half, and bfloat MUL forms, their modifier/operand contracts, and availability are listed in the [MUL coverage matrix](#mul-coverage); simulator execution remains unsupported |
| Modelled `setp` | Supported | Ordinary and half/bfloat comparisons, Boolean predicate sources, destination shapes, and target boundaries are documented in [SETP coverage](#setp-coverage); this does not execute comparisons |
| Modelled `set` | Supported | Ordinary and half/bfloat result/source types, comparison and Boolean domains, `.ftz`, operand containers, and target minima are documented in [SET coverage](#set-coverage); comparisons are not executed |
| Ordinary `selp` | Supported | All PTX 9.3 ordinary scalar types, predicate selection operands, and the `.f64` target boundary are documented in [SELP coverage](#selp-coverage); selection is not executed |
| Modelled `slct` | Supported | All PTX 9.3 ordinary data types, numeric selectors, `.ftz`, operand containers, and `.f64` target boundary are documented in [SLCT coverage](#slct-coverage); selection is not executed |
| Modelled `ld`/`st` | Supported subset | Scalar/vector, shared sub-spaces, cache-control combinations, ordered semantics, NC loads, and unified-address validation are described in [LD coverage](#ld-coverage) and [ST coverage](#st-coverage); memory execution and allocation remain outside the frontend |
| Named memory data vectors | Supported subset | Ordinary LD/ST, LDNC and LDU accept named v2/v4 ordinary registers through 128 declared bits; [provenance, brace compatibility and exclusions](#resolved-ir-design--named-memory-vector-sources). Array-address admission is independent. |
| Explicit vector MOV | Supported subset | Named/brace v2/v4 operands through 128 bits, same-width scalar MOV lanes, numeric expressions and scalar-special sources; distinct whole hardware vector reads. See [types, public API and unsupported scalar sources](#resolved-ir-design--explicit-vector-mov). Scalar bit pack/unpack remains separate and accepts the exact-width ordinary named aggregates specified in [aggregate rules](#resolved-ir-design--integrated-vector-data-movement). |
| Extended-precision integer | Supported | All documented §9.7.2 `add`/`addc`/`sub`/`subc`/`mad`/`madc` type, mode, and CC-effect combinations expose typed carry/borrow effects and target-aware validation; see [carry coverage](#carry-coverage). Runtime CC state remains outside the frontend |
| Modelled `mad` | Supported subset | Integer and carry forms remain available; explicit-rounding FP32/FP64 forms, operands, target minima, and excluded legacy profiles are documented in [MAD coverage](#mad-coverage). |
| Modelled `fma` | Supported | The 16 PTX 9.3 FMA variants, their modifier/operand contracts, and availability are listed in the [FMA coverage matrix](#fma-coverage); simulator execution remains unsupported |
| Modelled `div` | Supported subset | Frozen integer `div.u32` (PTX 1.0 / SM 0) and explicit FP32/FP64 forms; see [DIV coverage](#div-coverage). A zero divisor remains accepted with PTX-specified unspecified behavior. |
| Historical omitted floating controls | Supported subset | Thirteen exact PTX 1.0–1.3 forms retain omission and typed `floating_point_default`, including distinct `RcpImplicitF64`; explicit modes remain required from 1.4. Native SM13 and exact mapped SM10/11/12 preserve the documented header and option gates; the assembler discrepancy does not override the adopted manual interpretation. See [interpretation and bounds](#resolved-ir-design--historical-omitted-floating-controls). |

The conversion-family inventory is documented separately in
[conversion coverage](#conversion-coverage). It names the modelled forms and
their deliberate exclusions without recreating the retired manual opcode ledger.

The lexer may tokenize source outside this matrix, and Syntax AST may retain an
unknown opcode as text. Neither behavior means that the construct can be
lowered to Resolved IR.


<a id="syntax-coverage--ptx-93-directive-registry"></a>

### PTX 9.3 directive registry

This is the per-spelling registry for the 35 directives in PTX ISA Table 1,
plus five directives omitted by that table: `.attribute` from 5.4.8,
`.abi_preserve` and `.abi_preserve_control` from 11.4, and
`.blocksareclusters` and `.language` from 11.8. It answers the coverage
matrix's six pipeline questions. Legacy non-dot `@@DWARF` and attributes such
as `.ptr` are intentionally outside this dot-directive registry.

Legend: `D` = dedicated lexer token; `G` = generic `DotIdent` (still tokenized,
but not CST support). `T` = typed directive CST/AST; `E` = represented by an
existing declaration/function node; `R` = explicitly rejected by the parser.
`Y` = retained as an owned binding/Resolved-IR contract; `I` = a consuming
instruction retains/checks its bound identity; `C` = direct binding/declaration
semantic check; `V` = the current AST-free `validateModule` traversal performs
target-aware validation. Retention alone does not imply `V`: storage-declaration attribute availability
remains an AST/declaration-validation path. `V` validates only modeled source-profile
requirements; it does not establish launch feasibility, simulator execution, or
hardware behavior. `—` = no support at that stage.

| Directive | Token | CST | AST | Binding | Resolved IR | Target / semantic | Explicit boundary |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `.address_size` | D | T | T | — | Y | C | Owned effective 32/64-bit source-header width with explicit/defaulted provenance; it is revalidated as header integrity, not used to construct `checker::TargetInfo` or host-address meaning |
| `.alias` | G | T | T | Y | Y / I | C / V | Owned same-module device-function alias; no linker/backend aliasing |
| `.abi_preserve` | G | T | T | — | Y | C / V | Both forms own ABI metadata. PTX 9.0 / SM 80 minima are rechecked for current owned prototypes and independently supplied prototypes, including partial known context. No physical register assignment. |
| `.abi_preserve_control` | G | T | T | — | Y | C / V | Both forms own ABI metadata. PTX 9.0 / SM 80 minima are rechecked for current owned prototypes and independently supplied prototypes, including partial known context. No physical register assignment. |
| `.align` | D | E | E | Y | Y | C | Declaration/parameter alignment |
| `.attribute` | G | T | T | — | Y | C / V (function only) | Only typed `.managed` and `.unified(id,id)` placement/version subset. AST-free target availability covers function attributes; retained storage attributes are currently checked through AST/declaration validation. No runtime allocation or host-address interpretation. |
| `.branchtargets` | D | T | T | Y | Y / I | C / I | Owns bound, expanded logical targets; unused/nested presence requires PTX 2.1 / SM 20; `brx.idx` consumer is PTX 6.0 / SM 30, with no CFG/protocol proof |
| `.callprototype` | D | T | T | Y | Y / I | C / V / I | Owns normalized signature and supported ABI metadata. Both module validation routes recheck base PTX 2.1 / SM 20 and present suffix minima against the selected region; identity/scope/signature and indirect-call consumer checks remain independent. |
| `.calltargets` | D | T | T | Y | Y / I | C / I | Owns ordered bound/canonical function targets and their shared signature; unused/nested presence independently requires PTX 2.1 / SM 20; indirect-call instruction checks remain separate |
| Call-table flist arrays | D | T | T | Y | Y / I | C / I | Initialized scalar u32/u64 one-dimensional global/const arrays; owned real-storage identity, sparse explicit function slots, common full signature, lexical order and AST-free storage/function rejoin. Duplicates/implicit zero tails allowed; no runtime pointer-membership proof. |
| `.common` | D | E | E | Y | Y | C / V | Distinct Common linkage on ordinary module-scope global definitions; PTX 5.0 / SM 20, ordinary initializers/layout, AST-free eligibility and ordered target checks; no cross-object linker |
| `.const` | D | E | E | Y | Y | C | Existing variable declaration |
| `.entry` | D | E | E | Y | Y | C | Existing function node |
| `.explicitcluster` | D | T | T | — | Y | C / V | Entry-only, zero arguments, PTX 7.8 modeled availability; target launch feasibility deferred |
| `.extern` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.file` | D | T | T | Y | — | C | Decimal/octal/hex uint64 identity; every repeated normalized ID diagnoses; owned source-ordered payloads; file_size is a checked single unsigned64 word (0 through UINT64_MAX) in current and independently supplied data; timestamp domain unchanged; explicit numeric metadata requires PTX >= 3.2 (missing version defers under `AvailableContext`; replacement AST supplies version/metadata) |
| `.func` | D | E | E | Y | Y | C | Current local direct/consumed-finite may-call cycles require PTX 2.0 / SM 20; unused definitions included, open edges not certified acyclic; no runtime stack/CFG |
| `.global` | D | E | E | Y | Y | C | Existing variable declaration |
| `.local` | D | E | E | Y | Y | C | Existing variable declaration |
| `.loc` | D | T | T | Y | — | C | Owning ordered full64 triples, prior-base inline references and independent current/replacement metadata joins; no attachment |
| `.maxclusterrank` | D | T | T | — | Y | C / V | Entry-only normalized resource, PTX 7.8 modeled availability; conflicts with `.reqnctapercluster` |
| `.maxnreg` | D | T | T | — | Y | C / V | Entry-only normalized resource with modeled availability; occupancy implication deferred |
| `.maxnctapersm` | D | T | T | — | Y | C / V | Distinct deprecated entry hint, PTX >= 1.3; modeled u32 capacity, no hard occupancy cap |
| `.maxntid` | D | T | T | — | Y | C / V | Entry-only normalized resource; conflicts with `.reqntid`; launch feasibility deferred |
| `.minnctapersm` | D | T | T | — | Y | C / V | Entry-only normalized resource; warning/device feasibility deferred |
| `.noreturn` | D | E | E | — | Y | C / V | Device `.func`/`.callprototype`; return-parameter conflict is checked. Prototype PTX 6.4 / SM 30 minima are rechecked in both module routes, independently of whether the other context fact is known. |
| `.param` | D | E | E | Y | Y | C | Existing variable/formal/call-parameter declaration |
| `.pragma` | D | T | T | — | Y | V | Module/entry-header/statement records own raw strings, ranges and actual scopes; PTX >= 2.0 independently of target; no backend string interpretation or execution |
| `.reg` | D | E | E | Y | Y | C | Existing variable/formal declaration |
| `.reqnctapercluster` | D | T | T | — | Y | C / V | Entry-only normalized resource, PTX 7.8 modeled availability; conflicts with `.maxclusterrank` |
| `.reqntid` | D | T | T | — | Y | C / V | Entry-only normalized resource; conflicts with `.maxntid`; launch feasibility deferred |
| `.section` | D | T | T | Y | Y | C / V | Owning module metadata, typed integer/relocation grammar and static locality; no executable node or DWARF decoder |
| `.shared` | D | E | E | Y | Y | C | Existing variable declaration |
| `.sreg` | G | R | — | — | — | — | Unmodeled special-register declaration |
| `.target` | D | T | T | — | Y | V | Owned source-target options build `checker::TargetInfo` for recognized profiles; no physical target selection or translation guarantee; known debug + zero modern sections AND zero legacy records rejects, while actual DWARF recognition remains a qualified reference boundary |
| `.tex` | G | R | — | — | — | — | Unmodeled declaration directive |
| `.version` | D | T | T | — | Y | V | Owned source version participates in target-aware module validation |
| `.visible` | D | E | E | Y | Y | C | Existing linkage qualifier |
| `.weak` | D | E | E | Y | Y | C | PTX 3.1 / all targets; each supplied occurrence and current owned symbol/storage/function fact, including unused prototypes; no weak-linker simulation |
| `.blocksareclusters` | G | T | T | — | Y | C / V | Owned zero-argument entry marker; PTX 9.0 modeled availability and required `.reqntid` + `.reqnctapercluster`; launch rules deferred |
| `.language` | G | T | T | — | Y | C / V | Current raw nonempty official quoted-name/unsigned0..10 list is rechecked by one exported semantic decoder in both APIs, with duplicates/order retained; known PTX <9.3 rejects independently of missing target. No backend-language behavior |


<a id="syntax-coverage--implementation-priority"></a>

### Implementation priority

Future implementation planning, dependencies and priorities are tracked in the
repository [Issues](https://github.com/endingly/ptx_frontend/issues) and
[Milestones](https://github.com/endingly/ptx_frontend/milestones). Current SDK
support status and retained limitations are defined by this contract; planning
records do not independently establish support.


<a id="syntax-coverage--fixed-variable-addresses-adopted-reference-gap"></a>

### Fixed variable addresses: adopted reference gap

**`FIXED-ADDRESS-PRODUCTION`: `ADOPTED_REFERENCE_GAP`, not `CODE_PASS`.**
The pinned [PTX ISA 9.3 §5.4.1 overview](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#variable-declarations)
mentions an optional fixed variable address, but the referenced declaration,
array, initializer and alignment clauses provide no determinate executable
production, terminals, constraints or worked fixed-placement example. The
frontend does not parse or allocate absolute fixed variable placement.

Candidate tails `@ 0x100`, `.fixed(0x100)`, `.address(0x100)` and `:0x100`
after a variable name produce diagnostics under the project's strict known
declaration grammar. They are invented illustrations, not official PTX negative
oracles. A recovered CST or AST with diagnostics is not clean acceptance.
Ordinary initializer contents, array shape and alignment remain independent
facts; see the [storage boundary and examples](#storage-declarations--absolute-variable-placement).

Engineering completeness is qualified by the pinned executable source
productions and published interpretations, including this named reference gap;
it does not mean implementation of every capability mentioned in prose. A
future concrete official production or erratum requires a new architecture
decision and actual implementation review. Reproducible assembler observations
may supplement that evidence, but cannot alone establish normative grammar.

For `.target debug`, zero modern section records AND zero legacy records are a definite error. Nonzero records only satisfy the absence check: actual DWARF recognition remains **D-DEBUG-DWARF-PRESENCE-RECOGNITION**, not certified by names, emptiness, labels or typed bytes.


Historical entry body inputs: PTX 1.0–1.3 uses owned `LegacyEntryInput` roles with input LD/MOV, read-only/frame/cache joins and a checked 32764-byte logical ceiling; PTX 1.4+ header ABI and PTX 2.0+ body call slots stay distinct. Exact historical physical launch packing remains reference-boundary qualified. See [parameter contract](#parameter-declarations).

The whitespace terminal set is exactly ASCII SP, HT, LF, VT, FF, and CR. VT
counts one physical byte column and no line break; original CR/LF/CRLF rules,
comment/string boundaries, and lossless EOF trivia remain. This is the adopted
frontend interpretation, not Unicode normalization or an added escape. See
[physical whitespace positions](#syntax-ast-design--ascii-whitespace-and-physical-positions).


<a id="tcgen-descriptor-coverage"></a>

## TCGEN descriptor field and layout coverage

Technical reference and evidence: [tcgen_descriptor_coverage.md](us-en/converage/tcgen_descriptor_coverage.md).

<a id="tcgen-descriptor-coverage--tcgen-descriptor-field-and-layout-coverage"></a>

### TCGEN descriptor field and layout coverage

This API treats the [fixed PTX 9.3 Tables 43 and 45–48](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) as encoded facts. A caller supplies a known 64-bit shared descriptor, 32-bit instruction descriptor plus explicit MMA kind, or 64-bit zero-column word. These value-owned words do **not** come from, authenticate, or track an opaque source register. The copy instruction's `TcgenCopyDescriptorView` instead borrows its owning resolved register and checks its source carrier; it does not decode the register's runtime contents. The two roles have separate lifetimes and claims.

`ptx_tcgen_descriptors.hpp` provides typed field queries and pure defined-field reports. A report separates field violations from facts still needed for conditional checks. It does not assert allocation state, buffer extent, actual memory layout, source-register values, producer ordering, or full MMA legality. Selected [MMA forms](#tcgen-mma-family-coverage) consume instruction and zero-column roles independently; this descriptor foundation itself adds no operand grammar or source-value attachment.

Table 43 exposes 14-bit start, leading and stride encodings and their low-18-bit byte portions. Known bytes are checked for 16-byte alignment and the documented low-bit encoding relation, preserving higher address bits. Swizzle codes 0, 1, 2, 4 and 6 are recognized; 3, 5 and 7 are invalid. Fixed and reserved fields are checked independently. The eight documented relative K/MN rows are available as static symbolic layout data; their `T` factor means `128/element_bits` for the stated normalized representation and is not an invented packed 4/6-bit physical layout. Special code 1 has only its scoped MN 8×4 atom query, not a ninth relative row. Ordinary code 2/4/6 base-pattern checks require separately supplied repeating-pattern start: the base must be zero at the code's pattern boundary, and nonzero and equal to the encoded formula off that boundary. This does not infer the pattern start from the matrix start or impose a blanket alignment rule. Fixed Table 44 does not give an unambiguous no-swizzle/code-1 base-pattern row, so those cases retain an applicability obligation.

Absolute leading mode requires encoded swizzle 2 and base offset 0. When known, K major, both transpose bits clear, exact cataloged `sm_103a`, and PTX at least 8.8 are checked. Missing target, major or instruction context remains an explicit obligation. This field check does not establish the actual K48 MMA tuple.

Tables 45–47 select fields from an explicit seven-kind identity rather than the word. Queries expose A/B/D type codes **where encoded**, scale IDs/types, sparse state, N/M envelope, saturation, transpose, negate, K choice and WS reuse. MX kinds have no encoded D-type field; their operational rows fix the result type to F32. Defined-field validation enforces fixed/reserved fields and intrinsic type/scale/sparse-K rules. It does not turn dense sparse-selector bits or non-WS reuse into reserved fields, nor treat an encoded M/N envelope as a supported Table 42 MMA shape. Operational kind/shape/group/placement/target and source topology checks belong to the selected MMA forms.

Table 48 exposes four start counts and first-span flags, `generate_mask`, used/zero-column spans (encoded code plus one), and shift. Bit 39 is retained with its written polarity: `generate_mask` true enables pattern generation; false produces an all-zero mask and forces no B columns to zero. With supplied M/N, active partitions are one mask for M128, two for M64, and four for M32, with N divided accordingly. The conditional shift maximum is 16 for M32 and 32 otherwise; absent M leaves its tighter check pending. Inactive count/flag fields are not reserved-zero. Either bit-39 value still undergoes fixed/reserved and conditional field checks. Bits62–63 are **unclassified** by the fixed table: the report preserves them separately and never claims complete 64-bit descriptor validity.

This field API is partial rather than a complete Tensor Memory validity proof. Offline assembly of an instruction cannot prove these caller-supplied bits match a runtime register or memory layout.

The [descriptor-carrier interpretation](#matrix-reference-interpretations--logical-descriptor-carriers-and-fundamental-bit-data) distinguishes exact-width B/U/S descriptor roles from ordinary `SameWidth` bit data: F64/F32 are rejected for those roles, while `mov.b64`/`mov.b32` bit copies remain valid. TCGEN `idesc` is a 32-bit instruction descriptor, not TMA im2col. Lane-mask F32 and actual `.b64` mbarrier arrival-state data retain separate policies; runtime descriptor contents remain unproved.


<a id="tcgen-mma-a-collector-coverage"></a>

## Non-WS activation collector and ashift

Technical reference and evidence: [tcgen_mma_a_collector_coverage.md](us-en/converage/tcgen_mma_a_collector_coverage.md).

<a id="tcgen-mma-a-collector-coverage--non-ws-activation-collector-and-ashift"></a>

### Non-WS activation collector and ashift

The fixed [CUDA 13.3 PTX 9.3 TCGEN MMA syntax](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) permits `.ashift` and `.collector::a::{fill|use|lastuse|discard}` after the kind, in that order, on ordinary non-WS dense and sparse f16, tf32, f8f6f4 and i8 forms. These optional modifiers extend the existing eight source identities and keep their original A placement, mask, D-scale, metadata, shape and target rules. Collector before ashift has no source alias. Shared A may use an A collector without ashift. Ashift requires Tensor Memory A; it may stand alone or pair with lastuse/discard, but not fill/use.

The source qualifier resolves to `TcgenCollectorControl`, with separate typed buffer and operation fields. The all-Unspecified value and empty location represent omission; a written `.collector::a::discard` has a distinct value and source range. For non-WS operation semantics, omission derives effective A/discard without changing the owned source. Generic modifier checks compare the whole typed value, and the selected source check rejects malformed pairs, invalid enum values, wrong buffer, missing or extra locations and illegal ashift pairing. `tcgen_mma_a_collector_view` borrows controls only from a matching exact final class; full owned-module validation still works after syntax-tree destruction.

An independently supplied M can establish the ashift shape rule: M128 or M256 is allowed, M64 is contradictory, and unknown M remains an obligation. A caller claim that the A collector was invalid contradicts use/lastuse; unknown validity remains missing. Even a true claim does not prove prior fill or instruction ordering. Collector operations may reread shared or Tensor Memory sources, so A/B sources must remain stable until completion. No live descriptor, collector contents, allocation, synchronization history or GPU execution is inferred. The [consumer](../examples/tcgen_mma_a_collector_consumer) exercises the public source and conditional query contracts.

Collector and ashift forms inherit the required four/eight-entry mask; neither modifier makes it optional.


<a id="tcgen-mma-coverage"></a>

## TCGEN dense f16 MMA coverage

Technical reference and evidence: [tcgen_mma_coverage.md](us-en/converage/tcgen_mma_coverage.md).

<a id="tcgen-mma-coverage--tcgen-dense-f16-mma-coverage"></a>

### TCGEN dense f16 MMA coverage

This page describes the first source slice of the four
[`tcgen05.mma` spellings in fixed PTX 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma).
The initial slice is dense, non-weight-stationary kind f16 without block scaling, with both
CTA groups and both A placements. The required lane mask and optional D scale
are source topology choices settled by the fixed manual, a matched complete-module
assembler corpus and an independent Authority checkpoint. The distinct
[dense tf32 form](#tcgen-mma-tf32-coverage) and distinct
[dense i8 form](#tcgen-mma-i8-coverage) and the canonical
[dense unscaled f8f6f4 form](#tcgen-mma-f8f6f4-coverage) and
[dense MX8 block-scaled form](#tcgen-mma-mx8-coverage) and
[dense MX4 block-scaled form](#tcgen-mma-mx4-coverage) and
[dense MX NV four-bit form](#tcgen-mma-mxnv-coverage), and
[ordinary sparse f16/tf32/f8f6f4/i8 forms](#tcgen-mma-sparse-coverage),
and [sparse MX forms](#tcgen-mma-sparse-mx-coverage)
are also covered. Their ordinary non-WS f16/tf32/f8f6f4/i8 forms include
[typed A collector and ashift controls](#tcgen-mma-a-collector-coverage).
The six dense/sparse MX forms also include [typed A collector controls](#tcgen-mma-mx-a-collector-coverage). The separate [dense weight-stationary forms](#tcgen-mma-ws-dense-coverage) and [sparse weight-stationary forms](#tcgen-mma-ws-sparse-coverage) are covered. The [family matrix](#tcgen-mma-family-coverage) gives the current fixed-PTX source scope and its remaining runtime obligations.

Shared A/B descriptors use scalar General B64/U64/S64 **registers**; the
instruction descriptor uses General B32/U32/S32 registers. The assembler also
accepted literal zero in these positions, but the documented register role
remains the frontend rule. `enable-input-d` accepts a scalar predicate register
with or without written negation, or an integer predicate constant with
zero/nonzero truth, including 2. The accepted owned predicate representation
retains the truth value, location and written negation when applicable; it does
not retain the original integer magnitude. Special predicate registers are not
part of this first source contract.

The output-lane mask is required and has exactly four General
scalar 32-bit register entries for group 1 or eight for group 2; an explicit
empty vector is invalid. B32/U32/S32/F32 declarations are compatible as
32-bit **mask bits**. F32 here is not floating arithmetic or conversion.
Literal vector entries and wrong-width, vector or predicate register entries
are invalid. The optional D scale is an immediate with original evaluated
integer value 0..15, without low-bit aliases. The assembler accepted 2^32 as
an alias of zero, but the fixed source range remains stricter. Scaled forms
use the narrower exact100a/family100f target lineage. The single observed
`.mma.kind.cta_group` order is an alias of canonical `.mma.cta_group.kind`;
other modifier permutations are not admitted.

The operational catalogue uses [Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
for group 1 M64/128, N8..256 step8, K16 and group 2 M128/256,
N16..256 step16, K16. D=f16 requires A/B f16; D=f32 permits each A/B type in
the documented f16/bf16 domain. The combined table does not settle mixed
f16×bf16 pairing, which remains an explicit obligation instead of an invented
rejection or an assertion of full legality. K is uniquely determined and is
not independently specified in the instruction descriptor.

For supplied shared-descriptor facts, [Table 57](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
is checked per A and B role: normal K-major 16-bit matrices use the accepted
swizzle domain; transposed MN-major matrices exclude the 128-byte swizzle with
32-byte atomicity. The existing descriptor catalogue alone owns Table 43 and
Tables 45–48 encoded fields. Its defined-field validation must precede a
generated operational query, which separately reports completed
checks, operational violations and missing facts, including invalid enum
safety. A live descriptor register is opaque; caller-known
word values do not authenticate it or prove shared-memory contents.

The F/D/B/A path selection comes from **§9.7.17.10.5**, separate from Table 58
shared-swizzle atoms. The F half path permits independently supplied A/D
Tensor Memory lane halves 0 or 16 and compares valid known values when A is in
Tensor Memory; full D/B/A paths permit only zero. A known D half is checked
even when A is shared, and applicable unknown halves remain obligations.
Source syntax and assembly cannot prove allocation history, completion or
actual lane agreement. The accepted typed
TCGEN group rule is uniform only within each function's TCGEN body. TMA's
omitted/1/2 group mixtures have a different signal-routing contract and do
not join this check.

The generated exact `Tcgen05MmaF16` class owns its direct typed operands, with
four structural layouts for the eight group, A-placement and
scale-presence cases. `tcgen_mma_f16_view` borrows selected roles from a
`const Instruction&` and returns absence for other forms or a mismatched layout
tag. The checker rechecks owned
register, predicate, mask, immediate and address metadata after AST release.
A separate complete-module assembler checkpoint informed source spelling; it
does not prove runtime descriptor contents or GPU execution.

The eight ordinary dense/sparse generated classes now own
`WithLocs<ResolvedRegisterVector> disable_output_lane` directly, replacing the
optional member. Consumers must rebuild. F16/TF32 layout ordinals are shared
without/with D-scale 0/1 and Tensor Memory A without/with D-scale 2/3;
i8/f8f6f4 use shared 0 and Tensor Memory A 1. Successful borrowed views expose a
non-null mask pointer; its contents still need validation. WS and block-scaled
MX forms retain their independent no-mask contracts, including an omitted MX
selector whose field remains present with the `Absent` value. Assembler
acceptance of ordinary omitted masks is supplemental tool divergence, not
source permission. Ordinary named group-1 masks and native F16x2 bit carriers
remain separate producer work; their current limitations are not normative bans.


<a id="tcgen-mma-f8f6f4-coverage"></a>

## Dense unscaled f8f6f4 TCGEN MMA coverage

Technical reference and evidence: [tcgen_mma_f8f6f4_coverage.md](us-en/converage/tcgen_mma_f8f6f4_coverage.md).

<a id="tcgen-mma-f8f6f4-coverage--dense-unscaled-f8f6f4-tcgen-mma-coverage"></a>

### Dense unscaled f8f6f4 TCGEN MMA coverage

This page specifies the dense, non-WS, non-sparse,
non-convolution `.kind::f8f6f4` frontend slice. The fixed
[CUDA 13.3 PTX 9.3 `tcgen05.mma` specification](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
is the source. The accepted Table 43/45 descriptor catalogue owns field
encodings; the known-operation query never reads live descriptor registers.

The canonical order is
`tcgen05.mma.cta_group::{1,2}.kind::f8f6f4 [d-tmem], a, b-desc,
idesc, {disable-output-lane}, enable-input-d`. A is a shared descriptor
register or bracketed Tensor Memory address. The required mask contains
four or eight 32-bit bit-registers for group 1 or 2. Two A-placement layouts and two located group values yield four typed source topologies.
There is no source `scale-input-d`, `.satfinite`, or block-scale operand in this ordinary form. The frontend accepts the printed canonical modifier
order; the adjacent `.mma.kind::f8f6f4.cta_group` spelling is excluded.

A/B shared descriptors are scalar General B64/U64/S64 registers and `idesc`
is a scalar General B32/U32/S32 register. Literal zero is not a source
descriptor role. The predicate retains ordinary register identity and
written negation, or integer constant zero/nonzero truth. Mask entries
also admit F32 declarations as bit carriers. Missing, explicitly empty,
wrong-sized and wrong-typed masks are distinct. Owned source ranges,
declarations and binding must remain checkable after the syntax tree dies.

Table 42 gives dense K32, D F16/F32 and any of E4M3, E5M2, E2M3, E3M2,
E2M1 independently for A and B. All 25 defined input pairs are admitted by
the pair-domain rule. Group 1 permits M64/128 and N8..256 in steps of 8;
group 2 permits M128/256 and N16..256 in steps of 16. Four closed rows cover
the two groups and two output types. K is implicit, never an extra source
argument. F/D/B/A datapath paths follow group/M =
(1,64)/(1,128)/(2,128)/(2,256). The F half path permits caller-supplied A/D
lane halves 0 or 16 and compares valid known values when A is in Tensor Memory.
Full D/B/A paths require zero; a known D half is checked even with shared A,
and applicable unknown halves remain obligations.

Table 45 defines D codes 0 F16 and 1 F32, independent A/B codes 0 E4M3,
1 E5M2, 3 E2M3, 4 E3M2 and 5 E2M1. Saturation bit 3 must be zero;
transpose and negate bits are supported. Sparse is a defined field but a
separate violation for this dense operation. Defined-field failures,
operation violations and Missing facts are reported separately. A supplied
known word is an independent assertion, not an authentication of an opaque
source register.

For each shared role, Table 43 defined fields and context remain separately
checked. Normal A/B major and swizzle follow the accepted rule. E4M3/E5M2
are eight-bit; E2M3/E3M2 are six-bit; E2M1 is four-bit. Table 54 permits
transpose, but Table 57 has no 4/6-bit transposed row. A transposed low-bit
shared A or B therefore requires the corresponding
`ATransposeLayoutRule` / `BTransposeLayoutRule` **Missing**. The universal
128-byte/32-byte-atomic swizzle exclusion remains a hard A/B violation.
Eight-bit transposed shared roles follow the established role-specific
major/swizzle rule. Table 55's B-transpose N restriction applies **only
when B is eight-bit**: group 1 N16..256 step16, group 2 N32..256 step32.
It does not depend on A width. Tensor-Memory A has no shared-A-word/Table 57
obligation; its placement and path facts still apply.

Each caller-known four- or six-bit A/B role also carries an independent
`ALowBitPackingRule` / `BLowBitPackingRule` **Missing**, even when A lives in
Tensor Memory or the role is not transposed. The manual's low-bit packing
diagrams specifically name **mxf8f6f4**, so this unscaled slice does not
copy their padding or format rules. D F16 uses the lower 16 bits of a 32-bit
Tensor Memory word, but the known query cannot inspect actual matrix bytes,
allocation, completion or numerical GPU behavior. A report with no supplied
fact violations can still have unresolved obligations.

The modern100/110 unscaled target branches retain their original version floors.
Explicit historical rows add 101A from PTX8.6 and 101F from PTX8.8, preserving
exact source identity while matching the catalog's 110 validation-feature level.
Family membership remains the canonical `sm_110f` label; Generic101 gains no
A/F grant. i8 A-only and f16/tf32 D-scale restrictions remain separate.

This is a bounded source and known-facts representation. Scaled MX kinds,
sparse and WS forms, convolution, physical low-bit
packing proof, live descriptor bits, Tensor Memory lifetime and GPU execution
remain outside it. TMA and MMA can coexist in one module, but this slice does not prove
execution or cross-instruction completion.

The current ordinary non-WS dense and sparse forms also support [typed A collector and ashift controls](#tcgen-mma-a-collector-coverage); their caller-known history checks remain conditional.

The [descriptor-carrier interpretation](#matrix-reference-interpretations--logical-descriptor-carriers-and-fundamental-bit-data) distinguishes exact-width B/U/S descriptor roles from ordinary `SameWidth` bit data: F64/F32 are rejected for those roles, while `mov.b64`/`mov.b32` bit copies remain valid. TCGEN `idesc` is a 32-bit instruction descriptor, not TMA im2col. Lane-mask F32 and actual `.b64` mbarrier arrival-state data retain separate policies; runtime descriptor contents remain unproved.


<a id="tcgen-mma-family-coverage"></a>

## TCGEN MMA frontend family coverage

Technical reference and evidence: [tcgen_mma_family_coverage.md](us-en/converage/tcgen_mma_family_coverage.md).

<a id="tcgen-mma-family-coverage--tcgen-mma-frontend-family-coverage"></a>

### TCGEN MMA frontend family coverage

The fixed [CUDA 13.3 / PTX 9.3 TCGEN MMA clauses](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) define four source families. In the current target catalogue, all **22 `(family, kind)` identities** below have canonical syntax, direct typed instruction classes, owned-module validation and conditional caller-known queries. This is frontend coverage of those fixed forms, not authentication of live descriptor bits or GPU execution. The [canonical source data](../python/src/ptx_frontend/spec/resources/ptx_spec/tensor_memory_data_movement.yaml), [normalizer](../python/src/ptx_frontend/spec/normalize/tcgen_mma.py), [operational rows](../python/src/ptx_frontend/spec/tcgen_mma_operations.py), [descriptor fields](../python/src/ptx_frontend/spec/tcgen_descriptor_domains.py) and [C++ emitter](../python/src/ptx_frontend/code_gen/emit/tcgen_mma_operations.py) are the respective owners; the source census does not derive identities from the seven encoded descriptor kinds.

| Fixed source family | Kinds | Structural operand layouts | Focused contracts |
| --- | --- | ---: | --- |
| `tcgen05.mma` | f16, tf32, f8f6f4, i8, mxf8f6f4, mxf4, mxf4nvf4 | 18 | [f16](#tcgen-mma-coverage), [tf32](#tcgen-mma-tf32-coverage), [f8](#tcgen-mma-f8f6f4-coverage), [i8](#tcgen-mma-i8-coverage), [MX8](#tcgen-mma-mx8-coverage), [MX4](#tcgen-mma-mx4-coverage), [MX NV](#tcgen-mma-mxnv-coverage) |
| `tcgen05.mma.sp` | the same seven kinds | 18 | [ordinary sparse](#tcgen-mma-sparse-coverage), [sparse MX](#tcgen-mma-sparse-mx-coverage) |
| `tcgen05.mma.ws` | f16, tf32, f8f6f4, i8 | 16 | [dense WS](#tcgen-mma-ws-dense-coverage) |
| `tcgen05.mma.ws.sp` | the same four kinds | 16 | [sparse WS](#tcgen-mma-ws-sparse-coverage) |

The current 68 layouts count operand structure, not every CTA-group, selector,
collector-action or target combination. Ordinary non-WS dense/sparse masks are
required: four scalar entries for group 1 or eight for group 2. Removing the
24 omitted-mask layouts preserves all 22 family/kind identities and their
other domains. The earlier census recorded 92 layouts and 152 topology tuples;
those historical counts included the now-rejected omission alternatives.
Generated identities and conditional checks have separate [C++ tests](../submod/resolved_ir/test)
and [Python tests](../python/tests/spec).

Non-WS ordinary forms keep the kind-specific group, M/N/K, A placement, mask, D-scale, transpose and target rules. [A collector and ashift](#tcgen-mma-a-collector-coverage) extend their eight dense/sparse identities without adding overlapping classes. All six dense/sparse MX identities share two A placements, typed selector provenance, scale-A/B Tensor Memory addresses and [A collector controls](#tcgen-mma-mx-a-collector-coverage); they have no lane mask, D-scale or ashift. MX scale rows carry kind, sparse state, K, selector, role, factor count, allowed IDs and placement policy. MX8 omission derives 1X; MX4 omission derives block32; MX NV requires a written selector. The sparse MX query uses the actual sparse rows and their implicit F32 result: Tables 46/47 do not encode a D-type field. Its shared scale-row projection preserves dense versus sparse identity, so factor and ID checks select the matching rows.

Both WS families require CTA group 1 and expose B0–B3 collector controls with source omission distinct from written discard. Their optional final zero-column operand is a scalar 64-bit register; an independently supplied Table 48 word is checked only when that operand is present. Sparse WS also requires metadata after B and before `idesc`. The shared path rows select M32/G, M64/E or M128/D and require lane-half zero for applicable known A, D and metadata facts. Non-WS F/C half paths allow 0 or 16 and compare valid known halves; full paths require zero. D is checked even when A is shared. MX4/MX NV Python queries select their real K64/K96 row before this same lane rule, rather than inheriting the base f8 K32 path.

The [completion forms](#tcgen-sync-coverage) contribute eight `tcgen05.commit` forms, two fences and two Tensor Memory waits to the frontend inventory. The exact instruction classes, typed completion identity, borrowed views and [conditional collector/history reports](#tcgen-mma-a-collector-coverage) expose source and obligation information to a future analyzer. They do not run a sequence tracker or prove that a preceding fill, fence, barrier or allocation occurred. Ordinary source acceptance, owned reference/binding checks, target/version checks and caller-known word reports remain separate levels of evidence.

Known-value reports can reject inconsistent descriptor fields, shape/type/target combinations, sparse metadata indices, scale facts and lane claims. They **do not** prove that caller-supplied words equal opaque `idesc`, shared-descriptor, zero-column or Tensor Memory contents at runtime. The fixed Table 47 MX4 K96 scale-ID domain is 0/2, although later K96 diagrams depict 1/3; the frontend enforces the defined-word subset and records the conflict rather than inventing acceptance. The M32 sparse-WS metadata placement diagram is not established, so its report retains `MetadataLayoutRule`. Unscaled low-bit packing and some low-bit transpose rules remain explicit obligations where the fixed source does not settle a physical rule. Live sparse contents, scale placement across lanes, collector history, source stability until completion, allocation, synchronization and numerical results likewise remain unproved.

Adopted K96 encoding/layout separation: Table 47 owns A[29:30]/B[4:5] codes {0,2}; physical illustrated indices and byte alignment guarantees are separate quantities. Block32 has three factors (Mx3/3xN), NV block16 six (Mx6/6xN), each with a four-byte minimum. ID 2 with alignment 4/8 is valid under layout-defined placement; no `ID % alignment` or speculative encoded-to-physical conversion applies. Conflicting labels 1/3 remain documented, and missing optional facts retain obligations. The [K96 interpretation](#matrix-reference-interpretations--k96-encoded-scale-ids-and-physical-layout-facts) records this engineering decision and unchanged opaque-word/live-content/unsupported-known-vector-layout limits.

With the repository's configured vcpkg dependencies and Python test environment, the principal gates are:

```sh
cmake --preset ci-linux-clang-debug -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build --preset ci-linux-clang-debug
ctest --preset ci-linux-clang-debug --output-on-failure
.venv/bin/python -m unittest_parallel -s python/tests -t python -p 'test_*.py' --level=module -v
```

For local runs, unittest-parallel defaults to the logical CPU count; CI may explicitly set a worker count.

The bounded assembler checkpoint used complete PTX 9.3 modules and `/usr/local/cuda-13.3/bin/ptxas`; its module/log package was retained separately from this repository. The documented 31/31 status is a recorded tool observation, while the commands above and linked tests are reproducible from this source tree. No release CI, public ABI freeze or simulator/GPU execution claim follows from these checks.

<a id="tcgen-mma-family-coverage--historical-101-source-profiles-and-validation-features"></a>

### Historical 101 source profiles and validation features

`sm_101`/`compute_101` and their A forms begin at PTX8.6; F forms begin at PTX8.8. They remain admitted at PTX9.0/9.3, with no rename-imposed upper window. The corresponding 110 source profiles still begin at PTX9.0. `TargetIdentity` retains architecture101, flavor and exact written spelling. `same_target_identity` compares source architecture/flavor, so101A and110A remain distinct.

The additive base function `target_feature_architecture(identity)` consults explicit private catalog data: the three101 keys project to feature architecture110; other keys retain their architecture. Uncataloged typed keys do not become recognized. Public `TargetProfile` and `TargetInfo` layouts are unchanged. Source/owned/semantic factories use this feature level for numeric SM, retain source identity, and pass the existing110 baseline capabilities `cluster`, `aggregate_smem`, `reserved_smem`, `graph_exec`. Generic101 has no family; 101A/F enable only canonical `sm_110f`, with no100f/103f inheritance or FP64 mapping option. This bounded correspondence follows the published rename and family relation; it is not a physical hardware/driver guarantee.

Availability singular and span exact-target requirements compare validation-feature architecture plus exact flavor through that helper. This matching relation differs from source identity and stored-key equality. A manually supplied `TargetInfo::sm_version` remains the caller's concrete feature fact and is never overwritten by the checker. Source profile introductions, instruction/PTX floors, capabilities and shape constraints remain independent.

Historical instruction data changes only the adopted231 parents:219 preserve existing100A clauses and add101A, with101F only where an original110F branch exists;12 block-selector parents append an8.8 historical101A/F clause. All854 original110 conditions and their floors remain, the DNF limit stays six, and the25 restricted cluster gather4/im2col-W parents remain unchanged. TCGEN closed rows retain kind, scaled-D and explicit scale-vector restrictions: written block16/32 historical gates require8.8, while100-only vector and scaled-D paths remain100-only. Generated operational queries first validate the exact source profile and its minimum version, then apply feature matching. Unknown words and addresses remain unresolved obligations, not facts inferred from live registers.

Both module validators independently join current function `source_region`, `source_version` and exact `source_target` to the current owned region. A valid replacement AST cannot mask contradictory caches. Intrinsically coherent101/110 contexts may be retargeted through a supplied header when the current payload is legal under that effective context; retained availability is not a historical veto. Missing full instruction context still follows the existing `AvailableContext` deferral and strict `MissingValidationContext` policy.


<a id="tcgen-mma-i8-coverage"></a>

## Dense i8 TCGEN MMA coverage

Technical reference and evidence: [tcgen_mma_i8_coverage.md](us-en/converage/tcgen_mma_i8_coverage.md).

<a id="tcgen-mma-i8-coverage--dense-i8-tcgen-mma-coverage"></a>

### Dense i8 TCGEN MMA coverage

This page describes the dense, non-weight-stationary, non-convolution
`.kind::i8` frontend slice. Its packaged source, owned form, operational
catalogue and borrowed view remain independent of live descriptor contents.
The fixed
[CUDA 13.3 PTX 9.3 `tcgen05.mma` section](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
is the normative source; the accepted Table 43/45 descriptor catalogue remains
the sole encoding authority.

The printed source order is
`tcgen05.mma.cta_group::{1,2}.kind::i8 [d-tmem], a, b-desc, idesc,
{disable-output-lane}, enable-input-d`. `a` is either a shared-memory
descriptor register or a bracketed Tensor Memory address. The required mask
has four registers for group 1 and eight for group 2. There are two
structural layouts and two typed group values, or four source topologies.
The i8 grammar has **no** `scale-input-d` operand or `.satfinite` source
qualifier. A separate four-module CUDA 13.3 assembler checkpoint accepted
canonical and adjacent `.mma.kind::i8.cta_group` orders for **both** groups.
Authority approved exactly this adjacent alias as the same typed selected
form, retaining written group locations. Other permutations and duplicate
qualifiers remain invalid. Both orders select the same typed owned form.

Shared A/B descriptors are scalar General B64/U64/S64 **registers**. `idesc`
is a scalar General B32/U32/S32 register. Their live contents remain opaque.
`enable-input-d` is a scalar ordinary predicate register with optional written
negation, or an integer predicate constant whose zero/nonzero truth is
preserved. Special predicate registers are excluded. A required lane mask
contains exactly four/eight scalar General 32-bit registers; B32/U32/S32/F32
declarations are compatible as **bits**, not floating arithmetic. Absence and an explicit empty vector are both invalid. The owned resolved form must retain
source ranges, declarations and register identity after the syntax tree dies.

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
gives S8/U8 multiplicands, S32 accumulator/output and uniquely determined
K32. Dense group 1 permits M64/128 and N8,16,24,32, then N48..256 in steps
of 16; group 2 permits M128/256 and N32..256 in steps of 32. K is not a
separate source argument. A and B signedness are encoded independently in
Table 45. The fixed TCGEN text does not explicitly settle mixed S8×U8 or
U8×S8 pairing; a caller-known query must report that pair rule as an
**unresolved obligation**, neither a rejection nor a completed validation.
Same-type pairs may complete the pair check. The WGMMA same-type rule does
not govern this TCGEN form.

Table 45 bit 3 is the i8 saturation field. Both 0 (no saturation) and 1
(saturation) are defined; saturation is a caller-known encoded property, not
a source modifier or evidence of GPU numeric results. D type S32 is code 2;
A/B U8 and S8 are codes 0 and 1. Negate-A/B bits are forbidden for i8, while
transpose bits are supported. A known-word query validates defined fields
before operational rules, and reports field failures, proven operation
violations and missing facts separately. Caller-supplied known words are
independent assertions and do not authenticate live descriptor registers.

For shared operands, [Table 57](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
excludes 128-byte/32-byte-atomic swizzling for transposed 8-bit A **and** B;
each role is checked independently. [Table 55](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
adds a separate transposed-B N restriction: group 1 N16..256 in steps of 16,
group 2 N32..256 in steps of 32. Thus group-1 dense N8 and N24 are valid
shapes yet invalid with transposed B; N16, N32 and N48 are allowed when other
facts pass. Tensor Memory A has no A-shared-word obligation. The non-WS
F/D/B/A paths are selected by (group,M) = (1,64), (1,128), (2,128), (2,256).
On the F half path, independently supplied A/D lane halves 0 or 16 can be
compared when A is in Tensor Memory. Full D/B/A paths require zero; a known D
half is checked even with shared A. Source addresses do not prove the claimed
halves or allocation history.

The explicit i8 gates retain `sm_100a` from PTX8.6 and `sm_110a` from PTX9.0,
and add the historical `sm_101a` source profile from PTX8.6. Its source identity
stays 101A while the catalog's explicit feature correspondence is 110A.
`sm_101f` and Generic101 do not acquire this A-only operation. Family targets
and the unrelated `sm_103a` capability do not grant i8 MMA. Table43's exact103A
byte-stride descriptor rule remains independent. Operational queries require
the exact supplied source profile and its own introduction version before
matching feature gates; they do not infer descriptor words from source registers.

The other fixed dense, sparse and WS source forms appear in the
[family matrix](#tcgen-mma-family-coverage). Runtime descriptor contents,
allocation, completion and GPU behavior remain obligations. TMA's
omitted/group-1/group-2 group choices do not inherit the TCGEN-only uniform
group rule.

The current ordinary non-WS dense and sparse forms also support [typed A collector and ashift controls](#tcgen-mma-a-collector-coverage); their caller-known history checks remain conditional.


<a id="tcgen-mma-mx4-coverage"></a>

## Dense MX4 block-scaled TCGEN MMA

Technical reference and evidence: [tcgen_mma_mx4_coverage.md](us-en/converage/tcgen_mma_mx4_coverage.md).

<a id="tcgen-mma-mx4-coverage--dense-mx4-block-scaled-tcgen-mma"></a>

### Dense MX4 block-scaled TCGEN MMA

The fixed [CUDA 13.3 PTX 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) defines two dense, non-WS `.kind::mxf4.block_scale` source layouts: shared A or Tensor Memory A. Both use Tensor Memory D, shared B, a 32-bit instruction descriptor register, Tensor Memory scale A/B addresses, and a predicate source. The canonical forms have no lane mask or D-scale. The exact generated class `Tcgen05MmaMxf4` owns the typed selector and source locations; `tcgen_mma_mx4_view` borrows roles from only that class. Module checking revalidates these roles after the syntax tree is destroyed.

The selector keeps `Absent`, `Vec2X`, and `Block32` distinct. Omission defaults to block32 for this kind, but the source retains `Absent` and uses the base MMA target gate. Written `.scale_vec::2X` requires exact `sm_100a`; written `.block32` requires PTX 8.8 with an enabled `sm_100f` family feature or PTX 9.0 with an enabled `sm_110f` family feature. A source-valid instruction has an opaque live `idesc`, so its K cannot be certified from the spelling alone.

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) gives dense K64 for group 1 M128 and group 2 M128/256; group 2 M256 additionally permits K96 only on exact `sm_103a`. N ranges from 8 to 256 by eight for group 1 and from 16 to 256 by sixteen for group 2. D is F32 and A/B are E2M1 with UE8M0 scales. [Table 47](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) alone owns the instruction-word bits, including the K choice and scale A/B IDs 0 or 2. The known-word query checks these independently from source registers. In particular, known K96 plus written Vec2X has no target satisfying both the K96 exact `sm_103a` and Vec2X exact `sm_100a` requirements; it reports a target violation. Source Vec2X on `sm_100a` remains legal while K is unknown.

[Tables 59/60](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) assign two scale factors for Vec2X and K64 block32. K96 block32 has a distinct three-factor `Mx3`/`3xN` row with four-byte required alignment; K64 rows use `Mx2`/`2xN` and two-byte required alignment. Caller layout/alignment claims are conditional, and live scale placement, contents, and duplication to all 32 lane partitions remain unproved. The query accepts a stronger power-of-two alignment guarantee when its row policy permits it. For K96 block32, Table 47's encoded ID domain 0/2 is enforced. Later K96 layout figures also depict ID 1/3; this internal manual conflict is recorded rather than widening the defined-word domain. Table 59 lists Vec2X for all supported K, yet the physical Vec2X diagrams cover K64/K128 only; a hypothetical known K96 Vec2X query retains an explicit layout-rule obligation, even with caller-supplied facts.

Adopted Table 47 interpretation: scale A bits 29–30 and scale B bits 4–5 are dimensionless encoded IDs {0,2}; physical `SFA_ID`/`SFB_ID` labels 1/3 remain a reference discrepancy, not accepted codes. Caller alignment is a byte guarantee, not the ID or a physical-index conversion. For K96 `LAYOUT_DEFINED_PLACEMENT`, ID 2 with alignment 4/8 is valid: never apply `ID % alignment`, normalize 1/3, or invent an ID-to-address mapping. Missing role/layout/alignment facts retain unresolved obligations; supplied 0/2/3-byte guarantees are independently invalid. This is an engineering adoption, not a vendor erratum. See the [K96 interpretation](#matrix-reference-interpretations--k96-encoded-scale-ids-and-physical-layout-facts).

The manual prescribes paired four-bit elements in an eight-bit container for Tensor Memory A and shared A/B (without shared padding). The query exposes typed required packing and checks supplied claims, while retaining a live-content obligation. The [installed consumer](../examples/tcgen_mma_mx4_consumer) exercises the public source and known-value contracts. The separate [dense MX NV form](#tcgen-mma-mxnv-coverage) is covered; sparse MX4 is covered separately, and [MX A collector](#tcgen-mma-mx-a-collector-coverage) is supported. Ashift, WS, runtime allocation history, and GPU results remain outside this source contract.


<a id="tcgen-mma-mx8-coverage"></a>

## Dense MX8 TCGEN MMA coverage

Technical reference and evidence: [tcgen_mma_mx8_coverage.md](us-en/converage/tcgen_mma_mx8_coverage.md).

<a id="tcgen-mma-mx8-coverage--dense-mx8-tcgen-mma-coverage"></a>

### Dense MX8 TCGEN MMA coverage

This page covers the dense, non-WS `.kind::mxf8f6f4.block_scale` source form in fixed [CUDA 13.3 / PTX 9.3 §9.7.17.10.9.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma). The canonical statement is `tcgen05.mma.cta_group::{1|2}.kind::mxf8f6f4.block_scale{.scale_vec::1X|.block32} [d], a, b-desc, idesc, [scale-A], [scale-B], enable-input-d`. A is either a scalar 64-bit shared-descriptor register or a bracketed Tensor Memory address. D, scale A, and scale B are bracketed Tensor Memory addresses; the instruction descriptor is a scalar 32-bit register. This block-scaled syntax has **two** A-placement layouts and has no output-lane mask or D-scale operand. The predicate may be a scalar predicate register, including written negation, or an integer truth constant.

The Tensor Memory addresses are scalar General 32-bit lane/column carriers; B32/U32/S32 declarations are accepted by the frontend. F32, predicate, vector, and 64-bit declarations are rejected. A bounded CUDA 13.3 `ptxas` role-width probe rejected U32/S32 at D/A/scale-A/scale-B, although fixed §9.7.17.1.1 states a 32-bit-wide address without an exact `.b32` declaration requirement. This tool-specific difference does not change the frontend's existing width-compatible contract or establish runtime address values.

The selector is a located typed value with distinct `Absent`, `Vec1X`, `Vec2X`, `Vec4X`, `Block16`, and `Block32` identities. Only written `.scale_vec::1X` and `.block32` are currently legal for MX8. Omission remains `Absent` in owned IR; [Tables 59/60](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) uniquely determine the same effective 1X scale layout for an omitted MX8 selector. The query derives that layout without rewriting source provenance or equating the selector with later K-dependent block formats.

The [Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) MX8 row has K32 and D F32. Group 1 permits M128, N8..256 in steps of eight; group 2 permits M128/256, N16..256 in steps of sixteen. A and B independently use E4M3, E5M2, E2M3, E3M2, or E2M1 with UE8M0 scale factors. [Table 46](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) defines the instruction-word scale A/B data IDs and scale type. The existing descriptor catalogue is the sole owner of those encoded fields. Generated operational rows independently select typed `Mx1` for A and `1xN` for B, one scale factor and one-byte required sub-column alignment, with IDs 0..3. An ID selects a sub-column **inside** a Tensor Memory word; it is not an offset added to the scale address. Caller-supplied layout and alignment guarantees are checked separately for A and B; a stronger power-of-two guarantee is accepted when consistent with the selected ID. Unknown facts remain Missing.

The existing Table 43 defined-field checks and Table 54/55/57 shared A/B rules apply by role. Eight-bit transposed B uses N steps 16/32 for groups 1/2. Sections 9.7.17.10.4.3–.4 prescribe typed 4/6-bit packing: Tensor Memory A uses an eight-bit container per element; shared A/B use padded four- or six-bit containers. The query exposes the required packing and rejects contradictory caller facts while retaining an obligation for unverified live contents. The fixed manual does not specify the 4/6-bit transposed shared packing row as a complete validator; the query retains that separate obligation. The live `idesc`, A/B descriptors, and scale contents are opaque register/memory values. Caller-known words are conditional assertions, not proof of those live values, allocation history, ordering, or GPU results.

The base source form follows the exact `sm_100a` PTX 8.6, enabled `sm_100f` PTX 8.8, exact `sm_110a` PTX 9.0, and enabled `sm_110f` PTX 9.0 branches in the current target catalogue. Written `.scale_vec::1X` requires exact `sm_100a`; `.block32` requires the family-feature branch from PTX 8.8. The omitted selector uses the base branch. CUDA 13.3 `ptxas` also assembled an explicit 1X source on `sm_110a`, but the fixed MMA target notes and Table 63 specify the narrower written-selector requirement, which the frontend follows. Source resolution and the caller-known target query use the same gate.

The generated exact `Tcgen05MmaMxf8f6f4` class owns the selector, scale addresses, and source ranges. `tcgen_mma_mx8_view` borrows only a matching selected layout. Module checking revalidates operand types, bindings, and selector/scale source locations after syntax-tree destruction. The [installed consumer](../examples/tcgen_mma_mx8_consumer) exercises these source and known-value APIs. Sparse and other MX kinds have separate coverage; [MX A collector](#tcgen-mma-mx-a-collector-coverage) is now supported. Ashift, WS, physical low-bit packing proof, and execution remain outside this source contract.


<a id="tcgen-mma-mx-a-collector-coverage"></a>

## MX activation collector

Technical reference and evidence: [tcgen_mma_mx_a_collector_coverage.md](us-en/converage/tcgen_mma_mx_a_collector_coverage.md).

<a id="tcgen-mma-mx-a-collector-coverage--mx-activation-collector"></a>

### MX activation collector

The fixed [CUDA 13.3 PTX 9.3 TCGEN MMA syntax](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) permits `.collector::a::{fill|use|lastuse|discard}` after the optional or required scale selector on the existing dense and sparse `mxf8f6f4`, `mxf4`, and `mxf4nvf4` source identities. The six identities retain two A placements, their original scale-A/B operands, sparse metadata placement, shape rows, packing, selector omission rules and target intersections. MX NV still requires a written selector. None of these MX forms accepts ashift, a lane mask, D-scale, a B collector, or weight-stationary syntax.

The qualifier reuses the typed `TcgenCollectorControl` buffer/action pair. Its all-Unspecified value with no source range preserves omission; written A/discard has its own value and range. A source checker verifies the whole typed pair and provenance, and `tcgen_mma_mx_a_collector_view` borrows controls only after the existing exact dense/sparse MX layout view succeeds. `TcgenMxACollectorKnownFacts` exposes only the collector and an optional prior-validity assertion; its query reuses the A-buffer history rule without exposing ashift or an M constraint. Unknown history remains an obligation; a false validity assertion contradicts use/lastuse, while a true assertion does not prove prior fill or ordering. Sources may be reread until completion, so A/B source stability remains an obligation.

The existing independent MX operation queries still check caller-known descriptor words, shape, scale IDs/layout, packing, metadata and target conditions. Collector presence does not weaken or prove those conditions, and no live descriptor, collector contents, allocation history or GPU execution is inferred. The [installed consumer](../examples/tcgen_mma_mx_a_collector_consumer) exercises a sparse source, the borrowed view of an owned module after AST destruction, and the conditional history query.


<a id="tcgen-mma-mxnv-coverage"></a>

## Dense MX NV four-bit block-scaled TCGEN MMA

Technical reference and evidence: [tcgen_mma_mxnv_coverage.md](us-en/converage/tcgen_mma_mxnv_coverage.md).

<a id="tcgen-mma-mxnv-coverage--dense-mx-nv-four-bit-block-scaled-tcgen-mma"></a>

### Dense MX NV four-bit block-scaled TCGEN MMA

The fixed [CUDA 13.3 PTX 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) defines two dense, non-WS `.kind::mxf4nvf4.block_scale` source layouts, with shared A or Tensor Memory A. Both have Tensor Memory D, shared B, an opaque instruction descriptor register, Tensor Memory scale A/B addresses, and an input-D predicate. The canonical forms have no lane mask or D-scale. `Tcgen05MmaMxf4nvf4` owns the written selector and source locations; `tcgen_mma_mxnv_view` borrows roles from only this exact class. Owned module validation rechecks them after the syntax tree is destroyed.

The scale selector is **required**: `.scale_vec::2X`, `.scale_vec::4X`, `.block32`, or `.block16`. There is no omitted-selector default. The written vector selectors require PTX 8.7 and exact `sm_100a`; block selectors require PTX 8.8 with the `sm_100f` family feature, or PTX 9.0 with the `sm_110f` family feature. Source validity cannot certify the runtime K or scale contents of an opaque `idesc`.

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) permits dense K64 for group 1 M128 and group 2 M128/256; group 2 M256 also permits K96 on exact `sm_103a` from PTX 8.8. N runs 8–256 by eight for group 1 and 16–256 by sixteen for group 2. D is F32 and A/B are E2M1. The sole [Table 47](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) descriptor catalogue checks the known word's K choice, UE8M0/UE4M3 scale type, and scale IDs 0 or 2. Vec2X/block32 require UE8M0; Vec4X/block16 permit UE8M0 or UE4M3. Known K96 plus either vector selector has no target intersection with exact `sm_100a` and is reported as a target violation; the source remains legal on `sm_100a` while K is unknown.

[Tables 59/60](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) give two factors and two-byte required sub-column alignment for K64 Vec2X/block32, and four factors and four-byte alignment for K64 Vec4X/block16. K96 block32 has three factors, K96 block16 six; both have four-byte required alignment and a separate layout-defined placement policy. The query accepts a stronger positive power-of-two guarantee when its row permits it. For K96 block forms it still enforces the Table 47 ID domain 0/2. Later K96 layout figures depict IDs 1/3 too; this manual conflict is recorded rather than widening the defined-word domain. Table 59 gives factor counts for K96 Vec2X/Vec4X, but their physical layout rules are not established by the supplied diagrams. The query reports a layout-rule obligation even if caller layout facts are provided, alongside the target violation.

Adopted Table 47 interpretation: scale A bits 29–30 and scale B bits 4–5 are dimensionless encoded IDs {0,2}; physical `SFA_ID`/`SFB_ID` labels 1/3 remain a reference discrepancy, not accepted codes. Caller alignment is a byte guarantee, not the ID or a physical-index conversion. For K96 `LAYOUT_DEFINED_PLACEMENT`, ID 2 with alignment 4/8 is valid: never apply `ID % alignment`, normalize 1/3, or invent an ID-to-address mapping. Missing role/layout/alignment facts retain unresolved obligations; supplied 0/2/3-byte guarantees are independently invalid. This is an engineering adoption, not a vendor erratum. See the [K96 interpretation](#matrix-reference-interpretations--k96-encoded-scale-ids-and-physical-layout-facts).

The manual prescribes paired four-bit elements in an eight-bit container for Tensor Memory A and shared A/B, without shared padding. Typed packing rules and caller claims are checked, while live content, selected scale placement, duplication across all 32 lane partitions, allocation history, and GPU behavior remain unproved. The [installed consumer](../examples/tcgen_mma_mxnv_consumer) exercises the public source and known-value contract. [Sparse MX NV](#tcgen-mma-sparse-mx-coverage) and [MX A collector](#tcgen-mma-mx-a-collector-coverage) have separate contracts. Ashift and WS remain outside this source contract.


<a id="tcgen-mma-sparse-coverage"></a>

## Ordinary sparse TCGEN MMA

Technical reference and evidence: [tcgen_mma_sparse_coverage.md](us-en/converage/tcgen_mma_sparse_coverage.md).

<a id="tcgen-mma-sparse-coverage--ordinary-sparse-tcgen-mma"></a>

### Ordinary sparse TCGEN MMA

The fixed [CUDA 13.3 PTX 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) defines the non-WS `.mma.sp.cta_group.kind` source forms for f16, tf32, f8f6f4 and i8. Each has a mandatory bracketed Tensor Memory metadata address **after B and before `idesc`**. A may be shared or Tensor Memory; groups 1 and 2 are supported. The f16/tf32 forms keep the ordinary required lane mask and optional D-scale; f8f6f4/i8 keep the mask but forbid D-scale. The group 1 mask has four scalar 32-bit register entries and group 2 has eight. The `Tcgen05MmaSp*` final classes own these roles, and `tcgen_mma_sparse_view` borrows them only from a matching class and layout. Module checks retain binding and source-location integrity after the parser tree is destroyed.

The generated [Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) sparse rows use logical K 32/16/64/64 for f16/tf32/f8f6f4/i8. Sparse A stores half that K; B retains logical K. The independent known-value query checks shape, type, supported target and PTX version, the source kind against a caller-supplied instruction word, and its sparse bit. The sole [Table 45](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) descriptor catalogue owns encoded fields and the sparsity selector. Known selector values 0–3 are accepted for f16/tf32; f8f6f4/i8 require zero. The query does not read the live `idesc` register.

The fixed metadata encoding is 1:2 for tf32 (nibbles `E` or `4`) and 2:4 for the other three kinds (nibbles `4,8,C,9,D,6,E`). Caller-supplied nibble values are checked individually, while the live metadata contents and compressed A contents remain obligations. The F (group 1, M64) and C (group 2, M128) half paths allow lane-half identities 0 or 16 and compare every independently known applicable Tensor Memory A, D and metadata identity. The D (group 1, M128) and A (group 2, M256) full paths require zero for each applicable known identity. Shared A removes only the A lane comparison; known D and metadata still obey the path rule. These values are lane-half identities, not byte address alignments. Missing values remain explicit obligations, not presumed equality. For f8f6f4 E2M1/E2M3/E3M2 inputs, low-bit packing rules remain separate obligations because source and known words do not prove live layout contents.

The source form and known-value report do not establish runtime Tensor Memory allocation, sparse content, descriptor authenticity, synchronization, or GPU behavior. [Sparse MX](#tcgen-mma-sparse-mx-coverage), [A collector and ashift](#tcgen-mma-a-collector-coverage) and weight-stationary forms have separate contracts. The [installed consumer](../examples/tcgen_mma_sparse_consumer) exercises the public sparse source, borrowed metadata, and conditional query API.

The current ordinary non-WS dense and sparse forms also support [typed A collector and ashift controls](#tcgen-mma-a-collector-coverage); their caller-known history checks remain conditional.


<a id="tcgen-mma-sparse-mx-coverage"></a>

## Sparse block-scaled TCGEN MMA

Technical reference and evidence: [tcgen_mma_sparse_mx_coverage.md](us-en/converage/tcgen_mma_sparse_mx_coverage.md).

<a id="tcgen-mma-sparse-mx-coverage--sparse-block-scaled-tcgen-mma"></a>

### Sparse block-scaled TCGEN MMA

The fixed [CUDA 13.3 PTX 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) provides three non-WS sparse block-scaled source kinds: `mxf8f6f4`, `mxf4`, and `mxf4nvf4`. Each `tcgen05.mma.sp` form has two A placements, a bracketed Tensor Memory metadata address after B and before `idesc`, and Tensor Memory scale-A/B addresses after `idesc`. These forms allow an optional [A collector](#tcgen-mma-mx-a-collector-coverage), but no lane mask, D-scale, ashift, B collector, or weight-stationary suffix. The exact final classes own typed operands and written selector locations. `tcgen_mma_sparse_mx_view` borrows the metadata and scale roles only from a matching final class and layout; full module checking retains source and binding integrity after syntax-tree destruction.

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) gives sparse MX8 logical K64 and sparse MX4/MX NV logical K128. Group 1 has M128 and N8–256 by eight. Group 2 has **only M256**, with N16–256 by sixteen; dense group-2 M128 and dense K96 are not sparse rows. The Table 47 K-choice bit for a caller-known MX4/MX NV word must select the sparse-compatible K128 state. The sole descriptor catalogue checks encoded fields, including the sparse bit; the independent query compares its known kind and sparse bit with the source identity. Neither reads live `idesc` contents.

The selector may be omitted for sparse MX8 (effective `scale_vec::1X`) or MX4 (effective `block32`), while MX NV requires a written selector. Omission remains distinct from a written suffix. Sparse MX8 has one scale factor, one-byte required sub-column alignment, and valid scale IDs 0–3. Sparse MX4 `scale_vec::2X`/`block32` has two factors, two-byte alignment, IDs 0/2. Sparse MX NV `scale_vec::2X`/`block32` has two UE8M0 factors and IDs 0/2; `scale_vec::4X`/`block16` has four UE8M0 or UE4M3 factors, four-byte alignment, and ID 0. The generated [Tables 59/60](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) rows are explicit; factor counts are not inferred by dividing K by a block length. Caller-known alignment is a positive power-of-two guarantee satisfying the row, and byte-slot IDs must be consistent with that guarantee. Live selected scale placement and values remain obligations.

The base sparse MX4/MX NV target identities are exact `sm_100a`, `sm_110a`, or `sm_103a` at their fixed PTX floors; written vector selectors independently require exact `sm_100a`, and written block selectors require the `sm_100f`/`sm_110f` family feature and PTX 8.8/9.0. The sparse MX NV kind begins at PTX 8.7 on `sm_100a`. These are intersected source and known-operation checks, rather than a broad family-only base gate. The packed four/six-bit A/B rules from their corresponding dense kinds remain conditional caller-known checks.

Sparse MX8 metadata uses 2:4 grouping. Sparse MX4/MX NV metadata uses **pairwise 4:8** grouping: every two-element chunk is either all zero or all nonzero. Both use meaningful nibble codes `4,8,C,9,D,6,E`; equal codes do not make the grouping rules interchangeable. Known indices and full-path A/D/metadata lane-zero identities can be checked, while live metadata, compressed A, scale contents, allocation and synchronization remain unproved. The [installed consumer](../examples/tcgen_mma_sparse_mx_consumer) exercises the public source and generated conditional query.


<a id="tcgen-mma-tf32-coverage"></a>

## TCGEN dense tf32 MMA coverage

Technical reference and evidence: [tcgen_mma_tf32_coverage.md](us-en/converage/tcgen_mma_tf32_coverage.md).

<a id="tcgen-mma-tf32-coverage--tcgen-dense-tf32-mma-coverage"></a>

### TCGEN dense tf32 MMA coverage

This page describes the dense, non-weight-stationary, non-convolution
`.kind::tf32` source form and its caller-known operational query. The fixed
[PTX 9.3 `tcgen05.mma` clause](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
and the accepted descriptor catalogue set the field and source rules. A finite
complete-module CUDA 13.3 assembler checkpoint corroborated source spelling;
assembly alone cannot prove GPU behavior or live descriptor register values.

The canonical spelling is `tcgen05.mma.cta_group::1/2.kind::tf32`.
Group 1 and 2 each allow shared-descriptor or Tensor Memory A, required output
lane mask, and optional D scale. These are four structural operand layouts and
two typed group values, or eight source topologies. The checkpoint assembled
all sixteen historical mask-present/absent combinations at `sm_100a`/PTX9.3; omitted-mask acceptance is tool divergence. The exact adjacent written-order alias
`.mma.kind::tf32.cta_group::1/2` is also approved as the same typed selected
form. Group 1 alias assembly was observed; extending the same adjacent swap to
group 2 is an Authority grammar inference, not a separate assembler result.
Other permutations and duplicate qualifiers remain invalid.

Shared A/B descriptors use scalar General B64/U64/S64 **registers**, and the
instruction descriptor uses a scalar General B32/U32/S32 register. The
assembler accepted direct literal zero in each descriptor role, but the fixed
source names register operands; tool acceptance does not broaden the frontend
role. `enable-input-d` accepts a scalar predicate register with optional
written negation or an integer constant with zero/nonzero truth, including 2.
Special predicate registers remain excluded. The mask is required and has exactly four group-1 or eight group-2 scalar General 32-bit
register entries. B32/U32/S32/F32 declarations are compatible as **mask bits**;
F32 here does not request floating conversion. An explicit empty vector is
invalid. The optional D scale is an immediate whose **original** evaluated
integer is 0..15; a low-bit alias of a wider value is outside this range.
Located owned payloads must retain and recheck these roles after the parser's
syntax tree is released.

[Table 42](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)
fixes D=F32 and A=B=TF32. Dense group 1 has M=64/128 and N=8..256 in steps
of 8; group 2 has M=128/256 and N=16..256 in steps of 16. K=8 is uniquely
determined and is not a separate source operand. The accepted Table 45
descriptor catalogue owns encoded D/A/B type fields. The operational query
invokes its defined-field validators first, then reports checked
known facts, proven violations and still-missing facts separately. Live source
registers stay opaque; caller-supplied descriptor words are independent claims
and do not authenticate those registers.

For caller-known shared words, [Table 57](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
requires the 128-byte swizzle with **32-byte atomicity** for a transposed
32-bit A or B matrix. A and B are checked independently; normal K-major
shared operands use the accepted non-transposed swizzle domain. Tensor Memory A
has no A-shared-word obligation. The non-WS F/D/B/A paths are selected by
group/M: (1,64), (1,128), (2,128), (2,256). On the F half path, independently
supplied A/D Tensor Memory lane-half identities can be compared when A is in
Tensor Memory. Full D/B/A paths require lane-half zero, and a known D half is
checked even with shared A. Address spelling cannot derive those identities
or prove allocation history.

The source/target checkpoint supports plain tf32 on exact `sm_100a` from
PTX8.6, the `sm_100f` family including `sm_103a/f` from PTX8.8, and the
`sm_110f` family including `sm_110a/f` from PTX9.0. Optional D scaling has
the narrower exact-100a/100f-family gate; scaled forms were rejected on
`sm_110a/f`. Generic `sm_100`, `sm_110` and `sm_120a/f` are excluded.
Below-floor assembler cases whose no-MMA `.target` controls failed are
directive-confounded and are not independent MMA-floor evidence.

The [family matrix](#tcgen-mma-family-coverage) covers the other fixed dense,
sparse and WS source forms; live descriptor and memory facts remain runtime
obligations. This selected tf32 view
borrows from the exact `Tcgen05MmaTf32` form and preserves the existing f16
view and generated identity. The module's CTA-group consistency check remains
limited to TCGEN instructions. TMA omitted/group-1/group-2 forms may coexist
in one body and must not inherit that TCGEN-only uniform-group rule.

The current ordinary non-WS dense and sparse forms also support [typed A collector and ashift controls](#tcgen-mma-a-collector-coverage); their caller-known history checks remain conditional.


<a id="tcgen-mma-ws-dense-coverage"></a>

## Dense weight-stationary TCGEN MMA

Technical reference and evidence: [tcgen_mma_ws_dense_coverage.md](us-en/converage/tcgen_mma_ws_dense_coverage.md).

<a id="tcgen-mma-ws-dense-coverage--dense-weight-stationary-tcgen-mma"></a>

### Dense weight-stationary TCGEN MMA

The fixed [CUDA 13.3 PTX 9.3 TCGEN MMA specification](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) defines dense `.mma.ws.cta_group::1.kind` forms for f16, tf32, f8f6f4 and i8. The frontend preserves these as four exact final source classes. Each has shared or Tensor Memory A and an optional final scalar 64-bit zero-column descriptor register: `D, A, B, idesc, enableD[, zeroColumnDesc]`. The optional B0–B3 `fill`, `use`, `lastuse` or `discard` collector follows the kind. CTA group 2, A collector, block scaling, ashift, lane mask and D-scale are excluded from these forms. The written collector is an owned typed buffer/action pair with a source range; omission stays an all-Unspecified pair with no range and derives effective B0/discard only for operational interpretation. The [installed consumer](../examples/tcgen_mma_ws_dense_consumer) exercises the public source, borrowed `tcgen_mma_ws_view` and known-value query after syntax-tree destruction.

The Table 42 known-shape rows permit M32, M64 or M128 and N64, N128 or N256. K is 16 for f16, 8 for tf32, and 32 for f8f6f4 or i8. The selected paths are M32/G1×4, M64/E2×2 and M128/D4×1; each permits only lane-half zero. The shared-descriptor major/swizzle and low-bit transpose constraints retain their independent role checks. Low-bit f8 input packing remains an explicit obligation when applicable. An independently supplied Table 48 zero-column word is checked only when the source names the optional register; its absence then remains missing. Neither this word nor the register source authenticates live bits.

The query keeps B-buffer validity assertions separate by buffer. A false assertion contradicts `use`/`lastuse`, an unknown assertion remains missing, and a true one does not prove a prior fill or ordering. The collector may reread A/B, so source stability until completion remains an obligation. Descriptor values, Tensor Memory allocation, collector contents, sequencing, synchronization and GPU execution are not established by source acceptance or caller-known words. [Sparse WS](#tcgen-mma-ws-sparse-coverage) adds a mandatory metadata source and conditional sparse rules.


<a id="tcgen-mma-ws-sparse-coverage"></a>

## Sparse weight-stationary TCGEN MMA

Technical reference and evidence: [tcgen_mma_ws_sparse_coverage.md](us-en/converage/tcgen_mma_ws_sparse_coverage.md).

<a id="tcgen-mma-ws-sparse-coverage--sparse-weight-stationary-tcgen-mma"></a>

### Sparse weight-stationary TCGEN MMA

The fixed [CUDA 13.3 PTX 9.3 TCGEN MMA specification](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma) defines `.mma.ws.sp.cta_group::1.kind` for f16, tf32, f8f6f4 and i8. Four exact source classes each retain shared or Tensor Memory A and optional final scalar 64-bit zero-column register: `D, A, B, [metadata], idesc, enableD[, zeroColumnDesc]`. The bracketed Tensor Memory metadata address is mandatory between B and `idesc`. The B0–B3 collector has four written actions or canonical omission; omission derives B0/discard only in the operational report. `.mma.sp.ws`, CTA group 2, A collector, ashift, block scaling, lane mask and D-scale have no source form here. The [installed consumer](../examples/tcgen_mma_ws_sparse_consumer) exercises an owned source after AST destruction and the conditional report.

The Table 42 rows have M32/M64/M128, N64/N128 and logical K32/16/64/64 for f16/tf32/f8f6f4/i8. Sparse A stores K/2 elements while B retains logical K. The shared WS path data select M32/G1×4, M64/E2×2 or M128/D4×1 and require independently known A (when in Tensor Memory), D and metadata lane-half identities to be zero. The M32 metadata subcolumn mapping lacks an established fixed diagram, so the report retains a `MetadataLayoutRule` obligation even if a caller supplies lane claims. This does not reject the canonical M32 source.

The existing Table 45 sparse selector and kind-specific metadata nibble domains are reused. tf32 has 1:2 metadata indices E/4; f16, f8f6f4 and i8 have 2:4 indices 4/8/C/9/D/6/E. Supplied nibbles can contradict this domain, but no live metadata contents or 50% sparsity are inferred. The dense WS Table 43/48 shared and zero-column checks, low-bit packing obligations, kind target catalog and per-B-buffer conditional history rules continue unchanged. A false prior-validity assertion contradicts use/lastuse; a true assertion cannot prove a prior fill. Source A/B may be reread until completion. Descriptor authenticity, Tensor Memory allocation, collector sequence, synchronization and GPU execution remain runtime obligations. [Dense WS](#tcgen-mma-ws-dense-coverage) has the same B/zero source controls without sparse metadata.


<a id="tcgen-sync-coverage"></a>

## TCGEN specialized synchronization

Technical reference and evidence: [tcgen_sync_coverage.md](us-en/converage/tcgen_sync_coverage.md).

<a id="tcgen-sync-coverage--tcgen-specialized-synchronization"></a>

### TCGEN specialized synchronization

The frontend models `tcgen05.commit` and both operand-free specialized fences from fixed PTX 9.3. Commit has eight written forms: CTA group 1 or 2, generic or explicit `.shared::cluster` barrier-address spelling, and multicast absent or present. A multicast form requires a scalar General-class `.b16`, `.u16`, or `.s16` register mask; an immediate mask is not accepted. Each bit selects the destination `%cluster_ctarank`, and the mbarrier signal targets the same shared-memory offset in that CTA. The two fence directions, `before_thread_sync` and `after_thread_sync`, have no operands or CTA-group qualifier. These ten forms have exact final instruction classes and share a generated typed descriptor path.

```ptx
.version 9.0
.target sm_110a
.address_size 64
.visible .entry kernel() {
  .shared .align 8 .b64 barrier;
  .reg .b16 %mask;
  tcgen05.fence::before_thread_sync;
  tcgen05.commit.cta_group::1.mbarrier::arrive::one.multicast::cluster.b64 [barrier], %mask;
  tcgen05.fence::after_thread_sync;
  ret;
}
```

Commit records the protocol for prior applicable async TCGEN operations issued by the same thread and matching CTA group. On their completion, the system signals `mbarrier::arrive::one`, count one, at cluster scope. The barrier is accessed through the generic proxy even with explicit `.shared::cluster` spelling. A known barrier must be eight-byte aligned and in shared memory; dynamic pointers retain the obligation to resolve inside the cluster-shared window. The frontend checks known scalar pointer and mask declarations, source form, target, and per-function-body CTA-group consistency. A different function body may use the other group. It does not prove prior operations exist, mbarrier initialization, progress, peer CTA presence, dynamic address validity, cross-function agreement, or control-flow ordering.

`before_thread_sync` orders prior async TCGEN operations before later TCGEN and execution-ordering operations; `after_thread_sync` orders later operations after prior TCGEN and execution-ordering operations. They do not complete operations or replace `fence.proxy.async`. Commit's mbarrier-arrive-one completion identity is distinct from Tensor Memory load/store waits, bulk groups, generic async groups, and complete-tx-bytes.

These forms require exact `sm_100a` from PTX 8.6, family `sm_100f` from 8.8, or exact `sm_110a`/family `sm_110f` from 9.0. Generic and unrelated targets do not substitute. Official 13.3.73 assembler evidence corroborated all eight layouts, both fences, compatible 16-bit register masks, and these valid target endpoints. Direct literal masks failed with matched register controls; the assembler does not prove runtime mask values or synchronization behavior. The [TCGEN MMA frontend family coverage](#tcgen-mma-family-coverage) records how these completion forms meet the dense/sparse/WS source families without proving runtime protocol history.

Source: [CUDA 13.3 / PTX 9.3 commit and fence clauses](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-commit). [Copy and shift](#tensor-memory-copy-shift-coverage) are eligible prior TCGEN work for a matching group; their source forms do not themselves signal the barrier.


<a id="tensor-async-closeout"></a>

## Tensor-map and TMA frontend closeout matrix

Technical reference and evidence: [tensor_async_closeout.md](us-en/converage/tensor_async_closeout.md).

<a id="tensor-async-closeout--tensor-map-and-tma-frontend-closeout-matrix"></a>

### Tensor-map and TMA frontend closeout matrix

This matrix audits the M14-I01–I22 and M14-C01–C03 requirements against the
fixed [CUDA 13.3 PTX ISA 9.3][ptx] and this source tree. “Covered” means that a
supported source form can be parsed without losing its spelling or locations,
resolved to owned typed IR, and checked against its instruction-local operands
and selected PTX/target profile. The tensor map remains an **opaque 128-byte
object**. Neither the selected instruction nor caller-supplied
[`TensorMapKnownFacts`][facts-doc] authenticates its bytes. The fixed PTX 9.3
canonical tensor load, store, reduction, and prefetch templates map to 356
distinct selected identities in the current target catalog: 180 loads, 18
stores, 128 reductions, and 30 prefetches. These form 178 base/cache-hint
pairs with identical non-cache operands and optional policy layouts. This
inventory concerns the documented source templates and catalog targets; it
does not establish GPU execution, runtime synchronization, raw descriptor
contents, or undocumented aliases.

The primary #152 slice is tensor-map and tensor-copy forms. The non-tensor bulk
forms and their completion operations are shared dependencies delivered under
#151; `red.async` belongs to the atomic/reduction family. Their rows are included
to preserve the historical M14 work-item grouping, without reassigning ownership. The
exact supported forms and adjacent rejected forms are in the linked schema and
tests; a row does not authorize an unlisted suffix, mode, or target combination.

| Historical work-item ID | PTX 9.3 requirement | Source contract | Executable evidence | Ownership |
| --- | --- | --- | --- | --- |
| M14-I01 | [§5.5.1 dimension/format][dimension], [§5.5.2 access mode][modes], [§5.5.8 tensor map][map] | [typed rank/mode and Table 33 domains][ir]; [conditional element/swizzle/interleave facts][facts] | [replacement fields][replace-test], [known-facts rules][facts-test] | #152, descriptor values conditional |
| M14-I02 | [§5.5.8 map operand][map] | [owned `ResolvedTensorMapRef` address, identity and range][ir]; [selected forms][schema] | [parameter/symbol/register descriptor cases][tensor-test] | #152 |
| M14-I03 | [§9.7.9.26.5.2 coordinates][tensor-copy] | [owned coordinates and source ranges][ir]; [rank/mode binding][model] | [CST/AST composite][ast-test], [rank/sign conversion][tensor-test] | #152 |
| M14-I04 | [§9.7.9.27 `tensormap.replace`][replace] | [replacement field/code domain][ir], [type/target checker][checker] | [field, value, target and tamper cases][replace-test] | #152 |
| M14-I05 | [§9.7.14.17 `tensormap.cp_fenceproxy`][proxy] | [proxy form][schema], [scope/size checker][checker] | [proxy scope, operand and tamper cases][replace-test] | #152 |
| M14-I06 | [§9.7.9.26.4.1 global→cluster copy][bulk-copy] | [non-tensor bulk forms][schema], [address/completion checker][checker] | [copy and group topology][bulk-test] | #151 shared |
| M14-I07 | [§9.7.9.26.4.1 CTA→global copy][bulk-copy] | [directional bulk forms][schema], [address/completion checker][checker] | [copy and group topology][bulk-test] | #151 shared |
| M14-I08 | [§9.7.9.26.4.1 `.sem/.scope`][bulk-copy] | [exact qualifiers and availability][schema] | [qualifier matrix and invalid pairings][bulk-test] | #151 shared |
| M14-I09 | [§9.7.9.26.4.2 bulk reduction][bulk-reduce] | [operation/type and completion forms][schema] | [reduction/scope and negative cases][bulk-test] | #151 shared |
| M14-I10 | [§9.7.9.26.4.3 bulk prefetch][bulk-prefetch] | [L2/global prefetch forms][schema] | [reduction and prefetch cases][bulk-test] | #151 shared |
| M14-I11 | [§9.7.9.26.5.2 tiled load][tensor-copy] | [rank 1–5 shared destinations and mbarrier forms][schema]; [typed mode][ir] | [all ranks, direction and pointer checks][tensor-test] | #152 |
| M14-I12 | [§9.7.9.26.5.2 tiled store][tensor-copy] | [rank 1–5 global destinations and bulk-group forms][schema] | [store ranks and signed-coordinate checks][tensor-test] | #152 |
| M14-I13 | [§9.7.9.26.5.3 tensor reduction][tensor-reduce] | [selected op/rank/mode identities][schema]; [typed reduction metadata][model] | [identity, element-type and target cases][reduction-test] | #152 |
| M14-I14 | [§9.7.9.26.5.4 tensor prefetch][tensor-prefetch] | [rank 1–5 forms][schema], [owned tensor operand][ir] | [prefetch rank and tile provenance][tensor-test] | #152 |
| M14-I15 | [§9.7.9.26.5.2 im2col][tensor-copy] | [im2col and no-offset modes, U16 info roles][model] | [info arity/type][im2col-test], [no-offset forms][nooffset-test] | #152 |
| M14-I16 | [§9.7.9.26.5.2 W/W128][tensor-copy] | [W halo/offset and exact target availability][model] | [W-family target endpoints and negative forms][im2col-test] | #152 |
| M14-I17 | [§9.7.9.26.5.2 gather/scatter][tensor-copy] | [four-row role and rank-two modes][ir]; [forms][schema] | [four owned forms and adjacent rejections][gather-test] | #152 |
| M14-I18 | [§9.7.9.26.6.1 bulk commit][commit] | [distinct `bulk_group` completion kind][completion]; [form][schema] | [copy/group topology][bulk-test] | #151 shared |
| M14-I19 | [§9.7.9.26.6.2 bulk wait][wait] | [constant count and optional `.read`][schema] | [copy/group topology and invalid forms][bulk-test] | #151 shared |
| M14-I20 | [§9.7.9.12 `st.async`][store-async] | [shared mbarrier and global release forms][schema] | [store topology and destination-base cases][bulk-test] | #151 shared |
| M14-I21 | [§9.7.9.14 `st.bulk`][store-bulk] | [zero-fill size/type/target forms][schema] | [store size/version and owned binding cases][bulk-test] | #151 shared |
| M14-I22 | [§9.7.14.7 `red.async`][red-async] | [shared completion and global release forms][red-schema] | [async reduction modes and negative forms][atomic-test] | atomic/reduction shared |
| M14-C01 | [§9.7.9.26.6 completion][completion-ptx] | [one `AsyncCompletionKind` domain][completion] distinguishes async-group, bulk-group and mbarrier complete-tx-bytes; instruction forms select a value | [bulk topology][bulk-test], [tensor direction][tensor-test] | #151/#152 shared |
| M14-C02 | [§9.7.9.26.5.1 restrictions][restrictions] | [rank/mode/coordinate/direction/space checker][checker]; [conditional descriptor-facts query][facts] | [tensor][tensor-test], [im2col][im2col-test], [known facts][facts-test] | #152, opaque boundary |
| M14-C03 | [§9.7.9.26.4–.6 forms][tensor-copy] | [schema variants and exact target availability][schema] | [sm90a/sm100 positive and adjacent negative C++ cases][tensor-test], [bulk][bulk-test], [mode][im2col-test] | #151/#152 shared corpus |

The #152 frontend slice also includes cross-cutting forms: cluster multicast
masks are covered by the [schema][schema], [typed mask][ir], and
[positive/negative cases][multicast-test]; per-instruction `.cta_group::1/::2`
routing is covered by its [typed role][ir] and
[mixed-group/target cases][group-test]; tiled/reduction/store
`.im2col_no_offs` is covered by [mode tests][nooffset-test]; canonical
`.L2::cache_hint` and optional final B64 policy are covered by the
[schema][schema], [owned query][cache], and
[positive/negative/tamper cases][cache-test]. Their precise clauses are
[§9.7.9.26.5.2][tensor-copy], [§9.7.9.26.5.3][tensor-reduce], and
[§9.7.9.26.5.4][tensor-prefetch].

The separate [generic `prefetch.tensormap` form](#prefetch-coverage) accepts a
known shared address as a documented no-op; an explicit `.shared.tensormap`
spelling remains outside the selected forms. AST-free module validation checks
each bound address symbol's cached type and guaranteed alignment against its
owned declaration, including tensor-map references nested in tensor operands.
The [prefetch binding regressions][prefetch-test] and [nested tensor
regression][tensor-test] exercise these two boundaries after AST destruction.

The source-level regression tests above cover selected forms and adjacent
negative boundaries; they are not a proof that every possible instruction
combination assembles. The [26 recorded PTXAS 13.3.73 probes][ptxas] are a
bounded cache-control experiment, separate from the C++ corpus. They expose a
hint-only discrepancy between the formal PTX syntax and that assembler, and do
not demonstrate runtime cache effects.

Static checking can establish known address space/alignment, operand shape,
literal conversion and exact selected target availability. The
[22 caller-known-facts rule families][facts-doc] can conditionally check supplied
descriptor claims, including some bounds and swizzle-dependent alignment
relationships; missing facts remain `Unresolved`. Without sufficient supplied
facts or runtime values, raw descriptor fields, coordinates, peer/mask
membership, barrier locality/completion, bounds, swizzle-dependent alignment
and actual memory effects remain unproven. A full raw 128-byte decoder needs a
separately specified byte layout and is not part of this closeout.

[ptx]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html
[dimension]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensor-dimension-size-format
[modes]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensor-access-modes
[map]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensor-tensormap
[restrictions]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-tensor-copy-restrictions
[tensor-copy]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-tensor
[tensor-reduce]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-reduce-async-bulk-tensor
[tensor-prefetch]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-prefetch-tensor
[bulk-copy]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk
[bulk-reduce]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-reduce-async-bulk
[bulk-prefetch]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-prefetch
[commit]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-commit-group
[wait]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-wait-group
[completion-ptx]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-bulk-tensor-copy-completion
[replace]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-tensormap-replace
[proxy]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-tensormap-cp-fenceproxy
[store-async]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st-async
[store-bulk]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st-bulk
[red-async]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-red-async
[schema]: ../python/src/ptx_frontend/spec/resources/ptx_spec/data_movement_and_conversion.yaml
[red-schema]: ../python/src/ptx_frontend/spec/resources/ptx_spec/parallel_synchronization_and_communication.yaml
[ir]: ../submod/resolved_ir/include/ptx_resolved_ir_foundation.hpp
[model]: ../python/src/ptx_frontend/ir/resolved_ir.py
[completion]: ../python/src/ptx_frontend/spec/model.py
[checker]: ../submod/resolved_ir/src/ptx_resolved_ir_checker.cpp
[facts]: ../submod/resolved_ir/include/ptx_tensor_map_known_facts.hpp
[facts-doc]: #tensor-map-known-facts
[cache]: ../submod/resolved_ir/include/ptx_tensor_cache_controls.hpp
[ptxas]: tensor_cache_controls_ptxas.json
[ast-test]: ../submod/syntax/test/test_ptx_syntax_ast_parser.cpp
[replace-test]: ../submod/resolved_ir/test/test_tensormap_replacement.cpp
[tensor-test]: ../submod/resolved_ir/test/test_tensor_async_coverage.cpp
[prefetch-test]: ../submod/resolved_ir/test/test_prefetch_completeness.cpp
[reduction-test]: ../submod/resolved_ir/test/test_tensor_reduction.cpp
[im2col-test]: ../submod/resolved_ir/test/test_tensor_im2col_info.cpp
[nooffset-test]: ../submod/resolved_ir/test/test_tensor_no_offsets.cpp
[gather-test]: ../submod/resolved_ir/test/test_tensor_gather_scatter.cpp
[multicast-test]: ../submod/resolved_ir/test/test_tensor_multicast.cpp
[group-test]: ../submod/resolved_ir/test/test_tensor_cta_group.cpp
[cache-test]: ../submod/resolved_ir/test/test_tensor_cache_controls.cpp
[bulk-test]: ../submod/resolved_ir/test/test_bulk_async_coverage.cpp
[atomic-test]: ../submod/resolved_ir/test/test_atomic_reduction_coverage.cpp
[facts-test]: ../submod/resolved_ir/test/test_tensor_map_known_facts.cpp
<a id="tensor-async-coverage"></a>

## Tiled tensor asynchronous data movement

Technical reference and evidence: [tensor_async_coverage.md](us-en/converage/tensor_async_coverage.md).

<a id="tensor-async-coverage--tiled-tensor-asynchronous-data-movement"></a>

### Tiled tensor asynchronous data movement

The frontend models selected tiled tensor-map forms in [PTX ISA 9.3 §5.5 and §9.7.9.26.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html). The composite `[tensorMap, {coords}]` keeps its nested address, coordinate elements, punctuation, and source ranges through CST and AST lowering. Resolved IR owns a `ResolvedTensorMapRef` and a rank 1–5 `ResolvedTensorOperand`. The baseline tiled forms below accept omitted or explicit `.tile`; the `tile` modifier field records whether it was written.

| Form | Operands and completion | Earliest PTX / target |
| --- | --- | --- |
| `cp.async.bulk.prefetch.tensor.{1d…5d}.L2.global{.tile}` | `[tensorMap, {coords}]`; no completion operand | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cluster.global{.tile}.mbarrier::complete_tx::bytes` | `[dst], [tensorMap, {coords}], [mbar]`; mbarrier complete-tx-bytes | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cta.global{.tile}.mbarrier::complete_tx::bytes` | Same operands, CTA shared destination | 8.6 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.global.shared::cta{.tile}.bulk_group` | `[tensorMap, {coords}], [src]`; existing bulk-group commit/wait | 8.0 / SM 90 |

`tensorMap` is a generic pointer to an opaque 128-byte descriptor. A direct descriptor symbol may reside in kernel `.param`, `.const`, or `.global` storage; its declaration identity, storage space, alignment, and source range stay in owned IR. A register pointer retains its register identity, while its runtime storage and alignment remain unknown. Tensor data direction is modeled separately: load reads global tensor data into shared memory; store reads CTA shared memory and writes global tensor data.

The checker requires exactly one signed-32 coordinate per rank for ordinary tiled access and accepts compatible 32-bit registers. PTX 64-bit integer constants are converted to signed 32 bits at coordinate use, with the original source value and sign retained in owned IR: `4294967296` converts to zero, while `4294967295` converts to −1. Loads and prefetch may use negative converted coordinates. Stores reject statically negative converted coordinates; register coordinate values require runtime validation. Owned-module validation also compares a bound coordinate register's cached type with its declaration, including after the AST has been destroyed; declaration-free standalone checks still defer missing binding metadata. Known descriptor, shared data, and mbarrier addresses must be aligned to 64, 16, and 8 bytes respectively. The shared-data check is the tiled mode baseline; stronger alignment required by the descriptor's swizzle mode remains a runtime obligation because the descriptor is opaque here. Unknown register-pointer alignment is not treated as proven. The checker rejects a known shared/local or non-kernel parameter descriptor and verifies PTX/SM availability.

The modeled TMA family also includes selected `tensormap.replace` and `tensormap.cp_fenceproxy` forms, tensor reduction, rank 3–5 `im2col`/`im2col::w`/`im2col::w::128`, rank-2 four-row gather/scatter, cluster multicast masks, and explicit `.cta_group::1/::2` routing. Their exact mode, direction, operand, and target combinations are selected by the generated descriptors; an adjacent unsupported combination fails selection or checking. Omitted, group-1, and group-2 TMA spellings may coexist in one function because TMA group routing is per instruction. The separate [TCGEN group rule](#tensor-memory-allocation-coverage) applies only to TCGEN instructions.

[`TensorMapKnownFacts` and the selected-use projection](#tensor-map-known-facts) can conditionally check caller-supplied descriptor facts against a typed selected instruction. `project_tensor_known_access_context(const Instruction&, const checker::Context&)` returns a copied access and diagnostics: a non-tensor form has neither; malformed tensor metadata has diagnostics and no access; an unavailable target retains the copied access with availability marked false. This query does not decode or authenticate the raw 128-byte descriptor. Descriptor contents, rank agreement with actual bytes, swizzle, stride, bounds, barrier locality, and runtime synchronization remain unproven without caller facts or execution. Canonical `.L2::cache_hint` and optional final B64 cache-policy operands are covered for the supported tensor load, store, reduction, and prefetch forms; see [tensor cache controls](#tensor-cache-controls). A raw-object decoder is outside this frontend slice because the fixed manual does not provide a complete 128-byte field layout; the known-facts and cache queries do not decode descriptor bytes. The [frontend closeout matrix](#tensor-async-closeout) links the listed requirements to source and tests.

For destination `.shared::cluster`, rank-2 `.tile::gather4` and rank-3–5 plain
`.im2col::w` require exact `sm_100a` from PTX8.6, or the `sm_100f` family from
PTX8.8. The catalog's `sm_103a`/`sm_103f` profiles retain that family feature;
generic numeric `sm_100`/`sm_103` and cross-family `sm_110a`/`sm_110f` do not
satisfy this specific feature clause. Cache-hint, information, multicast and
written CTA-group forms inherit the same restriction. A separate CTA-group or
multicast allowance cannot override it: all applicable conditions intersect.

Adjacent `.im2col::w::128`, `.tile::scatter4` and ordinary tiled CTA-group forms
retain their independent `110a/f` grants. Shared-CTA gather4 and W retain their
own generic SM100-or-higher rule. The frontend follows the pinned PTX9.3
feature-specific Target Notes; possible manual/tool divergence does not grant
a broader target family. The [target regressions](../submod/resolved_ir/test/test_tensor_target_restrictions.cpp)
separate actual availability diagnostics from optional `TensorMapKnownFacts`
results. Missing descriptor facts can remain `Unresolved` even on an available
target; an unavailable target still returns copied selected-use facts with
availability false. Neither status authenticates MAP bytes or runtime completion.


<a id="tensor-memory-allocation-coverage"></a>

## Tensor Memory allocation coverage

Technical reference and evidence: [tensor_memory_allocation_coverage.md](us-en/converage/tensor_memory_allocation_coverage.md).

<a id="tensor-memory-allocation-coverage--tensor-memory-allocation-coverage"></a>

### Tensor Memory allocation coverage

The frontend models the PTX 9.3 `tcgen05` allocation-management trio:
`alloc`, `dealloc`, and `relinquish_alloc_permit`. Both
`.cta_group::{1,2}` values are supported. `alloc` accepts both an explicit
`.shared::cta` result-slot qualifier and the omitted generic qualifier. The
result is a four-byte value written to a separate shared-CTA slot; it is not
an allocation token returned directly in a register.

```ptx
.version 8.6
.target sm_100a
.address_size 64
.entry kernel() {
  .shared .align 4 .b32 slot;
  .reg .b32 %taddr;
  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 [slot], 32;
  ld.shared.b32 %taddr, [slot];
  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %taddr, 32;
  tcgen05.relinquish_alloc_permit.cta_group::1.sync.aligned;
  ret;
}
```

| Contract | Static frontend boundary |
| --- | --- |
| CTA group | Typed `TcgenCtaGroup::One` or `Two`; all supported TCGEN operations in one kernel/function body must agree. Separate bodies may choose different groups. A standalone instruction has no enclosing-body proof. |
| `nCols` | Scalar `.b32`/`.u32`/`.s32` general register, or integer immediate converted to unsigned 32 bits. Known converted values are 32, 64, 128, 256, or 512. Dynamic values retain the range, power-of-two, and group-uniformity obligation. |
| `taddr` | Distinct owned `TensorMemoryAddress` wrapping a scalar 32-bit register or converted immediate. The frontend does not infer a live allocation from its bits. Bits 31:16 are the lane address; bits 15:0 are the column address. |
| `alloc` result | Bracketed `ResolvedAddress` with a separate result-slot role, known shared-CTA provenance and four-byte natural alignment. An unresolved generic pointer retains a shared-window obligation. Known incompatible spaces and misalignment fail. |
| Action metadata | Each generated form carries a typed allocation action and permission effect. Group value maps to one issuing warp or one warp in each peer CTA. These describe obligations; they do not simulate runtime permission state. |

Integer immediates narrow **at use**. For example, `4294967328` and
`-4294967264` both convert to the legal count 32. The owned value retains
the original integer source and converted bits, and checker revalidation
requires them to agree after the syntax tree is destroyed.

Availability uses exact qualified targets: `sm_100a` from PTX 8.6, the
`sm_100f` family from PTX 8.8, and `sm_110a` plus the `sm_110f` family from
PTX 9.0. Generic `sm_100`, the `sm_120` family, and unknown targets do not
substitute for these profiles.

The frontend checks instruction-local types, known values, source provenance,
target gates, and per-body group consistency. It cannot prove peer CTA
participation, runtime allocation size evolution, deallocation before exit,
permission after relinquishment, or consistency across device-function calls.
Tensor Memory register `ld`, `st`, `ld.red`, and matching waits have a
[separate transfer contract](#tensor-memory-transfer-coverage). Copy, shift,
and specialized commit/fence operations have separate coverage contracts. No GPU execution or
numerical result is modelled here.

Dedicated C++ and Python tests and an installed-package consumer exercise the
allocation contract after releasing the syntax tree. `ptxas` 13.3.73 accepted sampled allocation
syntax at `sm_100a`/PTX 8.6, `sm_100f`/PTX 8.8, and
`sm_110a`/`sm_110f`/PTX 9.0; it also rejected sampled invalid counts,
carriers, and early or generic targets. These assembler samples do not prove
GPU execution or runtime collective behavior.


<a id="tensor-memory-copy-shift-coverage"></a>

## Tensor Memory copy and shift

Technical reference and evidence: [tensor_memory_copy_shift_coverage.md](us-en/converage/tensor_memory_copy_shift_coverage.md).

<a id="tensor-memory-copy-shift-coverage--tensor-memory-copy-and-shift"></a>

### Tensor Memory copy and shift

The frontend models the source-visible PTX 9.3 `tcgen05.cp` and `tcgen05.shift` forms as exact final instruction classes. Copy has **36** legal tuples: six shape/multicast pairs, two written CTA groups, and three paired format states. Shift has two group identities and accepts the two documented modifier orders for each group. Each semantic identity has direct typed operands, without an opcode-owner storage alternative.

| Copy shape | Written multicast |
| --- | --- |
| `.128x256b`, `.4x256b`, `.128x128b` | omitted |
| `.64x128b` | `.warpx2::02_13` or `.warpx2::01_23` |
| `.32x128b` | `.warpx4` |

The format pair is either absent, `.b8x16.b6x16_p32`, or `.b8x16.b4x16_p64`. Destination and source format tokens retain separate source locations in the owned form. A partial pair, two source formats, or a forbidden shape/multicast combination fails validation. Copy takes exactly `[taddr], s-desc`; shift takes exactly `[taddr]`. Shift has an implicit `.31x256b` movement shape, not a written qualifier. Both `tcgen05.shift.cta_group::1.down` and `tcgen05.shift.down.cta_group::1` are accepted as one group-1 identity, and similarly for group 2.

`[taddr]` uses the existing bracketed Tensor Memory address with U32 use conversion and retained 64-bit integer source. It admits a scalar General 32-bit register or converted integer immediate, without ordinary pointer offsets. For shift, the known converted lane component in bits 31:16 must be divisible by 32. The frontend checks this for known immediates; a register-valued lane remains a runtime obligation. It does not invent a byte-address or column restriction.

`s-desc` is a scalar General B64/U64/S64-compatible register. The copy form exposes a borrowed, distinct opaque TCGEN Table 43 descriptor-role view of that owned register, with known declaration and module-binding checks after AST release. The view ends with the owned instruction payload. A register's spelling does not reveal encoded descriptor contents: fixed/reserved bits, shared-memory base, leading stride, swizzle, padding, allocation and layout validity remain runtime obligations. This role is separate from WGMMA shared descriptors and TensorMap. The [TCGEN descriptor field and layout API](#tcgen-descriptor-coverage) queries caller-supplied known words independently; it does not decode or authenticate this borrowed register's runtime contents.

Copy uses exact `sm_100a` from PTX 8.6, family `sm_100f` from 8.8, or exact `sm_110a` / family `sm_110f` from 9.0. Shift uses only exact `sm_100a` from 8.6, exact `sm_103a` from 8.8, or exact `sm_110a` from 9.0. Generic, unrelated family, and `sm_120` targets do not substitute. The group qualifier participates in per-function-body group consistency with allocation, transfer and commit; separate bodies may use different groups. Both operations are eligible prior same-thread asynchronous TCGEN work for a matching `tcgen05.commit` mbarrier-arrive-one protocol. They do not themselves signal a barrier or prove completion. Specialized fences and `fence.proxy.async` retain their separate ordering and visibility obligations.

Official 13.3.73 assembler evidence accepted all 36 copy tuples, both shift orders for both groups, compatible U64/S64 descriptor carriers, and the supported target endpoints. Partial format pairs and tested invalid carriers and targets failed with matched controls. The PTX 8.5 `sm_100a` directive control failed, so those two below-floor instruction candidates were skipped and supply no isolated instruction-floor evidence. Assembly does not prove runtime descriptor layout, decompression behavior, data motion, peer participation, or completion. No GPU execution was performed.

Sources: [fixed CUDA 13.3 / PTX 9.3 copy](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-cp), [shift](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-shift), and [shared-memory descriptor Table 43](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#shared-memory-descriptor).


<a id="tensor-memory-coverage"></a>

## Tensor Memory named-mask boundary

Technical reference and evidence: [tensor_memory_coverage.md](us-en/converage/tensor_memory_coverage.md).

<a id="tensor-memory-coverage--tensor-memory-named-mask-boundary"></a>

### Tensor Memory named-mask boundary




<a id="tensor-memory-coverage--tcgen-mandatory-named-output-lane-masks"></a>

### TCGEN mandatory named output-lane masks

Exactly24 existing ordinary unscaled, non-WS dense/sparse MMA mask layouts admit a named source alongside braces. Their common physical carrier list is ordered B32/U32/S32/F32/F16x2, with native F16x2 appended for both forms. This changes only named permission and that common carrier list. The required `disable_output_lane` member remains `WithLocs<ResolvedRegisterVector>`, with the same kind, type B32, Element policy, layout identities and syntax arity1..8. No removed mask-omission layout returns, no zero/default mask is synthesized, and no descriptor-word or execution result is inferred from mask registers.

Named Element lowering uses the separately established actual-declaration-width inference: an ordinary General `.reg .v2/.v4` whole declaration, compatible physical type, <=128-bit extent, full source range and exact NamedProjection lanes. It does not guess4, select by opcode/target, add ordinary namedv8 or change fixed Element/Aggregate behavior. Existing CTA-group checking still requires exactly4 entries for group1 and8 for group2. Group1 `.v4` is legal; group1 `.v2` and group2 ordinary named `.v4` fail the full check. A lowering draft or missing-context deferral is not group legality. Group2 remains eight-brace-only for ordinary declaration syntax.

Current actual payloads, whole-base SymbolId/type/class/width and all4/8 lane provenance/ranges remain authoritative; callbacks borrow synchronously and reacquire each copied/assigned/cloned owner. Coherent retargeting to another real compatible declaration may pass current validation while a different saved operand AST still has an independent source-identity mismatch. Special/predicate/array/component-as-whole/InstructionOnly/wrong-width declarations and null sink lanes gain no permission. Matrix named machinery is a separately accepted prerequisite; this mask facet does not introduce it. Other masks, WS/MX/collector/ashift/scaled roles, target DNF/profile windows and required-presence/cardinality logic remain unchanged.


<a id="tensor-memory-transfer-coverage"></a>

## Tensor Memory register transfers

Technical reference and evidence: [tensor_memory_transfer_coverage.md](us-en/converage/tensor_memory_transfer_coverage.md).

<a id="tensor-memory-transfer-coverage--tensor-memory-register-transfers"></a>

### Tensor Memory register transfers

The frontend models PTX 9.3 `tcgen05.ld`, `tcgen05.st`, `tcgen05.ld.red`, and
`tcgen05.wait::{ld,st}` in the same canonical instruction category as Tensor
Memory allocation. These operations have distinct load and store completion
identities. A wait names prior matching operations issued by the executing
thread; the owned IR does not claim that an operation has already completed.

| Operation | Supported controls | Register fragment |
| --- | --- | --- |
| `ld` | Five data shapes; `.x1`–`.x128` subject to the table below; optional `.pack::16b` | Writable brace fragment of scalar 32-bit General registers |
| `st` | The same shapes and repeats; optional `.unpack::16b` | Readable brace fragment of scalar 32-bit General registers |
| `ld.red` | `.32x32b` or `.16x32bx2`; `.x2`–`.x128`; min/max; `.f32` with independent optional `.abs` and `.NaN`, or `.u32`/`.s32` without them | Writable brace fragment plus a separate scalar 32-bit reduction result |
| `wait` | Exactly `.wait::ld` or `.wait::st`, then `.sync.aligned` | No operands |

The shape/count contract follows the archived PTX 9.3 Tables 52 and 53.
`.32x32b`, `.16x64b`, and `.16x32bx2` use N registers for `.xN` through
`.x128`; `.16x128b` uses 2N registers through `.x64`; `.16x256b` uses 4N
registers through `.x32`. The 128-register ceiling is explicit for this
fragment kind. The omitted fragment maximum remains 64, and ordinary PTX
vectors retain their separate eight-element limit. Pack and unpack do not
change the register count.

```ptx
.version 9.0
.target sm_110a
.address_size 64
.entry kernel() {
  .reg .b32 %r<2>;
  .reg .b32 %taddr;
  .reg .u32 %red;
  tcgen05.ld.sync.aligned.32x32b.x2.b32 {%r0, %r1}, [%taddr];
  tcgen05.wait::ld.sync.aligned;
  tcgen05.ld.red.sync.aligned.32x32b.x2.min.u32 {%r0, %r1}, %red, [%taddr];
  tcgen05.wait::ld.sync.aligned;
  tcgen05.st.sync.aligned.32x32b.x2.b32 [%taddr], {%r0, %r1};
  tcgen05.wait::st.sync.aligned;
  ret;
}
```

`[taddr]` is a simple bracketed Tensor Memory address with the accepted
unsigned-32 use conversion; ordinary bracket offsets and byte-offset rules
are not admitted. The split `.16x32bx2` shape has a separate integer
`immHalfSplitoff`: trailing for load and reduction, between address and
fragment for store. Its owned value retains evaluated 64-bit source bits,
signed/unsigned source kind, and source location. The fixed ISA text does
not give this operand a use width, unit, range, or alignment, so the frontend
does not invent one or compute a second address. Once the AST is discarded,
structural source-kind corruption is detectable, while replacing one valid
source constant with another cannot be authenticated from that value alone.

The formal reduction spelling places `min` or `max` before the scalar type;
the documented example places the type first. Both tested orders resolve to
one typed identity and retain written modifier locations. This alias admits
no other permutations and no float controls on integer reductions.

Base load/store/wait require exact `sm_100a` from PTX 8.6, family `sm_100f`
from 8.8, or exact `sm_110a`/family `sm_110f` from 9.0. Reduction requires
family `sm_103f` from PTX 8.8 or family `sm_110f` from 9.0, with catalog
inheritance for qualified exact targets. Generic and unrelated targets do
not substitute for these introductions.

The frontend checks written forms, known register declarations, exact
fragment counts, source provenance, modifier domains, and target profiles.
Warp participation, uniform address values, allocation validity, matching
dynamic waits, and ordering through control flow remain runtime obligations.
There is no GPU execution or timing proof. [Tensor Memory copy and shift](#tensor-memory-copy-shift-coverage)
have a separate source-visible contract; [specialized commit and fence behavior](#tcgen-sync-coverage)
has its own completion and ordering contract.


<a id="testp-copysign-coverage"></a>

## `testp` and `copysign` coverage

Technical reference and evidence: [testp_copysign_coverage.md](us-en/converage/testp_copysign_coverage.md).

<a id="testp-copysign-coverage--testp-and-copysign-coverage"></a>

### `testp` and `copysign` coverage

This first floating-point slice models [PTX 9.3 §9.7.3.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-testp)
and [§9.7.3.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-copysign)
through the normal YAML, typed resolved IR, and target-aware checker path. It
does not execute floating-point operations.

`testp` accepts `finite`, `infinite`, `number`, `notanumber`, `normal`, and
`subnormal` for `.f32` and `.f64`, writes a predicate, and requires PTX 2.0 /
`sm_20`. The property is represented by the typed `TestProperty` domain, not a
comparison operator. `normal` follows the ISA classification contract; this
frontend does not evaluate values. Its source accepts typed floating literals
or same-width floating/bit register containers; integer literals and
wrong-width containers are rejected.

`copysign.f32` and `copysign.f64` also require PTX 2.0 / `sm_20`. Its first
source supplies the sign and its second source supplies the magnitude. Both
source positions admit typed floating literals or same-width floating/bit
register containers; integer literals and wrong-width containers are rejected.
Neither instruction permits a destination sink; `testp` also rejects a negated
predicate source.

PTXAS evidence was collected with CUDA 13.3.73 on `sm_90` using
`/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`: 54
probes, 29 accepted and 25 rejected. Five follow-up sink/negated-predicate
probes were all rejected. The probes cover both source positions, floating
immediates, same-width bit containers, invalid integer literals and widths, and
predicate destinations. That assembler cannot target `sm_20`, so the PTX 2.0 /
`sm_20` boundary is enforced by frontend checker tests rather than assembler
target evidence.

[Explicit floating MAD](#mad-coverage), [DIV](#div-coverage),
[reciprocal/square-root forms](#unary-float-coverage),
[floating transcendentals](#transcendental-coverage), and
[floating MIN/MAX](#min-max-coverage) are modelled separately. The remaining
Issue 142 work is reconciled by [floating and mixed ADD/SUB](#add-sub-coverage).


<a id="texture-coverage"></a>

## Texture and opaque resource coverage

Technical reference and evidence: [texture_coverage.md](us-en/converage/texture_coverage.md).

<a id="texture-coverage--texture-and-opaque-resource-coverage"></a>

### Texture and opaque resource coverage

The frontend uses the archived [CUDA 13.3 PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html), especially §§5.3 and 9.7.11. `tex`, `tld4`, `txq`, and `istypep` have canonical typed forms. YAML spellings become closed Python enum domains for mipmap mode, gather component, query, and resource role before C++ generation. The resolved form retains the exact geometry, omitted versus explicit mipmap selection, query or gather component, selected types, and written optional operands. `Instruction::check()` rechecks mutable operand shape, nested texture lanes, and resource carriers; module validation additionally checks source bindings, texturing mode, and feature gates after the syntax tree has been released.

| Family | Typed forms |
| --- | --- |
| `tex` | 1d, 2d, 3d, a1d, a2d, cube, acube, 2dms, a2dms; omitted/base mipmap, plus level/gradient where legal; v4 u32/s32/f16/f32 or v2 f16x2 results; optional offset, depth comparison, and residency where legal. |
| `tld4` | r/g/b/a gather from 2d, a2d, cube, or acube; v4 u32/s32/f32 results; legal offset, comparison, and residency. |
| `txq` | Texture dimensions, channel format, coordinate mode, array/mipmap/sample counts; sampler filter/address/unnormalized-coordinate queries; `.level` on dimensions only. |
| `istypep` | Runtime u64-compatible handle test for texref, samplerref, or surfref, without assuming the handle's runtime kind. |

Unified mode accepts direct texture declarations or a scalar u64-compatible indirect resource register. Independent mode requires separate direct texture and sampler declarations for accesses. Sampler queries use a sampler declaration in independent mode; `force_unnormalized_coords` is independent-only. One explicit texturing-mode selection applies across every target region in a module; omitted selections inherit it. The owned module checks that invariant after mutation, and validation against replacement source uses the replacement module's selection. Indirect resource use has a PTX 3.1 / sm20 feature gate; optional controls and each exact form retain their own canonical gates.

Opaque `.global .texref/.samplerref/.surfref` objects and entry input parameters keep symbol identity, kind, source shape, explicit alignment. Named static members belong only to module-global opaque storage. They have no invented byte size, default alignment, or ordinary address. Legacy `.tex .u32/.u64` declarations retain their source spelling and map to texture identity. `mov.u64` from an opaque symbol carries that identity rather than a generic storage address. Static member assignments preserve order and omissions. Opaque array initializers use nested aggregate object-index paths and per-object named fields; this combines the general aggregate syntax with §5.3 member initialization, which gives a direct scalar example but no special opaque-array example.

Coordinates retain each source lane and its location. Array layer and multisample index lanes have u32 interpretation within mixed 32-bit tuples; spatial lanes follow the selected coordinate type, and ignored padding lanes remain visible. Plain one-dimensional coordinates may be scalar registers, scalar immediates, or singleton packs, including in the bracket-omitted `tex` spelling; a1d has a separate array layer lane. Optional offset and gradient are brace packs (including singleton one-dimensional packs), with register or typed immediate lanes in any combination. Offset immediates retain their source values and must lie in [-8, 7]; depth comparison accepts a scalar F32 register or immediate. Texture result vectors preserve partial `_` destination sinks and their lane locations; source `_` lanes and all-sink result tuples remain invalid. `tex` also retains the archived Notes' bracket-omitted spelling and v4 coordinate compatibility for every geometry. The current CUDA 13.3 assembler rejects a tested bracket-omitted spelling even though the archived Notes permit it. An archived a2d offset example omits a required padding lane; the frontend follows the four-lane operation description, corroborated by the assembler. For a2dms, the lane roles follow the operation paragraph and example: layer, sample, x, y. `tld4` unified a2d/cube/acube forms follow the Description and ISA Notes and were accepted by the CUDA 13.3 assembler despite a narrower Syntax line.

The frontend validates statically known source facts and rejects known out-of-range offset constants. It does not decode texture objects, authenticate an indirect register's runtime kind, infer configuration from named initializers, or execute sampling. Runtime texture configuration, address modes, coordinate bounds, live offset register values, LOD, and residency remain program obligations.


<a id="transcendental-coverage"></a>

## Floating transcendental coverage

Technical reference and evidence: [transcendental_coverage.md](us-en/converage/transcendental_coverage.md).

<a id="transcendental-coverage--floating-transcendental-coverage"></a>

### Floating transcendental coverage

PTX 1.0–1.3 additionally accepts `sin.f32`, `cos.f32`, `lg2.f32`, and `ex2.f32` as exact omitted-control forms with approximate+FTZ interpretation. They reject PTX >=1.4 and omitted mode combined with written `.ftz`. Source omission and typed interpretation are separate; see [the public contract](#resolved-ir-design--historical-omitted-floating-controls).

The generated model adds the explicit approximate forms of [sin](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sin), [cos](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-cos), [lg2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-lg2), and [ex2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-ex2) from [PTX 9.3 §9.7.3.18–§9.7.3.21](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions), the single-precision [tanh](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-tanh) from §9.7.3.22, and the half/bfloat [tanh](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-tanh) and [ex2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-ex2) cohorts from §9.7.4.9 and §9.7.4.10. `.approx` is a typed fixed flag rather than an unchecked spelling, so explicit and historically omitted modes retain distinct identities. The frontend models parsing, owned resolution, and target validation; it does not execute numerical operations or compute approximation error bounds.

| Form | Minimum | Contract |
| --- | --- | --- |
| `sin.approx{.ftz}.f32`, `cos.approx{.ftz}.f32`, `lg2.approx{.ftz}.f32`, `ex2.approx{.ftz}.f32` | PTX 1.4 / all target architectures | Fixed typed `.approx`; optional `.ftz`; no rounding modifier. |
| `tanh.approx.f32` | PTX 7.0 / SM 75 | Fixed `.approx`; no `.ftz`, rounding, or saturation modifier. |
| `tanh.approx.{f16,f16x2}`, `ex2.approx.{f16,f16x2}` | PTX 7.0 / SM 75 | Fixed `.approx`; no `.ftz`. |
| `tanh.approx.{bf16,bf16x2}` | PTX 7.8 / SM 90 | Fixed `.approx`; no `.ftz`. |
| `ex2.approx.ftz.{bf16,bf16x2}` | PTX 7.8 / SM 90 | Fixed `.approx` and required typed `.ftz`. |

Each `.f32` source accepts a floating literal or a same-width native floating or bit register container, and the destination accepts the matching native or bit register container. The half and bfloat cohorts bind registers only, and admit no immediates: scalar `.f16` accepts `.f16` or `.b16`, packed `.f16x2` accepts `.f16x2` or `.b32`, and the BF16 forms require the `.b16`/`.b32` bit containers because `.bf16` is an instruction-only format rather than a `.reg` declaration format.

Integer literals and integer register containers, wrong widths, sinks, omitted `.approx` at PTX >=1.4, `.ftz` on any TANH cohort, `.ftz` on the F16 EX2 cohort, a missing `.ftz` on the BF16 EX2 cohort, and the unsupported `.f64` and `.f32x2` spellings are all rejected.

CUDA 13.3.73 `ptxas` evidence used `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`. Its 44-module matrix had 23 expected accepts, 21 rejects, and no assembler divergence. The rejections are corroborated by the assembler's own diagnostics, which name the same contracts the model enforces: `.approx modifier required for instruction 'sin'`, `Illegal modifier '.ftz' for instruction 'tanh'`, `.ftz modifier required for instruction 'ex2.bf16'`, and `Unexpected instruction types specified for 'sin'` for `.f32x2`. Focused `-arch=sm_75` probes accept the PTX 7.0 TANH and F16 EX2 cohorts, and `-arch=sm_80` probes reject both BF16 cohorts with `Feature 'ex2.bf16' requires .target sm_90 or higher`, confirming the SM boundaries independently of the frontend tests. Ten further probes pair each below-floor `.version` against its floor: `.version 1.3` is rejected with `Feature '.approx' requires PTX ISA .version 1.4 or later`, `.version 6.5` with `Feature 'tanh' requires PTX ISA .version 7.0 or later` and `Feature 'ex2.f16/f16x2' requires PTX ISA .version 7.0 or later`, and `.version 7.5` with `Feature 'ex2.bf16' requires PTX ISA .version 7.8 or later`. The 7.0 and 7.8 floors are confirmed in both directions, since `.version 7.0` / `sm_75` and `.version 7.8` / `sm_90` accept. This matrix could not exercise the accept side of the 1.4 floor: this `ptxas` selects targets only from `sm_75` upward, and `.version 1.4` rejects `.target sm_75` with `PTX .version 1.4 does not support .target sm_75`. That is a toolchain version/target coupling rather than a frontend contract, so the 1.4 accept side rests on the frontend checker tests. These probes establish syntax and contract evidence only, not old-GPU code generation.

[Floating MIN/MAX](#min-max-coverage) is now modelled separately. [Floating and mixed ADD/SUB](#add-sub-coverage) reconciles the remaining §9.7.3–§9.7.5 cohorts, and gains no execution semantics here.


<a id="unary-float-coverage"></a>

## Floating reciprocal and square-root coverage

Technical reference and evidence: [unary_float_coverage.md](us-en/converage/unary_float_coverage.md).

<a id="unary-float-coverage--floating-reciprocal-and-square-root-coverage"></a>

### Floating reciprocal and square-root coverage

PTX 1.0–1.3 additionally accepts `rcp.f32`, `sqrt.f32`, and `rsqrt.f32` as approximate+FTZ, `rcp.f64` and `sqrt.f64` as round-to-nearest-even without FTZ, and `rsqrt.f64` as approximate without FTZ. FP64 needs SM >=13; `rcp.f64` and `sqrt.f64` also permit exact generic SM10/11/12 plus `map_f64_to_f32`. Native SM13 source headers require PTX >=1.2; mapped SM10/11 permit PTX1.0 and SM12 requires PTX1.2. Bare forms reject from PTX1.4. `RcpImplicitF64` is distinct from explicit `RcpRnF64`, exposes the static RN interpretation, and adds no written `.rn`, `.approx`, or `.ftz` control. The historical CUDA 13.3.33 assembler disagreement remains evidence, not a source-acceptance rule. Source omission and typed interpretation are separate; see [the public contract](#resolved-ir-design--historical-omitted-floating-controls).

The generated model covers the explicit forms of [reciprocal](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rcp), [square root](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sqrt), and [reciprocal square root](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rsqrt), including the special [RCP](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rcp-approx-ftz-f64) and [RSQRT](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rsqrt-approx-ftz-f64) FP64 approximate-FTZ instructions. Approximate and rounded modes are separate typed variants; the frontend resolves and validates them but does not execute numerical operations.

| Form | Minimum | Contract |
| --- | --- | --- |
| `rcp.approx{.ftz}.f32`, `sqrt.approx{.ftz}.f32`, `rsqrt.approx{.ftz}.f32` | PTX 1.4 / all target architectures | Fixed `.approx`; FP32 `.ftz` is optional. |
| `rcp.rnd{.ftz}.f32`, `sqrt.rnd{.ftz}.f32` | PTX 2.0 / SM 20 | Required typed `rn`, `rz`, `rm`, or `rp`; FP32 `.ftz` is optional. |
| `rcp.rn.f64`, `sqrt.rn.f64` | PTX 1.4 / SM >=13, or exact generic SM10/11/12 with `map_f64_to_f32` | Fixed typed round-to-nearest; no `.ftz`. |
| `rcp.{rz,rm,rp}.f64`, `sqrt.{rz,rm,rp}.f64` | PTX 2.0 / SM 20 | Required directed rounding; no `.ftz`. |
| `rsqrt.approx.f64` | PTX 1.4 / SM 13 | Fixed `.approx`; no `.ftz`. |
| `rcp.approx.ftz.f64` | PTX 2.1 / SM 20 | Fixed `.approx` and required typed `.ftz`. |
| `rsqrt.approx.ftz.f64` | PTX 4.0 / SM 20 | Fixed `.approx` and required typed `.ftz`. |

Each source accepts a floating literal or same-width native floating/bit register container, and the destination accepts the matching native/bit container. Integer literals and integer containers, wrong widths, sinks, omitted modes at PTX >=1.4, mixed approximate/rounding spellings, and all rounded FP64 `.ftz` spellings are rejected. Historical probes using CUDA 13.3.73 `ptxas` recorded permissive acceptance of `rcp.rn.ftz.f64`; the canonical PTX syntax excludes it, so the frontend rejects it.

Historical CUDA 13.3.73 evidence used `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`: 60 matrix modules yielded 39 expected accepts, 20 rejects, and the one documented permissive FP64-FTZ acceptance; four focused special-FP64 immediate/container probes all passed.

[Floating transcendentals](#transcendental-coverage) and [floating MIN/MAX](#min-max-coverage) are now modelled separately. [Floating and mixed ADD/SUB](#add-sub-coverage) completes the reconciliation; see that document for the ADD/SUB contracts.

The implemented mapped-FP64 availability alternative
([#287](https://github.com/endingly/ptx_frontend/issues/287)) is narrowly limited
to the explicit `div.rn.f64`, `rcp.rn.f64`, and `sqrt.rn.f64` forms at PTX >=1.4
on exact generic SM10/11/12 with `map_f64_to_f32`. Native availability remains
SM >=13 without requiring that option. Directed rounding, approximate-FTZ
forms, and other FP64 instruction families do not gain this alternative.


<a id="video-coverage"></a>

## PTX 9.3 video instruction coverage

Technical reference and evidence: [video_coverage.md](us-en/converage/video_coverage.md).

<a id="video-coverage--ptx-93-video-instruction-coverage"></a>

### PTX 9.3 video instruction coverage

The fixed [CUDA 13.3 / PTX 9.3 video clauses](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#video-instructions) define 23 opcodes, represented by 23 typed variants and 39 operand layouts. Canonical `video.yaml` owns their closed type/modifier and operand contracts; generated concrete classes expose `video_lanes` and `video_operation`. `VideoType::{S32,U32}` remains separate from ordinary integer arithmetic types.

| Family | Opcodes | Minimum PTX / SM |
| --- | --- | --- |
| Scalar | `vadd`, `vsub`, `vmad`, `vabsdiff`, `vmin`, `vmax`, `vshl`, `vshr`, `vset` | 2.0 / 20 |
| Two lanes | `vadd2`, `vsub2`, `vavrg2`, `vabsdiff2`, `vmin2`, `vmax2`, `vset2` | 3.0 / 30 |
| Four lanes | `vadd4`, `vsub4`, `vavrg4`, `vabsdiff4`, `vmin4`, `vmax4`, `vset4` | 3.0 / 30 |

No maximum target or removal gate is specified. Old minimum targets are frontend contract tests; current assembler support for those targets is a separate tool limitation. Modern SM80/100 forms remain accepted. Narrow `u8/s8/u16/s16` suffix aliases are rejected.

Every register carrier is scalar, general, 32-bit integer/bit storage. Scalar source roles also accept integer constants with existing 32-bit conversion and original 64-bit source provenance; SIMD source roles require registers. Float, vector, predicate, sink, special-register and symbol operands are rejected. Immediate selectors are unsupported.

An ordinary vector's explicit `.xyzw/.rgba` component is an effective scalar
carrier wherever the existing role accepts a plain 32-bit register, including
permitted `vmad` register minus. The component selector is not a VIDEO
`.b0/.h0` selection: its VIDEO selector remains absent. Merge destinations still
require their actual VIDEO selector; chained `V.x.b0`, hardware components and
unpermitted minus remain rejected. Owned and standalone checking retain the
component child's range separately from an outer minus and reject fabricated
component-plus-VIDEO-selector combinations. This frontend domain follows the
ISA scalar-carrier contract; bounded CUDA 13.3 V13.3.33 probes rejected tested
`vadd V.x` and `vmad -V.x` forms even with initialized inputs, while ordinary
`add V.x` and negated scalar `vmad` controls assembled. This is a compiler
compatibility difference, not evidence of GPU behavior or complete assembler
parity.

`ResolvedVideoOperand` owns its located register-or-immediate value, optional typed selector, and register-negation flag/minus range. Scalar byte selectors are `b0..b3`, halfword selectors `h0..h1`. Scalar three-operand forms have an unselected destination. Four-operand secondary forms use `.add/.min/.max`; merge forms require a selected destination and no secondary suffix. Scalar saturation may combine with secondary operations. Shift instructions require `.clamp/.wrap` after optional `.sat` and use unsigned `btype`. `vset` has two source types and comparison only, with unsigned destination/C interpretation.

Packed source swizzles index the concatenated A+B carriers: two-lane digits range `0..3`, four-lane digits `0..7`. Repetition is legal for sources. Arrays retain written high-to-low digit order. Destination masks are nonempty descending subsets without repetition, including masked `.add` accumulation. Default selectors are A=`h10`/`b3210`, B=`h32`/`b7654`, destination=`h10`/`b3210`. `video_effective_selector` returns these typed defaults while preserving omission in the owned operand. SIMD saturation and `.add` cannot coexist; `vset2/4` has no saturation.

`vmad` owns each written source minus, optional `.po`, saturation and `.shr7/.shr15`. A/B may have scalar selectors; D/C are full carriers. Product minus is A-minus XOR B-minus; effective product minus cannot coexist with C-minus. `.po` forbids every written register minus, including a cancelling pair. Negative numeric constants are values and never feed these controls. C constants use B32 carrier conversion. `video_mad_interpretation` derives product/input-C/final signs from source types and register controls without executing arithmetic or changing the independent written dtype.

Two manual inconsistencies are resolved explicitly: `vset2/4` preserved lanes follow the syntax and pseudocode's supplied C, although nearby prose says B; `vmad` input-C interpretation follows the Description's intermediate sign, while its pseudocode's internal c128 extension uses final sign. The public sign query describes arithmetic interpretation rather than that internal extension action. Noncanonical `.max` SIMD comparison examples, duplicate/reversed destination masks and `varvg` spellings remain rejected.

The installed [video consumer](../examples/video_consumer/main.cpp) demonstrates AST-free ownership, selectors/defaults, register-minus versus literal-minus, derived sign and mutable-IR rejection. Frontend tests cover every opcode, legal type/control combinations, target boundaries, source binding/provenance and malformed mutations. CUDA 13.3.73 `ptxas` static probes accept all 23 representative rich forms and reject SIMD saturation-plus-add and saturated SIMD comparison. These checks establish frontend/static assembler legality; arithmetic execution and GPU observations are outside this frontend contract.


<a id="warp-sync-coverage"></a>

## PTX 9.3 shuffle and vote coverage

Technical reference and evidence: [warp_sync_coverage.md](us-en/converage/warp_sync_coverage.md).

<a id="warp-sync-coverage--ptx-93-shuffle-and-vote-coverage"></a>

### PTX 9.3 shuffle and vote coverage

The frontend models the documented `shfl.sync`, `vote.sync`, and the legacy shuffle and vote
forms below in PTX ISA 9.3 §9.7.9.6 and §9.7.14.10. It parses and retains the selected operation,
operand layout, and source-predicate negation, then checks local operand types
and target availability. It does not execute warp operations or prove runtime
member participation and convergence.

| Instruction | Supported forms | Destination | Source and mask | Availability |
| --- | --- | --- | --- | --- |
| `shfl.sync` | `.up.b32`, `.down.b32`, `.bfly.b32`, `.idx.b32` | 32-bit-compatible register `d`, optionally paired as `d|p` with a `.pred` register | 32-bit-compatible register `a`; register or immediate `.u32` controls `b`, `c`, and `membermask` | PTX 6.0 / SM 30 |
| `shfl` | `.up.b32`, `.down.b32`, `.bfly.b32`, `.idx.b32` | 32-bit-compatible register `d`, optionally paired as `d|p` with a `.pred` register | 32-bit-compatible register `a`; register or immediate `.u32` controls `b` and `c`; no membermask | PTX >=3.0, SM >=30, and (PTX <6.4 or SM <70) |
| `vote.sync` | `.all.pred`, `.any.pred`, `.uni.pred` | `.pred` register `d` | `.pred` register `a` or `!a`; register or immediate `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote.sync` | `.ballot.b32` | 32-bit-compatible register `d` | `.pred` register `a` or `!a`; register or immediate `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote` | `.all.pred`, `.any.pred`, `.uni.pred` | `.pred` register `d` | `.pred` register `a` or `!a`; no membermask | PTX 1.2 / exact generic `sm_12`, `sm_13`, `sm_20`, `sm_30`, `sm_32`, `sm_35`, `sm_37`, `sm_50`, `sm_52`, `sm_53`, `sm_60`, `sm_61`, `sm_62` |
| `vote` | `.ballot.b32` | 32-bit-compatible register `d` | `.pred` register `a` or `!a`; no membermask | PTX 1.2 / exact generic `sm_20`, `sm_30`, `sm_32`, `sm_35`, `sm_37`, `sm_50`, `sm_52`, `sm_53`, `sm_60`, `sm_61`, `sm_62` |

The shuffle predicate output is optional. A paired output requires both
registers; `_` is not accepted in either half, and negating the output predicate
is invalid. The vote source may be negated in every supported mode, while the
destination must be a register of the form's type. Synchronized forms require
the member mask; legacy shuffle and vote forms reject an extra mask and have no `.sync`.
The distinct public shuffle forms `ShflUpB32`, `ShflDownB32`, `ShflBflyB32`, and
`ShflIdxB32` retain the selected bare or paired destination, data source, b/c
controls, and owned source ranges. Their optional `dst_register` and
`dst_shfl_destination` fields preserve which destination layout was written;
paired data and predicate results use the existing typed destination path.
No sync or membermask field is synthesized. Legacy shuffle requires PTX >=3.0
and SM >=30, with PTX <6.4 or SM <70. Source targets must still be recognized
catalog profiles with legal introduction versions; uncatalogued profiles such
as `sm_63` remain unsupported. Deprecation from PTX6.0 does not reject the form.
The four distinct public forms `VoteAllPred`, `VoteAnyPred`, `VoteUniPred`, and
`VoteBallotB32` retain predicate negation and owned source ranges without
fabricating sync or mask fields. Their availability is limited to the exact
supported catalog profiles listed above, not all historical GPU profiles. They
reject `sm_70` and newer targets, including the historical PTX < 6.4 / `sm_70`
case outside this supported scope.

Legacy SHFL is deprecated from PTX6.0, which does not reject it. Removal from PTX6.4 applies only at SM70 or newer: PTX6.3/sm75 remains accepted, PTX6.4/sm75 rejects, and PTX9.3/sm62 remains accepted. Source profile introduction rules still apply; numeric SM69 is a checker unit boundary, not a source target. The four modes and two destination layouts keep their existing carrier, control and sink rules; synchronized forms and the separate vote target lists are unchanged.


<a id="wgmma-coverage"></a>

## PTX 9.3 WGMMA coverage

Technical reference and evidence: [wgmma_coverage.md](us-en/converage/wgmma_coverage.md).

<a id="wgmma-coverage--ptx-93-wgmma-coverage"></a>

### PTX 9.3 WGMMA coverage

The frontend models the five WGMMA spellings as 2,151 exact logical forms: 1,092 dense `wgmma.mma_async`, 1,056 sparse `wgmma.mma_async.sp`, and one form each for `wgmma.fence`, `wgmma.commit_group`, and `wgmma.wait_group`. The canonical source is the packaged `asynchronous_warpgroup_matrix_multiply_accumulate.yaml` resource. All forms require the `sm_90a` target family. Dense MMA and controls start at PTX 8.0, sparse MMA at PTX 8.2, and mixed `.s8.u8` / `.u8.s8` forms at PTX 8.4. A generic `sm_90` target does not satisfy the `sm_90a` requirement.

Dense forms cover `.f16`, `.bf16`, `.tf32`, FP8 `.e4m3`/`.e5m2`, integer `.s8`/`.u8` with optional `.satfinite`, and `.b1.and.popc`. Sparse forms cover the corresponding floating and integer types with one `.b32` metadata register and a type-specific selector: 0 or 1 for `.f16`/`.bf16` and `.tf32`, only 0 for FP8 and integer. Floating A/B immediate scales must each be exactly -1 or 1; the `.f16`/`.bf16` descriptor forms also expose legal transpose controls. The A source is either a 64-bit opaque shared-memory descriptor or a four-register fragment; B is always a shared-memory descriptor. The D register fragment is read/write. Its cardinality is shape and accumulator-type dependent, reaching 64 registers for packed `.f16` and 128 for `.f32`/`.s32` at N=256. Ordinary vector and selector operand limits remain separate.

`MatrixInstructionDescriptor` retains M/N/K, element and register types, row/column placement, exact fragment counts, A source placement, sparse metadata kind, and a `WarpGroup128` participation identity. `ResolvedSharedMatrixDescriptor` keeps the bound logical 64-bit B64/U64/S64 scalar General carrier opaque: its runtime address, alignment, swizzle, layout, and cross-warp uniformity are obligations for the producer and consumer. Each WGMMA semantic form is a final `Instruction` class with direct typed operands and immutable `matrix_topology`. MMA classes return that topology through `matrix_descriptor()`; protocol-control classes have no matrix descriptor. `Instruction::check()` verifies mutable operands, selected operand layout and target availability. The independent `WgmmaGroup` completion identity and issue/register-fence/commit/wait actions expose protocol obligations; the frontend does not prove dynamic warpgroup convergence, sequence ordering, completion before register reads, or GPU results.

`wgmma.wait_group` takes a nonnegative integer constant in a `.u64` owned immediate. PTX 9.3 states no numerical maximum, so the frontend does not impose an undocumented 0..7 or 32-bit cap. Its source sign and converted bits remain available for checker consistency. CUDA 13.3 `ptxas` accepts a complete module with wait count 2^32, while it rejects `-1`; assembler acceptance is sampled evidence rather than a new ISA bound.

Adopted engineering interpretation: the dedicated WGMMA shape table and integer fragment contract take precedence over the incomplete dense-integer Syntax enumeration ending at N=224 in this instance. The integer N grid is `8,16,24,32,48,64,...,256` (step 16 after 32); N=240/256 use 120/128 scalar S32 D registers. Both A placements, all S8/U8 pairs, and plain/satfinite forms remain included; integer N=232/248 are off-grid and N=264 is outside the range. See the [reference interpretation](#matrix-reference-interpretations--dense-integer-wgmma-n-boundary) for the bounded rationale and independent target/count controls.

Code generation keeps all 2,151 exact class identities and emits bounded 64-form shards. Public catalogue getters return spans backed by static storage, independent of source AST lifetime. [The installed matrix consumer](../examples/matrix_consumer/README.md) exercises WGMMA forms across a shard boundary and the control/MMA descriptor distinction.

Run the C++ and Python checks separately; the configured CTest preset excludes `python_test`:

```sh
PYTHONPATH="$PWD/python/src" ctest --preset ci-linux-clang-debug --parallel 1 --output-on-failure
PYTHONPATH="$PWD/python/src" ctest --test-dir out/build/ci-linux-clang-debug -C Debug -R '^python_test$' --parallel 1 --output-on-failure
```

Reference: [NVIDIA PTX ISA 9.3, asynchronous warpgroup-level matrix instructions](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-instructions).

The [descriptor-carrier interpretation](#matrix-reference-interpretations--logical-descriptor-carriers-and-fundamental-bit-data) distinguishes exact-width B/U/S descriptor roles from ordinary `SameWidth` bit data: F64/F32 are rejected for those roles, while `mov.b64`/`mov.b32` bit copies remain valid. TCGEN `idesc` is a 32-bit instruction descriptor, not TMA im2col. Lane-mask F32 and actual `.b64` mbarrier arrival-state data retain separate policies; runtime descriptor contents remain unproved.


<a id="wgmma-coverage--scalar-components-in-shared-matrix-descriptor-carriers"></a>

### Scalar components in shared matrix descriptor carriers

The existing `SharedMatrixDescriptor` source kind admits either an identifier or an explicit ordinary vector member. A legal `.reg .v2 .b64 A` declaration occupies128bits; `A.x`/`A.y` (or `.r`/`.g`) selects one scalar64-bit descriptor-register carrier. This is component extraction, not a whole-vector descriptor or register-A fragment. The same kind rule covers the existing dense/sparse shared A/B and register-A B descriptor roles; it is not an opcode- or A-position special case. No YAML type/domain or public payload/API changes are involved.

Binding still requires the real General declaration, its physical integer/Bit64 type where already allowed, actual SymbolId and family index, in-range explicit selector, base/leaf/outer ranges and effective scalar width. Known F64/otherFloat, wrong width/class or InstructionOnly declarations are not converted into Bit words. `.v4.b64` exceeds the ordinary128-bit declaration limit. `A<2>` declares suffixed variables rather than bareA: scalarA1 and vector-familyA1.x are distinct explicit cases, with no implicit first element/index. Source components require existing declaration context; no standalone width/SymbolId is guessed.

The syntax overlap between a shared scalar descriptor (`IdentifierRef|VectorMember`) and a whole named matrix fragment (`IdentifierRef|VectorPack`) remains only IdentifierRef. Public syntax-only ambiguity, the separately established for-all layout-pair certificate and private real-declaration selection remain independent. A wrong whole vector or32-bit component cannot fall through and be rescued as a scalar64 descriptor. Other scalar/member mappings, fragment member permissions, partial-context priorities and target policies are unchanged.

Current owned validation rejoins actual component facts and declarations; valid coherent retargets are data edits, while contradictory facts fail even beside healthy supplied syntax. Saved source identity independently rejects a genuinely different legal operand AST. Existing descriptor views/collectors and synchronous observers carry the actual RegRef/ranges through source destruction, copies/assignments/moves/function copies and instruction clones, with fresh borrows per owner. This source-admission contract does not establish runtime descriptor bits, shared addressing, synchronization or matrix arithmetic.

Current module validation also checks the full selected register owner against the existing 128-bit declaration ceiling before availability-context skips. A coherent 256-bit current owner fails with a capacity-specific diagnostic at the component range through both module validation APIs; missing target context cannot hide that known failure. The existing actual SymbolTable join still rejects contradictory or missing cached facts. This adds no standalone instruction-check guarantee.


<a id="declaration-semantics-design"></a>

## Declaration Semantics Design

Technical reference and evidence: [declaration_semantics_design.md](us-en/declaration_semantics_design.md).

<a id="declaration-semantics-design--shared-numeric-evaluation"></a>

### Shared numeric evaluation

`numericConstantValue()` evaluates an owned source tree to either integer
64-bit bits plus signedness or an f64 value. Its located error distinguishes an
invalid expression from a valid symbolic/deferred expression with no numeric
value. Instruction uses reject either error. The optional
`constantIntegerValue()` and `constantArrayExtent()` helpers remain compatible.
Mixed integer/floating arithmetic and casts are invalid. Division by integer
or floating zero, including negative zero, is invalid. Both conditional arms
and logical operands are checked even when their values are unselected.
Floating overflow is not subject to an additional blanket finite-only rule.

Decimal floating constants accept a decimal point with digits on either side,
including `1.`, `.5`, and `1.e2`, or a digit sequence with an exponent such as
`1e2`. Exponents use `e`/`E` and an optional sign followed by digits. They have
no size suffix and decode in binary64 before the existing destination
conversion to `.f32` or `.f64`. Integer destinations remain incompatible.
This follows [PTX ISA 9.3 floating-point
constants](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-constants).
The shared lexical float category does not broaden `.version`: its syntax still
requires decimal digits on both sides of the point in `major.minor`.

Binary `&`, `|`, and `^` retain the usual arithmetic conversions (two signed
inputs give a signed result; any unsigned input gives an unsigned result).
The detailed PTX 9.3 §4.5.5 binary-operator rule takes precedence over only the
conflicting `&`/`|`/`^` row in Table 5. This is the frontend's adopted
interpretation, not a NVIDIA-issued erratum. Historical assembler observations
and their exact tool versions are recorded in the
[bitwise policy](#bitwise-constant-policy). Expression results retain their
64-bit bits and signedness until use conversion; destination types and normalized
source-sign caches do not retroactively supply an expression type. Unary `~` gives unsigned, while remainder
interprets both inputs unsigned and produces signed. Constant-expression shifts
evaluate the count as a 64-bit value and interpret
its bits as unsigned, accepting exactly 0–63. Thus `1 << (-1 + 2)` is legal,
while evaluated counts `-1`, `64`, `65`, and `UINT64_MAX` are rejected rather
than masked or clamped. The result retains the left operand's type even when
the count is unsigned: signed negative right shifts fill with the sign bit,
unsigned right shifts fill with zero, and count zero preserves the input bits.

This 0–63 boundary is an adopted frontend expression-domain policy. The manual's
[constant-expression rules](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constant-expression-evaluation)
do not assign a result to counts of 64 or more. The policy is not a vendor
blanket error requirement or the execution behavior of PTX `shl`/`shr`
instructions. Concrete count failures remain non-deferred at the failing count
subtree, including invalid shifts in unselected conditional or logical children.
A valid symbolic relocation such as `symbol + 1` remains deferred where its
value is unknown; `symbol << 1` and `symbol >> 1` are invalid relocation
operations, rather than deferred numeric shifts. Initializer
and array-dimension declaration wrappers retain their existing whole-expression
invalid-initializer/invalid-dimension diagnostics. The common evaluator called
on that same AST expression still supplies the concrete count-subtree error;
MOV, call and address consumers propagate that count error directly.

Accepted normalized immediate and storage payloads survive source/AST destruction,
copies, assignments and instruction clones under the existing owned-IR checks.
Rejected shifts never produce such a payload; owned count-tree revalidation is
inapplicable, and no count-origin cache is introduced. Array extent, destination
type and address-range restrictions remain separate use-site constraints.

Module `.reg` and `.local` declarations are checked against the source-target
ABI assumption: PTX ISA 3.0 or later with a recognized active source target of
SM 20 or later requires these variables to be declared within a function.
The frontend does not model a caller-selected switch that disables ABI compilation.
Earlier PTX versions and recognized SM 1.x targets retain their historical
module placement. Target changes apply in source order; a later unknown or empty
target clears the previous placement context.

Declaration semantics checks every actual module occurrence, including unused
variables and conflicting redeclarations, and reports `InvalidDeclarationPlacement`
at the complete declaration range. Owned validation checks every variable identity
and retained occurrence against its source region, including registers with no
storage record, and reports `RuleViolation`. Both validation overloads recheck
placement after source/AST destruction or replacement source recontextualization.
`AvailableContext` defers this rule when either the version or recognized active
target is absent; strict validation reports `MissingValidationContext` at the
affected declaration. Other header and identity diagnostics remain independent.
Function-local declarations, nested blocks and valid register formals retain
their existing contracts. See the pinned PTX 9.3
[changes from PTX 1.x](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#changes-from-ptx-isa-version-1-x),
[register](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#register-state-space),
[local](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#local-state-space),
and [variable declaration](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#variable-declarations) rules.


<a id="declaration-semantics-design--arrays-and-initializers"></a>

### Arrays and initializers

Register declarations cannot use bracket array dimensions. The rule applies to
unused declarations and reports `InvalidRegisterDeclarationShape` at the first
bracket dimension, independently of type or vector diagnostics. Compact register
groups (`r<2>`) remain distinct. See [register declaration compatibility](#register-type-policy)
for the retained owned shape and validation contract.

Integer literals share decimal, leading-zero octal, binary, and hexadecimal decoding
with instruction immediates. For example, `010` initializes the value 8 and
denotes an array extent of 8; an unsigned suffix does not change the radix.

An integer literal that cannot be decoded in the 64-bit source domain is an
invalid expression, not a deferred value. Declaration checking reports
`InvalidIntegerLiteral` at the literal's own range, and `resolveModule()`
preserves that category and location. Folding cannot hide this failure, even
when a conditional has identical branches or the literal is in an unselected
branch. Valid deferred expressions remain distinct: equal-branch folding of
a valid but unevaluated comparison can still produce a constant.

An array dimension must evaluate to a positive integer constant. The evaluator
retains a 64-bit bit pattern plus `.s64`/`.u64` signedness for every integer
subexpression. It therefore supports negative intermediate values, casts,
usual arithmetic conversions, and unary, binary, and conditional operations
without rejecting expressions such as `-1 + 2`. `WARP_SZ` is evaluated here as
well; a symbol address is not an array extent. Only the first dimension may be
omitted: an initializer can infer it from its outermost list, while an external
storage declaration may leave its first dimension unknown without an initializer.

Initializer brace nesting must match the array rank. A vector declaration adds
an innermost aggregate extent of two or four. A list may contain fewer elements
than its declared extent because PTX zero-initializes the remainder; only an
overflowing list is diagnosed.

Scalar leaves distinguish integer, floating, and symbol-address expressions.
Integer and floating expressions must match their destination type category,
while an address may initialize only `.u32` or `.u64`. An initializer symbol
must name a function or a non-opaque `.global`/`.const` variable. `.texref`,
`.samplerref`, and `.surfref` identities cannot become initializer addresses,
including through `generic()`, byte-mask, or arithmetic wrappers. `generic()`
and the mask form are treated as initializer operators rather than ordinary
calls; this restriction does not affect the permitted `mov` retrieval of an
opaque handle.


<a id="declaration-semantics-design--unified-uuid-attributes"></a>

### Unified UUID attributes

For modeled `.unified(upper, lower)` attributes, declaration semantics decodes
both direct integer tokens as exact unsigned 64-bit UUID halves. Decimal,
leading-zero octal, binary, hexadecimal, and the supported unsigned suffix share the
integer-literal rules; overflow diagnoses the offending token rather than
truncating it. This applies uniformly to eligible global variables and device
function definitions or prototypes before storage metadata or a future function
metadata backend consumes the value. Placement, PTX-version, and target checks
remain separate declaration rules.

Recursion legality is a module-validation check over current typed calls after
binding, visibility, signature and table joins. `resolveModuleOnly` remains a
transport operation; its success alone does not validate recursion support.
PTX 2.0 / SM 20 is required for identified direct or finite-candidate cycles;
open calls are not certified acyclic. See [control flow](#control-flow-syntax-design).


<a id="declaration-semantics-design--redeclarations"></a>

### Redeclarations

Binding first maps same-name module items to a stable `SymbolId`; this pass then
decides whether the redeclaration is legal:

- matching `.extern` variable declarations may repeat;
- matching `.func` prototypes may be combined with at most one definition;
- variable state space, alignment, vector/base type, parameterized count, and
  array shape must be compatible;
- function kind, `.noreturn`, return/input interface, and linkage must be
  compatible;
- symbol-kind conflicts, linkage conflicts, signature changes, and multiple
  definitions report diagnostics carrying the previous source range;
- an `.extern .func` must be a prototype and cannot have a body.

`.weak` requires PTX 3.1 on all targets. The source pass checks each typed
variable/function occurrence at its weak qualifier, including unused prototypes.
Binding retains the first canonical linkage; compatible redeclarations require
identical linkage and signatures before successful module ownership handoff.
Mixed weak/nonweak declarations remain incompatible. A binding-only table from
rejected mixed source does not retain every historical qualifier; no additional
occurrence-history API or weak linker behavior is implied.

Redeclaration alignment compares its effective value while retaining source
provenance and ranges on every declaration occurrence. For modeled fundamental
storage, an omitted alignment is the scalar byte size (also for scalar arrays)
or the complete `.v2`/`.v4` element width (also for arrays of vectors). An
explicit valid alignment compares by decoded integer value, so equivalent octal
and decimal spellings match. Unsupported layouts and invalid alignment syntax
do not receive an inferred default. Parameterized counts and ABI-preserve
counts likewise compare by decoded integer value.

Verification note: CUDA 13.1 `ptxas` V13.1.115 accepted both declaration orders
for `.version 8.0` / `.target sm_80` / `.address_size 64` fixtures containing
matching implicit and explicit scalar, scalar-array, byte-array, `.v2 .u32`,
and `.v4 .u32` external declarations. For the `g` scalar fixture, whole-program
compilation emitted exactly `ptxas warning : Unresolved extern variable 'g' in
whole program compilation, ignoring extern qualifier`; the comparison is
therefore successful with that warning, not warning-free. The same assembler
rejected `.align 3` with `Alignment must be a power of two`.

Every function prototype and definition still owns a lexical scope. The
function symbol's `owned_scope` prefers the definition scope, so module
resolution uses the parameters and locals belonging to the body it resolves.


<a id="declaration-semantics-design--control-flow-metadata"></a>

### Control-flow metadata

The same pass checks function-local indirect-control metadata. A
`.calltargets` member must be a previously declared device `.func`, duplicate
members are rejected with both member ranges, and every valid member must have
the same canonical `FunctionSignature`. `.branchtargets` members must be labels
in the owning function; forward labels are valid. Compact entries such as
`N<5>` are checked against the existing local labels without creating synthetic
symbols, and report the compact-entry range for missing labels. The branch
table is an ordered index sequence: repeated explicit destinations and overlaps
with or between compact entries are valid and are not deduplicated.

`.callprototype` rejects `.noreturn` with return parameters, applies existing
alignment/array-extent checks to its formals, and requires an array formal to
use `.param`. Binding remains the owner of duplicate declaration labels.
Module resolution converts a valid prototype to the same canonical signature
as a function and reuses the validated first `.calltargets` member signature
for indirect-call ABI checking. Modeled ABI suffix availability is checked by
module validation; the [current limits](us-en/converage/coverage_status.md) still apply.

Call-table eligibility is checked only when ordinary storage is consumed as an
indirect-call flist. The module layer reuses owned sparse storage relocations and
this pass's complete `FunctionSignature` equality, preserving the exact-token
prior-function rule. Repeated table targets and implicit zero tails are permitted;
this differs deliberately from `.calltargets` duplicate policy. Unconsumed mixed
function/numeric or mixed-signature data remains ordinary legal storage.


<a id="declaration-semantics-design--parameter-declarations"></a>

### Parameter declarations

Parameters have a context-aware [coverage and validation contract](#parameter-declarations),
including supported types, unsized-array placement, pointer attributes, and
statically decidable ISA version/target and entry-size limits. This applies to
entry/device headers, call-prototype formals, and body-local `.param`
declarations; retained type spelling or array syntax alone does not establish validity.

Parameter element classification reuses fundamental metadata, excludes predicates,
and includes native scalar F16x2 without flattening its packed components. Validated vector
lanes participate in checked object bytes, natural alignment and padded entry totals;
unused prototype signatures receive the same shape, alignment and role checks.


<a id="declaration-semantics-design--entry-resource-constraints"></a>

### Entry resource constraints

For the supported entry-header `.maxnreg`, `.maxntid`, `.reqntid`, and
`.minnctapersm` directives, this pass compares a declared module `.version`
with their PTX minima (1.3, 1.3, 2.1, and 2.0 respectively). All four are
supported on every SM, so `.target` does not add a check here. `.reqntid` and `.maxntid`
are mutually exclusive within one entry and diagnose the later directive with
the earlier range as context. A lone `.minnctapersm` is a PTX warning rather
than an error; warning severity, backend resource feasibility, and numerical
limits remain outside this pass.


<a id="declaration-semantics-design--debug-metadata-boundary"></a>

### Debug metadata boundary

Binding owns the `.file`/`.loc` and `.debug_str` identity table: every duplicate
normalized file index diagnoses with current/previous ranges, `.loc` file references must bind, and
`function_name` must identify `.debug_str` itself or one of its raw labels.
This declaration pass intentionally adds no DWARF payload-expression, source
attachment, or resource-feasibility semantics.


<a id="declaration-semantics-design--current-boundary"></a>

### Current boundary

After declaration checking, module resolution projects the supported storage
forms into [owned storage metadata](#storage-declarations), including checked
byte extents and typed initializer values/references. This additional
normalization can diagnose a form outside its representable domain even when
the declaration checker alone accepts its expression category. Declaration
categories and related ranges survive through `ResolveDiagnostic`.

This pass does not perform opcode-specific instruction type checking or
link-time selection across modules. Integer constant-expression handling
covers the current AST grammar and propagates PTX `.s64`/`.u64` types; new
constant operators must extend classification, signedness propagation, and
evaluation together.

Binary bitwise operators follow the detailed PTX usual-conversion rule; see
[the signedness decision and reproducible assembler evidence](#bitwise-constant-policy)
for the conflicting summary-table wording and tool-version limitations.

`FunctionParameterContract::array_extents` is the sole normalized formal-axis
representation. Valid arrays compare by checked total element count for ABI
compatibility, independently of rank; invalid structural axes and overflow remain
invalid and retain their axis identity. Unknown outer dimensions preserve checked
known inner axes. Derived `CallArgumentProperties::array_size` is a total count,
not a cached first axis. Exact source identity remains separate; see
[parameter contracts and member migration](#parameter-declarations).

Register-state device and call-prototype formals have a separate default-ABI
minimum: when known PTX >=2.0 and the active recognized target is SM20+, the whole
scalar/vector value must be at least32bits. This measures explicit vector total
width, not each lane, and preserves source type/shape. It applies to unused input
and return formals too, with the supplied AST context for source-aware retargeting
and owned regions after AST death. Known legacy contexts disable the minimum;
missing context defers it under partial checking. Ordinary small/predicate registers
and compatible small actuals to `.param` formals remain legal. See
[parameter declarations](#parameter-declarations) for the complete boundary.

Instruction-root spellings remain ordinary identifiers in explicit declaration
name roles under the [contextual keyword interpretation](#lexer-design).
The official `bar` object/function examples preclude a blanket name exclusion;
this is a normative interpretation and regression clarification, not an added
missing-predicate fix. Source validation checks name leaves and AST-free owned
validation checks actual bound symbol names with the shared character grammar.
Diagnostic reference spellings do not reconstruct authoritative identity. All
independent declaration-role, type, ABI and prior-visibility restrictions remain.

The public `declaration_semantics::languageCode(std::string_view)` in
`ptx_declaration_semantics.hpp` synchronously borrows exactly one original operand
token and returns `optional<uint64_t>` without retaining the view. It is the one
decoder used by source item checks, ordered redeclaration compatibility and owned
function integrity. Quoted names `unknown`, `ptx`, `nvvm`, `cuda c++`, `cuda c++ tile`,
`tile ir`, `python-cutile`, `fortran`, `optix` map to 0,3,4,5,6,7,8,9,10 and compare
with deterministic ASCII case folding. Complete unsigned integer tokens 0..10
(including Reserved 1/2) accept existing radices and U suffixes. There is no trim,
expression evaluation, quoted-number coercion or unquoted-name alias. Outer
quotes are removed but inner escape bytes are not decoded into new name aliases;
this preserves the frontend decoder boundary, not a general PTX escape ban.
Omitted directive/default `ptx` handling belongs to the caller; an empty token
returns no code. Raw list order/quotes/case remain owned data, not a decoded cache.


<a id="declaration-semantics-design--historical-entry-body-inputs"></a>

### Historical entry body inputs

PTX 1.0–1.3 entry body `.param` declarations have the owning `LegacyEntryInput`
role in the sole `parameter_declarations` table, including scoped/group/array
identities. The function signature remains syntactic header ABI only, so an empty
header signature does not imply no host inputs. Lexical metadata order assigns
no launch addresses or packing. Resolution needs a version only for ambiguous
entry body roles; it reports `MissingValidationContext` without guessing from SM.
Typed modules retain their role when context is missing.

The optional `ResolvedSymbolRef::parameter_role` cache is derived from the actual
record while keeping structural `CallParameter`. Address/MOV/tensor projections
share input/return/call-slot directions. Both validators rejoin current records
and caches and enforce read-only entry-frame access, direct call-actual exclusion,
shape/scope and logical capacity before context skips. Legacy input LD is not
call-return staging; MOV preserves `.param`. Modern BodyLocal staging is unchanged.
The checked sum of per-member bytes times compact count has the known32764-byte
global ceiling; no double axes/vector multiplication or invented256 legacy cap.
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` remains qualified for exact historical
physical launch packing. See the [parameter contract](#parameter-declarations)
for context windows, intrinsic guarantees and preserved limitations.


<a id="declaration-semantics-design--deprecated-maximum-cta-hint"></a>

### Deprecated maximum-CTA hint

Entry headers accept `.maxnctapersm ncta` from PTX ISA 1.3 on every recognized
source target, including PTX 2.0 and later. Deprecation does not impose an upper
version bound. The distinct `MaxNctaPerSm` AST/owned kind is an optimization hint:
additional CTAs may still execute, so it is not a hard runtime occupancy cap.
`.maxntid` is optional, and `.minnctapersm` may coexist as a separate resource.

One complete unsigned integer token is checked before projection into the existing
`uint32_t` resource value, including zero and `UINT32_MAX`; overflowing values do
not wrap. This is the frontend's modeled resource capacity. The published grammar
does not specify this bit width, and validation does not prove hardware occupancy
or driver feasibility. CST/AST leaves retain written radix/suffix/ranges while
alive; owning contracts retain only kind, normalized values, explicit count and
range, with no parallel raw spelling field.

Both validators check current payload shape, entry placement and known range
containment. Known PTX context below 1.3 rejects even without a complete target;
missing version defers availability under `AvailableContext` while intrinsic
checks continue. The source-aware route uses the supplied effective version.
Coherent current numeric edits remain valid; changing written resource kind or
value in a replacement AST retains the existing source-association rule.

Source reference: [PTX ISA maximum-CTA directive](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#performance-tuning-directives-maxnctapersm).


<a id="lexer-design"></a>

## PTX Lexer Design

Technical reference and evidence: [lexer_design.md](us-en/lexer_design.md).

<a id="lexer-design--token-classification"></a>

### Token Classification




<a id="lexer-design--punctuation"></a>

### Punctuation

Individual punctuation characters have dedicated token kinds, including:

```text
, . : ; @ ( ) [ ] { } < > <= >= << >> - + * / % & && ^ | || ! ~ ? = == !=
```

A leading sign is not part of a numeric token. For example, `-1` produces
`Minus` followed by `Decimal`. Multi-character operators have dedicated token
kinds so the declaration constant-expression parser can retain exact
operators and precedence.


<a id="lexer-design--literals"></a>

### Literals

The lexer recognizes:

- Decimal and leading-zero octal integers, with an optional `u` or `U` suffix.
- Binary integers beginning with `0b` or `0B`, with an optional `u` or `U` suffix.
- Hexadecimal integers beginning with `0x` or `0X`, with an optional unsigned
  suffix.
- PTX bit-pattern floating literals `0f` plus eight hex digits.
- PTX bit-pattern double literals `0d` plus sixteen hex digits.
- Decimal floating literals with a decimal point or exponent, including `1.` and
  `1.e2`; a point must have digits on at least one side.
- Double-quoted strings containing escaped characters.
- The built-in spelling `WARP_SZ`.

Literal tokens preserve their exact source spelling. Numeric conversion and
string unescaping are intentionally outside the lexer.

For compatibility, both decimal and octal integers use `TokenKind::Decimal`
and lower to `AstImmediateKind::DecimalInteger`; these names do not determine
the numeric radix. Integer decoding treats a leading zero as octal (`010` is
8). An octal spelling containing `8` or `9`, such as `09U`, produces one
`Error` token spanning the whole literal. Decimal floating spellings such as
`09.0` and `09e1` remain floating tokens.

Binary integers use `TokenKind::Binary` and `AstImmediateKind::BinaryInteger`,
retaining their exact spelling and range. A malformed binary candidate such as
`0b`, `0b102U`, or `0b1UU` produces one `Error` token through its identifier tail.
Magnitude overflow is diagnosed during integer evaluation. Binary literals are
available in general integer expressions, dimensions, operands, debug metadata,
`.unified`, and `.language`. Bare integer fields also accept decimal, octal,
hexadecimal, and binary spellings with optional unsigned suffixes: declaration
and pointer alignments, parameterized counts, launch resources, ABI-preservation
counts, compact branch-target counts, file timestamps/sizes, and source-location
line/column values (including inline locations). Each field retains its existing
range, positivity, alignment, version, and target checks; these slots do not
accept signs, expressions, or `WARP_SZ`. Treating location line/column metadata
as general integers follows PTX 9.3 §§4.5.1 and 11.5.4; no decimal-only restriction
is stated there. `.address_size` retains its enumerated `32`/`64` source form,
version components retain their decimal grammar, and initializer masks remain
hex-only.


<a id="lexer-design--generic-identifiers"></a>

### Generic Identifiers

Ordinary identifiers use the following shape:

```text
[A-Za-z_$%][A-Za-z0-9_$]*
```

They are emitted as `TokenKind::Ident`. This category includes instruction
names, registers, special registers, labels, symbols, and target names such as
`sm_80`.

Percent is permitted only as the leading character of a percent-prefixed
identifier; it is not an identifier continuation character. Thus an internal
percent terminates the preceding identifier and begins a new leading-percent
identifier when followed by a valid continuation. A standalone `%` remains the
`TokenKind::Percent` remainder operator. The lexer also keeps `_` as an
`Ident` token for the designated sink and call-prototype-placeholder contexts;
those context-specific meanings are decided after lexing.

Keeping instruction names generic avoids changing the lexer whenever PTX adds
an instruction.


<a id="lexer-design--generic-dot-identifiers"></a>

### Generic Dot Identifiers

Most dot-prefixed spellings are emitted as `TokenKind::DotIdent`. The generic
form supports atoms joined by `::` and comma-separated atoms:

```text
.sat
.u32
.shared::cluster
.collector::b0::smem
.scale::2,1
```

Numeric-leading shapes such as `.16x64b` are also accepted.

Chained suffixes remain separate tokens. For example:

```text
add.sat.s32
```

is tokenized as:

```text
Ident("add") DotIdent(".sat") DotIdent(".s32")
```

Some PTX spellings contain an additional ordinary dot inside a single suffix.
Known cases, such as `.async.global` and `.b8x16.b6x16_p32`, have explicit rules
and remain a single `DotIdent` token.


<a id="lexer-design--dedicated-directive-tokens"></a>

### Dedicated Directive Tokens

A small set of stable, structurally important PTX directives has dedicated
token kinds. These include:

- Module and debug directives such as `.version`, `.target`, and `.file`.
- Visibility and linking directives such as `.visible` and `.extern`.
- Function directives such as `.entry` and `.func`, plus the structurally
  significant function-local `.callprototype`, `.calltargets`, and
  `.branchtargets` metadata directives.
- Kernel tuning directives such as `.maxnreg` and `.reqntid`.
- Declaration directives such as `.reg`, `.global`, and `.param`.

These rules appear before the generic `DOT_IDENT` rule. Flex selects the longest
match and uses rule order to resolve equal-length matches, so a dedicated
directive wins over the generic category.

A dedicated spelling keeps its token kind in every lexical context. For
example, `.global` is always `DotGlobal`, even when it appears in a sequence of
dot-prefixed names. Consumers that accept both categories should use the token
spelling where appropriate.


<a id="lexer-design--whitespace-and-comments"></a>

### Whitespace and Comments

Spaces, horizontal tabs (`\t`), ASCII form feeds (`\f`, byte `0x0c`), and
CR/LF line endings separate tokens and are retained as whitespace trivia. Form
feed advances one byte column without starting a new line. Trailing whitespace
is retained on the EOF token. Vertical tab (`0x0b`) and other unknown control
bytes remain lexical errors outside strings and comments; this supported subset
does not admit arbitrary ASCII controls. See the pinned
[PTX 9.3 source format](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#source-format).

Line comments begin with `//` and continue to, but do not consume, the line
ending as part of the comment rule. The following whitespace rule consumes the
line ending. A form feed inside a line comment does not terminate it; form
feeds inside strings and either comment form retain their exact content.

Block comments use an exclusive `BLOCK_COMMENT` scanner state:

1. `/*` enters the state.
2. All content is skipped until `*/`.
3. `*/` returns to the initial state.
4. End of input inside the state emits one `TokenKind::Error` and returns to
   the initial state so the next read reaches `TokenKind::Eof`.

Block comments are not nested.

Skipped text still passes through `YY_USER_ACTION`, so whitespace and comments
advance source positions even though they do not produce tokens.


<a id="lexer-design--error-behavior"></a>

### Error Behavior

An unknown character is emitted as `TokenKind::Error` with the offending byte
in `Token::text` and its normal source range.

An unterminated block comment emits one `TokenKind::Error` containing its full
text, source range, and preceding trivia. Subsequent reads return EOF with empty
text and no repeated trivia. Repeated `peek()` calls can observe the same cached
error until `consume()` advances past it.

The lexer reports errors as tokens rather than throwing exceptions. A caller
may stop at the first error or request additional tokens when recovery is
appropriate. An ordinary invalid character does not force EOF: following valid
tokens remain available. Module recovery relies on this progress contract.

EOF is emitted as `TokenKind::Eof`, with empty text and a zero-width current
position range.


<a id="lexer-design--known-limitations"></a>

### Known Limitations

- Token text is allocated separately for every emitted token.
- The lexer accepts ASCII identifiers only.
- Block comments cannot nest.
- String tokens preserve escapes and quotes; they are not decoded.
- Signs are separate from numeric literals.
- The decimal floating grammar does not recognize every spelling that a C++
  numeric conversion routine might accept.
- `yy_scan_bytes` accepts an `int` length, while the public constructor accepts
  `std::string_view::size_type`; extremely large inputs require an explicit size
  check before the narrowing conversion.
- The public object has no reset operation and cannot be moved.

These limitations should be changed only together with focused lexer tests that
define the new lexical contract.


<a id="lexer-design--contextual-instruction-keywords"></a>

### Contextual instruction keywords

For the archived PTX ISA 9.3, the frontend adopts a contextual reading of
instruction keywords. Section 4.3.2 calls instruction keywords reserved tokens,
while section 4.4 gives the ordinary identifier character grammar without an
explicit keyword exclusion. The concrete official examples declare
`.global .u32 bar[] = { 2, 3, 5 };` and reference `generic(bar)` in
[initializers](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#initializers).
The [function example](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#function-declarations-and-definitions)
also defines and directly calls a device function named `bar`.

The adopted reconciliation recognizes instruction roots in opcode positions and
applies the ordinary character grammar in explicit user-defined name positions.
It does not impose a context-free instruction-keyword name ban or a special
exception for `bar`. The reserved-token wording retains interpretive tension;
this is not a claim that the manual supplies a complete lexer grammar or that
every spelling fits every syntactic role. Existing type, state-space, ABI,
visibility, predefined-name and contextual-sink rules still apply.

PTX remains case-sensitive: `add` is an opcode in `add.u32`, while `add`, `Add`,
`ADD`, `_add`, `$add`, `%add`, `add0`, `add_` and `add$` can be ordinary explicit
names. No prefix stripping or case folding is used. `ADD.u32` does not become
`add.u32`; a dot-containing mnemonic is not an ordinary identifier. Standalone
`_`, `$`, `%`, empty names and invalid punctuation retain their existing role or
character-grammar restrictions.

Table 2 has 136 instruction roots. `fabric` is separately explicit in the
[fabric instruction sections](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fabric-instructions-submit),
although absent from that table. The expanded packaged opcode set matches these
137 roots; raw YAML is pre-expansion data and is not evidence of 40 missing roots.
Neither count defines an additional identifier-ban catalogue. A variable or
function named `fabric` does not bypass the independent syntax/version/target
checks for `fabric` instructions.


<a id="parameter-declarations"></a>

## Parameter declarations

Technical reference and evidence: [parameter_declarations.md](us-en/parameter_declarations.md).

<a id="parameter-declarations--parameter-declarations"></a>

### Parameter declarations

This is the frontend's PTX ISA 9.3 declaration boundary, not a launch ABI,
instruction-completeness claim, or runtime allocation model. Parsing alone does
not establish that a declaration is valid. `resolveModule()` runs declaration
validation before publishing metadata.


<a id="parameter-declarations--coverage-matrix"></a>

### Coverage matrix

| Form / context | Parser | Declaration validation | Installed public metadata |
| --- | --- | --- | --- |
| Entry header scalar/vector / sized multidimensional array | Retained | Fundamental non-predicate types, constant positive extents, alignment, pointer attributes, version and total-size checks | Ordered typed `parameter_declarations` with `EntryInput` role |
| Device input / return scalar/vector / sized multidimensional array | Retained | Same type/extent checks; `.param` needs PTX 2.0 / SM 20; modern functions have at most one return | `parameter_declarations`, `DeviceInput` / `DeviceReturn` |
| Final device input `.param .b8 bytes[]` | Retained | Only final input; PTX 6.0 / SM 30 minimum | Unknown outer extent, checked known inner axes and unknown byte extent, distinct from a scalar |
| Unsized entry, return, non-final input, or non-`.b8` input | Retained | Rejected | No resolved module |
| PTX 1.0–1.3 entry body input | Retained | Fully sized fundamental scalar/vector/axes/groups, read-only, actual entry scope, checked logical aggregate <=32764 | Sole `parameter_declarations` table, `LegacyEntryInput`; header signature remains header-only |
| Body-local scalar/vector / sized array, including multiple dimensions | Retained | Fundamental non-predicate type, positive constant extents, checked size, alignment, PTX 2.0 / SM 20 | `parameter_declarations`, `BodyLocal`, lexical scope and full array shape |
| Unsized body-local `.param` | Retained | Rejected | No resolved module |
| `.callprototype` scalar/vector / array formals | Retained | Device signature rules; prototype itself requires PTX 2.1 / SM 20 | Owned `declaration_semantics::FunctionSignature` from the semantic API; not a declaration table entry |
| Entry-header opaque `.texref`, `.samplerref`, `.surfref` | Retained | Name-only entry inputs; resource kind, shape, and version checked | Ordered `opaque_entry_parameters` with bound symbol identity, resource kind, and source shape; no byte layout |
| Device formal, return, or body-local opaque object | Type spelling retained | Rejected | None; illegal because opaque-object use is limited to module globals and entry parameter lists |
| Device `.reg .v2` / `.v4` input or return, including `.callprototype` | Retained as one formal | Non-predicate fundamental elements, at most 128 bits total; scalar/vector and width must match at calls | Typed `FunctionParameterContract::vector_shape`, bound vector width; no `.param` declaration record |
| Header multidimensional parameter array | Every axis retained | Positive sized axes, checked products; only a supported final scalar byte input may omit its outer extent | Exact axes in signature and own declaration records; total element count for call ABI |
| Body-local parameterized `.param` group (`name<count>`) | Retained | Supported scalar/vector parameter elements, positive uint32 count, no arrays or initializers; existing body-local availability applies | One `BodyLocal` parameter record and one owning symbol with count; members use the same `SymbolId` plus an index |
| `.pred`, unknown or instruction-only type spelling | Type spelling retained | Rejected for `.param` | None |
| Module-scope `.param` | Retained | Rejected | None |

Supported parameter elements are `.s8/.s16/.s32/.s64`, `.u8/.u16/.u32/.u64`,
`.b8/.b16/.b32/.b64/.b128`, and `.f16/.f16x2/.f32/.f64`. The public
`declaration_semantics::parameterScalarType()` classifies this supported set;
its result is a type classification, not validation of context or availability.
Instruction alternate formats such as `.bf16` and `.tf32` do not become legal
declaration types by retaining their spelling. `.reg` formals retain their
existing separate signature/address path; they are not `.param` table entries.
Within the owning function body, register-space input and return formals are
resolved as typed register operands and retain their lexical symbol identities.
Device `.reg` inputs and returns support `.v2` and `.v4` of non-predicate
fundamental elements, including `.f16x2`, with at most 128 bits in total. A
vector remains one formal and one named-register call argument; scalar literals
and differing vector widths do not satisfy its signature. Omitted alignment is
the total vector size. Direct calls, `.callprototype`, `.calltargets`, and owned
call-table signatures preserve the same typed shape after AST destruction.
Parameter-space formals remain data objects: use an instruction appropriate to
their parameter address or storage semantics rather than an arithmetic register
operand.


<a id="parameter-declarations--alignment-pointers-and-size"></a>

### Alignment, pointers, and size

Explicit declaration and pointee alignments must be positive powers of two.
Omitted declaration alignment is scalar size or total vector size, independently of
array count; an explicit value
is retained separately from whether it equals that natural value. The frontend
does not infer a maximum alignment from the ABI's wording about multiples of
1, 2, 4, 8, or 16 bytes.

`.ptr` belongs to entry inputs and requires PTX 2.2. The supported scalar pointer
types are `.b32`, `.b64`, `.s32`, `.s64`, `.u32`, and `.u64`. The declared
integer or bit type is preserved; both widths are accepted with either
`.address_size 32` or `.address_size 64`. Pointed state space may be `.const`, `.global`,
`.local`, or `.shared`; omission denotes generic space. Omitted pointee alignment
defaults to four bytes. Parameter storage alignment and pointee alignment are
different fields. No host pointer width or launch-address policy is inferred.
The PTX 2.2 pointer introduction is checked from current owned entry-input
metadata even when target context is missing. A known older version rejects;
an unknown version defers under available-context validation and still requires
context under strict validation. Source-aware validation uses the supplied
source version, while AST-free validation uses the selected owned source region.
This does not equate the declared pointer container width with address size.
Canonical function signatures compare effective alignments: omitted natural
alignment and an explicit equal value are compatible. Declaration metadata still
retains the explicit-alignment distinction. Direct and metadata-backed indirect
array calls use the same natural-alignment default.
Formal `.param` objects cannot be used directly as call actuals. Load an input
(including an entry pointer) into a register or stage it through a body-local
`.param` object first. Device formals do not acquire an entry-only `.ptr` attribute.

Entry header parameters require PTX 1.4. For normal, non-opaque entry parameters,
statically computed aggregate storage includes inter-parameter alignment padding.
The version-selected limit is 256 bytes at PTX 1.4, 4352 bytes at PTX 1.5–8.0,
and 32764 bytes at PTX 8.1 and later. Checked arithmetic rejects overflowing
declaration extents and aggregates. These are ISA limits, not CUDA/OpenCL
driver-specific limits; byte extents are not packed offsets or allocated memory.
Missing version/target information cannot prove an availability violation.
Body-local `.param` initializers are rejected by the parser; semantic validation
also guards against them in directly constructed ASTs.
Calls may omit a validated final unsized byte input when there is no payload;
other required input and return arguments remain subject to exact arity checks.


<a id="parameter-declarations--owned-metadata-and-remaining-scope"></a>

### Owned metadata and remaining scope

The former `ResolvedFunction::entry_parameters` member and `ResolvedEntryParameter`
type have been removed. This is a breaking C++ API change: consumers must migrate
and rebuild, not merely rename the member. Use `scalar_type` instead of the old
type string, the effective non-optional `alignment`, and `array_extents` /
`byte_extent` instead of `is_array` / `array_extent`. An empty extent list denotes
a scalar; an unknown axis denotes a supported unsized array. ABI layout and
packing remain consumer responsibilities.


<a id="parameter-declarations--retained-boundaries-and-normative-classification"></a>

### Retained boundaries and normative classification

The archived specification distinguishes four categories used by this document:
**legal supported** forms appear in the table, **legal but unsupported** forms
remain deliberately diagnosed until their typed metadata and access rules are
implemented, **illegal** forms remain rejected, and **unresolved** forms are
not promoted to either category from parser behavior alone.

Opaque entry parameters are legal ISA objects: the `.entry` directive permits
them, while the opaque-type section limits their declaration to module globals
and entry parameter lists. They are name-only texture/surface objects and
ordinary `ld.param` cannot load them; their physical layout is intentionally
hidden. Each function owns its opaque entry inputs in source order in
`opaque_entry_parameters`, retaining bound identity, kind, alignment, array
shape, and source location after AST release. Device and body opaque
declarations consequently remain rejected.

Fundamental `.f16x2` and legal `.param .v2/.v4` objects retain native type and
shape throughout declaration, call ABI and owned layout checks. Numeric packed
literal representation remains the separate reference gap described below.

Body-local scalar/vector `.param` compact groups reuse the ordinary group binding:
`value<2>` owns one symbol with count 2, while `value0` and `value1` refer to that
`SymbolId` with indices 0 and 1. Each declarator has one `BodyLocal` parameter
record and no storage declaration record. Its `byte_extent` is the extent of
one member object, not count times an allocation size; the symbol owns the count.
Lexical shadowing, member bounds, and call staging use the typed member identity.
Owned validation rejects missing/out-of-range member indices and indices on
ordinary symbols. A coherent in-range member-index edit is a valid semantic
change; owned validation does not reconstruct the original spelling. When an AST
is supplied, source association separately retains compact counts and written
member references. No expanded source declarations or per-member symbols are
manufactured.

Header dimensions are ordered outer-to-inner. Sized entry inputs and device/prototype inputs
and returns admit multiple axes. Only the outer axis of a supported final scalar
`.b8` device/prototype input may be omitted, for example `bytes[][2][3]`.
Known inner products are checked even when total bytes remain unknown. A caller's
payload need not have the same rank or be divisible by that inner product:
call ABI compares element type, vector shape, total count and effective alignment.
For example `[2][3]` and `[6]` are ABI-compatible. Each function's own signature
and declaration record must still agree on every axis. Source association also
distinguishes written shapes even when their ABI totals agree.

This is a source-breaking C++ member migration: `CstFunctionParameter` removes
`left_bracket`, `array_size`, `right_bracket` in favor of
`std::vector<CstArrayDimension> array_dimensions`; `AstFunctionParameter` removes
`is_array` and `array_size` in favor of `std::vector<AstArrayDimension>
array_dimensions`. `FunctionParameterContract` replaces `is_array` and
`array_extent` with `std::vector<std::optional<NormalizedNumericValue>>
array_extents`; `ResolvedOpaqueEntryParameter` replaces those same old fields with
`std::vector<std::optional<uint64_t>> array_extents`. Empty vectors denote scalars.
Numeric `ResolvedParameterDeclaration::array_extents` is unchanged. Binding
`Symbol::is_array` and `CallArgumentProperties::is_array/array_size` remain derived
category/ABI-total facts; they do not own exact axes. Invalid axes and overflowing
products remain invalid, rather than becoming ordinary unknown extents. Opaque
axes carry no invented physical byte size. No multi-bracket memory operand or
runtime allocation/bounds semantics is added.

No instruction families, simulator execution, argument packing, or physical
allocation are added here.


<a id="parameter-declarations--fundamental-vectors-and-packed-elements"></a>

### Fundamental vectors and packed elements

All 17 non-predicate fundamental element types are classified, including scalar
`.f16x2`. Its two half components form one four-byte packed scalar, not an implicit
`.v2` formal. Explicit `.v2/.v4` parameters contain two/four such elements; vectors
may total at most 16 bytes, giving 16 legal v2 and 12 legal v4 element combinations.
Header and body `.param` objects, device/prototype inputs and returns preserve this
shape. Sized parameter vector arrays are supported; natural alignment is the vector
size and bytes multiply that size by the dimensions. Compact counts remain separate
from each member object's extent. Register vector arrays, vector pointers, predicate,
opaque and instruction-only elements remain rejected. Final unsized byte input is scalar only.

Source and AST-free owned validation recompute the same padded entry byte limits
and checked extents. Native typed F16x2 parameter/register actuals retain exact dtype
and explicit shape; calls do not substitute instruction-style bit compatibility.
Use existing b32 memory access containers for packed storage, while named array
indices still scale by the declared scalar element (four bytes for F16x2), not vector size.

A numeric one-to-pair F16x2 call-literal constructor remains a reference gap:
integers, zero, decimal and F64 expressions do not define a splat or lane placement.
The exact-word contract requires a same-width bare unsigned direct `0f` binary32
literal to retain the entire 32-bit word for an otherwise eligible scalar `.f16x2`
Reg or Param input. Direct calls, `.callprototype`, `.calltargets`, and storage
function-list routes use the same formal-driven conversion. This does not admit a
scalar literal to a vector formal or return list, or classify `.bf16x2` as a call
formal. This exact-word route is distinct from ordinary scalar F16 numeric conversion.

Known opaque entry inputs remain name-only: numeric aggregate traversal skips them
without assigning bytes or padding, and continues checking later numeric/vector inputs.

Complete-module direct-call targets must have a prior declaration/definition of
the actual bound function or alias identity. Prior compatible prototypes suffice;
whole-module lookup of a later declaration does not establish visibility. This
source/owned check is independent of the exact parameter ABI and staging rules.

Under the default stack ABI, register-state device and call-prototype input and
return formals require at least 32 bits in the **whole value** when both PTX >=2.0
and the active recognized target architecture >=20 are known. The width is scalar
storage width times explicit vector lanes: `.v2.u8` and scalar `.f16/.pred` fail,
while `.v4.u8`, `.v2.f16` and native scalar `.f16x2` meet the minimum. Existing
fundamental/vector eligibility and maximum width remain independent. Source types
and shapes are preserved, with no implicit formal promotion. A known older version
or target disables this minimum; missing context defers it in available-context
validation, while strict validation retains its missing-context errors. Each
function uses its active target region; source-aware validation uses the supplied
AST and AST-free validation uses owned regions. Unused prototypes are checked too.
This is a formal rule: ordinary small/predicate register declarations, small
`.param` formals and matching small-register actuals to `.param` remain legal.

Otherwise eligible scalar F16 Reg and Param inputs share numeric literal conversion, including direct and metadata-backed indirect calls. A floating 1.5 or evaluated F64 equivalent becomes F16 word 0x3e00; original lexical/precast source is retained for owned recomputation. Modern Register-formal minimum width still rejects scalar Reg F16 independently, while established nonstack legacy calls and partial AvailableContext follow their existing gates. Strict missing context and scalar-to-vector no-splat remain. This does not add a packed F16x2 constructor.

Parameter address validation also rejoins the current symbol ID, lexical scope,
compact member and declaration record before context can be skipped. A known
Parameter object's effective space must belong to a nonempty operand domain
and agree with an associated current state-space modifier. Readonly entry input
checks use explicit Write/ReadWrite operand requirements or the existing Return
parameter constraint; DeviceInput is not generally readonly. Diagnostics merge
only an identical local address cause, preserving unrelated failures. This
check does not authorize new source forms or replace full target-aware checking.


<a id="parameter-declarations--historical-entry-body-inputs-and-typed-roles"></a>

### Historical entry body inputs and typed roles

PTX 1.0–1.3 entry body `.param` objects are read-only kernel inputs with
`ParameterDeclarationRole::LegacyEntryInput`. Their sole owning handoff is
`ResolvedFunction::parameter_declarations`, in lexical depth-first/declarator
order, including nested scopes and distinct shadowed identities. Compact groups
retain one record per bound group with per-member `byte_extent` and a count on
the symbol; arrays retain all axes. This order defines metadata traversal, with
no host launch slots, relative addresses, padding, or allocated offsets.

`FunctionSignature::parameters` and returns describe syntactic header ABI only.
A legacy entry can have an empty header signature and nonempty body inputs.
Consumers enumerate `EntryInput` records for header inputs and `LegacyEntryInput`
records for legacy body inputs; opaque header inputs remain separate. Legacy body
inputs require fully sized fundamental objects and keep existing type-specific
availability. This adds no legacy opaque, `.ptr`, unsized, or initializer permission.
PTX 1.4–1.x rejects entry body `.param`; PTX 2.0 and later selects `BodyLocal`
with independently known SM 20 minimum. Device body objects remain `BodyLocal`.

Fresh `resolveModuleOnly` and `resolveModule` need a PTX version only when an
entry contains body `.param` and its role cannot otherwise be selected. They
report `MissingValidationContext`, including with an SM 1.x target; they do not
guess a role from the target or require a target just to type it. Ordinary
fragments, entries without body parameters, header inputs, and device body
objects retain their previous context behavior. Already typed modules keep their
current role with missing context: available-context validation defers only the
unknown window; strict validation still requires complete context.

`ResolvedSymbolRef::parameter_role` is a synchronous record-derived cache and
preserves the structural `CallParameter` binding kind. Known legacy direct
references require the matching cache. Every engaged cache rejoins the actual
parameter record; corrupt, missing legacy, forged kind, or mismatched role caches
reject. Address, MOV, and tensor projections share the role-to-direction mapping:
entry/device/legacy inputs are `Input`, device returns `Return`, and body call
slots `CallArgument`. Legacy LD is an ordinary input read without return-load
staging. MOV takes its `.param` address without device-local materialization.
Actual records independently enforce read-only/entry-frame semantics and exclude
direct legacy input call actuals/returns; loading a value into a register preserves
legal register ABI calls. Modern body call-store/call/load adjacency remains.
Both validators check current records, caches, shapes, scopes, accesses, and
logical capacity before a missing-target skip; valid original syntax cannot
exempt a current corrupted payload. Replacement context checks the current role,
with no automatic reinterpretation or producer history.

The known global legacy-input ceiling is 32764 bytes. A checked logical sum uses
each per-member `byte_extent` times its compact count (otherwise one), across
all inputs, including unused and nested declarations. Axes/vector lanes already
contribute to `byte_extent` and are not multiplied again. Overflow or a total
above 32764 rejects during resolution and both validation routes, even without
context. The PTX 1.4 header-specific 256-byte limit is not applied to legacy body
inputs; existing later header limits remain unchanged.
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` remains reference-boundary qualified:
passing the known logical ceiling is not proof of physical ABI packing, a lower
historical threshold, or acceptance by every legacy driver/hardware launch ABI.


The shared current operand-domain pass classifies both actual `EntryInput` and
`LegacyEntryInput` records as read-only entry objects. It uses current typed
layout/field requirements before context skips, including non-LD/ST consumers;
MOV address acquisition stays legal, while an explicit writable operand rejects.
Legacy readonly diagnostics use their original label and instruction range;
only identical local causes merge. No opcode-specific writer classification or
new historical instruction permission is inferred.


<a id="python-generator-model"></a>

## Python Generator Model Design

Technical reference and evidence: [python_generator_model.md](us-en/python_generator_model.md).

<a id="python-generator-model--video-topology-projection"></a>

### Video topology projection

Frozen `VideoInstructionSpec` and `VideoOperandSpec` carry lane/operation, role, selector, coercion and written-negation obligations. Normalization validates field references and selector topology before syntax-language overlap checks. The same typed slot shape feeds selection and overlap validation, so scalar merge and secondary forms remain distinct. Backend projection rewrites modifier references through the existing field-alias map, including `.sat` → `saturate`; C++ policy receives typed enums and stable field IDs, not opcode-name or raw-suffix branches. Generated classes and checker descriptors consume this normalized contract.


<a id="register-type-policy"></a>

## Register declaration compatibility

Technical reference and evidence: [register_type_policy.md](us-en/register_type_policy.md).

<a id="register-type-policy--contract"></a>

### Contract

`register_width: same_width` checks both width and fundamental-type
compatibility. It is not enum identity: bit-size registers can carry compatible
same-width integer or floating operands, and signed/unsigned integer declarations
can be interchanged at the same width. Integer/float substitutions remain errors.
`mul.wide.u32` and `mad.wide.u32` still require 32-bit multiplicands and a 64-bit
result (and 64-bit addend for `mad`). These same-width operands reject wider
registers.

`exact` retains its existing enum-identity meaning. It is appropriate for
explicit declaration-format requirements, not ordinary arithmetic merely because
its width is fixed. `equal_or_wider` admits compatible wider bit carriers through
`.b128` for applicable `ld`/`st`/`cvt` operands under PTX ISA 9.3 Tables 27/28.
Declaration eligibility is checked centrally; instruction, format, PTX-version,
and SM gates still apply. Immediate conversion is unchanged.
Resolution still builds the same IR; generated checking applies the descriptor
policy to bound register declarations. Resolving a module alone is not a claim
that its instructions pass checking.

Declaration semantics separately validates every `.reg` type token, including
an unused register. Its fundamental declaration types are `.s8/.s16/.s32/.s64`,
`.u8/.u16/.u32/.u64`, `.b8/.b16/.b32/.b64/.b128`, `.f16`, `.f16x2`, `.f32`,
`.f64`, and scalar `.pred`. A recognized packed or alternate instruction format
(`.bf16`, `.tf32`, and the modeled packed spellings, for example) is diagnosed
as instruction-only; an unrecognized spelling is diagnosed as unknown. This
does not make the CST restrictive. Non-predicate `.v2`/`.v4` registers are
limited to 128 bits, while predicate registers are scalar. These declaration
rules preserve valid `.reg` function formals and parameterized register groups.

`base::ScalarTypeMetadata` is the canonical source for every modeled scalar's
PTX spelling, kind, byte width, and `.reg` declaration usage. Declaration
semantics uses that metadata; the generated resolver suffix table remains a
mechanical instruction-modifier mapping. `.param` deliberately retains its
separate storage-type contract.


Bracket arrays are forbidden in `.reg` declarations, including unused registers,
unsized or multidimensional arrays, and arrays of vector registers. Declaration
semantics reports `InvalidRegisterDeclarationShape` at the first bracket
dimension. This is separate from legal compact register groups such as
`.reg .u32 r<2>` and legal `.v2`/`.v4` register declarations. Arrays in `.param`,
`.global`, `.const`, `.shared`, and `.local` retain their existing rules.
This follows [PTX ISA 9.3 register state space](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#register-state-space).

`binding::Symbol::is_array` retains whether any declaration occurrence writes bracket
dimensions (or a formal parameter is an array), independently of `vector_width`
and `parameterized_count`. Owned validation checks every register symbol even
when it is unused and has no storage record; an illegal array reports
`RuleViolation` at the retained identifier range. Array-bearing storage or
parameter records bound to a register are also rejected. These checks remain
available after source and AST destruction; AST-backed validation also checks
replacement source declarations.

Module placement of `.reg` and `.local` follows the source-target ABI boundary
(PTX >= 3.0 and recognized SM >= 20); historical versions and SM 1.x remain
separate. See [declaration semantics](#declaration-semantics-design) for source
occurrence, owned lifetime and incomplete-context behavior.


<a id="resolved-ir-design"></a>

## C++ Resolved IR Design

Technical reference and evidence: [resolved_ir_design.md](us-en/resolved_ir_design.md).

<a id="resolved-ir-design--integrated-vector-data-movement"></a>

### Integrated vector data movement

The complete-module regression chains below exercise parsing, binding, resolution
and checking together. They also validate owned IR after the original source and
AST are destroyed and the module is copied and moved. This is frontend evidence,
not GPU execution, broadcast/truncation semantics or assembler parity.

| Supported chain or boundary | Preserved contract and compatibility |
| --- | --- |
| Named v4 f32 LD → scalar component → named/brace MOV → ST | One real declaration identity underlies canonical lanes; `.xyzw` and `.rgba` are explicit ordinary-register selectors, while named projections retain the whole identifier range without fabricated selectors. Ordinary `.w` does not add hardware `%tid.w`. |
| v2 u32 constant-expression MOV → alias/overlap MOV → ST | Folded typed values, parameterized register members and source form survive ownership transfer. Destination aliases of one lane are duplicates; repeated sources and source/destination overlap remain legal. |
| Named v2 f64 NC/LDU → MOV → ST | Memory and MOV retain their own type policies. Existing availability remains distinct: NC needs PTX 3.1 / SM 32, generic LDU PTX 2.0 / SM 20, explicit-global LDU PTX 2.0 plus f64 SM 13, and f64 MOV SM 13. |
| Named-array constant address → vector LD → MOV → ST | A declared `.v4 .f32 A[]` still has scalar index stride four bytes, not sixteen. `A[4]` contributes sixteen bytes and can satisfy v4 alignment; a dynamic index reduces the known alignment to `gcd(base alignment, 4)`, so the same vector access is rejected when only four-byte alignment is guaranteed. |
| Adjacent unsupported combinations | Atom/red remain brace-only; scalar bit pack/unpack accepts exact-width named ordinary aggregates and retains its sink and B128 gates; floating instruction types and scalar-to-scalar B128 remain excluded. Modern 256-bit memory uses legal scalar brace lanes, not oversized ordinary named declarations or widened vector MOV. |

Owned revalidation checks complete source forms, component origin/order, actual
declaration width, type/arity and source ownership, with located failures for
tampered payloads. These integration checks supplement the individual family
regressions; they do not imply the full ISA cross-product or a stable 1.0 API.


<a id="resolved-ir-design--explicit-vector-mov"></a>

### Explicit vector MOV

The existing `mov_v4_u32` / `MovV4U32` semantic identity now covers explicit
`.v2`/`.v4` MOV with `b/u/s16`, `b/u/s32`, `f32`, `b/u/s64`, and `f64`, with
at most 128 total bits (so no `.v4` × 64). It retains PTX 1.0 availability and
the f64 SM 13 floor. This is element-width MOV, not scalar bit-aggregate packing.

Public API migration: `MovV4U32::dst` is now `WithLocs<ResolvedRegisterVector>`;
`src` is `WithLocs<ResolvedMovVectorSource>`. The latter is a variant of
`ResolvedMovValueVector` (ordered `ResolvedMovVectorLane` register/immediate/
scalar-special values plus common `ResolvedVectorSource`) and the existing
`ResolvedVectorSpecialRegisterRef`. Its typed reference callback is
`IReferenceObserver::mov_vector_source`. Generic `RegOrImm`, `ResolvedValueVector`,
and scalar `ResolvedMovSource` have not widened.

Named and braced destinations/sources combine freely with exact arity and scalar
MOV same-width type compatibility, never memory widening. Named source/base/lane
identity, member, canonical order and exact whole-identifier locations reuse the
complete named provenance contract below; only that container admits implicit
projections. Brace destination sinks require at least one real register and no
duplicate physical destination (aliases count as duplicates). Source repeats and
source/destination overlap are legal. Brace sources accept scalar ordinary or
explicit-component registers, numeric literals/constant expressions and scalar
special reads. Each special lane retains its real registry identity, component,
historical scalar MOV type compatibility and PTX/SM/capability checks.

Whole hardware vectors remain separate width-four intrinsic-u32 reads, usable
only with u32/s32/b32 and their existing availability. No fourth scalar hardware
selector is invented. Sinks, predicate lanes, nested whole vectors, addresses,
functions and symbols are not source lanes. Bare scalar vector sources such as
`mov.v2.u32 V, 1` remain an unsupported frontend subset, not an ISA-invalid claim
or a broadcast/truncation promise. Scalar `.b16/.b32/.b64/.b128` pack/unpack
accepts braces or a complete ordinary named vector with exactly matching total
width, retaining B128 PTX 8.3 / SM 70 gates. Floating instruction types and
scalar-to-scalar B128 forms remain excluded. Other family fences and
modern 256-bit brace memory forms are unchanged. Owned validation rejoins complete
base/lane declarations and source ownership after source release/copy/move.


<a id="resolved-ir-design--named-array-addresses"></a>

### Named array addresses

Dedicated address-source MOV and generic address operands accept `A[index]`,
including LDNC/LDU, atomic/reduction, cache, asynchronous-copy, barrier, tensor-map,
response-object and fabric local-memory addresses. Admission follows each typed
binding: explicit register-only async data addresses remain restricted, independently
of generic barrier-object siblings. Tensor operands, fabric handles and texture/surface
accesses retain their separate grammars. Generated source and owned checks use the
same role policy; existing target, state-space, alignment and control rules still apply.
The named form does not compose with a `.unified` suffix. Known unified storage
still requires the existing bracket-qualified LD path and remains read-only for
ST; MOV may take its address, including named indexing, without dereferencing it.

`ResolvedAddress::named_index` owns an integer index's bits/signedness or the
exact bound scalar register, optional integer displacement, written operator,
scalar byte stride and source ranges. It is mutually exclusive with the old
byte offset. `A[1]` scales by the declaration's scalar base element size:
u32, v2.u32 and multidimensional u32 arrays all use four bytes, while u64 uses
eight. Instruction data types, transfer granules and opaque object sizes do not
determine this scalar stride. Array shape comes from owned storage/parameter records keyed by SymbolId;
standalone resolution without that context fails explicitly. Negative and
out-of-bounds constants are not rejected merely for array bounds.

Dynamic integer/bit registers retain their declared width and signedness,
including narrow types and s64, without runtime evaluation, casts, promotion,
truncation or invented sign extension. Only the constant contribution is
checked and scaled before MOV's signed-64 or memory's signed-32 byte domain.
Direct minus and plus-negative normalize to the same constant contribution
while retaining different source operations. Dynamic alignment is
`gcd(base alignment, scalar stride)`, further reduced by the constant bytes.
Owned validation rejoins declaration shape and register identity, rechecks
cached stride/displacement and ranges, and needs no Syntax AST lifetime.

Local ptxas CUDA 13.3 V13.3.33 probes confirm scalar-element scaling and permit
negative/out-of-bounds constants. That compiler rejects direct-minus dynamic
spelling while accepting plus-negative; the frontend deliberately accepts both.
These are compiler observations, not GPU execution or runtime width/sign rules.


<a id="resolved-ir-design--current-validation-limits"></a>

### Current validation limits

The entry point contracts below describe modeled checks, not exhaustive PTX
validation. Header/target, storage and ABI checks and their remaining boundaries
are recorded in [current coverage](us-en/converage/coverage_status.md). Complete source context is distinct
from completeness of the validator; runtime behavior remains outside this layer.


<a id="resolved-ir-design--status-and-scope"></a>

### Status and scope

| Exact architecture/flavor suffixes | Minimum PTX ISA |
| --- | --- |
| 10, 11 | 1.0 |
| 12, 13 | 1.2 |
| 20 | 2.0 |
| 30 | 3.0 |
| 35 | 3.1 |
| 32, 50 | 4.0 |
| 37, 52 | 4.1 |
| 53 | 4.2 |
| 60, 61, 62 | 5.0 |
| 70 | 6.0 |
| 72 | 6.1 |
| 75 | 6.3 |
| 80 | 7.0 |
| 86 | 7.1 |
| 87 | 7.4 |
| 89, 90 | 7.8 |
| 90a | 8.0 |
| 100, 100a | 8.6 |
| 120, 120a | 8.7 |
| 100f, 103, 103a, 103f, 120f, 121, 121a, 121f | 8.8 |
| 88, 110, 110a, 110f | 9.0 |

Syntax AST preserves source spelling, modifier order, and `SourceRange`.
Resolved IR records the selected semantic instruction form, resolved operand values,
and diagnostic locations. These are distinct frontend boundaries; public API compatibility is still pre-1.0. Lexical symbol
binding is connected to module resolution, execution predicates resolve to
declaration-aware values, and special registers, external symbols, and
genuinely undeclared references are distinct. The 16/32/64-bit scalar `mov`
type families accept register, immediate, and special-register sources; the
32/64-bit forms also accept data-symbol, `symbol+offset`, function-address, and
legal formal-parameter-address sources. Bit-size forms also support two/four-
element vector pack/unpack; `.b128` is vector-only. `mov.pred` reuses the declaration-aware
`ResolvedPredicate` representation. Generic and basic explicit-space scalar
plus braced-vector `ld`/`st` exercise the dereferenced-address path
for 14 bit-size, integer, and floating-point types from 8 through 64 bits.
Legacy memory-vector payloads are capped at 128 bits: `.v2` accepts modeled
types through 64 bits and `.v4` through 32 bits. PTX 8.8/SM 100 additionally
accepts exactly 256-bit `.v8` × 32-bit and `.v4` × 64-bit forms. Static natural
alignment checks bound data symbols with constant byte offsets and absolute
immediate addresses; register and standalone unresolved addresses stay unknown.
CFG/SSA and target lowering remain outside the static frontend contract. `ResolvedIndirectCallee` provides descriptor-
independent identity for a scalar General integer/bit `.reg` indirect target or a bound
function-local `.callprototype`/`.calltargets` label. Its enclosing
`ResolvedFunction` owns the matching ordered metadata payload and normalized
ABI separately, so an operand remains compact without making the metadata
unavailable. The final `CallDirect` class now has three additional
`IndirectCall` layouts (target/metadata, target/input/metadata, and
return/target/input/metadata), each available from PTX 2.1 / SM 20; normal
module indirect calls preserve the bound target and metadata identities, then
reuse the direct-call ABI contract through metadata-indexed canonical
signatures. ABI comparison does not create a second indirect-call model.

The operand-level `ResolvedIndirectCallee` `std::variant` additionally contains
`ResolvedCallTableRef`, whose ID is the actual
storage variable, not a metadata label. `ResolvedModule::call_tables` owns only
consumed tables, including scope/owner, declaration provenance, ordered explicit
slots with byte offsets and bound/canonical function IDs, exact initializer and
target-token ranges, and the full common signature. Sparse omitted tails do not
create slots. Owned validation reconstructs each contract from real storage,
initializer references and canonical function contracts, rejoins lexical flist
uses and prior declaration occurrences, and rejects missing/extra/stale contracts
or coherent cached-field mutations. Visitors expose the active
`ResolvedIndirectCallee` operand alternative.
No borrowed AST data or runtime pointer-membership proof is retained.





Matrix data forms expose immutable class topology through
`matrix_descriptor()`; mutable operand fields retain source-selected values and
locations. Neither a mutable logical-form tag nor a `matrix_logical_index`
lookup is part of this contract. Shared matrix descriptors retain a bound
register identity while their runtime bits remain opaque. The separate
[`TensorMapKnownFacts` query](#tensor-map-known-facts) accepts caller-supplied
descriptor facts. Its
`project_tensor_known_access_context(const Instruction&, const checker::Context&)`
adapter copies selected typed TMA access facts without retaining the AST:
non-tensor forms yield no access or diagnostics, malformed tensor metadata
yields diagnostics without access, and target unavailability preserves the
copied access with availability false. It does not decode raw descriptor bytes.

| Entry point | Success means |
| --- | --- |
| `resolveModuleOnly(ast)` | Binding, declaration semantics, instruction resolution, and call-ABI/staging checks passed. It does not run the final instruction/directive checker. |
| `resolveAndValidateModule(ast)` | Resolution and final checking passed, with a recognized source target and PTX version for each checked region. Missing context is an error. |
| `resolveModule(ast)` | Compatibility behavior: resolution plus final checks where context is available; targetless fragments remain accepted. |
| `validateModule(ast, module, policy)` | Source correspondence and final instruction/directive checks passed under the explicit policy (default: `RequireCompleteContext`). The module must already have passed resolution. |
| `validateModule(module, policy)` | Revalidates owned header, declaration/member identities, control metadata, typed call literals, operand layouts, and available source-region checker rules without traversing an AST. Manually constructed or mutated public IR must use this entry point before consumption. |
| `checkModuleAvailability(ast, module)` | Compatibility wrapper for validation with `AvailableContext`; despite its historical name, it runs the full instruction checker in contextualized regions. |

Resolution-only still enforces declaration availability when its source contains
the relevant version/target; it is not a bypass for malformed declarations.
Binding, declaration shape/type rules, operand resolution, and call ABI/staging
belong to resolution. The final generated checker owns remaining instruction
constraints (including target-independent layout/type relationships) as well as
PTX/SM/profile availability. Thus resolution-only does not guarantee that all
target-independent instruction constraints passed. All validation guarantees
are limited to the currently modeled instruction and declaration subset.
Each `.target` replaces the active source context, including clearing a previous
recognized target when the new spelling is unknown. Function headers, nested
body declarations, and local call prototypes use their containing function's
source region. This is separate from choosing a deployment target for lowering.
The explicit validation catalog includes historical `sm_10`, `sm_11`, `sm_12`,
`sm_13`, and `sm_20` with no modern capabilities or family features, allowing
legacy source contexts and the PTX 6.0 `sm_20`/`sm_30` declaration boundary to be
checked consistently; arbitrary numeric target spellings remain unrecognized.
Support is limited to catalog targets, rather than every historical GPU profile.

`ResolvedSourceTargetRegion::map_f64_to_f32` owns the effective programmer option
decoded only from the exact `map_f64_to_f32` spelling in that region's ordered
`target_options`. Every `.target` resets the option; omission means false even
after a region that enabled it. Both AST-backed and owned validation pass it to
`checker::TargetInfo::map_f64_to_f32`. Owned validation rejects contradictions
between the Boolean and retained spellings. This option is separate from hardware
capabilities and does not change f64 instruction identity, storage, or operand
widths. At PTX 1.4 or later, `div.rn.f64`, `rcp.rn.f64`, and `sqrt.rn.f64` accept
exact generic `sm_10`, `sm_11`, or `sm_12` only with this option; their existing
`sm_13` and later availability remains available without it. Other f64 forms,
directed rounding, and f32 forms keep their own gates. This frontend preserves
and validates the source option; it performs no lowering or emulation.

`RequireCompleteContext` also requires an initial `.version`, then `.target`,
then optional `.address_size`, before other module items. A version or address
size cannot appear later; subsequent `.target` directives still open valid source
regions. Header-only modules use the same rule, and an empty module lacks complete
context. `ResolvedModuleHeader::source_order` owns typed traversal events so this
check remains possible after source and AST destruction. Missing or inconsistent
order metadata cannot certify complete context. `AvailableContext` and
`resolveModuleOnly()` preserve fragment and out-of-order-header compatibility.
The AST overload checks the supplied AST's order when validating a replacement
source configuration.

Every `.target` item is classified: its first item must be an architecture
spelling, and later items may be `texmode_unified`, `texmode_independent`, `debug`,
or `map_f64_to_f32`. Unknown options and additional architecture items are
rejected during parsing and by both validators. Lexically valid unrecognized
targets remain parseable and produce `UnknownTarget` at validation. For every
explicitly cataloged `sm_<number>[a|f]` profile, `compute_<number>[a|f]` is a
synonym with the same architecture, flavor, enabled family features and
capabilities. Both lexical parsing and owned source regions preserve the written
prefix; semantic availability and explicit checker-target comparisons use
validation-feature architecture and flavor through the explicit catalog rename
correspondence. `TargetIdentity::operator==` still compares retained
source spelling as well, so source-provenance consistency checks remain exact.
Unknown numbers and unsupported flavors, including `compute_102a` and
`compute_102f`, do not acquire catalog profiles. Texture-mode
selection is checked for repetition and module conflicts. This contract adds neither
DWARF content checks nor floating-point instruction conversion. These source forms follow the archived
[PTX ISA 9.3 target directive](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target).

Header compatibility is checked in both validation policies, including header-only
modules and every later target region. `.version` requires PTX ISA 1.0 or later;
explicit `.address_size` requires 2.3, while an omitted address size retains the
32-bit default at earlier versions. Both explicit texture mode options require
1.5, and `debug` requires 3.0. `AvailableContext` defers checks that need an absent
version or target, but rejects contradictions for the context already supplied.
Replacement-AST validation uses that AST's header; owned validation rechecks
retained values, option spellings, provenance and traversal events after source
destruction. An explicit address-size event cannot become implicit merely by
changing its owned provenance.

The existing target catalog is the single owner of exact profile introduction
versions. `base::TargetProfile::minimum_ptx_version` exposes the base-owned
`PtxIsaVersion` components, independently of `checker::PtxVersion`.
`supports_map_f64_to_f32` is true only for exact generic SM10/11/12 profiles and
their compute synonyms; recognized other profiles reject that option even when
no version is supplied. This follows the archived target table and legacy option
inheritance, corroborated by CUDA 13.3 `ptxas` rejecting the option on SM13 or
higher. It does not add instruction emulation. Unknown target keys still produce
`UnknownTarget` and receive no guessed minimum or option permission.

The pinned [PTX ISA 9.3 target table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target)
provides the [exact profile minima listed above](#target-profiles), for both
`sm_` and `compute_` prefixes.

Validation rejects missing, extra, ambiguous, or structurally different
function/instruction associations with `ModuleSourceMismatch`. It still accepts
a separately parsed equivalent function body with shifted line numbers and
different target/version. Resolution-significant directives must still match.
Validation rebinds the supplied AST and repeats declaration semantics, including
availability under its replacement source context. Duplicate equivalent
declarations need an unambiguous occurrence match; they are not silently paired
by order. Instruction diagnostics use the IR's original owned ranges, while
directive diagnostics refer to the supplied AST. Missing strict-validation
context is reported as `MissingValidationContext`.

`ResolvedModule::header` owns the effective version, ordered source target
options, and address-size values for a targetless prefix and every subsequent
`.target` region. Each value records `Missing`, `Explicit`, or `Defaulted`
provenance. An omitted `.address_size` owns the PTX-defined 32-bit value with
`Defaulted` provenance; it never depends on the host. Source target order
defines source availability only; it is not a deployment or physical-backend
target. Invalid, duplicate, or inconsistent
header directives are retained as invalid ranges and rejected by owned
validation. `ResolvedFunction::source_region` selects this same owned context.

`ResolvedFunctionAttribute::values` has migrated from source spelling storage
to the typed optional `ResolvedUnifiedId` `unified_id` payload. For
`.attribute(.unified(uuid1, uuid2))`, `unified_id->upper` is UUID `uuid1`
(upper 64 bits) and `unified_id->lower` is UUID `uuid2` (lower 64 bits); no
byte-order or host-address conversion occurs. Function attributes and storage
declarations use this same named value type.
Malformed source UUID tokens remain declaration diagnostics, while AST-free
validation rejects a retained `.unified` attribute without that typed payload.

`resolveInstruction` is generated from the instruction database and dispatches
to per-opcode functions such as `resolveAdd`, each constructing an exact final
semantic-form class. This keeps opcode dispatch out of callers.
`resolveModule` first builds a `SymbolTable`, then constructs an explicit
`ResolveContext` for each function scope. The resulting `ResolvedModule` owns
that table, and each `ResolvedFunction` is identified by its function
`SymbolId`. Each function owns a single `parameter_declarations` table for its
validated `.param` declarations. Filtering by `ParameterDeclarationRole::EntryInput`
selects entry header inputs in source order.
`ResolvedFunction::label_positions` records each function label as
its bound `SymbolId` and a source-order boundary in the recursively flattened
instruction body: labels before the first instruction are at zero, consecutive
labels share a boundary, and a trailing label is at `body.size()`. Standalone
`resolveInstruction` and per-opcode resolvers remain declaration-free for
single-instruction tools. Raw directives and declarations remain in the Syntax
AST/symbol table instead of being copied into Resolved IR as unresolved string
fields; owned, normalized parameter and storage metadata are deliberate exceptions.
The owned symbol table additionally retains source-ordered `.file` declarations,
including exact filename and optional numeric metadata tokens and ranges, after
source/AST destruction. Both validation overloads recheck normalized index
uniqueness, first-symbol association, payload boundaries, declaration coverage,
and basic/inline `.loc` file-reference targets. Replacement source independently
rebinds its debug metadata: a valid replacement filename or index need not match
the original. Bound `.debug_str` identities also validate `.loc` metadata, but
`.file`, `.loc`, `.section`, and `.pragma` add no executable Resolved IR nodes or
instruction attachment.

A present `file_size` is one checked unsigned64 integer token, including zero
(unavailable) and UINT64_MAX. Decimal, leading-zero octal, binary, hexadecimal and
optional `u`/`U` suffixes use the existing checked integer-magnitude decoder;
overflow, signs, floating values, expressions and noninteger leaves reject without
truncation. Its raw text/range and absence remain owned; no decoded-size cache is
added. Current symbol-table sizes and supplied/rebound sizes are checked
independently before optional context gates. Invalid current data cannot be
masked by valid replacement metadata, while independently valid different `.file`
datasets retain debug-neutral source identity. Timestamp's separate `time_t`
domain and existing index, filename, optional-pair, range and reference rules are
unchanged.

Copies, assignments and moves own the complete vector alongside the module table.
Function-only copies do not acquire module advisory metadata. Current records are
intrinsically checked in both validation APIs. Supplied AST projection is checked
independently; its version selects the PTX 2.0 introduction, so valid neutral
replacement may remove pragmas and lower to PTX 1.5 without an old-presence veto.
AST-free availability uses the current module version. Known old versions reject
even without target context; only unknown version defers under `AvailableContext`.
Invalid current data cannot be masked by valid replacement, or vice versa. This
public layout addition requires consumer rebuild. The explicit public
`ptx_syntax_sections.hpp` value dependency supplies `AstSyntax` while `AstModule`
and `AstInstruction` remain incomplete through the module-only header boundary.

Explicit `.file` timestamp/file-size metadata (including `0, 0`) requires PTX ISA
3.2 or later; plain `.file` has no such gate. This presence-based check has no SM
or debug-option requirement and rejects a known older version even without a
recognized target. `AvailableContext` defers an absent version; strict validation
retains its existing completeness requirements. Owned validation uses the retained
header version and file records. AST-backed validation uses the supplied AST's
version and independently rebound file metadata, so replacement metadata and
versions are checked together. See [PTX ISA 9.3 §11.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-file).

`ResolvedFunction::parameter_declarations` owns validated `.param`
declarations: return formals first, input formals next, then body-local declarators
in lexical traversal order (including nested blocks). Each
`ResolvedParameterDeclaration` retains its `symbol_id`, `scope_id`, role
(`EntryInput`, `DeviceInput`, `DeviceReturn`, `BodyLocal`, or `LegacyEntryInput`), fundamental
`scalar_type`, effective byte `alignment`, `explicit_alignment`, `vector_width`,
outer-to-inner `array_extents`, checked `byte_extent`, and optional pointee
properties. Scalar shapes have no array extents; supported unsized device input
arrays retain a null outer axis and checked known inner axes, with no byte extent. The owning function and symbol
table preserve lexical identity even for shadowed local names. Values remain
inspectable after source text and Syntax AST destruction. A non-pointer has no
pointer properties; a pointer with no pointed state space is generic, and omitted
pointee alignment defaults to four bytes. This sole parameter table does not
include `.reg` formals or `.callprototype` signatures. See the
[parameter coverage and migration contract](#parameter-declarations)
for declaration validation, unsupported forms, and version boundaries. None of
these fields describes packed argument offsets or a runtime allocation.

Complete-module direct calls require a prior declaration or definition of the actual
bound device-function SymbolId. The shared source/owned pass uses retained lexical
events and declaration occurrences at exactly one positive target range inside its
call; unknown or ambiguous ordering is rejected. Compatible prior prototypes and
modern self-recursion work. Alias visibility uses the alias identity, not an earlier
canonical target. Copied/cloned owned calls may retarget to another compatible
already-declared function at that event without rejoining diagnostic spelling;
later-only targets and malformed use locations fail. Source association remains
independent. Instruction fragments retain partial-context resolution, while indirect
register/metadata/table calls keep their separate rules.

Call-staging adjacency follows the owned executable instruction sequence. Variable
and debug directives, metadata declarations, bare labels and lexical braces are
transparent. Transfers through a nested block may use the same still-visible outer
object; distinct shadowed declarations and out-of-scope actuals remain invalid.
Every intervening instruction interrupts adjacency, and transfers cannot be
predicated; the call itself may be predicated. Both resolution and AST-free
validation derive the same bound object/member identity from byte-address and
named-array forms, including symbolic indices. Offsets do not change which local
object the call must use. This check does not prove runtime index bounds, byte
initialization coverage or correctness across arbitrary incoming control paths.

The owned symbol table also retains ordered `.loc` events, without creating
executable instructions or function-local duplicate payloads. A common validator
checks both current owned records and independently rebound replacement records.
An `inlined_at` triple must match an earlier **base** triple in module lexical
order; the current base is inserted only after checking its reference. Nested
blocks, function boundaries and target regions do not reset that stream. Physical
ranges check ownership and within-record field order, never event sorting. Actual
function-definition occurrence scopes, not a canonical function's first owned
scope, associate records with bodies. Both datasets must be intrinsically valid;
different valid debug datasets remain compatible with debug-neutral source identity.
Numeric triples compare unsigned magnitudes across radices and suffixes, including
zero line/column and full uint64 values. This is frontend capacity, not a DWARF
width claim. File indices must independently bind to `.file`; function names must
bind to current typed `.debug_str` identities. Basic `.loc` requires PTX 1.0 and
inline attributes require PTX 7.2, with no architecture floor. Absent version may
defer availability under `AvailableContext`, never numeric, identity or precedence
failures. No instruction attachment, decoded line table or string-address execution
is implied.

A private iterative SCC check recomputes current canonical local-definition call
relations on each validation. Direct cycles are syntactic recursion; cycles that
need consumed finite target-set/table alternatives are statically possible
recursion. Both require independently known PTX 2.0 / SM 20 at every participating
caller's selected context; source-aware validation substitutes supplied context
while keeping the current owned graph. An original acyclic AST cannot mask a
coherent current callee edit. Copies and cloned calls retain no stale graph state.
Call identity/ABI/table joins precede the graph, and non-call instruction lanes
are not walked for edge collection. No public graph/cache is retained. Open
external/prototype/dynamic edges preclude whole-program acyclicity claims, and
`AvailableContext` deferral is not an acyclic certificate. This is no CFG or
runtime stack/linker execution model.

Current weak linkage in canonical symbols, storage occurrence records and
function contracts requires known PTX 3.1, independently of target context.
Every Weak symbol, including a symbol-only function, and storage record uses the
common module version guaranteed by the existing equality of all region versions.
Function occurrence contracts also use each exact selected region. Either retained function contract or canonical weak symbol is
sufficient, including unused prototypes. Source-aware validation checks current
facts under supplied context and independently checks every supplied weak
occurrence; weak qualifiers remain nonneutral source identity. Missing version
alone may defer under `AvailableContext`, while strict context and unknown-target
diagnostics remain. Copies retain existing typed facts; no cache-equality,
historical qualifier registry, public layout or linker simulation is added.

Current retained `.branchtargets` and `.calltargets` each require PTX 2.1 / SM 20,
including unused and nested declarations. Their availability reuses the same
minimum-only mechanism below: exact selected-region context when AST-free,
supplied active context for current records and independently supplied typed
nodes when source-aware. Known failing prerequisites reject before full context
is available. Target-list payload remains nonneutral source identity; these
checks add no CFG, execution, public layout or new member/signature policy.

Current retained call prototypes are checked even when unused or nested. Their
base requires PTX 2.1 / SM 20, `.noreturn` requires PTX 6.4 / SM 30, and either
ABI-preservation suffix requires PTX 9.0 / SM 80. AST-free validation uses exactly
the function's validated selected region. Source-aware validation checks current
prototype fields and independently supplied prototype syntax with the supplied
active context; version/target retargeting adds no historical payload veto.
These four conjunctive minimum-only rules evaluate known version and recognized
profile independently: a known failure rejects even if the other fact is absent.
Only unknown prerequisites defer under `AvailableContext`; strict missing-context
checks and unknown-target diagnostics remain. Target events clear the active
recognized profile, and no guessed version, target number or generic partial-DNF
interpretation is introduced. Ordinary function/instruction availability retains
its full-context gates. Counts, variants, identities and signatures retain their
existing contracts; this does not prove physical register preservation.


<a id="resolved-ir-design--named-memory-vector-sources"></a>

### Named memory-vector sources

Ordinary `ld`/`st`, `ld.global.nc`, and `ldu` accept a named ordinary `.reg .v2`
or `.reg .v4` data operand alongside braces. Declaration width must equal arity,
with at most 128 declared bits; memory equal-or-wider element rules remain.
Modern 256-bit vectors, asynchronous stores, atomic/reduction, matrix and texture
operands retain their separate brace restrictions. Scalar bit pack/unpack also
admits named ordinary vectors through its exact aggregate-width policy below.

At the common checker boundary, an effective scalar `Register` operand cannot
carry a whole-vector `ResolvedRegisterRef::vector_width`. An explicit component
remains scalar: its effective width is absent and its component metadata retains
the owning declaration width. Actual `Vector` operands keep their whole base and
ordered lane provenance under their dedicated checks. These rules also apply to
AST-free copies and instruction clones.

`ResolvedRegisterVector::elements` retains the lanes. Its owned `ResolvedVectorSource`
distinguishes `BraceList`/`NamedVector`, saves the full written range, and only for
named form owns the real `WithLocs<ResolvedVectorRegisterRef>` whole base. Ordered
lanes share its SymbolId, parameterized member and declared element type; each
has `NamedProjection` origin, canonical lane and actual declaration width, absent
effective `vector_width` and no selector. All base/lane locations repeat the exact
written identifier range; no `.x` text or selector range is fabricated.

Only a complete valid named container plus an opted descriptor admits projections.
Scalar/brace consumers still reject them. Reference traversal includes wholebase
and each lane; AST-free owned validation rejoins declarations and source ownership.
Actual braces always retain their full range. Legacy standalone manual braces may
omit it, but malformed supplied ranges reject; owned validation requires full
provenance and instruction/lane containment. Named provenance is always mandatory.
Data-source admission is independent of the generic array-address role policy.
No GPU execution or assembler parity is claimed.


<a id="resolved-ir-design--locations-and-primitive-values"></a>

### Locations and primitive values

`AstImmediateKind` retains the lexer's literal classification, including
`BinaryInteger` for `0b`/`0B` spellings. Decimal, octal, binary, and hex
integers, including their optional `U` suffix, first evaluate in the PTX
64-bit signed/unsigned source domain; unary minus preserves that source type
and unsigned negation wraps. Ordinary data uses retain the low target-width
bits. `WARP_SZ` is the source-defined signed integer constant `32`, including
in ordinary instruction-immediate positions; it is not a query of a target's
physical warp width. The generated operand descriptor independently selects
narrowing or
strict target-width representability for each semantic use; a fixed scalar type
expresses provenance only. Generated range, exact-value, and multiple-of
controls compare preserved source bits, while unconstrained controls opt into
strict conversion explicitly. Call literals checked against formal parameter
types and address offsets retain strict target-width representability. A signed
`-0` is numerically zero,
whereas floating negative zero retains its IEEE sign bit. Decimal floats
convert to `F32`, `F64`, and equal-source-width `B64`; a single leading `+`
is normalized at decimal decoding, while a leading `-`, signed zero, and exponent signs retain
their normal floating semantics. Exact-F32 `0f` literals reject parentheses
and both unary signs, including lexer-compacted signed spellings. Exact-F64
`0d` literals admit unary signs through the shared numeric conversion, preserving negative zero.
`0f<8 hex>` and `0d<16 hex>` are raw IEEE bit patterns for `F32` and `F64`
respectively. Scalar data slots that already admit constants also accept
unsigned direct exact-F32 into `B32`, and decimal/exact-F64 literals or
supported binary64 expressions into `B64`. This uses source precision rather
than arbitrary floating conversion: exact-F32 into `B64`, binary64 sources into
`B32`, and floating sources into `B8`/`B16`/`B128` are rejected. Typed immediates
retain their bit destination and raw payload without integer-source provenance;
owned call literal revalidation retains lexical source kind and spelling.
Integer-only controls and register-only slots keep their role requirements.
Exact-F32 operators remain excluded, including unselected branches. Other
floating formats require explicit quantization rules and must not silently take
the integer path.

`ResolvedRegisterRef` owns the complete source spelling and its
`ResolvedRegisterClass`. During module resolution it also stores the
declaration `SymbolId`, optional parameterized-member index, and declared
`ScalarType`, giving named registers such as `%tmp` and `name<count>` members a
stable identity. A numbered-register index remains an optional convenience,
not an identity. A known bound register family requires its semantic member;
current spelling must resolve in the actual declaration scope to that same
`SymbolId` and member. A plain nonfamily reference needs no member. Clearing a
numbered convenience index does not change identity, and coherent current
spelling/member retargeting does not require rewriting retained source identity
or the supplied original AST. An ordinary `.xyzw/.rgba` selection retains this same base
identity and parameterized member, with owned `ResolvedRegisterComponent`
metadata: canonical lane, actual declaration width, base spelling/range, full
component range and written selector spelling/range. Its effective shape is
scalar, so `vector_width` is absent; `declared_type` remains the declaration's
scalar element type. `same_register_storage` compares base/member/lane, making
`V.x` and `V.r` aliases but `V.x` and `V.y` distinct. Scalar and brace
checking require valid `ExplicitSelector` provenance tied to actual operand
locations. `NamedProjection` metadata is not admitted by these source
forms and carries no fabricated written selector. Owned validation rejoins the
base identity, declaration shape/type and source ranges. Ordinary address bases
and named-array register indices accept explicit `.xyzw/.rgba` scalar selections
through this same carrier. For example, `[V.x+4]` retains a byte displacement,
while `A[V.x-1]` retains the array's scalar-element stride and written subtraction.
The array object remains a data identifier. The selected owner must be a real
General `.reg .v2/.v4` declaration, within its existing 128-bit total-width cap;
its scalar element must satisfy the existing integer/bit address role no wider
than 64 bits. Group member identity and lane identity remain independent.
Dynamic alignment, checked scaled memory offsets, and MOV's wider offset domain
are unchanged; no runtime address, index bounds, layout or allocation is inferred.

This applies ordinary scalar-element inheritance to existing address/index
roles, rather than claiming that the manual explicitly illustrates these source
spellings. The public CST/AST variants append the existing member alternative,
so public producers and consumers must rebuild together. Exact written selector
aliases retain different source identities. Coherent current owned alias/lane
edits remain valid without authenticating producer history; malformed current
metadata cannot be hidden by a valid supplied AST. Component leaf ranges must
remain inside their address and instruction, and the selected physical owner
keeps its 128-bit cap, even when target checks defer. `AvailableContext` retains
the existing deferral of full instruction role checks when target context is
missing; it is not complete validation.
Calls admit explicit ordinary selected scalar inputs, returns and indirect register targets. Fabric, opaque-resource and tensor-map
roles retain their separate semantic exclusions despite shared address-child
parsing. `NamedProjection` does not impersonate an explicit scalar selector.
Hardware components remain a distinct registry-backed domain with only its
existing selectors.
The context-free standalone resolver preserves its previous
boundary: it accepts numbered registers and leaves symbol/type fields empty.
Owned-module validation traverses generated operand references structurally,
including register alternatives and nested operand containers. For every bound
register it compares cached scalar type, vector width, and register class with
the owned declaration. A mutated public IR cannot validate by retaining stale
register metadata after its binding changes; operand locations remain the
diagnostic source when available.
For every bound address symbol, the same structural traversal compares the
cached declaration type and guaranteed address alignment exactly with its owned
declaration, including an absent alignment. An operand cannot claim stronger
alignment or retain metadata from a different symbol binding.
An instruction's optional execution predicate is stored as the opcode-level
common field `std::optional<WithLocs<ResolvedPredicate>>`. Module resolution
requires it to bind to a `.pred` register, while standalone resolution accepts
a numbered `%pN` guard. `ResolvedBranchTarget` follows the same two-boundary
rule: module resolution stores the current function label's `SymbolId`, while
standalone resolution retains the source spelling with no symbol identity.

`ResolvedSpecialRegisterRef` retains the exact spelling, a stable
`SpecialRegisterId`, and an optional vector component. It does not store an
effective type that depends on an instruction or target. The independent
special-register semantic registry is the single source of truth for names,
stable identities, current declared element types, vector widths, and intrinsic
minimum PTX/SM targets; binding only reuses it for classification. A scalar
operand accepts a scalar special register or a component such as `%tid.x`, but
not an unselected vector base.

Read forms widened by the ISA are instruction semantics, not properties of the
register itself. The `mov` variant declares the special-register identity,
instruction width, effective type, and minimum PTX/SM in YAML
`operand_type_compatibilities`, which is generated into the checker descriptor.
The checker selects effective metadata only for that check and never mutates
Resolved IR. Current rules allow 16-bit `%tid`/`%ntid`/`%ctaid`/`%nctaid`
component reads from PTX 1.0 and 16/32-bit `%gridid` reads from PTX 1.0/1.3;
other uses retain the registry's current declared type and intrinsic
availability.

`mov.pred` has a separate variant because its destination is a
`ResolvedPredicate` and its source is a `ResolvedPredicateSource`, structurally
unlike the classified scalar source. Module
resolution requires an unnegated `.pred` destination, but accepts a plain or
negated predicate source and retains its negation and stable `SymbolId` value.
The source may instead be a `ResolvedPredicateConstant` holding a canonical
Boolean value, or a `ResolvedPredicateSpecialRegister` retaining both the special
register reference and negation. Integer constants normalize nonzero to true;
an optional `!` then inverts that value. Constants have no synthetic `SymbolId`.
The canonical `pred_source` shape used by SETP accepts predicate registers and
integer constants, including negation; MOV's `pred_or_sreg` additionally accepts
predicate special registers. Neither shape admits floating constants.
Standalone resolution continues to accept numbered predicate registers without
declaration context.

`MovScalar` owns the scalar dynamic type modifier
`.b16/.u16/.s16`, `.b32/.u32/.s32/.f32`, and `.b64/.u64/.s64/.f64`, including
the established `.b16/.b32/.b64` pack and unpack layouts.
`MovB128PackUnpack` is a distinct generated public variant with fixed
`.b128` for vector pack/unpack only. `ResolvedRegisterVector` stores two or
four optional `ResolvedRegisterRef` elements; an empty element is the
destination-only `_` sink. Resolution and checking require a bit-size
instruction type, equal total vector/instruction widths, no source sink, at
least one real destination register, and no sub-byte element.
`MovB128PackUnpack` carries PTX 8.3 / SM 70 availability.

The existing aggregate layouts accept ordinary named `.reg .v2`/`.v4` vectors
alongside braces. Arity comes from the bound declaration, not an implicit MOV
modifier. Named source metadata retains the real whole base and canonical
`NamedProjection` lanes with identical identifier ranges, declaration type,
`SymbolId` and parameterized-member identity; no explicit component selectors or
synthetic brace syntax are introduced. Layout selection uses declaration shape
only at descriptor-authorized aggregate/scalar alternatives. Unknown names and
context-free inputs never imply a named aggregate; existing scalar behavior and
diagnostics remain, including declaration-free scalar symbol-address sources.
Width/type and target errors do not retry another layout. Scalar and modifier-typed ordinary vector MOV keep
their existing layouts. Source form, opcode, type and source identity survive
owned module copy and instruction cloning.

`ResolvedFunctionRef` retains source spelling, a stable function `SymbolId`,
and the `.func`/`.entry` classification. A device-function address uses the
base PTX 1.0 availability of `mov`; a kernel-function address carries the PTX
3.1 / SM 35 requirement for target checking. Only a bare function name is
accepted; an offset form continues through data-symbol address resolution and
is rejected.

`ResolvedSymbolRef` retains source spelling. Module resolution also records the
declaration `SymbolId`, parameterized member, declaration kind, declared state
space, effective address state space, and representable declaration scalar
type. The two state spaces are identical for ordinary data variables. Direct
parameter memory addresses and kernel formal parameters used by `mov` retain
a `.param` address, while taking a device-function formal parameter address
with `mov` materializes it on the stack and produces a `.local` address. A
device-function formal-parameter `mov` address carries a PTX 2.0 / SM 20
baseline; a return-parameter address raises the PTX minimum to 6.0. A
function-local `.param` call-argument variable is a distinct bound symbol:
direct `ld.param`/`st.param` addresses retain `.param`, satisfy either parameter
direction, and require PTX 2.0 / SM 20; it remains non-addressable through `mov`.
Standalone resolution cannot
perform lexical binding, so it leaves identity and state-space fields empty as
it does for branch targets. `ResolvedMovSource` classifies registers,
immediates, special registers, data symbols, and address expressions after
binding, avoiding identifier-shape ambiguity during variant/layout selection.
Standalone resolution cannot tell whether an unbound name denotes data or a
function, so it remains a `ResolvedSymbolRef` with no identity.

A `ResolvedAddress` base is a variant of `ResolvedRegisterRef`,
`ResolvedImmediate`, and `ResolvedSymbolRef`. A bound register base must be an
integer or bit-size declaration no wider than 64 bits: floating declarations
and `.b128` are rejected before checker projection. This preserves PTX address
extension/truncation for narrower integer/bit declarations without treating a
known floating register as an unknown address. Declaration-free standalone
resolution has no type fact and therefore keeps the address base deferred. Its
optional offset retains the add/subtract operator and magnitude. A bracketed
memory address uses PTX's unsigned 32-bit immediate base and signed 32-bit
offset domain after that operator is applied: `-2147483648` is represented by
a subtraction magnitude of `2147483648`. Unbracketed `mov symbol+offset`
retains the separate signed 64-bit addend domain used by symbol-address and
relocation consumers. The shared IR continues to retain offset magnitudes as
signed 64-bit values; the 32-bit rule is a source-form legality check rather
than a relocation-domain narrowing.
A 32/64-bit integer or bit-size `mov d, symbol+offset` uses an unbracketed
address value restricted to an addressable data-symbol or formal-parameter
base. Scalar and braced-vector `ld`/`st` require bracketed dereference and
cover register, immediate, and bound-symbol bases. Each opcode uses
`GenericScalar`, `ExplicitScalar`, `GenericVector`, and `ExplicitVector`
variants. Their runtime type field accepts `.b8/.b16/.b32/.b64/.b128`,
`.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`, and `.f32/.f64`; the selected
`.b128` value requires PTX 8.3 / SM 70, and `.sys` with `.b128` requires PTX
8.4. Vector variants add a required runtime
`.v2/.v4/.v8` field, and the register-vector operand descriptor links its expected
element count to that field rather than duplicating variants per arity. Memory
vectors use element type policy: each register element is checked against the
instruction type, with `EqualOrWider` register width accepted. Legacy payloads
are capped at 128 bits; the generated cross constraint adds only exact 256-bit
`.v8` × 32-bit and `.v4` × 64-bit forms at PTX 8.8/SM 100, global when known,
with partial sinks and load destinations that contain only sinks. The default register-width policy is `SameWidth`, preserving
`mov/add/sub`, immediate, and special-register behavior. `ld` destination
and `st` source register descriptors, together with applicable `cvt` data
operands, select `EqualOrWider`, so the declared
register may be at least as wide as the instruction type. After that size
check, either side being a bit type is compatible, signed/unsigned fundamental
integers are mutually compatible, floats require the exact type/size, and
integer/float combinations remain incompatible. This covers wider load
destinations, store sources (including truncation), and applicable conversion
operands through `.b128` declared bit registers, following PTX ISA 9.3 Tables
27/28. Central declaration semantics classifies `.b128` as fundamental while
rejecting instruction-only declaration formats and register vectors over 128
bits. Declaration eligibility does not bypass instruction, format, PTX-version,
or SM gates: selected `.b128` memory instructions still require PTX 8.3 / SM 70.
The unsuffixed `isspacep` address-source role instead selects `WordOrDoubleword`
with a fixed u64 use type: ordinary B/S/U32 and B/S/U64 registers are legal,
independently of module address size; wider, floating and packed containers remain
excluded. This finite address-role rule does not restrict general data carriers.
Exact `.b128` compatibility remains unchanged for existing `mov` vector consumers.
The focused [LD coverage](#ld-coverage) and [ST coverage](#st-coverage)
documents define the current memory subset.

Explicit loads accept `.const/.global/.local/.param/.shared`, while stores
accept `.global/.local/.param/.shared`. `WithLocs` retains both runtime
state-space/type modifiers and their source ranges. Explicit `.f64` adds SM 13
through modifier-value availability; generic `.f64` needs no separate SM rule
because the generic variant already requires SM 20. Generated operand views
translate a bound symbol's effective address space, rather than its declaration
spelling, and the checker reports `AddressStateSpaceMismatch` when it differs
from the runtime field. Generic operand descriptors carry a static bound-space
allowlist with per-entry availability: loads accept known
`.const/.global/.local/.shared` addresses, with `.const` requiring PTX 3.1,
while stores accept `.global/.local/.shared` and reject known `.const` or
`.param` addresses. Explicit `ld.const` itself remains in the PTX 1.0 basic
explicit baseline.

Legacy cache operators are modeled on the same scalar/vector `ld/st` variants
rather than by duplicating variants per cache spelling. Loads accept
runtime `.ca/.cg/.cs/.lu/.cv`, stores accept runtime `.wb/.cg/.cs/.wt`, and
every explicit cache spelling carries PTX 2.0 / SM 20 modifier-value
availability. When source omits cache, Resolved IR stores
`CacheOperator::Unspecified` with empty `locs`. That sentinel is intentional
provenance metadata, not a claim that PTX lacks an effective hardware default:
the ISA still makes omitted `ld` behave as `.ca` and omitted `st` behave as
`.wb`, while the IR preserves `Unspecified` so omitted source does not masquerade
as an explicit modifier and does not trigger cache-value availability checks.

Memory consistency is a generated cross-modifier descriptor, not a Cartesian
set of `ld/st` variants. `MemoryConsistency::Omitted` (empty `locs`) remains
distinct from explicit `.weak`; `.volatile`, `.relaxed`, `.acquire`, and
`.release` retain their modifier locations. The checker requires scope only
for relaxed/acquire/release, rejects cache with volatile/ordered/mmio forms,
uses known address-space identity without guessing unknown generic addresses,
and applies the global/shared, `volatile.local` PTX 9.1, and scalar
`.mmio.relaxed.sys` rules. A generated `memory_vector` cross constraint detects
arity > 4, payload > 128, or sinks; it requires a 256-bit payload, global space
when known, and PTX 8.8/SM 100. Partial sinks are valid only for those modern
load/store vectors. Ordinary write-only load destinations also accept fully
represented brace tuples containing only sinks, with a selected `.v8` type
`.b32/.s32/.u32/.f32` or `.v4` type `.b64/.s64/.u64/.f64`. The private grant
joins actual owning null lanes, exact selected type/arity, complete brace/lane
ranges, and consistent borrowed projections. It adds no public descriptor flag
and does not infer type from null lanes. All-sink stores, legacy sinks, named
roots, MOV and texture results retain their existing restrictions. The global,
32-byte alignment and PTX 8.8/SM 100 checks still apply. Incomplete-context
`AvailableContext` success retains its existing instruction-check deferral and
is not a feature-availability guarantee. Static natural
alignment covers scalar, legacy `.v2/.v4`, and modern 256-bit total access sizes
when the address is known.

`ResolvedAddress` separately records the enclosing function kind. Generated
address views derive an optional parameter direction only from a bound
`InputParameter`, `ReturnParameter`, or a function-local call argument; they do
not infer it from spelling.
For explicit `.param`, the generated operand constraint requires input
parameters for `ld` and return parameters for `st`. A known wrong direction
reports `ParameterDirectionMismatch` without stacking target diagnostics. A
device-function `ld.param`, and every `st.param`, apply the YAML-provided PTX
2.0 / SM 20 function-availability constraint. A kernel input `ld.param`
retains the explicit-form baseline, while an address with unknown identity is
not assigned a direction. Its known device-function provenance still triggers
the load constraint; an unknown standalone load context does not. This
contextual rule does not change `ResolvedSymbolRef::address_availability`,
which continues to describe address-value semantics such as `mov`.


<a id="resolved-ir-design--checker-contract"></a>

### Checker contract

- membership of every projected dynamic modifier value in the selected
  variant's generated semantic domain. This check is independent of source
  locations and modifier spelling-presence: an omitted optional modifier is
  checked using that field's declared default, while an out-of-domain edited
  value with no provenance remains invalid and uses the instruction range as
  its diagnostic fallback. `ModifierValueDomainMismatch` reports a value
  outside this domain.
- minimum PTX version, SM version, and target family for the variant, selected operand layout, and actual modifier value;
- layout-tag bounds;
- layout-tag and required/forbidden optional-field agreement;
- operand field identity, resolved shape, and immediate or bound-register
  declaration types from structured descriptors.
- special-register intrinsic metadata and contextual type/availability selected
  by the current instruction width; this creates only a temporary checking view
  and does not change Resolved IR.
- static generic state-space allowlists and explicit modifier-derived
  constraints against known effective bound-symbol address spaces, including
  per-entry target availability; unknown register/immediate/standalone bases
  are not inferred.
- explicit `.param` input/return direction and function-context availability
  from the generated operand constraint; direction mismatches take precedence
  over that contextual availability.

Generated vector projections also accept caller-constructed or mutated public
IR without requiring a separate vector-size preverification pass. They retain
the original width/element count in `OperandView::vector_arity` and bound writes
to the fixed-capacity element arrays. The common checker rejects zero or
over-capacity vector counts with `InvalidVectorOperand` before inspecting
elements; supported counts still undergo their instruction-specific checks.
An oversized payload is never narrowed or clamped into a valid vector. This
guarantee covers vector projection sizes, not every possible malformed-IR
invariant or cross-instruction constraint.

`rule_id` is reserved for typed instruction-specific rules. Register visibility
and `.reg` state space are checked during module resolution; the common checker
handles generated address-space constraints, while cross-instruction
constraints remain outside its ABI.


<a id="resolved-ir-design--extension-rules"></a>

### Extension rules

Direct/indirect-call ABI plus function-local call-argument `.param` memory, qualified
`::entry`/`::func` forms, and call adjacency/predication constraints are covered
by module resolution. Scalar `.b128` is rejected by the generated `MovScalar`
type domain; wider `.b128` carriers use the central register declaration rules
and the selected instruction's width policy. Legacy scalar/vector `ld`/`st` cache operators, PTX 8.8 modern memory
vectors, static memory-address alignment, and memory-consistency qualifiers are
covered here.


<a id="resolved-ir-design--conversion-special-register-sources"></a>

### Conversion special-register sources

Ordinary `cvt` (PTX 9.3 section 9.7.9.22) owns `ResolvedCvtSource`, a
register/immediate/special-register union. Scalar read positions retain their
original immediate admission; destinations, vector register sources and
`cvt.pack` keep their existing contracts. Symbol and address values are excluded.
The checker validates intrinsic identity, indexed-family bounds and component
selection before consuming metadata. Presentation spelling does not determine
semantics. Special sources follow the operand's register-width policy, including
integer chops for wider declarations, while integer/float incompatibility remains.
CVT declares the same historical 16-bit thread/grid-component and 16/32-bit
grid-ID read compatibilities as MOV, using source use-width. Intrinsic identity
and current metadata remain unchanged; contextual availability applies per check.


<a id="resolved-ir-design--historical-omitted-floating-controls"></a>

### Historical omitted floating controls

For `1.0 <= PTX < 1.4`, thirteen exact forms preserve omitted source controls:
`div.f32`, `rcp.f32`, `sqrt.f32`, `rsqrt.f32`, `sin.f32`, `cos.f32`, `lg2.f32`,
and `ex2.f32` have `FloatingPointDefault::ApproximateFlushToZero`; `div.f64`,
`rcp.f64`, `sqrt.f64`, and `mad.f64` have `RoundNearestEven`; `rsqrt.f64` has `Approximate`
without FTZ. Their final classes expose `inline static constexpr
floating_point_default` and no source mode/rounding/FTZ members. DIV also omits
`.full`. Resolution never inserts explicit modifiers or rewrites the opcode.

FP32 forms keep the existing all-target rule. FP64 forms need SM >= 13; only
`div.f64`, `rcp.f64`, and `sqrt.f64` additionally accept exact generic `sm_10`, `sm_11`, or
`sm_12` with `map_f64_to_f32`. That option does not admit `rsqrt.f64` or `mad.f64`
on these targets. `RcpImplicitF64` is distinct from `RcpRnF64`; its RN default is a static interpretation, never a fabricated written control. Native SM13 requires a PTX1.2+ source header, while mapped SM10/11 permit PTX1.0 and SM12 requires PTX1.2. The historical CUDA 13.3.33 assembler discrepancy is evidence rather than a normative override of the pinned manual. Existing implicit `mad.f32` has its own contract.

Availability metadata uses optional `ptx_before: "1.4"` as an exclusive bound,
in plain requirements and each DNF clause. Missing bounds remain unbounded;
malformed or empty/reversed intervals are rejected. Plain upper-bound failures
report located `UnsupportedPtxVersion`; DNF failures retain
`UnsupportedAvailability`. Selection is version-independent; validation uses the
current standalone context or current source/owned/replacement module version.
Copies and clones retain the omitted identity and interpretation. Revalidation
at PTX 1.4 or later fails without converting the instruction to an explicit form.

<a id="resolved-ir-design--wmma-source-windows"></a>

### WMMA source windows

WMMA source spellings preserve the PTX 6.3 `.aligned` boundary. The 156
FP16-input / FP16-or-FP32 accumulator forms introduced in PTX 6.0 or 6.1 have
separate `*_implicit_aligned` variants and `*ImplicitAligned` final classes:
72 loads, 36 stores, and 48 multiply-accumulates. They retain their original
PTX floor and SM70 requirement and require PTX `< 6.3`. The existing modern
classes require an explicit `.aligned` and PTX `>= 6.3` (or their later native
floor). Historical classes have no `aligned` field, alias entry, default, or
fabricated source range. Both spellings retain immutable `MatrixFamily::WMMA_LOAD`,
`WMMA_STORE`, or `WMMA_MMA` topology: these families imply uniform execution by
the participating warp. This execution requirement is distinct from byte address
alignment; fragment, layout, address, and stride checks remain unchanged.

Standalone resolution selects the written spelling without a version context.
Module validation and instruction availability checks apply the half-open version
interval using current owned or replacement context. No historical counterparts
are added for `shared::cta`, integer, subbyte/bit, BF16, TF32, FP64, or other
forms introduced in PTX 6.3 or later. The source reference is the
[PTX 9.3 WMMA description shipped with CUDA 13.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-ld).

<a id="resolved-ir-design--wmma-satfinite-window"></a>

### WMMA satfinite window

The 96 existing FP16-input WMMA MMA forms (48 explicit `.aligned` forms and
48 implicit counterparts) expose optional Boolean `satfinite`, default `false`.
The selected `.satfinite` value requires PTX `< 6.5`, intersected with the form's
native floor and alignment interval: implicit forms use PTX 6.0/6.1 through 6.2,
explicit forms use PTX 6.3 through 6.4, all on SM70 or later. NVIDIA deprecated
floating `.satfinite` in PTX 6.4 and removed it in PTX 6.5. Omission retains each
form's existing availability, including modern PTX versions. Saturation maps
positive/negative infinity to the largest positive/negative finite value and NaN
to positive zero. Typed `satfinite.value` and source locations survive owned
resolution, copying and cloning; current context checks reject removal-era use
without changing the form identity. Integer saturation and BF16, TF32, FP64 and
other MMA families are unchanged. These are syntax and static validation
contracts; numerical GPU execution is not validated. See the
[CUDA 13.3 PTX 9.3 WMMA MMA reference](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-mma).

<a id="resolved-ir-design--owned-parameter-shapes"></a>

### Owned parameter shapes

Header numeric parameter records retain every ordered `array_extents` axis and
checked `byte_extent`. Their own signature axes must match exactly, while ABI
comparison across declarations/calls uses total count. Name-only
`ResolvedOpaqueEntryParameter::array_extents` replaces its old `is_array` and
`array_extent`; it retains sized axes without assigning opaque bytes. Function
copies own these vectors and clone body instructions independently. See the
[public parameter migration](#parameter-declarations).

<a id="resolved-ir-design--scalar-vector-leaves"></a>

### Scalar register-vector leaves

Ordinary register-vector operands and fixed-cardinality vector/tensor tuples
contain scalar register leaves. This role is independent of whether arity comes
from a modifier, an allowed-arity list, or fixed tuple bounds, and applies to both
element and aggregate policies. An owned whole-vector register cannot replace
one leaf even when its element dtype matches. Checking reports the offending
lane range, with the operand/instruction range as fallback. Explicit scalar
components and genuine named-vector projections retain absent effective vector
width and remain legal under their existing source contracts. The enclosing
named MOV/memory vector root keeps its own whole-vector shape; that root is not
a scalar leaf. Value-vector sources retain their existing scalar-register and
immediate/provenance checks. This changes no cardinality, dtype/carrier policy,
named-source permission or immediate range.

[Block-scaled MMA scale selectors](#mma-scale-selector-coverage) use
U16 logical values with closed B16/S16/U16 register carriers. Their two scalar
leaves, byte masks, separate A/B thread limits and unsigned literal conversion
remain independent of whole-vector shape and runtime register contents.

<a id="resolved-ir-design--named-data-vectors"></a>

### Named data vectors

Selected multimem data vectors, SULD destinations, and root `st.async` v2/v4 sources accept ordinary named register vectors alongside braces. A sole fixed Element arity is taken from the binding when no vector modifier supplies it. The existing whole-base identity and canonical `NamedProjection` lanes remain owned together. MM/SULD require matching element widths; root `st.async` retains its wider-carrier rule. Every named declaration remains v2/v4 and at most 128 bits; MM v8 remains brace-only. SUST value-vector sources use the source contract below.

`ResolvedValueVector` now owns `ResolvedVectorSource source` alongside ordered `RegOrImm` elements. Its default remains BraceList with no whole base; complete brace sources preserve mixed values, explicit components, and the full brace range. The 78 opted SUST v2/v4 bindings also accept ordinary named declarations. A named source has exactly one whole declaration authority in `source.whole_base`; every lane is a canonical General-register `NamedProjection` derived from that real SymbolId/member/type/width and the complete identifier range. Immediates, sinks, special registers, and independent scalar projections cannot form a named source. `valid_value_vector_source` reuses register-vector provenance validation; legacy standalone empty brace ranges remain compatible unless complete provenance is requested. Owned module validation requires the complete source and rejoins the base and each lane after syntax destruction.

Public ABI migration: rebuild consumers of `ResolvedValueVector` and `checker::OperandView`. The latter adds a borrowed `const ResolvedValueVector* value_vector`, valid only during the synchronous check and pointing to the actual owned payload. Generated projections verify arity, lane kinds, and register/immediate pointers against that payload before named permission or type checks. Copy, equality, assignment, and instruction clone include the owned source; `IReferenceObserver::value_vector` keeps its signature. Other value-vector permissions stay default false, without a blanket claim that every such ISA role forbids named syntax. No RegOrImm domain or numeric conversion rule changed.

<a id="resolved-ir-design--mbarrier-token-policies"></a>

### mbarrier token form policies

`checker::OperandDescriptor::mbarrier_state_token_form` is now `std::optional<MbarrierStateTokenForm>` with an absent default, replacing the former unconditional Register default. Rebuild public consumers for this member type/default migration. Absence means a non-token descriptor; all 93 generated typed-token bindings explicitly engage their existing Register, RegisterOrSink, or Sink policy, including Register inputs. The existing optional register payload and observer signature are unchanged. The private common checker reconciles actual Register shape, sink flag, borrowed reference, and optional declared-type cache before enforcing presence policy. Sink-capable policies require write destinations. Other scalar type/width/component/declaration and availability rules remain independent; unknown declaration type is not made globally mandatory. Generic register/predicate/SHFL sinks with no token policy retain their existing rules. Caller-supplied absence chooses non-token checking and does not authenticate a token. Owned copies and clones enforce the same form after source destruction; neither this policy nor a B64 carrier proves runtime phase contents, progress, or token authenticity.

<a id="resolved-ir-design--general-register-views"></a>

### General register views

A scalar `Register` view claims a General register. The common checker rejects a known non-General class in either the actual borrowed reference or its class cache, and rejects known predicate type facts under that claim. The same rule applies to scalar Register leaves after valid register-vector cardinality is established, with lane-range diagnostics. Genuine named-vector whole bases are not scalar leaves; General explicit components and canonical `NamedProjection` lanes retain their existing rules. Predicate and special-register views keep their own shape contracts. Unknown General types and legacy views without actual facts still defer to existing checks: this rule adds no required SymbolId or declared type, universal type-cache equality, or general instruction-only declaration eligibility check. Contextualized numeric types remain governed by their existing conversion rules. Class validation does not prove runtime register contents.

<a id="resolved-ir-design--mbarrier-carriers-and-reports"></a>

### mbarrier carriers and reports

Mbarrier register carrier typing is role-specific even though the written instruction modifier remains `.b64`. Opaque arrival/arrival-drop outputs and the opaque `pending_count` state input accept fundamental B64/S64/U64/F64 declarations of exactly 64 bits. Non-parity test/try-wait state inputs have semantic U64 type and accept B64/S64/U64, rejecting F64 and every other width. The frontend retains each actual declared type, SymbolId, member/component, and range; it performs no numeric floating conversion and proves no runtime token contents or prior operation. The aligned shared-memory `.b64` object is distinct from the register carrier. CTA RegisterOrSink and cluster mandatory Sink policies and availability remain independent.

Report-value outputs retain the frontend's qualified logical B8 model, with same-size B8/S8/U8 carriers. The pinned PTX 9.3 text does not unambiguously map this heterogeneous report operand to a normative width/type; later official uint8 examples corroborate the retained model but do not close that fixed-reference gap. Report carrier tests establish this retained model, not unconditional ISA-width certification. Opaque F64 acceptance follows the adopted fixed-reference type interpretation; supplementary assembler rejection is documented tool divergence, not a new frontend type restriction.

<a id="resolved-ir-design--scalar-f16-numeric-sources"></a>

### Scalar F16 numeric sources

Scalar F16 numeric constants use IEEE binary16 conversion at the use site. Decimal and evaluated floating expressions, exact F64 bits, and bare unsigned exact F32 literals round once to nearest, ties to even. Binary64 bits convert directly to binary16; an exact F32 input widens exactly first. Signed zero, gradual subnormals, overflow to signed infinity and quiet NaNs with retained sign/high payload follow the deterministic frontend literal policy, independently of instruction rounding, FTZ or saturation. This does not simulate a CVT result. Integer-origin values remain distinct from floating constants. Exact F32 signs, parentheses and operators remain rejected; Bit16 does not inherit numeric half conversion, and F16/F16x2 data initializers remain forbidden.

Twenty fields in eleven existing generated classes migrate from `WithLocs<ResolvedRegisterRef>` to `WithLocs<RegOrImm>`; rebuild public consumers and use `std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>` on `.value`. Variant names, shared F16/BF16 add/sub domains, selector order and all width/availability rules are unchanged.

| Generated class | Migrated fields |
| --- | --- |
| `AddMixedF32`, `SubMixedF32` | `src` each |
| `FmaMixedF32F16` | `src1`, `src2` |
| `SetHalfF16F16`, `SetHalfF16F16Boolean` | `src1`, `src2` each |
| `SetHalfBf16F16`, `SetHalfBf16F16Boolean` | `src1`, `src2` each |
| `SetHalfIntegerF16`, `SetHalfIntegerF16Boolean` | `src1`, `src2` each |
| `SetpF16`, `SetpF16Boolean` | `src1`, `src2` each |

A canonical current F16/BF16 immediate is a low16 word with no integer-source provenance or integer-negative flag. Ordinary instruction IR retains the current typed word, so a coherent in-range word edit can be valid without recovering the original literal. Shared mixed BF16 fields accept numeric BF16 sources and canonical typed BF16 words under the scalar conversion contract below. Calls retain the original lexical input or preconversion F64 expression value and recompute the half word during owned validation. Every otherwise eligible scalar Reg/Param input uses that common conversion; modern narrow Register-formal ABI restrictions, vector no-splat and missing-context requirements remain independent. The four scalar F16 SETP sources also admit the explicit raw Bit16 route specified in [scalar Bit16 sources](#resolved-ir-design--explicit-scalar-bit16-sources); raw integers and numeric floating values retain distinct origin/type rules. Packed numeric construction and additional source permissions remain separate scope.

<a id="resolved-ir-design--cvt-pack-integer-sources"></a>

### CVT pack integer sources

The ordinary `cvt.pack.sat` S32 inputs accept integer constants and expressions as well as same-width B32/S32/U32 registers. The existing 64-bit evaluator narrows each input to its low32 S32 word and retains original64-bit integer bits/sign metadata; `0x100000001U` therefore has use word1 without a target-range requirement. The frontend does not saturate inputs to the destination lane width or precompute the packed result. Floating-valued inputs remain incompatible with S32; an expression whose final category is Integer, including an FP comparison, follows the integer route. Independent B32 carry sources keep their existing literal/precision contract, including same-width unsigned direct0f bit data; this does not admit literals in register-only stochastic `rbits`. Carry topology, destination widths and target boundaries remain unchanged.

Public ABI migration: `src1` and `src2` in `CvtPackSatU8S32B32`, `CvtPackSat16S32` and `CvtPackSatSmallS32B32` change from `WithLocs<ResolvedRegisterRef>` to `WithLocs<RegOrImm>`. Rebuild consumers and inspect `.value` with `std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`. Six fields change in place; no class, modifier domain or selector order changes. Copied/assigned/cloned instructions retain typed words, source caches, declaration identities and locations. Ordinary current IR word/reference edits are checked against current facts; source association remains a separate retained-syntax check.

<a id="resolved-ir-design--stochastic-value-vectors"></a>

### Stochastic conversion value vectors

The three stochastic quad classes `CvtRsSatfiniteF8x4F32`, `CvtRsSatfiniteF4x4F32`, and `CvtRsSatfiniteF6x4F32` expose `src.value` as `ResolvedValueVector`, replacing `ResolvedRegisterVector`; consumers must rebuild and inspect each `RegOrImm` alternative. Brace sources retain the complete written range and lane order while numeric floating sources use the common F32 conversion. Integer-origin sources remain incompatible, and exact F32 constants retain the bare unsigned literal rule. This conversion computes source F32 words only; it does not execute stochastic conversion, saturation, or the packed result. Named V4 F32/B32 sources use `source.whole_base` as the sole declaration authority and canonical `NamedProjection` lanes, with the existing General class, arity, binding, and 128-bit declaration limits. Copies and clones own both provenance and lanes. A borrowed `OperandView.value_vector` must point to the actual payload and match its lane pointers, types, ranges, and descriptor permission; default-false named permission remains effective. The destination carrier policy and independent register-only `rbits` rule, including its widening policy, remain unchanged.

<a id="resolved-ir-design--physical-register-eligibility"></a>

### Physical register declaration eligibility

Ordinary register declaration eligibility uses the actual physical declaration type and the central `ScalarTypeMetadata::register_declaration_usage` classification. Known General-register reference types must be Fundamental, including when reached through explicit components, named projections, or vector register leaves; absent standalone declaration facts retain their existing deferral. Logical BF16/TF32/packed instruction formats and canonical typed immediate words are independent of physical register declarations. Contextual operand type caches do not replace the actual declaration type. Owned validation separately checks every concrete ordinary `SymbolKind::Variable` in Register state space, including unused declarations; missing or unrecognized concrete types cannot establish eligibility. Predicate declarations and native scalar F16x2 remain Fundamental. Retained source identity records historical syntax association and does not exempt a current mutated binding table from declaration legality. Copies must validate within a module that owns the relevant symbol table.


<a id="resolved-ir-design--owning-debug-section-values"></a>

### Owning debug section values

`ResolvedModule.sections` owns source-ordered section fragments. The public
`ResolvedDebugSection` alias reuses `syntax_ast::AstSectionDirective` and its
small value-only variants: every string/range is copied, without AST/CST/source
borrows or numeric caches. Default module copy/assignment/move/swap therefore
include nonempty integer and relocation payloads. This public CST/AST/Module
migration changes source and binary layout; consumers must rebuild.

A shared checker decodes already-tokenized integer magnitudes and applies original
B8/B16/B32/B64 intervals `[-2^(w-1), 2^w-1]`. Full UINT64_MAX and negative 2^63
survive; U suffixes do not relax ranges. The manual's `.b16 -65535` example
conflicts with its explicit interval and is rejected in favor of that interval.
B32/B64 symbolic data retains unresolved single addresses, signed byte addends
(including checked `label-integer` shorthand), and differences of unique known
labels/section bases in one logical section. Equal section names join fragments;
linked addresses and relocation results remain unknown. Strings, arbitrary
expressions and mixed symbol/integer lists are not inferred productions.

Known PTX gates are section 2.0, label addend 3.2, B16 data 6.0, label definition
7.2, label difference and negative values 7.5; minus zero is mathematically zero.
Missing context follows existing validation policies. AST-free checks revalidate
current owning records; source-aware checks independently validate both current
and supplied section values with the supplied effective context and rebind its
debug labels. Valid changed section data does not cause executable source-identity
mismatch. Invalid current data cannot be hidden by valid replacement sections.
This checks the bounded source grammar and static locality, not DWARF decoding,
linked byte values, target-debug DWARF recognition or GPU execution.


<a id="resolved-ir-design--target-debug-absence-and-recognition-boundary"></a>

### Target debug absence and recognition boundary

The file-wide `.target debug` requirement applies if any target directive selects
`debug`, including earlier or later regions. Known `debug` with zero owning modern section records AND zero legacy records
is a determinate violation, even under `AvailableContext` with
missing version information. `.file`, `.loc`, or retained debug-string symbol
identities cannot replace serialized debug records. The existing PTX 3.0 option gate and complete
context requirements remain independent. Source-aware validation applies the same
private absence rule separately to current owned options/modern sections/legacy records and supplied AST
options/modern sections/legacy records; neither dataset can conceal the other's definite absence or
invalid typed payload. Valid differing datasets retain section-neutral source
association. No presence cache, recognition enum or decoder is added.

**D-DEBUG-DWARF-PRESENCE-RECOGNITION remains a reference boundary, not a code PASS.**
The pinned [target clause](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target)
and [section grammar](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-section)
do not specify a sufficient recognition criterion for arbitrary names, empty
sections, label-only sections or nonempty typed data. A `.debug_` prefix, whitelist,
nonempty payload or mandatory `.debug_info`/`.debug_abbrev` pair would invent such a
criterion. The paired witness is one structural control, not a universal minimum
or a decoder. Validation success for nonzero structurally valid section or legacy records
therefore does not certify that actual DWARF information was found. The finite
debug-off/on boundary tests record this qualification explicitly; full recognition
and linked/binary authenticity are not claimed.

Current owned `.language` values are context-independent intrinsic data: an
absent optional denotes omission/default `ptx`; an engaged vector must be nonempty
and every raw token must satisfy the shared public semantic decoder. Multiple
values and duplicates retain exact order and spelling. Both module validation
APIs recheck current values at the owning function range, even without version or
target; legal supplied source cannot hide invalid current words. Valid current
name/code/list edits or optional removal do not require historical equality.
Replacement AST identity remains raw and ordered: changing `"ptx"` to `3` or
reordering a valid list can still produce the existing source-association
mismatch, while ordered decoded aliases remain compatible for redeclarations.
Presence requires PTX 9.3 on all targets; a known old selected/supplied version
rejects even with no target, and only missing version may defer availability under
`AvailableContext`. Domain checking never defers; strict context requirements stay
unchanged. The new decoder is an additive exported semantic function; clients
must use the matching installed header/library. No payload layout or backend
language execution guarantee is added.

Owned entry-input `.ptr` availability is derived from the existing parameter
record and checked against PTX 2.2 before requiring complete target context.
A coherent version retarget updates the selected region and function metadata;
copies and instruction clones preserve the same check after AST destruction.
Source-aware validation instead applies its supplied effective version to current
pointer presence, without treating the historical cached version as a veto.
Existing pointer type, shape, alignment and pointee-space checks remain separate;
the availability diagnostic uses the established function-range fallback.

Parameter modifier metadata uses the exact typed identity
`(MemoryStateSpace::Parameter, ParameterAddressQualifier)`. The domain and
availability descriptors append a `parameter_qualifier` member defaulting to
`Default`; `ModifierValueView` appends an optional member with an engaged
`Default`. Existing positional aggregate initializers keep their meaning, but
these public layouts change ABI: rebuild consumers with matching headers and
libraries. An unknown projection (`nullopt`), invalid enum, or qualified
non-Parameter space rejects independently of target and spelling presence.
A non-default Parameter qualifier is semantically present even after locations
are cleared. Plain Parameter therefore cannot select an 8.3 qualified row.

Generated views use the current direct address or selected direct symbol tied
to the same normalized modifier binding. Nested symbol defaults do not supply
a second written qualifier. Fixed forms use their unique canonical identity;
register alternatives need no qualifier payload. Associated faithful written
qualifiers must agree. The existing typed parameter-address preservation policy
can derive an Entry frame qualifier from a fixed plain modifier; that frame
fact is validated separately and does not change the fixed modifier identity.
Existing frame, readonly, declaration, source identity, and Shared/atomic
qualifier guards continue to apply.

Current-owned Parameter address checks run before missing version/target context
can skip instruction validation, in both `validateModule` overloads. Exact
variant/layout operand descriptors provide closed address-space domains and
current associated state-space equality. Empty domains stay unconstrained;
unknown runtime register pointees are not inferred. Device formal address
materialization still uses its existing policy and may produce Local space.
Known entry inputs cannot supply an explicitly writable operand or a Return
parameter operand. `OperandAccess::Read` describes operand value access and
never classifies memory effects; MOV address acquisition and tensor-map reads
remain legal.

`detail::IReferenceObserver::operand_context` is an appended default no-op
virtual callback. A nonnull descriptor brackets the immediately following
address-bearing top-level payload callback; nullptr resets the context.
Descriptors and spans are borrowed synchronously. Collectors copy needed
scalar/domain facts and retain no borrowed descriptor. Existing payload
callbacks and their order stay unchanged. Existing derived source observers
need no new override, but the added virtual slot changes ABI: rebuild observers
and producer libraries against matching headers.


<a id="resolved-ir-design--historical-entry-body-inputs"></a>

### Historical entry body inputs

PTX 1.0–1.3 entry body `.param` declarations have the owning `LegacyEntryInput`
role in the sole `parameter_declarations` table, including scoped/group/array
identities. The function signature remains syntactic header ABI only, so an empty
header signature does not imply no host inputs. Lexical metadata order assigns
no launch addresses or packing. Resolution needs a version only for ambiguous
entry body roles; it reports `MissingValidationContext` without guessing from SM.
Typed modules retain their role when context is missing.

The optional `ResolvedSymbolRef::parameter_role` cache is derived from the actual
record while keeping structural `CallParameter`. Address/MOV/tensor projections
share input/return/call-slot directions. Both validators rejoin current records
and caches and enforce read-only entry-frame access, direct call-actual exclusion,
shape/scope and logical capacity before context skips. Legacy input LD is not
call-return staging; MOV preserves `.param`. Modern BodyLocal staging is unchanged.
The checked sum of per-member bytes times compact count has the known32764-byte
global ceiling; no double axes/vector multiplication or invented256 legacy cap.
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` remains qualified for exact historical
physical launch packing. See the [parameter contract](#parameter-declarations)
for context windows, intrinsic guarantees and preserved limitations.


The shared current operand-domain pass classifies both actual `EntryInput` and
`LegacyEntryInput` records as read-only entry objects. It uses current typed
layout/field requirements before context skips, including non-LD/ST consumers;
MOV address acquisition stays legal, while an explicit writable operand rejects.
Legacy readonly diagnostics use their original label and instruction range;
only identical local causes merge. No opcode-specific writer classification or
new historical instruction permission is inferred.


<a id="resolved-ir-design--exclusive-numeric-sm-availability-bound"></a>

### Exclusive numeric SM availability bound

Plain availability and each `any_of` clause accept optional `sm_before: 70`. Together with `sm: 30` this means `30 <= SM < 70`; a missing lower bound means zero and an omitted upper bound means unbounded. The field must be a strict uint32 integer greater than the minimum; zero, booleans, floats, strings, explicit null, overflow and empty/reversed intervals are rejected by typed normalization and rendering. JSON Schema applies integer/range constraints; the typed boundary also rejects integral floating values. The existing maximum of six ordered alternatives and prohibition on mixing `any_of` with plain fields remain. No target intersection solver is introduced.

`AvailabilityClause::sm_before` and `AvailabilityDescriptor::sm_before` append disengaged `std::optional<uint32_t>` members after `ptx_before`; rebuild public consumers for this aggregate layout change. The predicate compares concrete `TargetInfo::sm_version`, independently of profile spelling or flavor, while all existing exact-target, family, capability and mapping requirements remain conjunctive. Plain failures produce located `UnsupportedSmVersion` in variant, layout, modifier, operand-value, parameter-address and memory-vector paths; DNF failures retain `UnsupportedAvailability`.

Legacy SHFL uses `any_of: [{ptx: "3.0", ptx_before: "6.4", sm: 30}, {ptx: "3.0", sm: 30, sm_before: 70}]`. Source modules still require recognized profiles and legal profile/version headers. Under `AvailableContext`, missing version or recognized target continues to defer whole-instruction availability, even if another known fact appears incompatible; such success is not full ISA certification. Strict validation reports `MissingValidationContext`; an explicit unknown profile remains `UnknownTarget`. No partial DNF evaluator or opcode-specific context rule is added.


<a id="resolved-ir-design--historical-101-source-profiles-and-validation-features"></a>

### Historical 101 source profiles and validation features

`sm_101`/`compute_101` and their A forms begin at PTX8.6; F forms begin at PTX8.8. They remain admitted at PTX9.0/9.3, with no rename-imposed upper window. The corresponding 110 source profiles still begin at PTX9.0. `TargetIdentity` retains architecture101, flavor and exact written spelling. `same_target_identity` compares source architecture/flavor, so101A and110A remain distinct.

The additive base function `target_feature_architecture(identity)` consults explicit private catalog data: the three101 keys project to feature architecture110; other keys retain their architecture. Uncataloged typed keys do not become recognized. Public `TargetProfile` and `TargetInfo` layouts are unchanged. Source/owned/semantic factories use this feature level for numeric SM, retain source identity, and pass the existing110 baseline capabilities `cluster`, `aggregate_smem`, `reserved_smem`, `graph_exec`. Generic101 has no family; 101A/F enable only canonical `sm_110f`, with no100f/103f inheritance or FP64 mapping option. This bounded correspondence follows the published rename and family relation; it is not a physical hardware/driver guarantee.

Availability singular and span exact-target requirements compare validation-feature architecture plus exact flavor through that helper. This matching relation differs from source identity and stored-key equality. A manually supplied `TargetInfo::sm_version` remains the caller's concrete feature fact and is never overwritten by the checker. Source profile introductions, instruction/PTX floors, capabilities and shape constraints remain independent.

Historical instruction data changes only the adopted231 parents:219 preserve existing100A clauses and add101A, with101F only where an original110F branch exists;12 block-selector parents append an8.8 historical101A/F clause. All854 original110 conditions and their floors remain, the DNF limit stays six, and the25 restricted cluster gather4/im2col-W parents remain unchanged. TCGEN closed rows retain kind, scaled-D and explicit scale-vector restrictions: written block16/32 historical gates require8.8, while100-only vector and scaled-D paths remain100-only. Generated operational queries first validate the exact source profile and its minimum version, then apply feature matching. Unknown words and addresses remain unresolved obligations, not facts inferred from live registers.

Both module validators independently join current function `source_region`, `source_version` and exact `source_target` to the current owned region. A valid replacement AST cannot mask contradictory caches. Intrinsically coherent101/110 contexts may be retargeted through a supplied header when the current payload is legal under that effective context; retained availability is not a historical veto. Missing full instruction context still follows the existing `AvailableContext` deferral and strict `MissingValidationContext` policy.


<a id="resolved-ir-design--deprecated-maximum-cta-hint"></a>

### Deprecated maximum-CTA hint

Entry headers accept `.maxnctapersm ncta` from PTX ISA 1.3 on every recognized
source target, including PTX 2.0 and later. Deprecation does not impose an upper
version bound. The distinct `MaxNctaPerSm` AST/owned kind is an optimization hint:
additional CTAs may still execute, so it is not a hard runtime occupancy cap.
`.maxntid` is optional, and `.minnctapersm` may coexist as a separate resource.

One complete unsigned integer token is checked before projection into the existing
`uint32_t` resource value, including zero and `UINT32_MAX`; overflowing values do
not wrap. This is the frontend's modeled resource capacity. The published grammar
does not specify this bit width, and validation does not prove hardware occupancy
or driver feasibility. CST/AST leaves retain written radix/suffix/ranges while
alive; owning contracts retain only kind, normalized values, explicit count and
range, with no parallel raw spelling field.

Both validators check current payload shape, entry placement and known range
containment. Known PTX context below 1.3 rejects even without a complete target;
missing version defers availability under `AvailableContext` while intrinsic
checks continue. The source-aware route uses the supplied effective version.
Coherent current numeric edits remain valid; changing written resource kind or
value in a replacement AST retains the existing source-association rule.

Source reference: [PTX ISA maximum-CTA directive](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#performance-tuning-directives-maxnctapersm).


<a id="resolved-ir-design--legacy-debug-serialization"></a>

### Legacy debug serialization

The case-sensitive `@@DWARF` marker introduces one closed production at module,
function-body or nested-block statement scope. It is supported for PTX ISA
**[1.2, 2.0)** on every recognized source target. Function headers do not admit it.
The forms are `.byte` unsigned hexadecimal lists (0..255), `.4byte` lists
(0..UINT32_MAX) and `.quad` lists (0..UINT64_MAX). Lists are nonempty. The two wider
forms also admit one ordinary or singly dotted symbolic label, retained unlinked;
there is no ordinary PTX symbol-resolution requirement for that label.

`0x`/`0X` with optional `U`/`u` follows existing PTX integer token inheritance;
this suffix choice is derived rather than explicit wording of the legacy syntax.
Signs, decimal/octal/binary values, mixed symbol/numeric lists, addends,
differences, label definitions and arbitrary host assembly are outside this model.
The published example's header is supported as `.section name, "", @progbits`.
Generalizing its name to an ordinary or singly dotted identifier is derived from
the example; flags remain exactly empty and the type remains exactly `progbits`.

Whitespace, comments and newlines separate tokens without ending a record. One
structurally complete payload and an optional semicolon end it. The next token is
handled by ordinary PTX grammar; malformed records preserve following markers,
blocks and functions through structural recovery. Lossless CST tokens retain
comments and source text. Owning AST values retain exact marker/word/header
spellings and within-record ranges, including separate `@` and `progbits` leaves.

`ptx_syntax_legacy_dwarf.hpp` contains only small owning values. It does not expose
module, instruction or expression AST definitions. One shared `AstLegacyDwarfRecord`
is used in module/body variants, and `ResolvedModule::legacy_dwarf_records` is the
sole ordered stream of `{value, scope_id}` wrappers. Actual declaration-occurrence
and block scopes come from the module's symbol table; a canonical function's
preferred owned scope does not replace its definition occurrence. Metadata creates
no executable node, label slot or instruction-provenance index. Module and function
source identities exclude it while ordinary program constructs remain relevant.

Both current and supplied datasets validate their own typed words, ranges, scope
ownership and format windows. Source-aware validation uses the supplied effective
version for both datasets; AST-free validation uses current module context. Known
out-of-window versions reject without a complete target. Missing version under
`AvailableContext` defers only the format window; intrinsic checks continue and
strict missing-context checks remain. Coherent current metadata edits and differing
valid datasets do not require old source-byte equality. Cross-format replacement
requires removal or reconstruction of current incompatible format data.

For `.target debug`, definite absence means **zero modern sections AND zero legacy
records**, independently in each dataset. An unknown linked label, a header or
nonempty bytes only avoid this definite-absence result. They do not certify actual
DWARF recognition, decoding, linked addresses or binary authenticity. A known full
legacy context cannot combine the pre-2.0 format with the PTX-3.0 debug option;
missing-version tests are partial-context controls only.

References: [legacy debug marker](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-atatdwarf)
and [legacy-format window](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives).


<a id="resolved-ir-design--indirect-function-address-carriers"></a>

### Indirect function-address carriers

An indirect function address uses a General `.reg` effective scalar with a
fundamental signed, unsigned or bit type of 8, 16, 32 or 64 bits. Floating types,
packed/alternate instruction-only types, `.b128`, predicates, special registers
and whole vectors do not satisfy this role. A selected ordinary component uses
the same element-type rule and retains its real owner identity, selector ranges
and the 128-bit vector-declaration limit. Written call components use the same invariant as typed public register-component values.

This is a **derived** address-carrier interpretation of
[PTX §6.4.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#addresses-as-operands)
and the function-address-register requirement in
[§9.7.13.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-call),
not a claim that `call` explicitly enumerates all twelve carrier types. Address
value widths of 32/64 bits do not impose a 32-bit register minimum or require a
64-bit register under `.address_size 64`. Eligibility does not prove that a
narrow register actually contains a representable function address, a linked
code address or a member of a runtime target set.

Source resolution, direct typed checks and both module validation APIs apply
the role before missing version/target can defer it. Owned checks rejoin actual
symbol declarations and reject lying or erased type caches; valid supplied
source cannot mask bad current data. Generated views borrow the active RegRef
alternative without reinterpreting metadata/table alternatives. Completely
unknown standalone facts retain their existing unknown/context behavior.
Formal input/return ABI rules and ordinary floating call inputs remain separate.


<a id="resolved-ir-design--selected-scalar-call-operands"></a>

### Selected scalar call operands

Call inputs and the single parenthesized return accept ordinary `.xyzw/.rgba`
register selections. The target accepts an identifier or an explicitly selected
indirect register; final prototype/target-set/table metadata stays identifier-only.
Anonymous brace actuals and direct function projections remain unsupported.
`ResolvedCallParameterRef::component` is a default-disengaged owning optional;
null preserves the whole-variable contract and old aggregate construction.

Source and both owned-module validation routes first join the real General
`.reg` v2/v4 declaration, SymbolId, parameterized member, element type/state,
physical 128-bit owner bound and explicit selector provenance. Only then does
the existing call ABI comparison see an effective scalar. Arrays, pointer,
size and alignment constraints are retained. Deleting or contradicting a cache
cannot hide a known declaration. The return's outer parenthesized range stays
in `WithLocs`; its selected leaf remains contained inside the group and instruction.
Copied observer values, module copies and instruction clones own the selection.

Modern register-formal minimum width remains a formal ABI rule, not a global
component minimum: matching legal subword `.param` formals use the existing
scalar actual policy. Whole named-vector actuals, constants and parameter-memory
staging retain their existing rules. This is derived scalar-element inheritance,
not pointer execution, hidden moves or synthetic scalar declarations. Coherent
current edits remain legal; written source identity is checked independently.
Public CST/AST variants and the owning call reference require matching producer
and consumer rebuilds. No instruction schema, generated layout or callback changes.


<a id="resolved-ir-design--alternate-scalar-literal-conversion"></a>

### Alternate scalar literal conversion

Scalar BF16, E4M3 and E5M2 numeric conversion uses the existing literal helper and evaluated F64 route, with integer-bit round-to-nearest, ties-to-even directly from binary64. Bare unsigned exact F32 literals widen exactly first. Signed zero and gradual subnormals are retained. BF16 overflows to signed infinity at `(2-2^-8)*2^127`; E5M2 does so at 61440. Their NaNs retain sign and high payload bits and set the quiet bit. E4M3 has no infinity: the exact 464 tie rounds to finite 448, while every larger finite magnitude and infinity becomes signed `0x7f` NaN; NaNs also become signed `0x7f`. These are adopted deterministic frontend literal choices, independent of runtime rounding, FTZ or saturation; they do not execute a GPU conversion.

The returned immediate keeps its logical format, a low16 BF16 or low8 FP8 word, and no integer-origin or integer-negative metadata. All in-width current words remain valid, including signaling NaNs; converter quieting does not impose literal history on later current-word edits. Integer-origin inputs, narrow Bit carriers and packed formats do not gain scalar floating conversion. Existing exact F32 sign, wrapper and expression restrictions remain. No public types, signatures or source permissions change.

BF16 numeric input includes mixed ADD/SUB `src`, the source of `CvtF32Bf16`, `CvtF32Bf16Ftz` and `CvtNonF32BaseFromBf16`, and the exact later BF16 FMA/SET, atomic and value-vector members listed in this contract. The mixed result and other input stay F32; selected rounding, width and availability checks remain independent. E4M3/E5M2 have no admitted scalar opcode source here: helper and generic checker coverage does not establish source/module-clone support. Other BF16 register-only, vector, packed, call and storage permissions remain separate. Calls still recompute supported F16 values from preserved preconversion sources. Partial-target instruction deferral remains unchanged.


<a id="resolved-ir-design--packed32-exact-words-and-native-call-formals"></a>

### Packed32 exact words and native call formals

The exact-word contract covers exactly F16x2, BF16x2, E4M3x4 and E5M2x4 through the existing literal helper. A bare unsigned direct `0f` literal without an evaluated `source_value` retains its entire 32-bit payload and selected logical type, including raw sign and signaling-NaN-looking bits. It performs no component conversion, rounding, quieting, saturation or splat. Integer origins, decimal/F64 numeric packing, signed or wrapped exact F32 expressions, and truncation to packed16 do not gain a producer. Underdefined numeric one-to-many mappings remain a reference boundary, rather than a blanket normative packed-literal ban.

An ordinary current packed word is in width, has absent integer-source metadata (including no optional zero), and has no integer-negative flag. Coherent in-width current edits do not authenticate erased literal history. A call literal differs because it already retains spelling/kind or evaluated input: an otherwise eligible scalar native F16x2 Reg/Param formal must recompute that retained direct word through the same helper. Typed-word-only or integer-origin edits reject; editing retained direct spelling and typed bits coherently may remain valid, with saved AST identity checked separately. The four existing direct/prototype/target-list/storage-function-list signature routes share this rule. Vector formals, return-actual grammar, formal ABI readiness and instruction-only BF16x2 classification are unchanged.

The helper alone grants no source permissions. The current multimem vector, scalar packed and atomic source sections below state their separately admitted exact-word roles. Existing scalar alternate RNE/NaN policies and the integer-origin distinction remain independent. Public structures, metadata and generated fields do not change.


<a id="resolved-ir-design--referenced-debug-function-name-bytes"></a>

### Referenced debug function-name bytes

The function-name reference contract adds a bounded check over current typed `.debug_str` payloads, independent of target/version availability. Same-name fragments concatenate in retained order; labels mark byte positions, not implicit terminators. The label or section base plus its checked unsigned64 offset selects the first byte. Known B8 integer leaves are interpreted only after their original signed/unsigned range checks. The selected known ASCII prefix must end with an explicit zero before the known logical-section end; empty names are allowed. A known byte128..255 before zero, a known ended nonzero sequence, or a selected position at/beyond the known end is a RuleViolation at the label or selecting offset range. A found zero finishes this reference proof, so later unrelated bytes are irrelevant to it.

Range-valid B16/B32/B64 numeric words contribute order-independent facts from their exact width-bounded bit patterns. Byte-count summaries distinguish zero, nonzero ASCII and high-bit chunks without selecting serialization. Nonzero ASCII words continue toward later data; an all-zero selected suffix or a whole ASCII word containing zero terminates; a whole zero-free word with high-bit data is invalid. A suffix is known to contain a zero when more zero chunks exist than were skipped. ASCII-only suffixes which might already have terminated retain both possibilities, so a later known NUL can finish every active alternative. If every remaining ordering is invalid but the reason differs, the content diagnostic states the combined ASCII/NUL failure.

Genuinely order-dependent zero/high-bit words and suffixes which may have skipped all zeros remain not-proven. Symbolic/relocated byte values are also unknown, although checked extents still prove bounds. The frontend does not pick endianness, replace unknown values by zero, build a dense image, or decode DWARF. Unknown content cannot certify a valid linked string, and cannot excuse an earlier definite violation. A proved NUL makes later data irrelevant to that reference. Neither a label nor a fragment boundary supplies a missing NUL; another section or external object cannot be assumed to finish the name.

Both current-owned sections/table and supplied replacement sections/rebound table are checked independently. Coherent current byte edits and independently valid different debug datasets remain allowed without original-content replay. Missing context cannot mask a known content violation. Full unsigned64 location leaves still parse and bind; an offset outside a known finite section is a stronger byte-bounds failure, not numeric overflow. An original source rejected before ownership has no owned/copy/clone/lifetime success; separate legal terminated sources establish those owner checks. Public structures, source identity, location precedence, section locality/ranges/version rules and generated metadata remain unchanged. Source acceptance and current validation apply this predicate through the same private reference check.


<a id="resolved-ir-design--floating-value-vector-source-contract"></a>

### Floating value-vector source contract

Exactly nine existing `src` fields use `WithLocs<ResolvedValueVector>`: weak and ordered floating stores, and floating add reductions, at v2/v4/v8. The classes, selector/layout identities, element type domains, arities, defaults, availability and addresses remain unchanged. This is a public field/factory/observer payload migration from `ResolvedRegisterVector`; downstream consumers must rebuild with matching generated headers and libraries. Wrap each old register lane in `RegOrImm`, preserve locations and `ResolvedVectorSource`, and inspect alternatives with `std::get_if`. A former null register element is not an immediate or a newly permitted sink.

All64 selected type branches have typed value carriers. Exactly32 have one-value numeric producers (15 F16/F32 and17 BF16/E4M3/E5M2), and20 have an exact whole32-bit bare unsigned direct `0f` producer (F16x2/BF16x2/E4M3x4/E5M2x4). The12 E4M3x2/E5M2x2 branches have no exact16 lexical producer or adopted numeric packing constructor: literal-source and literal-source-lifetime positives remain Source_N_A. Their real register controls and independently constructed canonical low16 typed words remain valid evidence; source absence is not a typed-data ban. Numeric scalar conversion reuses the existing single-round binary64 policy; packed32 retains every written word bit without splat, quantization or quieting. Integer-valued source expressions remain incompatible with logical Float lanes.

Mixed braces may contain registers and immediates. The50 v2/v4 named branches retain actual General whole-base declaration identity, exact NamedProjection lanes, element SameWidth and the16-byte declaration cap. The14 v8 branches remain brace-only; the existing128-element projection buffer and256-bit generic capacity are unchanged, and this cohort needs at most8 lanes/128 bits. Immediate projection is not a user sink. Synchronous observers receive actual payloads and all lane ranges, including lane8, and must not retain callback references. Copies/assignments/clones reacquire views from their own owners.

Ordinary typed words require the selected logical type, in-width bits and absent integer-origin/negative flags; coherent current edits do not replay erased floating source history. Current payload/pointer/range/provenance checks and retained source-identity checks remain independent. Address alignment, selected type/modifier/target gates and incomplete-context deferral still apply. The scalar packed and atomic source sections state their separately admitted inputs. This vector contract does not add packed numeric constructors or widen load destinations, and makes no runtime multimem mapping or GPU execution claim.


<a id="resolved-ir-design--explicit-scalar-bit16-sources"></a>

### Explicit scalar Bit16 sources

The implementation has passed full-model/default audits, normal generation, focused
Debug checks and a matching installed public consumer. The recorded implementation evidence is revision-specific. The `integer_immediate_type` descriptor member is
an optional trailing `ScalarType`, absent by default. Exactly eight existing
scalar SETP source rows engage B16: src1/src2 on the plain and Boolean F16/BF16
forms. Four BF16 source members migrate from register references to `RegOrImm`,
with logical BF16 and SameWidth; the four F16 members keep their existing kind,
logical type and width. The descriptor ABI requires rebuilding all consumers.

The source is evaluated once. An integer result chooses the explicit raw B16 arm:
`1` is word `0x0001`, and `0x10001` narrows to the same word while retaining its
64-bit integer origin. A floating result uses the existing logical converter:
`1.0` gives F16 `0x3c00` or BF16 `0x3f80`. Direct unsigned `0f` keeps its existing
root route and lexical precision; no wrapper or trial conversion classifies it.
Ordinary logical floating operands with absent policy still reject integer
origin, including the packed formats. The scalar B16 support does not itself extend atomic, storage, call or other
scalar permissions. The separate packed SETP extension is described below.

Current immediates select their arm by their actual type. Both words fit 16 bits;
numeric logical words have no integer origin or integer negative flag. B16
origin is optional; if present its mask/sign consistency remains checked, and a
cache-free word requires a false negative flag. Coherent word/arm edits do not
authenticate erased source history. Changed supplied syntax still has its
existing separate source association. Complete target context is required for
instruction-check proof; AvailableContext deferral is not a legality certificate.
Actual F16 carriers remain B16 or F16; BF16 carriers remain actual B16, with the
existing intrinsic declaration/class guards. No Base compatibility rule changes.


<a id="resolved-ir-design--packed-setp-raw-and-exact-words"></a>

### Packed SETP raw and exact words

The current packed SETP contract uses the existing trailing optional
`integer_immediate_type`; its finite pairs are F16/BF16 with B16 and
F16x2/BF16x2 with B32. It adds no descriptor field or immediate payload.
Exactly src1/src2 of plain and Boolean `SetpF16x2`/`SetpBf16x2` become
`WithLocs<RegOrImm>` (eight public members). The four BF16x2 bindings use
logical BF16x2 and SameWidth instead of physical B32 and Exact. F16x2 logical
type and width, predicate-pair destinations, Boolean combine operands,
comparison domains, FTZ rules and target minima remain unchanged.

A source is evaluated once: an Integer result takes raw B32 under the existing
narrowing/range policy and retains evaluated 64-bit source/sign facts. `1` is
word `0x00000001`, and narrowing `0x100000001` yields the same low word.
A bare unsigned direct `0f3fc03fc0` takes logical F16x2 or BF16x2 and preserves
all 32 bits through the shared packed-word helper. It performs no numeric
packing, splat, rounding or NaN quieting. Decimal/F64 numeric packed
constructors have no adopted producer; signed, wrapped or arithmetic `0f`,
`0h`, and F64 truncation do not gain one through this policy.

The Integer-source B32 arm retains source64/sign facts consistent with its low32 word.
The recorded evidence does not classify a cache-free current Bit32 word by analogy
with the separate scalar F16/Bit16 decision; its exact current flags and outcomes
remain unproved by this evidence. Genuine logical packed-word
current edits use independent direct-word producers. Logical packed words have
no integer-origin cache (even optional zero) or integer-negative flag. Coherent
current typed-word edits do not authenticate erased source history; supplied
source association remains independent. Actual register carriers are B32 or
native F16x2 for F16x2, and B32 for BF16x2; declaration/class/whole-container
checks remain in force. Missing-context deferral does not certify legality.
The existing eight scalar B16 policies and all absent-policy sources are unchanged.

Focused checks cover the finite source-policy pairs, source categories, current
values, declaration references, generated views, and owning copy/clone behavior.
Consumers must rebuild against matching generated headers and libraries. These
frontend checks do not establish GPU execution behavior or full PTX coverage.


<a id="resolved-ir-design--named-matrix-fragments"></a>

### Named matrix fragments

Eligible MMA fragments, WGMMA destinations/register-A fragments, and WMMA MMA
A/B/C fragments accept an ordinary declared `.reg.v2` or `.reg.v4` whole
identifier where the exact role has two or four physical registers. Element
policy checks each physical lane type; logical matrix element types and existing
cardinality remain independent. The complete declaration must fit within 128 bits.
This does not add named v1/v8/v128 syntax or a legal `.v4.f64` declaration.

Owned IR retains the whole declaration SymbolId and each ordered NamedProjection,
including source ranges. Current validation checks those facts after source/AST
release and after copy, assignment, or clone. A permission flag alone cannot
replace provenance. Existing legal large brace fragments remain supported.

When WGMMA register-A and shared-descriptor forms share identifier syntax, the
private resolver uses actual lexical declaration shape with paired typed
descriptors: whole vectors choose the authorized fragment path and scalar
registers choose the descriptor path. Wrong vector count/type remains an error;
unknown declarations do not prove scalarity. The public syntax-only selector
keeps its context-free contract and can report ambiguity. Selection never chooses
by target availability or by trial resolution. WMMA load/store operands and WMMA MMA destinations,
address/descriptors, scale IDs, sparse metadata, and other excluded roles retain
their existing brace/scalar/tuple rules. GPU scheduling and runtime resource
validity remain outside frontend validation.

The specification loader admits declaration-separable overlap only when every
layout cross-pair proves separation. The added proof uses an IdentifierRef-only
intersection, explicit named Element permission, an exact existing two/four-lane
count and a scalar SharedMatrixDescriptor competitor. Missing facts, Aggregate
policy, generic scalar roles or another shared syntax alternative supply no proof.
Targets and defaults do not resolve overlap. This validation proof preserves the
public selector's exact ambiguity diagnostic at the instruction range.


<a id="resolved-ir-design--scalar-components-in-shared-matrix-descriptor-carriers"></a>

### Scalar components in shared matrix descriptor carriers

The existing `SharedMatrixDescriptor` source kind admits either an identifier or an explicit ordinary vector member. A legal `.reg .v2 .b64 A` declaration occupies128bits; `A.x`/`A.y` (or `.r`/`.g`) selects one scalar64-bit descriptor-register carrier. This is component extraction, not a whole-vector descriptor or register-A fragment. The same kind rule covers the existing dense/sparse shared A/B and register-A B descriptor roles; it is not an opcode- or A-position special case. No YAML type/domain or public payload/API changes are involved.

Binding still requires the real General declaration, its physical integer/Bit64 type where already allowed, actual SymbolId and family index, in-range explicit selector, base/leaf/outer ranges and effective scalar width. Known F64/otherFloat, wrong width/class or InstructionOnly declarations are not converted into Bit words. `.v4.b64` exceeds the ordinary128-bit declaration limit. `A<2>` declares suffixed variables rather than bareA: scalarA1 and vector-familyA1.x are distinct explicit cases, with no implicit first element/index. Source components require existing declaration context; no standalone width/SymbolId is guessed.

The syntax overlap between a shared scalar descriptor (`IdentifierRef|VectorMember`) and a whole named matrix fragment (`IdentifierRef|VectorPack`) remains only IdentifierRef. Public syntax-only ambiguity, the separately established for-all layout-pair certificate and private real-declaration selection remain independent. A wrong whole vector or32-bit component cannot fall through and be rescued as a scalar64 descriptor. Other scalar/member mappings, fragment member permissions, partial-context priorities and target policies are unchanged.

Current owned validation rejoins actual component facts and declarations; valid coherent retargets are data edits, while contradictory facts fail even beside healthy supplied syntax. Saved source identity independently rejects a genuinely different legal operand AST. Existing descriptor views/collectors and synchronous observers carry the actual RegRef/ranges through source destruction, copies/assignments/moves/function copies and instruction clones, with fresh borrows per owner. This source-admission contract does not establish runtime descriptor bits, shared addressing, synchronization or matrix arithmetic.

Current module validation also checks the full selected register owner against the existing 128-bit declaration ceiling before availability-context skips. A coherent 256-bit current owner fails with a capacity-specific diagnostic at the component range through both module validation APIs; missing target context cannot hide that known failure. The existing actual SymbolTable join still rejects contradictory or missing cached facts. This adds no standalone instruction-check guarantee.


<a id="resolved-ir-design--tcgen-mandatory-named-output-lane-masks"></a>

### TCGEN mandatory named output-lane masks

Exactly24 existing ordinary unscaled, non-WS dense/sparse MMA mask layouts admit a named source alongside braces. Their common physical carrier list is ordered B32/U32/S32/F32/F16x2, with native F16x2 appended for both forms. This changes only named permission and that common carrier list. The required `disable_output_lane` member remains `WithLocs<ResolvedRegisterVector>`, with the same kind, type B32, Element policy, layout identities and syntax arity1..8. No removed mask-omission layout returns, no zero/default mask is synthesized, and no descriptor-word or execution result is inferred from mask registers.

Named Element lowering uses the separately established actual-declaration-width inference: an ordinary General `.reg .v2/.v4` whole declaration, compatible physical type, <=128-bit extent, full source range and exact NamedProjection lanes. It does not guess4, select by opcode/target, add ordinary namedv8 or change fixed Element/Aggregate behavior. Existing CTA-group checking still requires exactly4 entries for group1 and8 for group2. Group1 `.v4` is legal; group1 `.v2` and group2 ordinary named `.v4` fail the full check. A lowering draft or missing-context deferral is not group legality. Group2 remains eight-brace-only for ordinary declaration syntax.

Current actual payloads, whole-base SymbolId/type/class/width and all4/8 lane provenance/ranges remain authoritative; callbacks borrow synchronously and reacquire each copied/assigned/cloned owner. Coherent retargeting to another real compatible declaration may pass current validation while a different saved operand AST still has an independent source-identity mismatch. Special/predicate/array/component-as-whole/InstructionOnly/wrong-width declarations and null sink lanes gain no permission. Matrix named machinery is a separately accepted prerequisite; this mask facet does not introduce it. Other masks, WS/MX/collector/ashift/scaled roles, target DNF/profile windows and required-presence/cardinality logic remain unchanged.


<a id="resolved-ir-design--ordinary-bf16-fmaset-source-members"></a>

### Ordinary BF16 FMA/SET source members

`FmaMixedF32Bf16`, `SetHalfIntegerBf16`, and
`SetHalfIntegerBf16Boolean` expose `src1` and `src2` as
`WithLocs<RegOrImm>`. Existing class names, identities and field order remain;
C++ consumers must rebuild against matching headers and libraries and use
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>` on `.value`.

These six sources accept numeric Float literals and Float-result expressions.
The existing logical BF16 converter maps `1.5`, `1.0+0.5`,
`0d3ff8000000000000` and `0f3fc00000` to type BF16 and word `0x3fc0`.
FMA uses its fixed `input_type` modifier; SET uses fixed logical BF16.
Integer literals and Integer-result expressions remain excluded, including an
optional integer-origin value of zero. This grant has no integer-immediate policy.
The accepted actual register declaration remains the ordinary scalar `.b16`
carrier; SameWidth does not admit `.f16/.u16/.s16`, special registers, whole
vectors or standalone instruction-only `.bf16` declarations.

Owned validation reads current logical type, low sixteen bits and flags. It
requires no floating lexical history; coherent canonical current-word edits can
validate after source/AST destruction, copying, assignment or cloning. Retained
bad type, high bits, integer origin or negative metadata remain errors. Source
association still checks the provided AST against retained syntax identity.
This scalar numeric conversion adds no packed pair/splat or truncation rule;
packed exact-word and constructor policies remain separate. Instruction rounding,
saturation, comparison, predicate combination and execution are not evaluated.
Targets, destinations, other sources and all packed SET forms keep their contracts.


<a id="resolved-ir-design--scalar-f16-atomic-source-members"></a>

### Scalar F16 atomic source members

`AtomGlobalAddNoftzF16::src` and `RedGlobalAddNoftzF16::src` use
`WithLocs<RegOrImm>` in both the ordinary and cache-policy layouts. Rebuild
consumers and select `ResolvedRegisterRef` or `ResolvedImmediate` with `get_if`
or `visit`; the existing class identities, field locations and layout order do
not change. The cache policy remains a separate B64 register operand, and the
atomic destination retains its register-or-sink contract.

Only these four scalar F16 source roles inherit floating constant use. They keep
logical F16 and SameWidth: actual B16/F16 general registers and their existing
legal components remain supported. Numeric floating expressions use the common
binary16 converter (`1.5` produces F16 word `0x3e00`); an integer-origin source
is not a floating permission. Current canonical F16 words need no floating
lexical history, but require low16 bits, no integer-origin cache and no floating
negative metadata. IEEE sign and NaN payload bits remain data. This is scalar
numeric conversion, not a packed raw-word, pair or splat constructor.

The existing same-width Bit compatibility rule also accepts a canonical current
B16 word such as `0x3e00` without integer-origin or negative metadata. This is
inherited current-data compatibility, independent of the original AST spelling;
it adds no Bit literal-source permission and does not change the F16 producer or
its integer-source exclusion. Other Bit flag and packed-format rules are unchanged.

The ordinary form keeps PTX6.3/SM70 availability; the global cache-policy layout
keeps PTX7.4/SM80 availability. Address, alignment, ordering, scope, `noftz`,
source identity and existing AvailableContext/RequireCompleteContext rules remain
independent. Packed F16x2, BF16/BF16x2, atomic vectors, compare/swap and all other
source permissions remain unchanged. Frontend checks do not certify GPU runtime
behavior or opaque cache-policy contents.


<a id="resolved-ir-design--scalar-bf16-atomic-source-members"></a>

### Scalar BF16 atomic source members

`AtomGlobalAddNoftzBf16::src` and `RedGlobalAddNoftzBf16::src` use
`WithLocs<RegOrImm>` in both `no_hint` and `with_policy` layouts. Consumers
must rebuild with matching headers/libraries and inspect `.value` through
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`.
The two existing class identities and member names remain. Atomic destinations
remain B16/Exact register-or-sink values; cache policies remain B64 registers.
Addresses, alignment, qualifiers, modifier order and target floors are independent
of this source permission. Scalar F16, packed and vector atomic cohorts retain
their separately defined contracts.

The four source keys use logical BF16 and SameWidth with the existing converter.
Numeric Float sources `1.5`, `1.0+0.5`, `0d3ff8000000000000` and
`0f3fc00000` produce the existing BF16 `0x3fc0` golden. Actual register sources
still require the ordinary scalar B16 declaration under intrinsic type and
bit-compatibility rules; SameWidth does not admit F16/U16/S16, standalone BF16,
special registers or whole vectors. Integer-origin literals/expressions are
excluded; no integer-immediate policy or new conversion path is introduced.

The producer stores logical BF16. Logical BF16 current words require low sixteen
bits and clean origin/negative flags; this grant does not change pre-existing
current Bit-carrier compatibility. Floating lexical history is unnecessary, so a coherent canonical word can
validate after AST destruction, copy, assignment and clone. Stale high bits,
type/origin/class/provenance or range facts remain errors. Source association
checks the supplied AST against retained syntax independently of current-word
retargeting. Synchronous observers borrow the actual member and ranges only for
the traversal. These scalar numeric rules add no packed pair/splat/truncation
permission. The frontend validates representation and availability; it does not
execute an atomic update, memory ordering, cache behavior or BF16 arithmetic.

Current family references retain a real declaration/member identity: erasing a
required family index or contradicting its current spelling is invalid. Coherent
current spelling/member retargets and erasing only the numbered convenience
cache remain valid; validation does not authenticate old operand spelling.
Known reference contradictions are checked under partial context, while the
existing whole-instruction deferral remains. Required delivery evidence includes
actual generated views and real borrowed callbacks, not a manual projection.


<a id="resolved-ir-design--packed32-atomic-source-members"></a>

### Packed32 atomic source members

Exactly four existing public `src` members migrate from `WithLocs<ResolvedRegisterRef>` to `WithLocs<RegOrImm>`: `AtomGlobalAddNoftzF16x2`, `AtomGlobalAddNoftzBf16x2`, `RedGlobalAddNoftzF16x2`, and `RedGlobalAddNoftzBf16x2`. Each member covers both `no_hint` and `with_policy`, giving eight source keys. Consumers must rebuild with matching current headers and seven frontend archives; an older prefix cannot certify this generated member change. Existing class identities, factories, field order and optional cache-policy storage remain intact.

Use `std::get_if<ResolvedRegisterRef>(&form.src.value)` or `std::get_if<ResolvedImmediate>(&form.src.value)` and consume the actual alternative. BF16x2 sources carry logical BF16x2/`SameWidth`; their accepted declared register remains B32, while BF16x2 destinations keep B32/`Exact`. Native F16x2 sources keep F16x2/B32 compatibility. The B64 cache-policy register, address/target/modifier rules, declaration guard and component provenance are independent.

Only bare unsigned direct `0f` supplies packed words; all32 bits, including signs and NaNs, survive. Numeric pair/splat/new `0h`/`0d` truncation producers are Source_N_A / reference-qualified. The source converter produces the selected logical packed type. Canonical low32 F16x2/BF16x2 current-word positives with no integer origin and false floating-negative metadata are valid without lexical history. Existing current Bit-carrier compatibility remains unchanged; these examples do not narrow inherited `SameWidth` policy or establish a new B32 rule. Copies, assignment and clones own their words, SymbolIds and locations. Borrowed `OperandView` and synchronous `reg_or_imm` observer values must refer to the current owner; callbacks must not retain them or mutate/reenter the owner. Source/AST lifetime and supplied-source identity remain distinct checks. See [atomic coverage](#atomic-reduction-coverage--packed32-source-words-and-member-migration); this payload migration provides no runtime atomic or numeric packed-construction proof.


<a id="resolved-ir-design--scalar-packed32-source-members"></a>

### Scalar packed32 source members

The existing classes `MultimemStFloatPackedNoneScalarDefaultWeak`,
`MultimemStFloatPackedNoneScalarDefaultOrdered` and
`MultimemRedFloatPackedAddScalarDefaultRed` expose `src` as
`WithLocs<RegOrImm>`. Consumers must rebuild with matching installed headers and
libraries and inspect `.value` with `std::get_if<ResolvedRegisterRef>` or
`std::get_if<ResolvedImmediate>`. These are three changed source fields, each
retaining the existing `modifier(type)` domain F16x2/BF16x2 and SameWidth policy.
Variant identities, member names, controls, address/global/alignment and target
contracts remain; the nine prior ValueVector source fields retain their API.

Each of the six branches admits a bare unsigned direct `0f` packed32 word through
the existing shared helper. All 32 bits are data, including encoded sign and
signaling/quiet NaN-looking patterns: no quantization, splat or NaN quieting occurs.
F16x2 retains ordinary B32/native F16x2 register carriers; BF16x2 retains B32.
Existing scalar component provenance rules remain. No whole vector or new tuple
source is granted. Integer-origin literals/results, unary signs, parentheses,
operators, decimal/F64 packed constructors, numeric pairs and implicit packing
remain excluded. There is no low-word `0d` truncation or new lexical format.

The source converter produces the selected logical packed type. Canonical current-word
positives use that type, at most 32 bits, absent integer origin (optional zero is still
present) and `is_negative=false`. Existing current Bit-carrier compatibility remains
unchanged; these examples do not narrow inherited `SameWidth` policy or establish a
new B32 rule. Canonical current-word edits do not authenticate erased producer history. Copy/assignment/
clone and AST-free checks retain actual member/range/provenance; supplied AST
association independently checks retained syntax identity. Synchronous observers
borrow the real owner only during traversal. The frontend does not execute
multimem mappings, stores, reductions, ordering or GPU behavior.

The scalar packed tests establish selected logical F16x2/BF16x2 canonical words. Compatibility of a Bit-typed B32 **current** word is a separate inherited-policy question and remains unproved here; scalar F16 Bit16 controls do not decide it. This qualification grants no additional source literal or constructor.


<a id="resolved-ir-design--atomic-vector-value-sources"></a>

### Atomic vector value sources

The 26 existing `AtomVector*`/`RedVector*` classes expose `src` as
`WithLocs<ResolvedValueVector>` in both preserved layouts. Consumers must rebuild
against matching headers/libraries and inspect each `RegOrImm` lane with
`std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>`. Destinations
remain register vectors; optional B64 cache policies and exact class identities,
field names, arity/type domains, qualifiers and target gates retain their contracts.
The source clauses remain brace-only: no whole named vector or source sink grant.

All 128 type/arity/layout branches accept floating immediate lanes, mixed with
existing physical register carriers where appropriate. The 80 F16/BF16/F32 numeric
branches reuse the existing logical converter; the 48 F16x2/BF16x2 branches accept
bare unsigned direct `0f` exact32 words. Packed words retain every bit, including
encoded sign and NaN payloads, with no quantization, splat or quieting. Integer
literals/results remain excluded for all floating source lanes. Packed decimal,
F64, unary/wrapped/operator or pair constructors remain reference-qualified
unavailable; typed current-word capacity is not a numeric source constructor.

Logical immediate type is separate from physical register type. Explicit Element
allowlists retain B16/F16/U16/S16 for F16, B16 for BF16, B32/F16x2 for F16x2,
B32 for BF16x2 and B32/F32/U32/S32 for F32. Membership cannot waive physical lane
width, General/scalar class, intrinsic declaration eligibility, component, owner
or provenance checks. Without an explicit list, existing compatibility applies.
Bit register lanes are neutral; integer and floating register families cannot mix,
and immediate Float lanes do not participate in that register-family test.
Scalar operand compatibility is unchanged.

Canonical current logical Float lanes are checked for in-width bits, absent
integer origin (optional zero still counts as present), and clean negative metadata.
This does not decide current Bit16/Bit32 arms for other logical formats from a
scalar F16 rule; those cases need their own format-specific evidence. Coherent canonical edits need no erased floating lexical history.
Source association separately checks provided AST identity. Actual ValueVector
owner pointers and all shape/type/register/immediate/bit/range mirrors must agree;
synchronous observers borrow that actual owner only during traversal. Real AST
release, copy/assignment/function copy/clone preserve ownership. Register sinks,
whole containers and stale mirrors cannot substitute for read-only scalar lanes.
These frontend contracts do not execute atomic arithmetic, ordering, cache effects
or runtime global-memory behavior.


<a id="storage-declarations"></a>

## Resolved storage declarations

Technical reference and evidence: [storage_declarations.md](us-en/storage_declarations.md).

<a id="storage-declarations--resolved-storage-declarations"></a>

### Resolved storage declarations

This is a declaration-to-consumer contract, not a memory allocator or a claim
of complete PTX declaration conformance. Parameters remain separate:
`ResolvedFunction::parameter_declarations` describes sized `.param` declarations;
the `EntryInput` role identifies ordinary entry inputs. Opaque entry inputs live
in `ResolvedFunction::opaque_entry_parameters` without fabricated byte layout.
Registers and call parameters do not enter the storage list.

Module `.reg` and `.local` declarations are checked against the source-target
ABI assumption: PTX ISA 3.0 or later with a recognized active source target of
SM 20 or later requires these variables to be declared within a function.
The frontend does not model a caller-selected switch that disables ABI compilation.
Earlier PTX versions and recognized SM 1.x targets retain their historical
module placement. Target changes apply in source order; a later unknown or empty
target clears the previous placement context.

Declaration semantics checks every actual module occurrence, including unused
variables and conflicting redeclarations, and reports `InvalidDeclarationPlacement`
at the complete declaration range. Owned validation checks every variable identity
and retained occurrence against its source region, including registers with no
storage record, and reports `RuleViolation`. Both validation overloads recheck
placement after source/AST destruction or replacement source recontextualization.
`AvailableContext` defers this rule when either the version or recognized active
target is absent; strict validation reports `MissingValidationContext` at the
affected declaration. Other header and identity diagnostics remain independent.
Function-local declarations, nested blocks and valid register formals retain
their existing contracts. See the pinned PTX 9.3
[changes from PTX 1.x](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#changes-from-ptx-isa-version-1-x),
[register](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#register-state-space),
[local](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#local-state-space),
and [variable declaration](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#variable-declarations) rules.


<a id="storage-declarations--absolute-variable-placement"></a>

### Absolute variable placement

Absolute fixed variable placement is not parsed or allocated. PTX ISA 9.3
§5.4.1 mentions the capability without a determinate executable production in
the referenced declaration clauses. This remains the named
[`ADOPTED_REFERENCE_GAP`](#syntax-coverage--fixed-variable-addresses-adopted-reference-gap),
not implemented fixed-address support.

| Ordinary declaration | Retained fact |
| --- | --- |
| `.global .u32 x;` | Zero initialization, 4-byte extent, natural alignment 4 |
| `.global .u32 x=0x100;` | Initializer contents 256, 4-byte extent |
| `.global .u32 x[256];` | 256 elements, 1024-byte extent |
| `.global .align 256 .u32 x;` | Alignment divisor 256, 4-byte extent |
| `.global .align 16 .u32 x[2]={0x100,0};` | Alignment 16, two elements, 8-byte extent, contents 256 and 0 |

None selects absolute address 256. The owned records describe declarations,
contents and layout inputs, with no absolute-placement field or allocator.
Implicit zero initialization does not fabricate a list of explicit initializer
values. These ordinary records can be validated and copied after source/AST
destruction; no fixed-placement source or owned case is implied.


<a id="storage-declarations--identity-and-layout-inputs"></a>

### Identity and layout inputs

Each source declarator contributes one record in source traversal order.
`symbol_id` names its existing module symbol; `scope_id` identifies its lexical
scope, and `owner_function` is absent for module-level storage. Nested blocks
and same-spelled locals therefore remain distinguishable. Repeated compatible
external declarations share a symbol identity but retain separate occurrences.
Existing linkage and multiple-definition checks still apply; an extern
declaration followed by a same-named plain definition is rejected.

`.common` is retained as distinct `SymbolLinkage::Common` on ordinary module-scope
`.global` definitions (PTX 5.0, SM 20 or later). Scalar, vector, array, and
parameterized declarations use the existing storage layout rules. Omitted
initializers retain `Zero`; explicit zero, nonzero, and sparse initializers retain
`Explicit`. Functions, opaque objects, other state spaces, and conflicting linkage
qualifiers are rejected. Same-module repeated definitions remain errors.
Both validation overloads check eligibility and their selected version/target
context; AST-free validation rejoins declarations to ordered source target events.
The [PTX 9.3 `.common` rules](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#linking-directives-common)
select the largest definition across object files and allow only one object file
to initialize it, which must also provide the largest definition. These cross-object
rules belong to a downstream linker; the frontend does not select or merge objects.

Both `validateModule` overloads rejoin every record to its bound declaration
occurrence and require the storage list to be complete, including each compatible
extern occurrence. `SymbolTable::declarationOccurrences()` exposes a read-only
span of these owned occurrences. Validation checks lexical ownership, state space,
linkage, declaration kind, scalar type, vector width, positive array shape,
checked byte extent, alignment, and parameterized count. An unsized external first
dimension has absent byte extent; the dynamic-shared flag must agree with its
external shared declaration. Clearing the list or changing a record to zero bytes,
invalid alignment, or an unrelated identity is rejected after AST destruction.
The owned shape is checked for internal consistency; binding does not retain a
second copy of array dimensions or reconstruct source text.


<a id="storage-declarations--initializers"></a>

### Initializers

Initialization has four explicit states: `Uninitialized` for shared/local or
opaque objects without source initialization; `Zero` for implicit ordinary global/constant initialization; `Explicit`
for a source initializer, including an empty brace list; and `External` when
this module supplies no initial contents. An explicit initializer is a sparse
list of scalar values at byte offsets within the declared object. Missing
positions in ordinary `Explicit` mode are zero-filled; the frontend does not expand a
large aggregate into a byte buffer. Recursive array/vector brace positions
determine each offset. Opaque named-member initializers use separate typed field
groups and object-index paths; they never enter this byte-offset list or imply
default values for omitted fields.

`StorageConstant` holds the normalized element bits: `bits` is the low 64-bit
word and `high_bits` is the high word, independent of host byte order. The high
word is zero for elements of at most 64 bits. `StorageRelocation`
retains the bound symbol, optional parameterized member, state-space/generic/
function address interpretation, a byte addend, and an optional raw byte mask
(`0xff` shifted by 0, 8, ..., 56 bits).
Mask callees must be integer literals; decimal, leading-zero octal, binary,
and hexadecimal spellings (including an unsigned suffix) use the common integer
decoder and select the same eight allowed mask values. For example,
`255(257)`, `0377(257)`, `0b11111111(257)`, and `0xff(257)` each store `1`.
Expression-valued or parenthesized mask callees and floating literals are rejected.
The mask selects a byte after applying the addend and places that byte in the
low eight bits. Arithmetic after extraction is rejected rather than reordered.
Symbol identity follows lexical binding, including same-named declarations in
inner scopes. Function references may be bare or byte-masked; generic conversion
and address arithmetic on function references are rejected.
For a function-address initializer, the referenced function spelling must have
an earlier declaration occurrence; a later definition found by two-pass binding
does not make the use legal. Compatible prototypes and definitions still share
their stable identity, while an alias must itself have been declared before an
initializer uses that alias spelling.
Neither unresolved external symbols nor function references receive fabricated
addresses. Downstream consumers resolve these references against their own
linking/allocation model, without reparsing initializer text.

Known source versions before PTX 3.1 retain implicit generic addressing for
global symbols. Versionless fragments use the current state-space interpretation.
Address masks require PTX 7.1 and constant-integer masks PTX 7.3 when a source
version is present. Kernel-function initializers require PTX 3.1;
a versionless fragment is not a target-validity certificate.

Only fundamental scalar declaration types enter the scalar type alternative;
instruction-only packed/alternate formats must use their bit-container types.
Integer/bit initializer elements include `.b128`, with 16-byte array strides.
Integer literals and expressions still use PTX's `.s64`/`.u64` evaluation domain;
the wider destination does not enable 128-bit literals or expression arithmetic.
For `.b128`, the frontend sign-extends a signed result and zero-extends an
unsigned result only after evaluation. Thus `-1` fills both words with ones,
whereas `-1U` fills only the low word. Precisely, after evaluating the whole
expression, `lo = source.bits` and `hi = (!source.is_unsigned && lo.bit63) ?
UINT64_MAX : 0`. Sparse aggregate positions remain zero.

Owned `StorageConstant` records erase the source expression and signedness. Both
module validator APIs check each retained constant leaf of actual `.b128`
storage against `hi == 0 || (hi == UINT64_MAX && lo.bit63)`, including unused
storage, comma declarators, and every flattened array leaf. Thus a low word with
bit 63 set permits both high zero and high all-ones; current value edits between
these forms do not recover source history. The check is independent of target
context and initialization mode. Empty and implicit-zero storage do not gain
synthetic leaves, and existing external and parameterized-group source
initializer restrictions remain. Invalid values report the owned leaf range,
falling back to the declaration range when the leaf has no usable range.

This widening is an explicit frontend policy, not a GPU-verified conformance
claim: PTX specifies conversion at initialization but does not explicitly state
the upper-half rule for `.b128`. No GPU validation was available, and observed
anomalous upper words in ptxas 13.3.33 output are not reproduced by this contract.

Floating initialization supports decimal and exact-F64 literals with signs and
parentheses, including decimal trailing-point forms such as `1.` and `1.e2`;
supported compound arithmetic and integer-condition ternaries use the shared
binary64 constant evaluator, followed by the common `.f32`/`.f64` destination
conversion. Mixed integer/floating operands promote to binary64 using integer
signedness; floating comparisons produce integer results. Exact-F32 literals
retain their raw payload only as unsigned direct initializers. Parentheses,
unary signs, arithmetic, comparisons, casts, and conditional expression uses of
exact-F32 are rejected even in unselected branches. Decimal and exact-F64 unary
signs remain valid, including negative zero. Bit destinations accept only equal
source precision: `.b32` receives a direct exact-F32 literal, while `.b64`
receives decimal/exact-F64 literals or supported binary64 expressions. Raw
payload and negative-zero bits are preserved. `.b8/.b16/.b128` and mismatched
`.b32/.b64` source precision are rejected; integer destinations remain incompatible
with floating results. Opaque object
metadata includes module-global declarations with preserved array shape,
explicit alignment, and typed named-member initialization. See the
[texture contract](#texture-coverage) for its identity and static-field boundary.
These boundaries do not turn unsupported values into zero.


<a id="storage-declarations--diagnostics-and-boundaries"></a>

### Diagnostics and boundaries

`ResolveDiagnostic::declaration_kind` and `previous_range` preserve categories
and related locations supplied by declaration semantics. Other diagnostic
stages retain their existing contract; this is not a general diagnostic API
redesign.

The normalization path reuses the existing parser, binding identities, and
constant-expression machinery. A form outside its typed initializer domain
must produce an explicit diagnostic, not an empty initializer or a guessed
value. Storage metadata does not certify every declaration-type target rule,
linker rule, launch limit, or runtime access restriction.

The normative baseline is the [PTX ISA 9.3 variable
rules](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#variables).
The executable contract is exercised by the [storage regression
tests](../submod/resolved_ir/test/test_storage_declaration_metadata.cpp) and
the [installed consumer](../examples/texture_consumer/main.cpp).

Allocation order, padding between distinct objects, external symbol resolution,
dynamic shared-memory size, per-CTA/per-thread instances, and runtime memory
contents remain downstream responsibilities.


<a id="symbol-binding-design"></a>

## Symbol Binding Design

Technical reference and evidence: [symbol_binding_design.md](us-en/symbol_binding_design.md).

<a id="symbol-binding-design--reference-binding"></a>

### Reference binding

- `DeclaredSymbol` for an ordinary declaration in this module;
- `ExternalSymbol` for a reference bound to an explicit `.extern` declaration;
- `SpecialRegister` for a predefined PTX special register that needs no user
  declaration;
- `Unresolved` when none of the above matches.


<a id="symbol-binding-design--current-diagnostics-and-boundary"></a>

### Current diagnostics and boundary

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
`mov`. Lexical binding records declaration identities and metadata;
instruction-specific state-space and special-register/type compatibility is
checked during Resolved IR resolution/checking for modeled forms. Remaining
work concerns unmodeled instruction/operand forms rather than a missing
compatibility phase.

Instruction-keyword spellings in explicit name roles follow the
[contextual lexical interpretation](#lexer-design), including the official
`bar` object/function examples. Binding preserves exact case and spelling;
source name checks and AST-free owned checks apply to actual declaration/symbol
names without a global keyword exclusion or diagnostic-spelling history join.
Independent role, type, ABI and prior-declaration checks remain authoritative.


<a id="syntax-ast-design"></a>

## CST and Syntax AST design

Technical reference and evidence: [syntax_ast_design.md](us-en/syntax_ast_design.md).

<a id="syntax-ast-design--handwritten-parser-implementation"></a>

### Handwritten parser implementation

Private types distinguish full numeric expressions from address-base expressions
that stop above additive precedence, remaining allowed tree height from actual
constructed height, and lexical block depth. Tree budgets include the current
root and are copied for siblings; named initializers and brace lists add one
level, while scalar initializer wrappers add none. Expression/initializer height
is limited to 128 and nested block depth to 256. Function and nested-block bodies
share their item/recovery driver, preserving their distinct missing-brace messages.
Operand helpers document opener/closer consumption and retain half-open ranges.
Module dispatch and recovery share source-form classification, including `.alias`
spelled as a dotted identifier; body synchronization keeps its separate policy.
This classification describes source grammar and does not select ISA variants.


<a id="syntax-ast-design--instruction-constant-expressions"></a>

### Instruction constant expressions

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


<a id="syntax-ast-design--named-array-address-syntax"></a>

### Named array-address syntax

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


<a id="syntax-ast-design--named-memory-vector-syntax"></a>

### Named memory-vector syntax

Named data vectors use the existing identifier node, without synthesizing brace
lanes or selectors in CST/AST. Only opted ordinary LD/ST, LDNC, LDU and explicit
vector MOV slots admit
this shape; declaration-bound resolution records the owned whole source and
implicit lane origins described in [Resolved IR](#resolved-ir-design--named-memory-vector-sources).
Brace syntax and the separate named-array address grammar are unchanged.


<a id="syntax-ast-design--narrowed-syntax-ast-responsibility"></a>

### Narrowed Syntax AST responsibility

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


<a id="syntax-ast-design--remaining-location-work"></a>

### Remaining location work

`SourceRange` currently stores line and column only. A future multi-file CST
and robust edit system should extend locations with a source identity and byte
offsets. This does not require widening the Syntax AST responsibility.


<a id="syntax-ast-design--arithmetic-negated-register-syntax"></a>

### Arithmetic-negated register syntax

`CstNegatedRegisterOperand` retains the minus token and a child limited to one identifier or vector member. `AstNegatedRegisterOperand` owns the minus range, child range and full range. The parser recognizes `-Ident[.selector]` before signed-numeric immediate parsing; it does not introduce recursive unary operands. The distinct `ArithmeticNegatedRegister` syntax shape is enabled only for approved `vmad` source slots. Binding, lowering, source identity and layout classification traverse this child explicitly. Predicate `!` negation remains separate, and `!-%r` is rejected.

Function-header and local `.callprototype` arrays reuse `CstArrayDimension` and
`AstArrayDimension` vectors in `array_dimensions`, preserving outer-to-inner
brackets, optional expressions and ranges. These replace the singular CST
bracket/size fields and AST `is_array/array_size`; an empty vector means scalar.
Token round-trip is preserved. Dimension and AST expression trees remain move-only;
the existing `AstConstantOperand` copy operation deep-clones its owned expression. See the exact
[public member migration](#parameter-declarations).

`ptx_syntax_sections.hpp` exposes only the owning syntax leaf and section value records. Including the public module header keeps full instruction/module/expression AST types incomplete; `ptx_syntax_ast.hpp` remains the complete-AST entrypoint.


<a id="syntax-ast-design--legacy-debug-serialization"></a>

### Legacy debug serialization

The case-sensitive `@@DWARF` marker introduces one closed production at module,
function-body or nested-block statement scope. It is supported for PTX ISA
**[1.2, 2.0)** on every recognized source target. Function headers do not admit it.
The forms are `.byte` unsigned hexadecimal lists (0..255), `.4byte` lists
(0..UINT32_MAX) and `.quad` lists (0..UINT64_MAX). Lists are nonempty. The two wider
forms also admit one ordinary or singly dotted symbolic label, retained unlinked;
there is no ordinary PTX symbol-resolution requirement for that label.

`0x`/`0X` with optional `U`/`u` follows existing PTX integer token inheritance;
this suffix choice is derived rather than explicit wording of the legacy syntax.
Signs, decimal/octal/binary values, mixed symbol/numeric lists, addends,
differences, label definitions and arbitrary host assembly are outside this model.
The published example's header is supported as `.section name, "", @progbits`.
Generalizing its name to an ordinary or singly dotted identifier is derived from
the example; flags remain exactly empty and the type remains exactly `progbits`.

Whitespace, comments and newlines separate tokens without ending a record. One
structurally complete payload and an optional semicolon end it. The next token is
handled by ordinary PTX grammar; malformed records preserve following markers,
blocks and functions through structural recovery. Lossless CST tokens retain
comments and source text. Owning AST values retain exact marker/word/header
spellings and within-record ranges, including separate `@` and `progbits` leaves.

`ptx_syntax_legacy_dwarf.hpp` contains only small owning values. It does not expose
module, instruction or expression AST definitions. One shared `AstLegacyDwarfRecord`
is used in module/body variants, and `ResolvedModule::legacy_dwarf_records` is the
sole ordered stream of `{value, scope_id}` wrappers. Actual declaration-occurrence
and block scopes come from the module's symbol table; a canonical function's
preferred owned scope does not replace its definition occurrence. Metadata creates
no executable node, label slot or instruction-provenance index. Module and function
source identities exclude it while ordinary program constructs remain relevant.

Both current and supplied datasets validate their own typed words, ranges, scope
ownership and format windows. Source-aware validation uses the supplied effective
version for both datasets; AST-free validation uses current module context. Known
out-of-window versions reject without a complete target. Missing version under
`AvailableContext` defers only the format window; intrinsic checks continue and
strict missing-context checks remain. Coherent current metadata edits and differing
valid datasets do not require old source-byte equality. Cross-format replacement
requires removal or reconstruction of current incompatible format data.

For `.target debug`, definite absence means **zero modern sections AND zero legacy
records**, independently in each dataset. An unknown linked label, a header or
nonempty bytes only avoid this definite-absence result. They do not certify actual
DWARF recognition, decoding, linked addresses or binary authenticity. A known full
legacy context cannot combine the pre-2.0 format with the PTX-3.0 debug option;
missing-version tests are partial-context controls only.

References: [legacy debug marker](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-atatdwarf)
and [legacy-format window](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives).


<a id="syntax-ast-design--ascii-whitespace-and-physical-positions"></a>

### ASCII whitespace and physical positions

Outside strings and comments, whitespace is exactly SP (`0x20`), HT (`0x09`),
LF (`0x0a`), VT (`0x0b`), FF (`0x0c`), and CR (`0x0d`). This six-byte set is
the frontend's adopted engineering interpretation of PTX's ASCII and equivalent
whitespace rule, rather than a normative six-terminal enumeration or a Unicode
whitespace policy.

SP, HT, VT, and FF each advance one physical byte column. VT and FF do not
start a source line; existing CR, LF, and CRLF handling is unchanged. Exact
bytes and ranges remain in tokens, leading trivia, trailing EOF trivia, and
`CstFile::sourceText()`. A physical VT inside a string or comment remains raw
content under that token's existing rules; no backslash-v escape or source
normalization is added. Logical/debug locations remain a separate contract.


<a id="tensor-cache-controls"></a>

## Tensor async cache controls

Technical reference and evidence: [tensor_cache_controls.md](us-en/tensor_cache_controls.md).

<a id="tensor-cache-controls--tensor-async-cache-controls"></a>

### Tensor async cache controls

The canonical tensor `cp` model previously contained 178 original tensor identities and
241 original operand layouts. It adds one cache-hint sibling per identity and
two sibling layouts per original layout, for 356 tensor identities, 723 tensor
layouts, and 473 total `Cp` identities. The original 178 identities and their
layouts remain available.

For an eligible tensor load, store, reduction, or prefetch, the written
`.L2::cache_hint` follows the existing mode, multicast, and CTA-group suffixes.
The final `cache_policy` operand, when present, follows im2col information and
the multicast mask. The three source states are no hint/no policy, written
hint/no policy, and written hint/final policy. A policy without a written hint
is invalid. The hint is an instance-level source fact rather than a fixed
property of a variant. The new sibling inherits the parent's exact target,
mode, rank, destination, multicast, group, completion, coordinate and typed
operand contracts. Existing parent identities and layouts remain available.

The final policy use selects B64 through the ordinary located `reg_or_imm`
carrier and `Narrow` conversion. Scalar General B64, U64 and S64 registers are
eligible; incompatible declared type, non-General class, vector width, or a
missing declaration for a bound identity is rejected. An unbound scalar whose
type is unknown remains an obligation. Integer literals `0`, `1`,
`18446744073709551615`, and signed `-1` retain their original evaluated
64-bit source bits and source sign. The last two produce the same B64 word but
different sign provenance. No policy-bit, eviction-mode or numeric source
range decoder is implied. The policy does not alter instruction completion or
memory-consistency metadata.

Optional im2col information may be absent or present, and multicast masks and
explicit CTA groups can coexist. The equal-arity absent-info-plus-policy and
present-info-without-policy layouts are distinguished by scalar policy versus
vector information shape, not just operand count. The existing accepted
multicast/group suffix alias is preserved; arbitrary suffix permutation is
not introduced. Selected cache information must own its values and source
locations after syntax AST destruction. Owned register class, type, vector,
binding, source bits and sign remain visible to direct and full-module
validation and reference collection. The selected tensor-known-facts context
continues to describe the same direction, rank, destination, target and
availability for a cache sibling.

Formal PTX 9.3 syntax independently brackets the hint and policy and states
that policy requires the hint, so the frontend accepts a hint-only form.
An earlier 32-invocation PTXAS 13.3.73 corpus reported 13 accepted and 19
rejected, plus one dependency skip; its exact inputs are not checked in.
A fresh 26-invocation PTXAS 13.3.73 matrix on this implementation accepted
12 and rejected 14. The five load/store/reduction/prefetch hint-plus-B64-policy
cases assembled; the matching five hint-only and five policy-without-hint cases
were rejected. Additional probes accepted im2col information with policy,
multicast plus CTA group in both supported suffix orders, B64 literals `0` and
`-1`, and U64/S64 policy registers. They rejected an information-present
hint-only form, a missing multicast mask, and a B32 policy register. PTXAS
also rejected an information-omitted im2col load with final policy as
"vector expected," even though the formal optional-information syntax and
the frontend's typed layout permit it. These assembler observations do not
establish cache effects, runtime validity, or a cache-specific target floor.
The [26 exact source modules and observed outcomes](tensor_cache_controls_ptxas.json)
retain per-case hashes, targets, and replay commands. The earlier 32-invocation
corpus cannot be reproduced from those files.

The public `query_tensor_cache_controls(const Instruction&)` query copies the selected written hint, optional located policy, diagnostics, and an applicability indicator from the exact owned form. It does not inspect opaque tensor-map bytes or infer descriptor
content. See [caller-known tensor-map facts](#tensor-map-known-facts) for the
separate conditional descriptor query, and
[tensor async coverage](#tensor-async-coverage) for the supported forms.


<a id="tensor-map-known-facts"></a>

## Caller-known tensor-map facts

Technical reference and evidence: [tensor_map_known_facts.md](us-en/tensor_map_known_facts.md).

<a id="tensor-map-known-facts--caller-known-tensor-map-facts"></a>

### Caller-known tensor-map facts

The frontend exposes a conditional query through the installed
`ptx_tensor_map_known_facts.hpp` header and Resolved IR library. The Python
catalog and query provide the corresponding specification-level result.
`project_tensor_known_access_context` copies a typed context from a selected
exact `Instruction` form and checker context, with diagnostics for malformed
tensor metadata. A non-tensor instruction produces no access or diagnostics.

A tensor-map operand still denotes an opaque 128-byte object. A
`TensorMapKnownFacts` value holds claims independently supplied by the caller;
it neither decodes that object nor tracks `tensormap.replace` across a control
flow graph. The facts own their values and no AST lifetime. The selected
`TensorKnownAccessContext` adapter comes from the selected typed instruction,
not a variant-name string. Its direction, mode, rank, coordinate roles,
optional reduction operation, destination/group topology and actual target
availability remain separate from descriptor claims. Known S32 coordinates and
U16 im2col information are checked *after* instruction-use conversion, with
original literal bits retained when available. A register with unknown runtime
value stays unknown.

The query reports one of `Checked`, `Violated`, `Unresolved`, or
`NotApplicable` for **each** of 22 rule families. A checked row establishes
only a relationship among supplied values. It does not certify map bytes,
address ownership, GPU bounds or synchronization. `Unresolved` names missing
facts or an undocumented relationship; `NotApplicable` means the rule does not
govern this access. Invalid enum, arity, source/bits or contradictory caller
claims yield diagnostics before dependent lookup. Missing and known zero are
distinct.

The families cover encoded element and direction; reduction operation/type;
tiled box bytes and traversal steps; packed fill, operation, geometry and
swizzle restrictions; interleave; swizzle atomicity, destination alignment and
repeating-pattern offset; 96-byte swizzle; 128-byte 8-byte flip; exact
`sm_103a` B6p2x16 store override; exact `sm_120a` cluster exclusions; store
coordinate/corner signs; im2col shape, U16 information and W/W128 profiles;
four-row gather/scatter; and canonical selected-form availability. The
`sm_103a` store rule replaces conflicting generic packed requirements, rather
than combining with them. Target/version results must come from the existing
selected variant and modifier-value availability queries, including Table 63's
W-cluster `sm_110f` family at PTX 9.0. The focused catalog contains no second
target DNF, reduction matrix or Table 33 encoding table.

Element-count dimensions, traversal steps in elements, global and tensor byte
strides, packed `Box-Size[0]`/`Tensor-Size[0]` in bytes, total accessed-box
bytes, MAP-object address, global data base, accessed-box address and shared
destination address are distinct optional inputs. Spatial im2col lower and
upper corners are signed offsets from **opposite edges**, not absolute
endpoints; their arrays have rank-minus-two arity. W width, halo and offset,
pixels-per-column and channels-per-pixel retain separate element-count roles.
No packed element-to-byte formula, replacement-stride mapping or inclusive
corner transform is guessed. An explicit zero dimension or pixel count is
reported as an unclassified geometry obligation where the fixed PTX rule does
not decide it; Driver Encode limits remain API-only.

Table 33 code 15 retains one raw code and opposite load/store interpretations.
The caller supplies a closed Table 33 enum, its original code and the result of
the canonical field-specific projection. The element claim may also state one
of two code-15 semantic interpretations: load/prefetch or store/reduce. An
absent interpretation leaves element identity and dependent packed geometry
unresolved; an opposite interpretation violates the directional identity rule
and cannot select a packed subtype. A rejected projection is diagnosed without
authenticating map bytes. C++ checks enum/code agreement through the existing
Table 33 helper; no second encoding table is introduced. The C++
renderer now separates a small public rule-metadata header from an out-of-line
private query source; both are registered in the standard generation plan.
No-swizzle atomicity use is `NotApplicable` even when a valid raw atomicity code
is supplied. An exact `sm_120a` cluster load with *applied* atomicity is
violated; a raw code alone leaves active use unresolved. Reduction membership
reuses the accepted operation/type table. Exact common U32/S32/U64/S64/F16/
BF16/F32 spellings may be compared with a separately supplied descriptor
interpretation; FTZ, TF32 and B32/B64 have no approved general conversion or
descriptor-code bridge. Unknown active atomicity, runtime peer/mask membership,
barrier completion and descriptor contents remain obligations.

The fixed PTX 9.3 manual supplies no complete raw 128-byte tensor-map field
layout. Raw-object decoding is outside the #152 frontend closeout and would
require a separately specified byte-layout scope; this caller-known-facts
query does **not** satisfy or replace it. Canonical
[cache-hint/policy operands](#tensor-cache-controls) are supported on the
selected tensor forms and remain independent of descriptor facts. See the
[tensor async coverage](#tensor-async-coverage) and
[frontend closeout matrix](#tensor-async-closeout) for exact forms, evidence,
and remaining obligations.


<a id="tensor-map-known-facts--conservative-whole-relation-boundary"></a>

### Conservative whole-relation boundary

The following five rule IDs deliberately keep `Unresolved` after their local checks pass, even when every relevant field supported by this caller-facts API has a known value. Supplying values does not supply the missing mathematical relationship or authenticate their provenance. A report can have no malformed-claim diagnostics and no `Violated` rows while still containing these unresolved obligations. There is no aggregate descriptor-verification result.

| Rule ID | Implemented local checks | Whole relation that remains `Unresolved` | Relevant supported caller values |
| --- | --- | --- | --- |
| 17 `STORE_BOUNDS` | Store/reduce scope, nonnegative converted coordinates, lower-edge offsets nonnegative and upper-edge offsets nonpositive; no-offset im2col stores require both edge arrays. | Whole tiled/store box containment within the full tensor and the opposite-edge transform. | Full/tiled dimensions, traversal and coordinates; applicable edge arrays. |
| 18 `IM2COL_SHAPE` | Im2col scope, rank 3–5, spatial arity rank−2, rank-specific signed edge-offset ranges. | Filter-base/spatial-box opposite-edge relation, including selected spatial-coordinate provenance. | Full/tiled dimensions, traversal, known coordinates, spatial box and both edge arrays. Coordinate values alone do not establish that provenance. |
| 19 `IM2COL_INFO` | General-im2col scope, rank 3–5, rank-specific information arity and converted U16 bounds; unknown fill leaves excess requested pixels unresolved. | Pixels/traversal/available-NDHW/fill relationship. | Dimensions, traversal, coordinates, information, pixels per column, channels per pixel, available NDHW count and fill. |
| 20 `W_PROFILE` | W/W128 scope, rank 3–5, no interleave, required swizzle, active 128-byte/flip8 exclusion, D/H extents one and accepted information bounds. | W/W128 full width, opposite-edge, bounds and count relationship. | Dimensions, traversal, coordinates, spatial box, information, W width and edge offsets, counts, fill and layout claims. |
| 21 `GATHER_SCATTER` | Gather4/scatter4 scope, rank two, five coordinate roles, no interleave and tiled-box height one. | Per-selected-row full bounds/extent relationship. | Full/tiled dimensions, traversal and all five known coordinate roles. The API does not add a separate per-row extent or provenance fact. |

Known local counterexamples remain `Violated`; structural inapplicability remains `NotApplicable`. Absent or unknown fields remain unknown, never known zero. These whole-relation limits do not alter any of the 22 stable IDs, dependency masks or malformed-claim quarantine rules. A field mentioned in an unresolved detail is not automatically a new metadata dependency.

This is a documented scope boundary, not five implemented solvers. Neither complete caller-supported values nor passing local checks certify tensor-map bytes, actual GPU bounds, address ownership, synchronization or runtime protocol completion. The existing local `Checked` outcomes retain only their stated conditional meaning.


<a id="yaml-instruction-spec"></a>

## YAML Instruction Spec: Structure and Principles

Technical reference and evidence: [yaml_instruction_spec.md](us-en/yaml_instruction_spec.md).

<a id="yaml-instruction-spec--extension-boundary"></a>

### Extension boundary

For the separate, measured cost of changing that canonical model, see the
[generated-model build scalability baseline](us-en/build_scalability.md); it is not
opcode acceptance or ISA-conformance evidence.

Adding a new operand primitive, modifier domain/value representation, or
semantic constraint is a coordinated change: extend the schema only as needed,
then the Python Syntax/Resolved model and normalization, C++ resolved payload
and resolver, descriptor emission, checker implementation, and tests. Do not
silently encode such a requirement in a string or an enum merely because YAML
can parse it. The frontend contract is source acceptance, binding, resolved
modeling, and target-aware validation; it neither supplies simulator execution
nor establishes physical-hardware conformance.


<a id="yaml-instruction-spec--availability-rules-and-examples"></a>

### Availability, rules, and examples

Plain availability and every `any_of` clause may include `ptx_before: "1.4"`:
the PTX version must be strictly below that exclusive bound and at least `ptx`
(default `"0.0"`). Omission means no upper bound. Both versions must be decimal
`major.minor` strings with uint16 components; malformed, empty, equal, or reversed
intervals are rejected. The generator carries the optional bound to
`AvailabilityDescriptor::ptx_before` or `AvailabilityClause::ptx_before`.
Existing minimum-only metadata keeps its behavior. Plain upper-bound failures
report a located `UnsupportedPtxVersion` with the exclusive limit; DNF failures
retain `UnsupportedAvailability`.

Common checker logic interprets minimum PTX, SM, and `family` requirements.
`family` is the minimum family-specific source-feature target: the checker
looks only in the source target profile's `enabled_family_features`. The
explicit catalog is: `sm_100` → none; `sm_100f`/`sm_100a` → `sm_100f`;
`sm_103` → none; `sm_103f`/`sm_103a` → `sm_100f`, `sm_103f`; `sm_120` →
none; `sm_110` → none; `sm_110f`/`sm_110a` → `sm_110f`;
`sm_120f`/`sm_120a` → `sm_120f`; `sm_121` → none; and
`sm_121f`/`sm_121a` → `sm_120f`, `sm_121f`. Do not infer this set from the
SM number or target suffix. It is distinct from PTX-to-physical-GPU translation
compatibility, which is not
modelled. An `a` target is an exact identity, not a family spelling: use
`any_of: [{target: sm_100a}]` when that exact target is required. Capability
clauses, exact target, and `family` are independent constraints. `rule` is a
closed semantic-rule ID for instruction-specific checking. Normalization rejects
unknown or non-string rule spellings before IR construction; emitters dispatch
on the typed identity, while checker descriptors retain the spelling only as
inert metadata. `examples`, `doc`, and
`description` document intent; they do not replace executable tests.

An `any_of` clause may also contain `family`; it is an AND-term within that
clause and uses the same `enabled_family_features` lookup, rather than requiring
that exact target spelling. The schema, normalizer, and generated descriptor
permit one through six clauses. `setmaxnreg` uses all six: exact `sm_90a`
(PTX 8.0), exact `sm_100a` (8.6), exact `sm_120a` (8.7), enabled `sm_100f`
and `sm_120f` families (8.8), and enabled `sm_110f` family (9.0). See
[miscellaneous coverage](#miscellaneous-coverage) for the instruction contract.

The explicit generic catalog includes `sm_32`, `sm_35`, `sm_37`, `sm_53`, `sm_60`,
`sm_61`, `sm_62`, `sm_86`, `sm_87`, and `sm_88`, with no enabled family features.
The first three have no modeled capabilities; `sm_53` and `sm_60`–`sm_62` have
`graph_exec`; `sm_86`–`sm_88` have `reserved_smem` and `graph_exec`. Suffix variants
and unlisted architectures remain unsupported. This is the explicit
[PTX 9.3 target table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target)
contract, independent of the module's PTX instruction-version checks.

The canonical `div_rn_f64`, `rcp_rn_f64`, and `sqrt_rn_f64` variants each use four
clauses: the original PTX 1.4 / SM 13 minimum, plus exact generic `sm_10`, `sm_11`,
and `sm_12` alternatives requiring this option and PTX 1.4. Their syntax identity,
explicit `.rn`, f64 type, and 64-bit operands are unchanged. This option adds no
alternative to other f64, directed-rounding, approximate reciprocal, or f32 forms.

`operand_layouts[].availability` accumulates with variant availability; it
does not override it, and only the selected layout contributes its constraint.
When more than one layout matches the same syntax, the resolver accepts only a
unique layout with strictly more specific operand shapes. Equal or incomparable
candidates are a YAML modeling error; availability cannot resolve the syntax
ambiguity.


<a id="yaml-instruction-spec--current-add-coverage"></a>

### Current Add coverage

The section-specific `add` definitions in `arithmetic.yaml` merge automatically.
They cover the standard `.f32/.f32x2/.f64` forms,
mixed-precision `.f32.{f16,bf16}`, and the half-precision
`.f16/.f16x2/.bf16/.bf16x2` forms. Rounding resolves to `RoundingMode`, and
value availability expresses the `sm_20` requirement of `.rm/.rp.f32`.
Packed and half/bfloat register carriers retain their role rules; admitted numeric and exact-word sources are enumerated in the [current source-producer sections](#current-source-producers).

The mixed variant has two named slots: `result_type` is fixed to `f32`, while
`input_type` accepts `f16/bf16`. Structured operand type expressions refer to
those slots directly, so neither resolver nor checker re-infers operand types
from modifier position or text. The variant requires PTX 8.6 / sm_100 and
supports optional rounding and `.sat`.


<a id="yaml-instruction-spec--typed-video-contracts"></a>

### Typed video contracts

The `video` category owns 23 opcode variants and 39 operand layouts. A variant's closed `video` mapping carries `lanes: scalar|two|four`, `operation: arithmetic|shift|compare|mad`, and optional validated Boolean `sat_modifier`/`po_modifier` field references. Each `video_operand` has its own closed mapping: logical `position`, typed `selector` policy, optional `type_modifier` referring to a `video_type` field, and `allow_immediate`/`allow_negate` flags. The normalizer rejects contradictory roles, SIMD immediates, non-Mad negation, incorrect selector topology and missing field references.

`type_use: bit_carrier` is restricted to Mad C and cannot coexist with `type_modifier`; it selects B32 immediate coercion. A type modifier infers `ModifierField` interpretation; omitted type use defaults to `Unsigned`, used by set D/C. The four closed modifier domains are `video_type`, `video_secondary_op`, `video_shift_mode`, and `video_scale`. Video semantics never become generic integer-operation aliases. Syntax shape derivation admits selected destinations only for scalar merge layouts and arithmetic-negated registers only in approved Mad source slots. See [video coverage](#video-coverage).


<a id="yaml-instruction-spec--exclusive-numeric-sm-availability-bound"></a>

### Exclusive numeric SM availability bound

Plain availability and each `any_of` clause accept optional `sm_before: 70`. Together with `sm: 30` this means `30 <= SM < 70`; a missing lower bound means zero and an omitted upper bound means unbounded. The field must be a strict uint32 integer greater than the minimum; zero, booleans, floats, strings, explicit null, overflow and empty/reversed intervals are rejected by typed normalization and rendering. JSON Schema applies integer/range constraints; the typed boundary also rejects integral floating values. The existing maximum of six ordered alternatives and prohibition on mixing `any_of` with plain fields remain. No target intersection solver is introduced.

`AvailabilityClause::sm_before` and `AvailabilityDescriptor::sm_before` append disengaged `std::optional<uint32_t>` members after `ptx_before`; rebuild public consumers for this aggregate layout change. The predicate compares concrete `TargetInfo::sm_version`, independently of profile spelling or flavor, while all existing exact-target, family, capability and mapping requirements remain conjunctive. Plain failures produce located `UnsupportedSmVersion` in variant, layout, modifier, operand-value, parameter-address and memory-vector paths; DNF failures retain `UnsupportedAvailability`.

Legacy SHFL uses `any_of: [{ptx: "3.0", ptx_before: "6.4", sm: 30}, {ptx: "3.0", sm: 30, sm_before: 70}]`. Source modules still require recognized profiles and legal profile/version headers. Under `AvailableContext`, missing version or recognized target continues to defer whole-instruction availability, even if another known fact appears incompatible; such success is not full ISA certification. Strict validation reports `MissingValidationContext`; an explicit unknown profile remains `UnknownTarget`. No partial DNF evaluator or opcode-specific context rule is added.


<a id="yaml-instruction-spec--historical-101-source-profiles-and-validation-features"></a>

### Historical 101 source profiles and validation features

`sm_101`/`compute_101` and their A forms begin at PTX8.6; F forms begin at PTX8.8. They remain admitted at PTX9.0/9.3, with no rename-imposed upper window. The corresponding 110 source profiles still begin at PTX9.0. `TargetIdentity` retains architecture101, flavor and exact written spelling. `same_target_identity` compares source architecture/flavor, so101A and110A remain distinct.

The additive base function `target_feature_architecture(identity)` consults explicit private catalog data: the three101 keys project to feature architecture110; other keys retain their architecture. Uncataloged typed keys do not become recognized. Public `TargetProfile` and `TargetInfo` layouts are unchanged. Source/owned/semantic factories use this feature level for numeric SM, retain source identity, and pass the existing110 baseline capabilities `cluster`, `aggregate_smem`, `reserved_smem`, `graph_exec`. Generic101 has no family; 101A/F enable only canonical `sm_110f`, with no100f/103f inheritance or FP64 mapping option. This bounded correspondence follows the published rename and family relation; it is not a physical hardware/driver guarantee.

Availability singular and span exact-target requirements compare validation-feature architecture plus exact flavor through that helper. This matching relation differs from source identity and stored-key equality. A manually supplied `TargetInfo::sm_version` remains the caller's concrete feature fact and is never overwritten by the checker. Source profile introductions, instruction/PTX floors, capabilities and shape constraints remain independent.

Historical instruction data changes only the adopted231 parents:219 preserve existing100A clauses and add101A, with101F only where an original110F branch exists;12 block-selector parents append an8.8 historical101A/F clause. All854 original110 conditions and their floors remain, the DNF limit stays six, and the25 restricted cluster gather4/im2col-W parents remain unchanged. TCGEN closed rows retain kind, scaled-D and explicit scale-vector restrictions: written block16/32 historical gates require8.8, while100-only vector and scaled-D paths remain100-only. Generated operational queries first validate the exact source profile and its minimum version, then apply feature matching. Unknown words and addresses remain unresolved obligations, not facts inferred from live registers.

Both module validators independently join current function `source_region`, `source_version` and exact `source_target` to the current owned region. A valid replacement AST cannot mask contradictory caches. Intrinsically coherent101/110 contexts may be retargeted through a supplied header when the current payload is legal under that effective context; retained availability is not a historical veto. Missing full instruction context still follows the existing `AvailableContext` deferral and strict `MissingValidationContext` policy.


<a id="evidence"></a>

## Evidence and reproducible checks

The fixed implementation revision `a81ca9599e654f92e0700f2c001ba1aa676e2d04`
received qualified independent acceptance for this static frontend scope under
the archived manual, default ABI, declared profiles and published
interpretations. This is revision-specific engineering evidence, not an
unconditional assertion of complete ISA implementation or freedom from defects.

The obligation classification was188 shared-predicate proofs,110 finite-model
joins,9 outside-static obligations,1 source-production reference boundary, and5
optional conservative boundaries:313 obligations, not313 passing programs. The
recorded full Debug suite had2359 tests:2357 passed,0 failed,2 explicit macro
skips; its Python component ran720 tests. These are prior acceptance observations,
not newly executed checks for this documentation migration. Earlier dated test,
assembler and build measurements below keep their own fixture/revision scope.

Portable implementation and regression entry points include the
[target catalog](../submod/base/src/ptx_target.cpp),
[parser](../submod/syntax/src/),
[resolved frontend and checker](../submod/resolved_ir/src/),
[C++ regressions](../submod/resolved_ir/test/),
[specification resources](../python/src/ptx_frontend/spec/resources/ptx_spec/),
[Python tests](../python/tests/), and [installed consumer examples](../examples/).
The retained technical references link focused positive/negative controls, source
identity and current-payload checks, ownership/lifetime witnesses and tool
provenance. No new build, CTest, Python suite, assembler or GPU run is implied
by centralizing this documentation.

<a id="syntax-ast-design--cst-ownership-and-representation"></a>

### CST ownership and representation

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
`.file` indices and `.debug_str` section/typed-label identities is performed in a
separate debug-metadata binding namespace, and `.loc` validates those
references. Attaching source locations to instructions or labels remains
deferred. At outermost scope, `.section name { ... }` retains matched braces,
trivia and exact payload tokens in `CstSectionDirective.payload_tokens`. Its
`payload` is the single token-indexed structural parse. The AST `payload`
now contains owning `AstSectionEntry` values (label definitions or typed data),
replacing the old flat `vector<AstSyntax>`. Integer leaves own an optional sign,
original magnitude spelling and ranges; symbolic data distinguishes a single
symbol, checked signed addend and same-section label difference. No expression
AST or second raw semantic payload is retained. Section names are metadata,
not ordinary declarations. `.loc` attachment and offset validation remain
separate. `.pragma` preserves a nonempty
comma-separated string list at module, entry-header, and function/nested-block
statement scope. Pragma strings do not create bound names; the resolved module
owns their advisory records with the actual bound placement scope. Entry-header
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

<a id="symbol-binding-design--scopes-and-symbols"></a>

### Scopes and symbols

Debug identities use a separate module metadata namespace. `.file` indices are
normalized to `uint64_t` (decimal/octal/binary/hex and an optional `u`/`U` suffix) and bind
as `DebugFile` symbols. Every repeated normalized index is an error, even for
identical filenames and metadata; diagnostics identify the current and first index
ranges. The first `SymbolId` survives only for diagnostic recovery and `.loc`
binding. Different indices may name the same file. `SymbolTable::debugFiles()`
borrows source-ordered owned records containing the normalized index, bound symbol,
exact index/filename tokens, optional timestamp/file-size tokens, and their source
ranges; rejected repeats remain recorded. Filename escapes are retained without
decoding or path deduplication, and absent numeric metadata retains its default-zero
meaning. This follows [PTX ISA 9.3 §11.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-file)
and CUDA 13.3 `ptxas` duplicate-index diagnostics. A `.debug_str` section binds its name and each typed `AstSectionLabelDefinition` as `DebugStringLabel` symbols. Ordinary `SymbolTable::lookup()` skips
both debug kinds, so a program declaration may use the same spelling. `.loc`
creates `DebugFile` references for its basic and `inlined_at` file fields, plus
a `DebugFunctionName` reference for `function_name`; the latter can resolve
only the `.debug_str` section itself or one of its labels. These metadata
identities diagnose unresolved references but never behave as PTX operands.

Declaration spelling is checked after lexing, using the ASCII grammar in
[PTX ISA 9.3 §4.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#identifiers):
a letter may stand alone; a leading `_`, `$`, or `%` requires at least one
following letter, digit, `_`, or `$`. Percent is allowed only at the start.
Thus `_x`, `$x`, `%r`, and parameterized bases such as `_x<2>` remain legal,
while ordinary declarations named `_` or `$` receive `InvalidIdentifier` at
the name token. Compact declaration prefixes follow the same rule: `_x<2>`
and `$x<2>` are legal, while `_<2>` and `$<2>` are rejected. This applies to
variables, functions, ordinary parameters,
labels, aliases, and raw `.debug_str` payload labels. Debug file indices and
the synthetic `.debug_str` section identity are outside this name grammar.
The declaration checker inspects AST spellings independently of symbol lookup;
AST-free module validation checks retained names and reports `RuleViolation`
at the declaration name range. Source association still preserves spelling
identity and does not normalize names.

The lexer retains `_` as an identifier for contextual uses. Instruction sinks
remain operand-specific. Under
[PTX ISA 9.3 §11.3.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-directives-callprototype),
`.callprototype` keeps its mandatory function-name `_` and permits dummy `_`
input and return names; those dummy names do not become bound symbols or owned
ABI names. `$` has no corresponding exception.

<a id="symbol-binding-design--parameterized-variable-names"></a>

### Parameterized variable names

Declaration collection compares the represented name sets. A parameterized
declaration that overlaps an explicit name, or another parameterized
declaration with a different base, produces a same-scope duplicate diagnostic
with the previous range. The base itself is not a generated member, so
`name<2>` and an explicit `name` remain distinct symbols.
Ordinary/ordinary comparisons use the two literal spellings; ordinary/group
comparisons test the ordinary spelling against the group's members; and
group/group comparisons test only generated members. Zero-count invalid groups
have no members and do not create overlap candidates, while same-base compact
declarations retain the exact-declaration duplicate policy.

Parameterized names are valid in every state space, but cannot also declare an
array or initializer. The previous `.reg`-only restriction was removed, and
the public CST/AST field is now consistently named `parameterized_count`.


<a id="yaml-instruction-spec--operands-and-operand-patterns"></a>

### Operands and operand patterns

Examples of generated operand kinds include `reg`, `imm`, `reg_or_imm`,
`pred`, `pred_or_not`, `addr`, and `reg_vector`; this is not an exhaustive kind
list. Current value-vector, call, video and matrix carrier permissions are
recorded in their [resolved source-role contracts](#resolved-ir-design). `pred_or_not` accepts `%pN`
or `!%pN` and preserves the complement bit in resolved IR. The schema can
describe many more PTX kinds. Adding a schema enum does not implement it:
Python Syntax/Resolved models, the C++ resolver, and the checker must all be
extended. `type` may reference a modifier (`modifier(type)`) or name a fixed
scalar type such as `u32`. The only supported type-expression function today is
`modifier(name)`, which reads an active `kind: type` modifier of the current
variant. The schema retains `same_as(...)`, `one_of(...)`, and
`same_size_as(...)` as future syntax, but the normalizer explicitly rejects
them as unsupported.

Alternatively, `parameter.entry_only: true` with `direction: input` may
accompany a static state-space allowlist containing `param`. The checker applies
this constraint to a known parameter address even without a state-space modifier,
requiring both input direction and entry-function provenance. The Boolean
defaults to false, preserving existing explicit `.param` consumers; non-Boolean
values, return direction with entry-only, and detached static constraints are
rejected. Suffix-free `prefetch.tensormap` uses this narrow contract. Unknown
standalone or register address provenance remains unknown and accepted.

The scalar form is equivalent to a one-entry list without an additional target
requirement. Static values and `expr: modifier(...)` are mutually exclusive.
The resolved descriptor maps every static value to `MemoryStateSpace` and keeps
its availability. The common checker rejects a known effective space outside
the list and checks availability on the matching entry; an unknown register,
immediate, or standalone address remains accepted. Current generic scalar/vector
loads use the example policy above, while generic scalar/vector stores allow only
`.global/.local/.shared`.

The current scalar/vector `ld/st` variants reuse one type set containing
`.b8/.b16/.b32/.b64`, `.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`, and `.f32`,
then append `.f64` at the variant. Legacy loads additionally model
`.ca/.cg/.cs/.lu/.cv`, legacy stores model `.wb/.cg/.cs/.wt`, and all explicit
cache spellings attach PTX 2.0 / SM 20 availability while omission resolves to the
non-spellable `unspecified` sentinel. PTX's effective hardware defaults still
follow the ISA (`ld` behaves as `.ca`, `st` behaves as `.wb` when the modifier
is omitted), but Resolved IR intentionally preserves `Unspecified` so source
provenance and modifier-value availability stay distinguishable. Explicit `.f64` attaches SM 13
availability; generic `.f64` does not duplicate it because the generic variant
already requires SM 20. Data operands use `type: {expr: modifier(type)}`, so
the runtime modifier and its location drive the common fundamental-type check.
Legacy memory-vector payloads are capped at 128 bits: `.v2` accepts modeled
types through 64 bits and `.v4` through 32 bits. A generated `memory_vector`
constraint additionally permits only 256-bit `.v8` × 32-bit and `.v4` × 64-bit
forms at PTX 8.8/SM 100, with global space when known.
A register operand may also select an explicit width policy:

```yaml
- name: dst
  kind: reg
  role: dst
  access: write
  type: {expr: modifier(type)}
  register_width: equal_or_wider
```

`register_width` defaults to `same_width`, which permits compatible fundamental
types at the same width. `exact` instead requires identical declaration-type
enums; reserve it for explicit format restrictions, not ordinary operands with
a fixed width. See [register declaration compatibility](us-en/register_type_policy.md)
for the normative source and regression boundary. The normalizer rejects the non-default
`equal_or_wider` value on a non-register operand or an operand without a type
expression, preventing a silent no-op. `reg_vector` operands may also use this
policy, applying it to each vector element. The resolved operand descriptor
stores the policy; no runtime Resolved IR field is generated. Current scalar
`ld` destinations, scalar `st` sources, and legacy `.v2/.v4` memory vector
elements use `equal_or_wider`: the declared register size must be at least the
instruction size, after which either-side bit types and signed/unsigned integer
pairs are compatible, floats require exact type/size, and integer/float pairs
remain incompatible. Immediate checks stay same-width. Conversion-only `cvt_scalar_src` and
`cvt_register_src` carriers admit scalar special registers and selected hardware
components; the former also admits immediates. Their special-register checks
apply the declared width policy. Ordinary `reg_or_imm` keeps its existing domain. The
current scalar `ld/st` domains do include `.b128`, with modifier-value
availability at PTX 8.3 / SM 70; `.sys` with `.b128` additionally requires PTX
8.4. An exact `.b128` declaration therefore satisfies a selected `.b128` memory
instruction. The `equal_or_wider` escape hatch still does not admit a wider
actual `.b128` register for a narrower selected instruction. The focused
[LD](us-en/converage/ld_coverage.md) and [ST](us-en/converage/st_coverage.md) documents state the supported
memory forms and their remaining boundaries.

`vector.allow_named` is a Boolean, default false. Its ordinary schema domain is
aggregate- or element-policy `reg_vector`, or read-only, nonsink, element-policy
`value_vector`. The dedicated named `matrix_fragment` branch instead requires
exactly `allow_named: true` and `type_policy: element`, fixed pack cardinality
2 or 4, and no `vector.arity`. These schema permissions do not opt in unlisted
source roles. The flag propagates to the binding and `OperandDescriptor::allow_named_vector`,
adding identifier syntax only to the opted slot. Ordinary LD/LDNC/LDU destinations
and ordinary ST sources opt in. Additional admitted roles include selected root
`st.async`, multimem and SULD register vectors, SUST value vectors, and selected
matrix/TCGEN named operands; each retains its own arity, type and provenance
conditions. See [named data vectors](#resolved-ir-design--named-data-vectors),
[named matrix fragments](#resolved-ir-design--named-matrix-fragments), and
[TCGEN masks](#resolved-ir-design--tcgen-mandatory-named-output-lane-masks).
No permission is inferred for an unlisted bulk/async or other operand role.
The named declaration remains limited to v2/v4 and 128 bits, independently of
modern 256-bit brace support. See [owned provenance](us-en/resolved_ir_design.md#named-memory-vector-sources).

```yaml
vector: {arity: [2, 4], type_policy: aggregate, allow_sink: true}
```
For memory vectors, the generated cross rule permits partial sinks only for an
exact 256-bit modern payload; legacy vectors and all-sink forms remain invalid.

```yaml
- name: dst
  kind: reg_vector
  role: dst
  access: write
  type: {expr: modifier(type)}
  register_width: equal_or_wider
  vector: {arity: {expr: modifier(vector)}, type_policy: element}
```

`type_policy: aggregate` checks a vector payload against the whole instruction
bit width and is used by `mov` pack/unpack. `type_policy: element` checks each
element against the instruction type and is used by legacy memory vectors.
Those memory vectors retain their 128-bit legacy forms (`.v2` through 64-bit
types and `.v4` through 32-bit types) and add only the PTX 8.8 256-bit forms
described above.
`VectorArity` is a required modifier domain; it has no optional/default form.
