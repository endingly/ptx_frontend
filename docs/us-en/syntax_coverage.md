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
| Structured control syntax | Not supported | Nested scopes and directive-driven control-flow metadata |
| Recovery/editing | Not supported | Missing tokens, recovery nodes, multi-error parsing, and token edits |
| Resolved opcodes | Partial | Only opcodes present in the YAML database; currently `add`, `sub`, `bar`, `bra`, selected scalar/vector `mov`, and generic/basic-explicit scalar plus legacy `.v2/.v4` braced-vector `ld`/`st` for `.b8/.b16/.b32/.b64`, `.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`, and `.f32/.f64`; legacy vector payloads are at most 128 bits (`.v2` through 64-bit types, `.v4` through 32-bit types; `.v4` 64-bit is deferred); generic loads accept known `.const/.global/.local/.shared` spaces (`.const` requires PTX 3.1), generic stores accept `.global/.local/.shared`, and explicit forms require an exact runtime-modifier match; bound `.param` loads require input parameters and stores require return parameters, with function-context PTX/SM checks; load destination/store source registers and vector elements may be wider under the bit/integer/float kind rules, while other typed operands remain same-width and unknown address identity is not inferred |

The lexer may tokenize source outside this matrix, and Syntax AST may retain an
unknown opcode as text. Neither behavior means that the construct can be
lowered to Resolved IR.

## Near-term order

1. Extend `ld/st` with memory consistency qualifiers, modern vector forms, and
   cross-modifier rules. `.b128` is not part of the current
   scalar family. Function-local call argument `.param`, `::entry`/`::func`,
   and call adjacency/predication remain part of the later call-context work.
2. Add a non-`Flat` descriptor layout algorithm for call groups and variadic operands, then integrate `call`.
3. Represent `.calltargets`/`.callprototype`/`.branchtargets` and remaining module/function directives.
4. Expand YAML instruction coverage independently of module grammar work.

The PTX ISA variable-declaration overview mentions an optional fixed address,
but the current specification provides no separate grammar, constraints, or
examples. The frontend will not invent syntax from that sentence; a node will
be added only when normative grammar or verifiable `ptxas` behavior is
available.
