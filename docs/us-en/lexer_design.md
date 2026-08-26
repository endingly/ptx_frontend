# PTX Lexer Design

## Overview

The PTX lexer converts source text into a stream of tokens with owned text and
source ranges. It is implemented with a reentrant Flex scanner and exposed
through the C++ `PtxLexer` class.

The lexer deliberately performs only lexical classification. It recognizes
punctuation, literals, identifiers, selected stable directives, whitespace,
and comments. It does not maintain an exhaustive list of PTX instructions,
instruction suffixes, types, shapes, cache operators, or scopes.

The lexer source tree is `submod/lexer/`. Its main implementation files are:

- `submod/lexer/src/ptx_lexer.l`: Flex rules, scanner configuration, and position tracking.
- `submod/lexer/src/ptx_lexer.cpp`: C++ ownership wrapper around the generated scanner.
- `submod/lexer/include/ptx_lexer.hpp`: public lexer interface and token representation.
- `submod/lexer/include/ptx_token.hpp`: token kinds and Flex semantic value types.
- `submod/lexer/test/test_ptx_lexer.cpp`: lexer behavior tests.

## Design Goals

The lexer is designed to provide:

1. Stable lexical behavior as the PTX instruction set grows.
2. Owned token text that remains valid after the lexer advances or is destroyed.
3. Accurate one-based line and column ranges.
4. Independent scanner state for every `PtxLexer` instance.
5. One-token lookahead without exposing Flex implementation details.
6. Explicit error tokens for malformed or unknown input.

The lexer is not intended to:

- Validate instruction syntax or modifier combinations.
- Interpret the semantic meaning of identifiers or dot-prefixed names.
- Convert numeric text into C++ numeric values.
- Recover from every malformed construct automatically.
- Intern or deduplicate token strings.

## Public Interface

The public lexer interface is:

```cpp
class PtxLexer {
 public:
  struct Token {
    TokenKind kind;
    std::string text;
    SourceRange range;
  };

  explicit PtxLexer(std::string_view src);

  Token next();
  Token peek();
  Token consume();
};
```

`next()` always requests the next token directly from the scanner.

`peek()` returns the next token without consuming it from the C++ interface. A
single cached token implements this behavior. Repeated calls to `peek()` return
copies of the same token.

`consume()` returns the cached token when one exists; otherwise it behaves like
`next()`.

Calling `next()` directly while a token is cached by `peek()` bypasses the
cache. Callers should use either the `next()` iteration style or the
`peek()`/`consume()` style consistently.

`PtxLexer` is neither copyable nor movable. Each instance owns a Flex scanner,
its input buffer, position state, and optional lookahead token.

## Scanner Integration

Flex generates `_ptx_lexer.cpp` and `_ptx_lexer.hpp` in the private generated
build directory. These files are implementation details and are hidden behind
the `PtxLexer::Impl` Pimpl type.

The scanner uses the following Flex options:

```text
reentrant
bison-bridge
noyywrap
nounput
noinput
extra-type="ptx_frontend::PtxLexerExtra*"
```

`reentrant` gives each lexer its own `yyscan_t` instead of using global scanner
state. Separate lexer instances may therefore be used independently. A single
instance is not designed for concurrent access.

`yy_scan_bytes` copies the constructor input into a Flex-owned buffer. The
caller's source buffer does not need to remain alive after `PtxLexer`
construction.

The scanner lifetime is:

1. Allocate `PtxLexer::Impl`.
2. Initialize `yyscan_t` with `yylex_init`.
3. Attach `PtxLexerExtra` with `yyset_extra`.
4. Copy the input into a Flex buffer with `yy_scan_bytes`.
5. Delete the buffer with `yy_delete_buffer` during destruction.
6. Destroy the scanner with `yylex_destroy`.

## Text Ownership

During a Flex action, `yytext` points into the scanner's internal buffer. The
Flex semantic value temporarily exposes that text as a `std::string_view`:

```cpp
yylval->sv = std::string_view(yytext, yyleng);
```

This view is used only until `yylex` returns. `PtxLexer::next()` immediately
copies it into `Token::text`, which is a `std::string`:

```cpp
return Token{kind, std::string(sval.sv), sval.range};
```

Consequently:

- A token owns its spelling.
- Advancing the scanner cannot invalidate an earlier token's text.
- Destroying the lexer cannot invalidate a returned token's text.
- No global string pool or `ptx_intern` function is required.

The cost is one string construction per emitted token. This is intentional: the
current design prioritizes explicit ownership and a simple public contract over
string interning or zero-copy token storage.

## Token Classification

### Punctuation

Individual punctuation characters have dedicated token kinds, including:

```text
, . : ; @ ( ) [ ] { } < > <= >= << >> - + * / % & && ^ | || ! ~ ? = == !=
```

A leading sign is not part of a numeric token. For example, `-1` produces
`Minus` followed by `Decimal`. Multi-character operators have dedicated token
kinds so the declaration constant-expression parser can retain exact
operators and precedence.

### Literals

The lexer recognizes:

- Decimal integers, with an optional `u` or `U` suffix.
- Hexadecimal integers beginning with `0x` or `0X`, with an optional unsigned
  suffix.
