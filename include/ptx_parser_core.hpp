#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ptx_ir/base.hpp"
#include "ptx_ir/source_loc.hpp"
#include "ptx_lexer.hpp"

namespace ptx_frontend {

struct ParsedModifier {
  std::string text;
  std::string name;
  SourceRange range;
};

struct ParsedOpcode {
  std::string text;
  SourceRange range;
  std::vector<ParsedModifier> modifiers;
};

class ParseError : public std::runtime_error {
 public:
  ParseError(SourceRange range, std::string message)
      : std::runtime_error(std::move(message)), range_(range) {}

  [[nodiscard]]
  SourceRange range() const noexcept {
    return range_;
  }

 private:
  SourceRange range_;
};

class ParserCore {
 public:
  explicit ParserCore(std::string_view src);

  PtxLexer::Token peek();
  PtxLexer::Token consume();

  bool consumeIf(TokenKind kind);

  PtxLexer::Token expect(TokenKind kind, std::string_view expected);

  void expectComma();

  PtxLexer::Token expectSemicolon();

  ParsedOpcode parseOpcodeWithModifiers();

  /**
   * Parse one source-level PTX operand.
   *
   * Text parsing always produces ParsedOp.
   */
  WithLoc<ParsedOp> parseOperandWithLoc();

 private:
  static bool isDotLikeToken(TokenKind kind);

  static std::string_view stripDot(std::string_view text);

  PtxLexer lexer_;
};

}  // namespace ptx_frontend
