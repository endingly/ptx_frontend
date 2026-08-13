# Syntax AST design and source-fidelity boundary

## Current responsibility

The current `syntax_ast` is the syntax-structure layer between the lexer and
Resolved IR. It represents opcodes, modifiers, predicates, operands, and their
syntax shapes, while retaining `SourceRange`, node text, and selected leading
trivia for diagnostics.

Its purpose is to support:

- syntax and resolve diagnostic locations;
- YAML-descriptor-driven variant and operand-layout selection;
- semantic `Syntax AST -> Resolved IR` resolution.

## It is not currently source-faithful

`syntax_ast` is **not a lossless AST or a CST**. It is not the backing format
for a formatter or source-preserving rewriting, and `parse -> print` is not
guaranteed to reproduce byte-identical PTX.

The parser rebuilds text for composite nodes such as addresses, immediates,
vector packs, and predicates. Those nodes retain only the leading trivia of
their first token. Whitespace and comments between tokens, trivia adjacent to
commas, semicolons, or delimiters, and complete token boundaries are therefore
not stably represented in the AST.

For example, the comments and layout in this source cannot be reconstructed
from the current AST alone:

```ptx
add /* opcode-type */ .u32 %r1 /* before comma */, %r2, 1 /* trailing */ ;
```

`AstSyntax::text` is for diagnostics, lexical literal interpretation, and
unbound identifier spelling. It is not a formatting API or a resolved symbol
name.

## Relationship to lexer tokens

Each `PtxToken` retains its text and leading trivia, and the EOF token retains
trailing trivia. The lexer can therefore produce a lossless token sequence.
The current streaming parser consumes that sequence without retaining the full
buffer or token spans in `AstInstruction`; source fidelity is lost at that AST
boundary.

## Future CST direction

When the project needs formatting, automated fixes, or source-level rewrites,
it should introduce a dedicated CST layer:

```text
source -> token buffer -> CST -> Syntax AST -> Resolved IR
```

The recommended minimum design is:

- `SyntaxFile` owns immutable source text and the complete token buffer;
- CST nodes retain token ranges for opcodes, modifiers, operands, punctuation,
  and delimiters;
- `SourceRange` gains a source ID and byte offsets, with line/column retained
  for presentation;
- Syntax AST is projected from the CST while retaining its current
  resolution-friendly structure;
- formatting and source rewrites operate as CST/token edits rather than asking
  Resolved IR to recover source layout.

Until that layer exists, new AST APIs must not claim lossless round-tripping or
formatter support.
