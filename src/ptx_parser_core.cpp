#include "ptx_parser_core.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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

double parseDecimalFloat(std::string_view text, SourceRange range) {
  double value = 0.0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);

  if (error != std::errc{} || ptr != end || !std::isfinite(value)) {
    throw ParseError(range, "invalid floating-point operand");
  }

  return value;
}

template <typename Float, typename Bits>
Float parseBitPatternFloat(std::string_view text, SourceRange range) {
  static_assert(sizeof(Float) == sizeof(Bits));

  if (text.size() < 3) {
    throw ParseError(range, "invalid floating-point bit pattern");
  }

  const Bits bits =
      static_cast<Bits>(parseUnsignedInteger(text.substr(2), 16, range));
  Float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

struct ParsedImmediate {
  ImmediateValue value;
  SourceRange range;
};

bool isImmediateToken(TokenKind kind) {
  switch (kind) {
    case TokenKind::Decimal:
    case TokenKind::Hex:
    case TokenKind::F32Hex:
    case TokenKind::F64Hex:
    case TokenKind::F64:
      return true;

    default:
      return false;
  }
}

ParsedImmediate parseImmediate(PtxLexer& lexer) {
  bool negative = false;
  SourceRange range = lexer.peek().range;

  if (lexer.peek().kind == TokenKind::Minus ||
      lexer.peek().kind == TokenKind::Plus) {
    auto sign = lexer.consume();
    negative = sign.kind == TokenKind::Minus;
    range.start = sign.range.start;
  }

  auto token = lexer.consume();
  range.end = token.range.end;

  if (!isImmediateToken(token.kind)) {
    throw ParseError(token.range, "expected immediate operand");
  }

  if (token.kind == TokenKind::Decimal || token.kind == TokenKind::Hex) {
    const bool has_unsigned_suffix =
        !token.text.empty() &&
        (token.text.back() == 'u' || token.text.back() == 'U');

    if (negative && has_unsigned_suffix) {
      throw ParseError(range, "unsigned integer operand cannot be negative");
    }

    const uint64_t magnitude = parseUnsignedInteger(
        token.text, token.kind == TokenKind::Hex ? 16 : 10, token.range);

    if (!negative) {
      return {ImmediateValue::from_value(magnitude), range};
    }

    constexpr uint64_t kMinMagnitude =
        uint64_t{1} << (std::numeric_limits<int64_t>::digits);

    if (magnitude > kMinMagnitude) {
      throw ParseError(range, "signed integer operand is out of range");
    }

    const int64_t value = magnitude == kMinMagnitude
                              ? std::numeric_limits<int64_t>::min()
                              : -static_cast<int64_t>(magnitude);
    return {ImmediateValue::from_value(value), range};
  }

  if (token.kind == TokenKind::F64) {
    double value = parseDecimalFloat(token.text, token.range);
    if (negative) {
      value = -value;
    }
    return {ImmediateValue::from_value(value), range};
  }

  if (token.kind == TokenKind::F32Hex) {
    float value =
        parseBitPatternFloat<float, uint32_t>(token.text, token.range);
    if (negative) {
      value = -value;
    }
    return {ImmediateValue::from_value(value), range};
  }

  double value =
      parseBitPatternFloat<double, uint64_t>(token.text, token.range);
  if (negative) {
    value = -value;
  }
  return {ImmediateValue::from_value(value), range};
}

bool startsImmediate(TokenKind kind) {
  return isImmediateToken(kind) || kind == TokenKind::Minus ||
         kind == TokenKind::Plus;
}

uint8_t vectorMemberIndex(std::string_view text, SourceRange range) {
  if (text == ".x")
    return 0;
  if (text == ".y")
    return 1;
  if (text == ".z")
    return 2;
  if (text == ".w")
    return 3;
  throw ParseError(range, "expected vector member .x, .y, .z or .w");
}

std::string formatVersion(PtxVersion version) {
  return std::to_string(version.major) + "." + std::to_string(version.minor);
}

}  // namespace

ParserCore::ParserCore(std::string_view src, ParserOptions options)
    : lexer_(src), options_(std::move(options)) {}

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

