#include "ptx_parser.hpp"
#include <string>

namespace ptx_frontend {

Parser::Parser(std::string_view src) : lexer_(src) {}

PtxLexer::Token Parser::peek() {
  return lexer_.peek();
}

PtxLexer::Token Parser::consume() {
  return lexer_.consume();
}

bool Parser::consumeIf(TokenKind kind) {
  if (peek().kind != kind) {
    return false;
  }
  consume();
  return true;
}

PtxLexer::Token Parser::expect(TokenKind kind, std::string_view expected) {
  auto tok = consume();
  if (tok.kind != kind) {
    throw ParseError(tok.range, "expected " + std::string(expected));
  }
  return tok;
}

void Parser::expectComma() {
  expect(TokenKind::Comma, "','");
}

PtxLexer::Token Parser::expectSemicolon() {
  return expect(TokenKind::Semicolon, "';'");
}

bool Parser::isDotLikeToken(TokenKind kind) {
  switch (kind) {
    case TokenKind::DotIdent:
    case TokenKind::DotGlobal:
    case TokenKind::DotConst:
    case TokenKind::DotShared:
    case TokenKind::DotLocal:
    case TokenKind::DotParam:
      return true;
    default:
      return false;
  }
}

std::string_view Parser::stripDot(std::string_view text) {
  if (!text.empty() && text.front() == '.') {
    return text.substr(1);
  }
  return text;
}

ParsedOpcode Parser::parseOpcodeWithModifiers() {
  auto opcode_tok = expect(TokenKind::Ident, "instruction opcode");

  ParsedOpcode parsed;
  parsed.text = opcode_tok.text;
  parsed.range = opcode_tok.range;

  while (isDotLikeToken(peek().kind)) {
    auto mod_tok = consume();

    ParsedModifier mod;
    mod.text = mod_tok.text;
    mod.name = stripDot(mod_tok.text);
    mod.range = mod_tok.range;

    parsed.modifiers.push_back(mod);
    parsed.range.end = mod_tok.range.end;
  }

  return parsed;
}

}  // namespace ptx_frontend