# Declaration Semantics Design

## Position and API

Declaration semantics runs after lexical binding and before Resolved IR. Its
public entry point is:

```cpp
auto diagnostics =
    declaration_semantics::checkDeclarations(module, binding.table);
```

The pass uses the structured Syntax AST for initializer and array constraints,
and the module declaration sequence for cross-declaration compatibility.
`resolveModule()` runs both binding and this pass automatically and accumulates
their diagnostics before resolving any instruction.

## Shared numeric evaluation

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
[bitwise policy](bitwise_constant_policy.md). Expression results retain their
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

## Arrays and initializers


Register declarations cannot use bracket array dimensions. The rule applies to
unused declarations and reports `InvalidRegisterDeclarationShape` at the first
bracket dimension, independently of type or vector diagnostics. Compact register
groups (`r<2>`) remain distinct. See [register declaration compatibility](register_type_policy.md)
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

## Unified UUID attributes

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
open calls are not certified acyclic. See [control flow](control_flow_syntax_design.md).

## Redeclarations

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

## Control-flow metadata

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
module validation; the [current limits](converage/coverage_status.md) still apply.

Call-table eligibility is checked only when ordinary storage is consumed as an
indirect-call flist. The module layer reuses owned sparse storage relocations and
this pass's complete `FunctionSignature` equality, preserving the exact-token
prior-function rule. Repeated table targets and implicit zero tails are permitted;
this differs deliberately from `.calltargets` duplicate policy. Unconsumed mixed
function/numeric or mixed-signature data remains ordinary legal storage.

## Parameter declarations

Parameters have a context-aware [coverage and validation contract](parameter_declarations.md),
including supported types, unsized-array placement, pointer attributes, and
statically decidable ISA version/target and entry-size limits. This applies to
entry/device headers, call-prototype formals, and body-local `.param`
declarations; retained type spelling or array syntax alone does not establish validity.

Parameter element classification reuses fundamental metadata, excludes predicates,
and includes native scalar F16x2 without flattening its packed components. Validated vector
lanes participate in checked object bytes, natural alignment and padded entry totals;
unused prototype signatures receive the same shape, alignment and role checks.

## Entry resource constraints

For the supported entry-header `.maxnreg`, `.maxntid`, `.reqntid`, and
`.minnctapersm` directives, this pass compares a declared module `.version`
with their PTX minima (1.3, 1.3, 2.1, and 2.0 respectively). All four are
supported on every SM, so `.target` does not add a check here. `.reqntid` and `.maxntid`
are mutually exclusive within one entry and diagnose the later directive with
the earlier range as context. A lone `.minnctapersm` is a PTX warning rather
than an error; warning severity, backend resource feasibility, and numerical
limits remain outside this pass.

## Debug metadata boundary

Binding owns the `.file`/`.loc` and `.debug_str` identity table: every duplicate
normalized file index diagnoses with current/previous ranges, `.loc` file references must bind, and
`function_name` must identify `.debug_str` itself or one of its raw labels.
This declaration pass intentionally adds no DWARF payload-expression, source
attachment, or resource-feasibility semantics.

## Current boundary

After declaration checking, module resolution projects the supported storage
forms into [owned storage metadata](storage_declarations.md), including checked
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
[the signedness decision and reproducible assembler evidence](bitwise_constant_policy.md)
for the conflicting summary-table wording and tool-version limitations.

`FunctionParameterContract::array_extents` is the sole normalized formal-axis
representation. Valid arrays compare by checked total element count for ABI
compatibility, independently of rank; invalid structural axes and overflow remain
invalid and retain their axis identity. Unknown outer dimensions preserve checked
known inner axes. Derived `CallArgumentProperties::array_size` is a total count,
not a cached first axis. Exact source identity remains separate; see
[parameter contracts and member migration](parameter_declarations.md).

Register-state device and call-prototype formals have a separate default-ABI
minimum: when known PTX >=2.0 and the active recognized target is SM20+, the whole
scalar/vector value must be at least32bits. This measures explicit vector total
width, not each lane, and preserves source type/shape. It applies to unused input
and return formals too, with the supplied AST context for source-aware retargeting
and owned regions after AST death. Known legacy contexts disable the minimum;
missing context defers it under partial checking. Ordinary small/predicate registers
and compatible small actuals to `.param` formals remain legal. See
[parameter declarations](parameter_declarations.md) for the complete boundary.

Instruction-root spellings remain ordinary identifiers in explicit declaration
name roles under the [contextual keyword interpretation](lexer_design.md).
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


## Historical entry body inputs

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
physical launch packing. See the [parameter contract](parameter_declarations.md)
for context windows, intrinsic guarantees and preserved limitations.


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
