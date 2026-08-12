#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

#include <utility>

namespace ptx_frontend {
namespace {

bool isImmediate(TokenKind kind) {
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

bool isModifier(TokenKind kind) {
  return kind == TokenKind::DotIdent || kind == TokenKind::DotGlobal ||
         kind == TokenKind::DotConst || kind == TokenKind::DotShared ||
         kind == TokenKind::DotLocal || kind == TokenKind::DotParam;
}

}  // namespace

PtxSyntaxParser::PtxSyntaxParser(std::string_view source) : lexer_(source) {}

PtxSyntaxParser::Token PtxSyntaxParser::peek() { return lexer_.peek(); }

PtxSyntaxParser::Token PtxSyntaxParser::consume() { return lexer_.consume(); }

bool PtxSyntaxParser::atImmediateStart() {
  const Token token = peek();
  return isImmediate(token.kind) || token.kind == TokenKind::Plus ||
         token.kind == TokenKind::Minus;
}

std::expected<PtxSyntaxParser::Token, SyntaxParseDiagnostic>
PtxSyntaxParser::expect(TokenKind kind, std::string_view name) {
  Token token = consume();
  if (token.kind == kind) {
    return token;
  }
  return std::unexpected(
      SyntaxParseDiagnostic{token.range, "expected " + std::string(name)});
}

syntax_ast::AstSyntax PtxSyntaxParser::syntaxFrom(Token token) {
  return syntax_ast::AstSyntax{std::move(token.text), token.range,
                               std::move(token.leading_trivia)};
}

syntax_ast::AstSyntax PtxSyntaxParser::combinedSyntax(const Token& first,
                                                       const Token& last,
                                                       std::string text) {
  return syntax_ast::AstSyntax{std::move(text),
                               SourceRange{first.range.start, last.range.end},
                               first.leading_trivia};
}

std::expected<syntax_ast::AstImmediate, SyntaxParseDiagnostic>
PtxSyntaxParser::parseImmediate(bool allow_sign) {
  Token first = peek();
  std::string text;
  if (allow_sign && (first.kind == TokenKind::Plus ||
                     first.kind == TokenKind::Minus)) {
    first = consume();
    text = first.text;
  }

  Token literal = consume();
  if (!isImmediate(literal.kind)) {
    return std::unexpected(
        SyntaxParseDiagnostic{literal.range, "expected immediate literal"});
  }
  text += literal.text;
  return syntax_ast::AstImmediate{combinedSyntax(first, literal, std::move(text))};
}

std::expected<PtxSyntaxParser::AstOperand, SyntaxParseDiagnostic>
PtxSyntaxParser::parseAddress(bool bracketed, Token open) {
  Token first = bracketed ? open : peek();
  std::variant<syntax_ast::AstIdentifierRef, syntax_ast::AstImmediate> base;
  Token last;
  std::string text = bracketed ? open.text : "";

  if (atImmediateStart()) {
    auto immediate = parseImmediate();
    if (!immediate) {
      return std::unexpected(immediate.error());
    }
    last = peek();
    text += immediate->syntax.text;
    base = std::move(*immediate);
  } else {
    auto identifier = expect(TokenKind::Ident, "address base");
    if (!identifier) {
      return std::unexpected(identifier.error());
    }
    last = *identifier;
    text += identifier->text;
    base = syntax_ast::AstIdentifierRef{syntaxFrom(std::move(*identifier))};
  }

  std::optional<syntax_ast::AstAddressOffset> offset;
  if (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus) {
    Token op = consume();
    const SourcePos offset_start = op.range.start;
    auto magnitude = parseImmediate(false);
    if (!magnitude) {
      return std::unexpected(magnitude.error());
    }
    last.range = magnitude->syntax.range;
    last.text = magnitude->syntax.text;
    text += op.text + magnitude->syntax.text;
    offset = syntax_ast::AstAddressOffset{
        syntaxFrom(std::move(op)), std::move(*magnitude),
        SourceRange{offset_start, last.range.end}};
  }

  if (bracketed) {
    auto close = expect(TokenKind::RBracket, "']'");
    if (!close) {
      return std::unexpected(close.error());
    }
    last = *close;
    text += close->text;
  }

  return AstOperand{syntax_ast::AstAddress{
      combinedSyntax(first, last, std::move(text)), std::move(base),
      std::move(offset), bracketed}};
}

std::expected<PtxSyntaxParser::AstOperand, SyntaxParseDiagnostic>
PtxSyntaxParser::parseVectorPack(Token open) {
  std::vector<syntax_ast::AstVectorElement> elements;
  Token last = open;
  std::string text = open.text;

  if (peek().kind == TokenKind::RBrace) {
    return std::unexpected(
        SyntaxParseDiagnostic{peek().range, "vector operand cannot be empty"});
  }

  for (;;) {
    if (peek().kind == TokenKind::Ident) {
      Token token = consume();
      text += token.text;
      last = token;
      elements.emplace_back(
          syntax_ast::AstIdentifierRef{syntaxFrom(std::move(token))});
    } else if (atImmediateStart()) {
      auto immediate = parseImmediate();
      if (!immediate) {
        return std::unexpected(immediate.error());
      }
      text += immediate->syntax.text;
      last.range = immediate->syntax.range;
      last.text = immediate->syntax.text;
      elements.emplace_back(std::move(*immediate));
    } else {
      return std::unexpected(SyntaxParseDiagnostic{
          peek().range, "expected identifier or immediate vector element"});
    }

    if (peek().kind != TokenKind::Comma) {
      break;
    }
    Token comma = consume();
    text += comma.text;
    last = comma;
  }

  auto close = expect(TokenKind::RBrace, "'}'");
  if (!close) {
    return std::unexpected(close.error());
  }
  text += close->text;
  return AstOperand{syntax_ast::AstVectorPack{
      combinedSyntax(open, *close, std::move(text)), std::move(elements)}};
}

std::expected<PtxSyntaxParser::AstOperand, SyntaxParseDiagnostic>
PtxSyntaxParser::parseOperand() {
  if (peek().kind == TokenKind::Exclamation) {
    Token first = consume();
    auto name = expect(TokenKind::Ident, "predicate operand");
    if (!name) {
      return std::unexpected(name.error());
    }
    return AstOperand{syntax_ast::AstPredicateOperand{
        .syntax = combinedSyntax(first, *name, first.text + name->text),
        .negated = true,
        .name = syntax_ast::AstIdentifierRef{syntaxFrom(std::move(*name))},
    }};
  }
  if (peek().kind == TokenKind::LBracket) {
    return parseAddress(true, consume());
  }
  if (peek().kind == TokenKind::LBrace) {
    return parseVectorPack(consume());
  }
  if (atImmediateStart()) {
    auto immediate = parseImmediate();
    if (!immediate) {
      return std::unexpected(immediate.error());
    }
    return AstOperand{std::move(*immediate)};
  }

  auto identifier = expect(TokenKind::Ident, "operand");
  if (!identifier) {
    return std::unexpected(identifier.error());
  }
  Token base_token = *identifier;
  auto base = syntax_ast::AstIdentifierRef{syntaxFrom(std::move(*identifier))};

  if (peek().kind == TokenKind::DotIdent) {
    Token selector = consume();
    return AstOperand{syntax_ast::AstVectorMember{
        combinedSyntax(base_token, selector, base.syntax.text + selector.text),
        std::move(base), syntaxFrom(std::move(selector))}};
  }
  if (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus) {
    Token op = consume();
    const SourcePos offset_start = op.range.start;
    auto magnitude = parseImmediate(false);
    if (!magnitude) {
      return std::unexpected(magnitude.error());
    }
    const SourceRange range{base_token.range.start, magnitude->syntax.range.end};
    return AstOperand{syntax_ast::AstAddress{
        syntax_ast::AstSyntax{base.syntax.text + op.text + magnitude->syntax.text,
                              range, base.syntax.leading_trivia},
        std::move(base),
        syntax_ast::AstAddressOffset{
            syntaxFrom(std::move(op)), std::move(*magnitude),
            SourceRange{offset_start, range.end}},
        false}};
  }
  return AstOperand{std::move(base)};
}

std::expected<syntax_ast::AstInstruction, SyntaxParseDiagnostic>
PtxSyntaxParser::parseInstruction() {
  std::optional<syntax_ast::AstPredicate> predicate;
  Token first = peek();
  if (peek().kind == TokenKind::At) {
    Token at = consume();
    const bool negated = peek().kind == TokenKind::Exclamation;
    if (negated) {
      consume();
    }
    auto name = expect(TokenKind::Ident, "predicate identifier");
    if (!name) {
      return std::unexpected(name.error());
    }
    predicate = syntax_ast::AstPredicate{
        negated, syntax_ast::AstIdentifierRef{syntaxFrom(std::move(*name))},
        SourceRange{at.range.start, name->range.end}};
  }

  auto opcode = expect(TokenKind::Ident, "instruction opcode");
  if (!opcode) {
    return std::unexpected(opcode.error());
  }
  if (!predicate) {
    first = *opcode;
  }
  Token last = *opcode;
  syntax_ast::AstInstruction instruction{
      .opcode = syntax_ast::AstOpcode{syntaxFrom(std::move(*opcode))},
      .modifiers = {},
      .operands = {},
      .predicate = std::move(predicate),
      .range = {},
  };

  while (isModifier(peek().kind)) {
    Token modifier = consume();
    last = modifier;
    instruction.modifiers.push_back(
        syntax_ast::AstModifier{syntaxFrom(std::move(modifier))});
  }

  if (peek().kind != TokenKind::Semicolon) {
    for (;;) {
      auto operand = parseOperand();
      if (!operand) {
        return std::unexpected(operand.error());
      }
      instruction.operands.push_back(std::move(*operand));
      last = peek();
      if (peek().kind != TokenKind::Comma) {
        break;
      }
      consume();
    }
  }

  auto semicolon = expect(TokenKind::Semicolon, "';'");
  if (!semicolon) {
    return std::unexpected(semicolon.error());
  }
  instruction.range = SourceRange{first.range.start, semicolon->range.end};
  return instruction;
}

}  // namespace ptx_frontend