- PTX bit-pattern floating literals `0f` plus eight hex digits.
- PTX bit-pattern double literals `0d` plus sixteen hex digits.
- Decimal floating literals with a fractional part or exponent.
- Double-quoted strings containing escaped characters.
- The built-in spelling `WARP_SZ`.

Literal tokens preserve their exact source spelling. Numeric conversion and
string unescaping are intentionally outside the lexer.

### Generic Identifiers

Ordinary identifiers use the following shape:

```text
[A-Za-z_$%][A-Za-z0-9_$%]*
```

They are emitted as `TokenKind::Ident`. This category includes instruction
names, registers, special registers, labels, symbols, and target names such as
`sm_80`.

Keeping instruction names generic avoids changing the lexer whenever PTX adds
an instruction.

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

### Dedicated Directive Tokens

A small set of stable, structurally important PTX directives has dedicated
token kinds. These include:

- Module and debug directives such as `.version`, `.target`, and `.file`.
- Visibility and linking directives such as `.visible` and `.extern`.
- Function directives such as `.entry` and `.func`, plus the structurally
  significant function-local `.callprototype` metadata directive.
- Kernel tuning directives such as `.maxnreg` and `.reqntid`.
- Declaration directives such as `.reg`, `.global`, and `.param`.

These rules appear before the generic `DOT_IDENT` rule. Flex selects the longest
match and uses rule order to resolve equal-length matches, so a dedicated
directive wins over the generic category.

A dedicated spelling keeps its token kind in every lexical context. For
example, `.global` is always `DotGlobal`, even when it appears in a sequence of
dot-prefixed names. Consumers that accept both categories should use the token
spelling where appropriate.

## Whitespace and Comments

Spaces, tabs, and line endings are skipped.

Line comments begin with `//` and continue to, but do not consume, the line
ending as part of the comment rule. The following whitespace rule consumes the
line ending.

Block comments use an exclusive `BLOCK_COMMENT` scanner state:

1. `/*` enters the state.
2. All content is skipped until `*/`.
3. `*/` returns to the initial state.
4. End of input inside the state emits `TokenKind::Error`.

Block comments are not nested.

Skipped text still passes through `YY_USER_ACTION`, so whitespace and comments
advance source positions even though they do not produce tokens.

## Source Locations

Source positions are one-based. A new lexer starts at line 1, column 1.

Every matched rule runs `YY_USER_ACTION`:

1. Save the current position as the token start.
2. Advance over every matched byte.
3. Save the resulting position as the token end.

`SourceRange` therefore represents a half-open range `[start, end)`. For a
single-character token at column 4, the range is column 4 through column 5.

Line endings are handled as follows:

- `\n` increments the line and resets the column to 1.
- `\r` does the same.
- `\r\n` is treated as one line ending when both bytes are part of the same
  Flex match.

Columns count bytes, not Unicode code points. PTX identifiers are currently
restricted to ASCII, so this matches the accepted lexical grammar.

EOF and an unterminated block comment use a zero-width range at the current
position.

## Error Behavior

An unknown character is emitted as `TokenKind::Error` with the offending byte
in `Token::text` and its normal source range.

An unterminated block comment emits `TokenKind::Error` with empty text and a
zero-width range at end of input.

The lexer reports errors as tokens rather than throwing exceptions. A caller
may stop at the first error or request additional tokens when recovery is
appropriate.

EOF is emitted as `TokenKind::Eof`, with empty text and a zero-width current
position range.

## Build Integration

CMake uses `FLEX_TARGET` to generate the scanner implementation and header in
the private generated directory. The generated source is compiled into the
`lexer` target together with `submod/lexer/src/ptx_lexer.cpp`.

Changes to `submod/lexer/src/ptx_lexer.l` cause the scanner to be regenerated
as part of a normal build.

## Testing Strategy

When `BUILD_TESTING` is enabled, CMake builds lexer tests as the independent
`test_lexer` executable. They do not share a test executable with higher-level
components.

The current test suite covers:

- EOF emission.
- Generic instruction identifiers.
- Generic type and modifier suffixes.
- Compound and numeric-leading dot identifiers.
- Dedicated module, function, visibility, and declaration directives.
- Register declarations, memory operands, and predicate punctuation.
- Single- and multi-character constant-expression operators.
- Integer, hexadecimal, floating bit-pattern, decimal floating, and string
  literals.
- Whitespace and comment skipping.
- Unterminated block comment errors.
- `peek()` and `consume()` behavior.

Build the lexer test target with:

```sh
cmake --build out/build/ci-linux-gcc-debug --target test_lexer
```

or through CTest:

```sh
ctest --test-dir out/build/ci-linux-gcc-debug --output-on-failure
```

## Known Limitations

- Token text is allocated separately for every emitted token.
- The lexer accepts ASCII identifiers only.
- Block comments cannot nest.
- A `\r\n` sequence inside a block comment is currently consumed as two
  separate Flex matches and therefore advances the line twice. Outside block
  comments, CRLF is normally consumed in one whitespace match and advances the
  line once.
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
