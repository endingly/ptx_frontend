#include "ptx_parser.hpp"

namespace ptx_frontend {

// ── internal parser state ────────────────────────────────────────────────
struct PtxParser {
  PtxLexer lex;

  explicit PtxParser(std::string_view src) : lex(src) {}

  // ── error helper ──────────────────────────────────────────────────────
  tl::unexpected<ParseError> err(const std::string& msg) {
    return tl::unexpected(ParseError{lex.peek().line, msg});
  }

  // ── token helpers ─────────────────────────────────────────────────────
  bool check(TokenKind k) { return lex.peek().kind == k; }

  tl::expected<PtxLexer::Token, ParseError> expect(TokenKind k,
                                                   const char* what) {
    auto tok = lex.peek();
    if (tok.kind != k)
      return err(std::string("expected ") + what + ", got '" +
                 std::string(tok.text) + "'");
    return lex.consume();
  }

  bool match(TokenKind k) {
    if (check(k)) {
      lex.consume();
      return true;
    }
    return false;
  }

  // ── top-level parse ───────────────────────────────────────────────────
  tl::expected<Module, ParseError> parse() {
    Module mod;

    // .version X.Y
    // .target smXXX
    // .address_size 64
    // Then zero or more directives
    //
    // TODO: implement each rule below

    return mod;
  }
};

// ── public API ──────────────────────────────────────────────���─────────────
tl::expected<Module, ParseError> parse_module(std::string_view src) {
  PtxParser p(src);
  return p.parse();
}

}  // namespace ptx_frontend