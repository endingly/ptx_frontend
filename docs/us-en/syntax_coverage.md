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
| Formal parameters | Supported subset | `.reg`/`.param`, alignment, scalar type, pointer space/alignment, sized and unsized arrays |
| Variable declarations | Supported subset | Module/function scope, linkage qualifiers, `.reg`/`.param`/`.local`/`.shared`/`.global`/`.const`, alignment, vector/base type, register banks, and multidimensional arrays |
| Function body | Supported subset | Variable declarations, labels, and supported instruction syntax |
| Declaration extensions | Not supported | Initializers, fixed addresses, and fully parsed constant expressions in array dimensions |
| Other directives | Not supported | Debug, section, pragma, module variable, and structured kernel-tuning directives |
| Structured control syntax | Not supported | Nested scopes and directive-driven control-flow metadata |
| Recovery/editing | Not supported | Missing tokens, recovery nodes, multi-error parsing, and token edits |
| Resolved opcodes | Partial | Only opcodes present in the YAML database; currently `add`, `sub`, and `bar` |

The lexer may tokenize source outside this matrix, and Syntax AST may retain an
unknown opcode as text. Neither behavior means that the construct can be
lowered to Resolved IR.

## Near-term order

1. Add declaration initializers, fixed addresses, and constant expressions.
2. Add call-specific and branch-specific operand grammar.
3. Represent kernel tuning and remaining module directives.
4. Build symbol tables after declaration and parameter syntax is stable.
5. Expand YAML instruction coverage independently of module grammar work.
