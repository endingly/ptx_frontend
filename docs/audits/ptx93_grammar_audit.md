# PTX ISA 9.3 grammar and declaration audit

**Status: scoped audit complete; one directive interpretation remains open.** This
is an evidence record for the lexical, syntax, declaration, operand, ABI, and
directive scope listed below. It is not a complete PTX conformance verdict or a
replacement for [`sdk_support.md`](../sdk_support.md).

## Baseline and source

- Repository revision: `1b44ad8212aa254610b3afb447c9e774df03710d`, branch
  `audit/ptx93-manual-conformance`.
- Normative source: [NVIDIA CUDA 13.3 PTX ISA 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html),
  title `1. Introduction — PTX ISA 9.3 documentation`.
- Retrieved HTML: 3,577,575 bytes; SHA-256
  `40026f79b65fee3f8c8b3e717c6f80c6385e8d97215318d6e4b7f6106f58d5a4`.
  Section text used for inspection is under `/tmp/ptx93-manual/sections/`;
  the structural index is `/tmp/ptx93-manual/index.tsv`.
- `docs/sdk_support.md` was read as the maintained description of current
  behavior and limitations, not as the normative oracle.

## Scope and evidence labels

The audit covered manual chapters 4, 5, 6, 7, and 11, with focus on lexer and
parser behavior, declaration semantics, static binding, module configuration,
and ABI metadata. For chapter 6, the review was limited to common operand rules
and address, array, vector, label, and function-name forms. It did not audit all
instruction-specific operand tuples; the separate opcode audit owns that scope.
Chapter 5's texture, sampler, surface, and tensor object forms were inspected
only where they intersect ordinary declaration syntax and storage.

- **Source inspected** means the manual text was compared with implementation
  paths and existing focused tests. It does not establish execution of every
  legal or illegal form.
- **Executed** refers only to deterministic probes run by the shared builder and
  retained in [`ptx93_probes/results.json`](ptx93_probes/results.json). I did
  not build the common tree or run tests.
- **Candidate/reference gap** means the official wording/example and retained
  frontend interpretation do not establish a conformance defect by themselves.
- No production, test, or maintained-support-contract file was changed for this
  audit.

## Coverage ledger

| Manual scope | Source comparison and repository evidence | Evidence state |
|---|---|---|
| §4.1–4.3 source form, comments, statements, guards | Lexer rules in `submod/lexer/src/ptx_lexer.l:66-103`; CST and syntax parsers in `submod/cst/src/` and `submod/syntax/src/`; token, recovery, and statement tests in `submod/lexer/test/`, `submod/cst/test/`, and `submod/syntax/test/`. | Source inspected; tests read, not run here. |
| §4.4 identifiers | Manual grammar compared with `base::isIdentifier` in `submod/base/include/ptx_identifier.hpp:13-24` and declaration checks in `submod/semantic/src/ptx_declaration_semantics.cpp`. | Source inspected; no 1024-character probe run in this task. |
| §4.5 constants and expressions | Lexer literal rules at `ptx_lexer.l:89-99`; CST expression parsing and AST lowering; expression typing/evaluation in `submod/semantic/src/ptx_declaration_semantics.cpp:90-620`; relevant tests include `submod/semantic/test/test_ptx_declaration_semantics.cpp` and `submod/resolved_ir/test/test_warp_size_literals.cpp`. | Source inspected; tests read, not run here. |
| §5.1 state spaces and §5.2 types | State-space/type declarations compared with semantic checking and storage resolution. Special cases reviewed include `.pred`, `.tex`, opaque resource declarations, legacy module-scope `.reg`/`.local`, and formal parameter spaces. | Source inspected; executed probes cover selected global/register vectors, not all declaration spaces or formal roles. |
| §5.3 resource objects; §5.4 declarations, vectors, arrays, initialization, alignment, names, attributes | Variable checking in `ptx_declaration_semantics.cpp:2429-2570`; storage layout and type/state validation in `submod/resolved_ir/src/ptx_storage_declarations.cpp:932-1088`; tests in `test_ptx_declaration_semantics.cpp` and `test_storage_declaration_metadata.cpp`. | Source inspected for ordinary declaration paths; opaque resource behavior is a supported subset. |
| §6.1–6.4 common operand rules and address/array/vector/name forms | Address offsets and bound address operands in `submod/resolved_ir/src/ptx_resolved_ir_bound_operands.cpp:1107-1592`; named-array displacement and retained-address validation in `submod/resolved_ir/src/ptx_named_array_address.cpp:25-118`; vector/member resolution in `ptx_resolved_ir_bound_operands.cpp`; related tests under `submod/resolved_ir/test/`. | Source inspected; opcode-specific rules excluded. |
| §7.1 function interfaces and §7.2–7.3 variadic/stack-allocation references | Formal validation and ABI/version checks in `ptx_declaration_semantics.cpp:1880-2155`; call visibility in `submod/resolved_ir/src/ptx_call_target_visibility.cpp:48-104`; call ABI and resolution tests in `submod/semantic/test/` and `submod/resolved_ir/test/`. | Source inspected. No independent full ABI probe was run here. |
| §11.1 module directives | CST/parser ordering and source configuration are checked in `submod/resolved_ir/src/ptx_module_availability.cpp`; related target/address/version tests include `test_ptx_target_identity.cpp` and `test_resolved_module_target_checking.cpp`. | Source inspected. |
| §11.2–11.4 function/control/performance directives | `.entry`/`.func`/`.alias`, `.callprototype`, `.calltargets`, `.branchtargets`, and retained resource/ABI attributes compared with semantic checks and directive tests. | Source inspected within the implemented subset. |
| §11.5 debug directives | `.file` and `.loc` binding in `submod/binding/src/ptx_symbol_table.cpp:838-859` and `:1040-1070`; owned-file revalidation in `submod/resolved_ir/src/ptx_module_availability.cpp:3316-3422`; focused tests in `test_debug_file_metadata.cpp`. | Source inspected; repeated-index case executed. |
| §11.6–11.8 linkage, cluster, and miscellaneous directives | Qualifier/declaration checks in `ptx_declaration_semantics.cpp`, source-configuration and availability checks in `ptx_module_availability.cpp`, plus focused directive tests. | Source inspected within the implemented subset. |

