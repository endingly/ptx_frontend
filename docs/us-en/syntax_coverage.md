# PTX Syntax Coverage

## Purpose

This matrix describes implemented parser behavior. It is not a claim of full
PTX ISA support. The reference grammar is NVIDIA's
[PTX ISA documentation](https://docs.nvidia.com/cuda/parallel-thread-execution/).

| Area | Status | Implemented subset |
| --- | --- | --- |
| Tokens and trivia | Partial | Identifiers, dot identifiers, literals, punctuation, comments, whitespace, and selected stable directives |
| Instruction fragment | Partial | Predicate guard, opcode/modifiers, ordinary operands, addresses, vector members, and vector packs |
| Module header | Supported subset | `.version`, `.target`, `.address_size` |
| Functions | Supported subset | `.entry`/`.func` definitions, `.func` prototypes, visibility/linkage qualifiers, return and input parameter lists, `.noreturn` |
| Formal parameters | Supported subset | `.reg`/`.param`, alignment, scalar type, pointer space/alignment, and arrays sized by structured constant expressions |
| Variable declarations | Supported subset | Module/function scope, linkage qualifiers, `.reg`/`.param`/`.local`/`.shared`/`.global`/`.const`, alignment, vector/base type, parameterized names, multidimensional arrays, and `.global`/`.const` initializers |
| Function body | Supported subset | Variable declarations, labels, and supported instruction syntax |
| Constant expressions | Supported subset | Literals/symbols, parentheses, `.s64`/`.u64` casts, unary/binary/conditional operators, `generic(symbol)`, and mask initializer operators |
| Initializers | Supported subset | Scalar expressions, recursive brace lists, and an unsized first dimension; `.extern`, parameterized-name, and non-`.global`/`.const` initializers are rejected |
| Symbol binding | Supported subset | Module/function scopes, variables/parameters/functions/labels, local shadowing, parameterized members, and instruction/initializer/dimension references |
| Declaration semantics | Not supported | Initializer type/dimension/element-count validation and linkage-compatible redeclarations |
| Other directives | Not supported | Debug, section, pragma, module variable, and structured kernel-tuning directives |
| Structured control syntax | Not supported | Nested scopes and directive-driven control-flow metadata |
| Recovery/editing | Not supported | Missing tokens, recovery nodes, multi-error parsing, and token edits |
| Resolved opcodes | Partial | Only opcodes present in the YAML database; currently `add`, `sub`, and `bar` |

The lexer may tokenize source outside this matrix, and Syntax AST may retain an
unknown opcode as text. Neither behavior means that the construct can be
lowered to Resolved IR.

## Near-term order

1. Carry bound register/symbol `SymbolId` values into Resolved IR and the checker.
2. Validate initializer types, array shapes, and element counts.
3. Add call-specific and branch-specific operand grammar.
4. Represent kernel tuning and remaining module directives.
5. Expand YAML instruction coverage independently of module grammar work.

The PTX ISA variable-declaration overview mentions an optional fixed address,
but the current specification provides no separate grammar, constraints, or
examples. The frontend will not invent syntax from that sentence; a node will
be added only when normative grammar or verifiable `ptxas` behavior is
available.
