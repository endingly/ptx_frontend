#pragma once
#include <stdexcept>
#include <vector>
#include "ptx_ir/base.hpp"
#include "ptx_ir/source_loc.hpp"
#include "ptx_lexer.hpp"

namespace ptx_frontend {

struct ParsedModifier {
  std::string_view text;  // ".sat"
  std::string_view name;  // "sat"
  SourceRange range;
};

struct ParsedOpcode {
  std::string_view text;  // "add"
  SourceRange range;
  std::vector<ParsedModifier> modifiers;
};

class ParseError : public std::runtime_error {
 public:
  ParseError(SourceRange range, std::string message)
      : std::runtime_error(std::move(message)), range_(range) {}

  SourceRange range() const { return range_; }

 private:
  SourceRange range_;
};

class Parser {
 public:
  explicit Parser(std::string_view src);

  PtxLexer::Token peek();
  PtxLexer::Token consume();

  bool consumeIf(TokenKind kind);

  PtxLexer::Token expect(TokenKind kind, std::string_view expected);
  void expectComma();

  // 返回 semicolon token，方便 generated parser 合并 instruction range。
  PtxLexer::Token expectSemicolon();

  ParsedOpcode parseOpcodeWithModifiers();

  template <OperandLike Operand>
  inline WithLoc<Operand> parseOperandWithLoc() {
    auto tok = consume();

    if (tok.kind == TokenKind::Ident) {
      auto value = ParsedOp::from_value(RegOrImmediate<Ident>::Reg(tok.text));
      return WithLoc<ParsedOp>{value, tok.range};
    }

    if (tok.kind == TokenKind::Decimal || tok.kind == TokenKind::Hex) {
      // 第一版可以先简单处理，或者直接 throw。
    }

    throw ParseError(tok.range, "expected operand");
  }

 private:
  static bool isDotLikeToken(TokenKind kind);
  static std::string_view stripDot(std::string_view text);

  PtxLexer lexer_;
};

};  // namespace ptx_frontend