## Findings and candidate evidence

### Vector declaration limits: source-consistent; initial suspected gap withdrawn

The manual §5.4.2 states: “Vectors of length 2 and 4 of any non-predicate
fundamental type” are supported, and “Vectors cannot exceed 128-bits in length.”
The register path enforces `.v2`/`.v4`, rejects predicate vectors, and checks
`metadata->size_bytes * vector_width > 16` in
`submod/semantic/src/ptx_declaration_semantics.cpp:2429-2484`. Formal parameters
perform the same non-predicate and 128-bit checks at `:1918-1937`; body `.param`
declarations check width at `:2367-2379`. Addressable storage rejects `.pred`
at `submod/resolved_ir/src/ptx_storage_declarations.cpp:964-968` and applies the
128-bit width check at `:1047-1053`. Texture and opaque declarations have
separate scalar/nonvector restrictions at semantic `:2488-2498` and storage
resolution `:979-986`.

The shared probes accepted `.global .v2 .f64 V;` and rejected
`.global .v4 .f64 V;` with `UnsupportedStorageDeclaration: Storage vector width
exceeds the supported 128-bit PTX limit.` They also rejected `.global .v2 .pred`
as addressable predicate storage, rejected `.reg .v4 .f64`, accepted an
exactly-sized initializer and an inferred first array dimension, and rejected an
initializer with excess elements. The specific cases are `global-v2-f64`,
`global-v4-f64`, `global-v2-pred`, `reg-v4-f64`, `array-fixed-exact`,
`array-inferred`, and `array-fixed-excess` in `docs/audits/ptx93_probes/`.
This removes the suspected general storage-width gap and adds evidence for
predicate storage, register width, and initializer shape. Other
shared/local/constant and formal-parameter variants were source-inspected but
not independently executed in these fixtures. No vector-width mismatch is
reported.

### `.file` repeated index: candidate/reference gap, not confirmed defect

Manual §11.5.3 says `.file` associates a source filename with an integer index.
Its example lists `.file 1 "example.cu"`, `.file 2 "kernel.cu"`, then
`.file 1 "kernel.cu", 1339013327, 64118`. The current implementation rejects
every repeated normalized ID: `collectDebugFile` binds each directive
to the same metadata name in `submod/binding/src/ptx_symbol_table.cpp:838-859`,
and `submod/resolved_ir/src/ptx_module_availability.cpp:3384-3388` independently
reports duplicate indices. `docs/sdk_support.md` records this as an intentional
retained interpretation around lines 3589, 3688, and 8905-8909; focused negative
cases are in `submod/resolved_ir/test/test_debug_file_metadata.cpp:20-64`.

