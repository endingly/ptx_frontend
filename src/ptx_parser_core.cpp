#include "ptx_parser_core.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

namespace ptx_frontend {
namespace {

std::string_view stripUnsignedSuffix(std::string_view text) {
  if (!text.empty() && (text.back() == 'u' || text.back() == 'U')) {
    text.remove_suffix(1);
  }

  return text;
}

uint64_t parseUnsignedInteger(std::string_view text, int base,
                              SourceRange range) {
  text = stripUnsignedSuffix(text);

  if (base == 16 && text.size() >= 2 && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2);
  }

  uint64_t value = 0;

  const char* begin = text.data();
  const char* end = text.data() + text.size();

  const auto [ptr, error] = std::from_chars(begin, end, value, base);

  if (error != std::errc{} || ptr != end) {
    throw ParseError(range, "invalid integer operand");
  }

  return value;
}

}  // namespace

ParserCore::ParserCore(std::string_view src) : lexer_(src) {}

PtxLexer::Token ParserCore::peek() {
  return lexer_.peek();
}

PtxLexer::Token ParserCore::consume() {
  return lexer_.consume();
}

bool ParserCore::consumeIf(TokenKind kind) {
  if (peek().kind != kind) {
    return false;
  }

  consume();
  return true;
}

PtxLexer::Token ParserCore::expect(TokenKind kind, std::string_view expected) {
  auto token = consume();

  if (token.kind != kind) {
    throw ParseError(token.range, "expected " + std::string(expected));
  }

  return token;
}

void ParserCore::expectComma() {
  expect(TokenKind::Comma, "','");
}

PtxLexer::Token ParserCore::expectSemicolon() {
  return expect(TokenKind::Semicolon, "';'");
}

bool ParserCore::isDotLikeToken(TokenKind kind) {
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

std::string_view ParserCore::stripDot(std::string_view text) {
  if (!text.empty() && text.front() == '.') {
    text.remove_prefix(1);
  }

  return text;
}

ParsedOpcode ParserCore::parseOpcodeWithModifiers() {
  auto opcode_token = expect(TokenKind::Ident, "instruction opcode");

  ParsedOpcode opcode{
      .text = std::move(opcode_token.text),
      .range = opcode_token.range,
      .modifiers = {},
  };

  while (isDotLikeToken(peek().kind)) {
    auto modifier_token = consume();
    auto modifier_name = std::string(stripDot(modifier_token.text));

    opcode.modifiers.push_back(ParsedModifier{
        .text = std::move(modifier_token.text),
        .name = std::move(modifier_name),
        .range = modifier_token.range,
    });

    opcode.range.end = modifier_token.range.end;
  }

  return opcode;
}

WithLoc<ParsedOp> ParserCore::parseOperandWithLoc() {
  auto token = consume();

  if (token.kind == TokenKind::Ident) {
    auto operand = ParsedOp::from_value(
        RegOrImmediate<Ident>::Reg(std::move(token.text)));

    return WithLoc<ParsedOp>{
        std::move(operand),
        token.range,
    };
  }

  if (token.kind == TokenKind::Decimal) {
    const uint64_t value = parseUnsignedInteger(token.text, 10, token.range);

    auto operand = ParsedOp::from_value(ImmediateValue::from_value(value));

    return WithLoc<ParsedOp>{
        std::move(operand),
        token.range,
    };
  }

  if (token.kind == TokenKind::Hex) {
    const uint64_t value = parseUnsignedInteger(token.text, 16, token.range);

    auto operand = ParsedOp::from_value(ImmediateValue::from_value(value));

    return WithLoc<ParsedOp>{
        std::move(operand),
        token.range,
    };
  }

  throw ParseError(token.range, "expected PTX operand");
}

}  // namespace ptx_frontend
