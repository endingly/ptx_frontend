#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "ptx_ir/syntax/ptx_syntax_ast.hpp"
#include "ptx_lexer.hpp"

namespace ptx_frontend {

struct SyntaxParseDiagnostic {
  SourceRange range;
  std::string message;
};

/** Parses PTX instruction syntax for diagnostics and semantic resolution. */
class PtxSyntaxParser {
 public:
  explicit PtxSyntaxParser(std::string_view source);

  std::expected<syntax_ast::AstInstruction, SyntaxParseDiagnostic>
  parseInstruction();

 private:
  using AstOperand = syntax_ast::AstOperand;
  using Token = PtxLexer::Token;

  [[nodiscard]] Token peek();
  Token consume();
  [[nodiscard]] bool atImmediateStart();

  std::expected<Token, SyntaxParseDiagnostic> expect(TokenKind kind,
                                                     std::string_view name);
  std::expected<syntax_ast::AstImmediate, SyntaxParseDiagnostic> parseImmediate(
      bool allow_sign = true);
  std::expected<AstOperand, SyntaxParseDiagnostic> parseOperand();
  std::expected<AstOperand, SyntaxParseDiagnostic> parseAddress(bool bracketed,
                                                                Token open);
  std::expected<AstOperand, SyntaxParseDiagnostic> parseVectorPack(Token open);

  static syntax_ast::AstSyntax syntaxFrom(Token token);
  static syntax_ast::AstImmediateKind immediateKindFrom(TokenKind kind);
  static syntax_ast::AstSyntax combinedSyntax(const Token& first,
                                              const Token& last,
                                              std::string text);

  PtxLexer lexer_;
};

}  // namespace ptx_frontend
