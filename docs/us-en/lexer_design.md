# PTX Lexer Design

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#lexer-design). This page retains technical reference material and evidence.

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

[SDK support: Token Classification](../sdk_support.md#lexer-design--token-classification).

### Punctuation

[SDK support: Punctuation](../sdk_support.md#lexer-design--punctuation).

### Literals

[SDK support: Literals](../sdk_support.md#lexer-design--literals).

### Generic Identifiers

[SDK support: Generic Identifiers](../sdk_support.md#lexer-design--generic-identifiers).

### Generic Dot Identifiers

[SDK support: Generic Dot Identifiers](../sdk_support.md#lexer-design--generic-dot-identifiers).

### Dedicated Directive Tokens

[SDK support: Dedicated Directive Tokens](../sdk_support.md#lexer-design--dedicated-directive-tokens).

## Whitespace and Comments

[SDK support: Whitespace and Comments](../sdk_support.md#lexer-design--whitespace-and-comments).

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
- `\r\n` is treated as one line ending, including inside block comments.

Columns count bytes, not Unicode code points. PTX identifiers are currently
restricted to ASCII, so this matches the accepted lexical grammar.

EOF uses a zero-width range at the current position. An unterminated block
comment retains the range from its opening `/*` through end of input.

## Error Behavior

[SDK support: Error Behavior](../sdk_support.md#lexer-design--error-behavior).

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

[SDK support: Known Limitations](../sdk_support.md#lexer-design--known-limitations).

## Contextual instruction keywords

[SDK support: Contextual instruction keywords](../sdk_support.md#lexer-design--contextual-instruction-keywords).
