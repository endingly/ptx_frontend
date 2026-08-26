# PTX Syntax Coverage

## Purpose

This matrix describes implemented parser behavior. It is not a claim of full
PTX ISA support. The reference grammar is NVIDIA's
[PTX ISA documentation](https://docs.nvidia.com/cuda/parallel-thread-execution/).

| Area | Status | Implemented subset |
| --- | --- | --- |
| Tokens and trivia | Partial | Identifiers, dot identifiers, literals, punctuation, comments, whitespace, and selected stable directives |
| Instruction fragment | Partial | Predicate guard, opcode/modifiers, ordinary operands, addresses, vector members/packs, and dedicated call/branch operand shapes |
| Module header | Supported subset | `.version`, `.target`, `.address_size` |
| Functions | Supported subset | `.entry`/`.func` definitions, `.func` prototypes, visibility/linkage qualifiers, return and input parameter lists, `.noreturn` |
| Formal parameters | Supported subset | `.reg`/`.param`, alignment, scalar type, pointer space/alignment, and arrays sized by structured constant expressions |
| Variable declarations | Supported subset | Module/function scope, linkage qualifiers, `.reg`/`.param`/`.local`/`.shared`/`.global`/`.const`, alignment, vector/base type, parameterized names, multidimensional arrays, and `.global`/`.const` initializers |
| Function body | Supported subset | Variable declarations, labels, and supported instruction syntax |
| Constant expressions | Supported subset | Literals/symbols, parentheses, `.s64`/`.u64` casts, unary/binary/conditional operators, `generic(symbol)`, and mask initializer operators |
| Initializers | Supported subset | Scalar expressions, recursive brace lists, and an unsized first dimension; `.extern`, parameterized-name, and non-`.global`/`.const` initializers are rejected |
| Symbol binding | Supported subset | Module/function scopes, variables/parameters/functions/labels, local shadowing, parameterized members, and instruction/initializer/dimension/control-flow references |
| Declaration semantics | Supported subset | Positive array extents, inferred first extent, initializer type/brace shape/element limits, symbol addresses, and module linkage-compatible redeclarations |
| Other directives | Not supported (rejected) | Debug, section, pragma, module variable, and structured kernel-tuning directives; unmodeled function-header tokens never silently enter the AST |
| Structured control syntax | Supported subset | `.callprototype`, `.calltargets`, and `.branchtargets` have dedicated function-local CST/AST syntax; binding and declaration semantics validate their labels/members/contracts. Generated `IndirectCall` layouts resolve a `.reg` target plus bound prototype/target-set metadata at PTX 2.1 / SM 20, and module resolution applies the shared call ABI contract; branch-target instruction connection remains unsupported |
| Recovery/editing | Not supported | Missing tokens, recovery nodes, multi-error parsing, and token edits |
| Resolved opcodes | Partial | Only opcodes present in the YAML database; currently `add`, `sub`, `bar`, `bra`, direct and descriptor-backed indirect `call`, selected scalar/vector `mov`, and generic/basic-explicit scalar plus braced-vector `ld`/`st` for `.b8/.b16/.b32/.b64`, `.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`, and `.f32/.f64`; module-resolved direct and indirect `call` share canonical return/input ABI checking for arity/order, `.reg/.param` type/shape, `.param .b8` arrays, pointers, and formal-typed literals. Indirect signatures come from the local prototype or validated target set and retain no call-table support. PTX 8.8/SM 100 modern vectors accept only `.v8` × 32-bit or `.v4` × 64-bit 256-bit payloads, require global space when known, and allow partial `_` sinks. Legacy vectors remain at most 128 bits and reject sinks. Static natural alignment checks bound data symbols plus constant byte offsets and absolute immediate addresses; register and standalone unresolved addresses remain unknown. `ld/st` support omitted/explicit `.weak`, `.volatile`, scoped relaxed/acquire/release, and PTX 8.2 scalar `.mmio.relaxed.sys`, with generated cross-modifier checks; generic loads accept known `.const/.global/.local/.shared` spaces (`.const` requires PTX 3.1), generic stores accept `.global/.local/.shared`, and explicit forms require an exact runtime-modifier match; bound `.param` loads require input parameters and stores require return parameters, with function-context PTX/SM checks; load destination/store source registers and vector elements may be wider under the bit/integer/float kind rules, while other typed operands remain same-width and unknown address identity is not inferred |

The lexer may tokenize source outside this matrix, and Syntax AST may retain an
unknown opcode as text. Neither behavior means that the construct can be
lowered to Resolved IR.

## Implementation priority

The [project roadmap](../../.agents/project_roadmap.md) is the sole authority
for implementation status, dependencies, and priority. This matrix records
capability boundaries only and intentionally does not repeat that ordering.

The PTX ISA variable-declaration overview mentions an optional fixed address,
but the current specification provides no separate grammar, constraints, or
examples. The frontend will not invent syntax from that sentence; a node will
be added only when normative grammar or verifiable `ptxas` behavior is
available.