The builder executed the manual-style sequence above (`file-repeated.ptx`):
parsing succeeded, then resolution failed with `Binding/DuplicateSymbol` on the
second index-1 declaration. A single `.file 1 "kernel.cu", 1339013327, 64118`
declaration (`file-single.ptx`) parsed and resolved. The fixture results are in
[`ptx93_probes/results.json`](ptx93_probes/results.json). These results establish implementation behavior, not the
meaning of duplicate indices in the manual example. The manual does not explain
whether that example is one sequential PTX module or defines replacement
semantics. Authority review classifies the current uniqueness rule as a retained
stricter interpretation/reference gap; this report does not call it a confirmed
PTX defect. A future adjudication needs a manual clarification or an external
tool result tied to the exact sequence.

### Source-format preprocessor boundary

Manual §4.1 says the C preprocessor `cpp` may process PTX and that lines
beginning with `#` are preprocessor directives. The lexer has no `#` rule and
falls through to `TokenKind::Error` (`submod/lexer/src/ptx_lexer.l:250-253`).
The maintained syntax matrix describes tokens as partial but does not claim an
integrated C preprocessor. This is therefore recorded as an API boundary to
clarify, not a confirmed gap: callers may preprocess before passing PTX to this
frontend. No preprocessing behavior was executed in this audit.

## Consistency notes and exclusions

- **Identifiers (§4.4):** the manual permits letter-leading names, or names
  beginning `_`, `$`, or `%` followed by one or more identifier characters.
  `base::isIdentifier` implements that ASCII character rule; it rejects
  standalone `_`, `$`, and `%` as ordinary identifiers. Contextual sinks are
  handled separately. The lexer token regex is slightly broader, but semantic
  identifier validation applies the manual rule. No length cap was found in
  the inspected declaration/binding paths. The manual's suggested minimum
  support of 1024 characters was not tested.
- **Constants (§4.5):** integer radix/suffix tokens, decimal and exact hex float
  tokens, `WARP_SZ`, operator trees, and exact-f32 literal restrictions have
  corresponding implementation and tests. `WARP_SZ` is materialized as signed
  32 in `submod/resolved_ir/src/ptx_resolved_ir_literals.cpp:573+`. Exact `0f`
  literals are treated as direct values rather than operands inside numeric
  constant expressions, consistent with §4.5.2. No exhaustive arithmetic edge
  matrix was executed as part of this audit.
- **Declaration forms (§5):** source checks align with the manual's `.v2` and
  `.v4` fundamental vectors, positive constant array extents, initializer
  brace shapes, global/constant initializer spaces, power-of-two storage
  alignment, and parameterized-name restrictions. Function-scope register and
  local-memory placement is conditioned on the documented ABI/version/target
  boundary; focused tests cover legacy and modern contexts.
- **ABI (§7):** no `...` variadic prototype form appears in the 9.3 grammar.
  Section 7.2 says unimplemented variadic support was removed and points to a
  final unsized `.param .b8` input as a way to implement it. The frontend checks
  that shape and version/target availability. Calls require a prior function
  declaration through the resolved call-visibility path. Physical stack layout,
  register allocation, runtime bounds, and actual ABI execution are outside this
  static frontend audit.
- **Directive order (§11.1):** manual requirements reviewed were one initial
  `.version`, an immediately following first `.target`, subsequent target
  regions, and optional `.address_size` directly after the initial target and
  at most once. Source configuration and module validation paths retain ordered
  regions and explicit/default address width; no mismatch was identified in the
  inspected checks.
- **Historical/partial directives:** banked `.const[bank]` syntax describes a
  pre-2.2 form and was not treated as a current 9.3 required form. Legacy `.tex`
  is explicitly deprecated but retained in the manual; the frontend models its
  scalar `.u32`/`.u64` declaration boundary. Debug section/DWARF payloads and
  resource/linker/backend effects are represented only by the documented
  frontend subset.
- **Fixed addresses:** §5.4.1 mentions an optional fixed variable address, but
  the inspected description supplies no determinate syntax or semantics to
  compare. This remains a reference ambiguity, not a rejection finding.

## Execution record and limits

The executed probes cited here are the builder's selected vector, array, and
`.file` fixtures retained in
[`ptx93_probes/results.json`](ptx93_probes/results.json). I did not run project
tests, build the common tree, fuzz, or invoke NVIDIA tools. Existing tests were
read as implementation evidence. In particular, “source inspected” does not
prove all directive options, targets, initializer combinations, expression edge
cases, or ABI interactions.

**Result:** no confirmed mismatch was established in this bounded grammar and
declaration review. The repeated `.file` index is the sole concrete candidate
requiring reference interpretation; preprocessor input is an API-boundary
question. Opcode-form completeness and runtime/tool conformance remain outside
this report.