WithLoc<ParsedOp> ParserCore::parseOperandWithLoc(OperandKind kind) {
  const auto parse_identifier = [&]() -> WithLoc<ParsedOp> {
    auto token = expect(TokenKind::Ident, "identifier operand");

    if (peek().kind == TokenKind::DotIdent) {
      auto member = consume();
      auto operand = ParsedOp::from_value(ParsedOp::VecMemberIdx{
          .base = std::move(token.text),
          .member = vectorMemberIndex(member.text, member.range),
      });
      return {std::move(operand), {token.range.start, member.range.end}};
    }

    auto operand =
        ParsedOp::from_value(RegOrImmediate<Ident>::Reg(std::move(token.text)));
    return {std::move(operand), token.range};
  };

  const auto parse_immediate = [&]() -> WithLoc<ParsedOp> {
    auto immediate = parseImmediate(lexer_);
    auto operand = ParsedOp::from_value(std::move(immediate.value));
    return {std::move(operand), immediate.range};
  };

  const auto parse_address = [&]() -> WithLoc<ParsedOp> {
    bool bracketed = false;
    SourcePos range_start = peek().range.start;
    if (peek().kind == TokenKind::LBracket) {
      bracketed = true;
      range_start = consume().range.start;
    }

    if (startsImmediate(peek().kind)) {
      auto immediate = parseImmediate(lexer_);
      if (bracketed) {
        auto close = expect(TokenKind::RBracket, "']'");
        immediate.range.start = range_start;
        immediate.range.end = close.range.end;
      }
      auto operand = ParsedOp::from_value(std::move(immediate.value));
      return {std::move(operand), immediate.range};
    }

    auto base = expect(TokenKind::Ident, "address base");
    SourceRange range{range_start, base.range.end};

    if (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus) {
      const bool negative = consume().kind == TokenKind::Minus;
      auto offset_token = consume();
      if (offset_token.kind != TokenKind::Decimal &&
          offset_token.kind != TokenKind::Hex) {
        throw ParseError(offset_token.range, "expected integer address offset");
      }

      const uint64_t magnitude = parseUnsignedInteger(
          offset_token.text, offset_token.kind == TokenKind::Hex ? 16 : 10,
          offset_token.range);
      const uint64_t limit =
          negative ? uint64_t{1} + std::numeric_limits<int32_t>::max()
                   : std::numeric_limits<int32_t>::max();
      if (magnitude > limit) {
        throw ParseError(offset_token.range, "address offset is out of range");
      }

      const int32_t offset = negative && magnitude == limit
                                 ? std::numeric_limits<int32_t>::min()
                                 : (negative ? -static_cast<int32_t>(magnitude)
                                             : static_cast<int32_t>(magnitude));
      range.end = offset_token.range.end;

      if (bracketed) {
        range.end = expect(TokenKind::RBracket, "']'").range.end;
      }

      auto operand = ParsedOp::from_value(
          ParsedOp::RegOffset{std::move(base.text), offset});
      return {std::move(operand), range};
    }

    if (bracketed) {
      range.end = expect(TokenKind::RBracket, "']'").range.end;
    }

    auto operand =
        ParsedOp::from_value(RegOrImmediate<Ident>::Reg(std::move(base.text)));
    return {std::move(operand), range};
  };

  const auto parse_vector = [&]() -> WithLoc<ParsedOp> {
    auto open = expect(TokenKind::LBrace, "'{'");
    ParsedOp::VecPack elements;

    if (peek().kind == TokenKind::RBrace) {
      throw ParseError(peek().range, "vector operand cannot be empty");
    }

    for (;;) {
      if (peek().kind == TokenKind::Ident) {
        auto token = consume();
        elements.push_back(RegOrImmediate<Ident>::Reg(std::move(token.text)));
      } else if (startsImmediate(peek().kind)) {
        auto immediate = parseImmediate(lexer_);
        elements.push_back(
            RegOrImmediate<Ident>::Imm(std::move(immediate.value)));
      } else {
        throw ParseError(peek().range,
                         "expected register or immediate vector element");
      }

      if (!consumeIf(TokenKind::Comma)) {
        break;
      }
    }

    auto close = expect(TokenKind::RBrace, "'}'");
    auto operand = ParsedOp::from_value(std::move(elements));
    return {std::move(operand), {open.range.start, close.range.end}};
  };

  switch (kind) {
    case OperandKind::Identifier:
    case OperandKind::Register:
      return parse_identifier();

    case OperandKind::Immediate:
      return parse_immediate();

    case OperandKind::RegisterOrImmediate:
      return startsImmediate(peek().kind) ? parse_immediate()
                                          : parse_identifier();

    case OperandKind::Address:
      return parse_address();

    case OperandKind::AddressOrIdentifier:
      return peek().kind == TokenKind::LBracket ? parse_address()
                                                : parse_identifier();

    case OperandKind::Vector:
      return parse_vector();
  }

  throw ParseError(peek().range, "unsupported PTX operand kind");
}

void ParserCore::requireAvailability(
    const InstructionAvailability& availability, SourceRange range) const {
  if (availability.removed) {
    throw ParseError(range, "PTX instruction variant has been removed");
  }

  const auto& target = options_.target;

  if (target.ptx.has_value() && *target.ptx < availability.min_ptx) {
    throw ParseError(range, "PTX instruction variant requires PTX " +
                                formatVersion(availability.min_ptx) +
                                " or newer");
  }

  if (target.sm.has_value() && *target.sm < availability.min_sm) {
    throw ParseError(range, "PTX instruction variant requires sm_" +
                                std::to_string(availability.min_sm) +
                                " or newer");
  }

  if (target.family.has_value() && !availability.family.empty() &&
      *target.family != availability.family) {
    throw ParseError(range, "PTX instruction variant requires target family '" +
                                std::string(availability.family) + "'");
  }
}

}  // namespace ptx_frontend
