#include "ptx_cst_parser_detail.hpp"

#include <algorithm>
#include <utility>

namespace ptx_frontend {
using namespace cst_detail;

/** Retain legacy literal leaves while preserving compound numeric source trees. */
std::expected<syntax_cst::CstNumericOperand, CstParseDiagnostic>
PtxCstParser::parseNumericOperand(ExpressionBoundary boundary) {
  using namespace syntax_cst;
  auto parsed = parseConstantExpression(
      boundary == ExpressionBoundary::Full
          ? 0
          : constantBinaryPrecedence(TokenKind::Plus) + 1,
      RemainingTreeDepth{maxConstantTreeDepth});
  if (!parsed)
    return std::unexpected(parsed.error());
  auto& expression = parsed->expression;
  const auto range = expression.token_range;
  for (auto index = range.first; index < range.last; ++index) {
    if (token(index).kind == TokenKind::Ident &&
        token(index).text.starts_with('%'))
      return std::unexpected(CstParseDiagnostic{
          token(index).range,
          "registers cannot participate in constant expressions"});
  }
  if (const auto* literal = std::get_if<CstConstantLiteral>(&expression.node))
    return CstNumericOperand{
        CstImmediate{std::nullopt, literal->literal, range}};
  if (const auto* unary = std::get_if<CstConstantUnary>(&expression.node)) {
    const auto kind = token(unary->operator_token).kind;
    if (kind == TokenKind::Plus || kind == TokenKind::Minus) {
      if (const auto* literal =
              std::get_if<CstConstantLiteral>(&unary->operand->node))
        return CstNumericOperand{
            CstImmediate{unary->operator_token, literal->literal, range}};
    }
  }
  return CstNumericOperand{CstConstantOperand{
      std::make_unique<CstConstantExpression>(std::move(expression)), range}};
}

bool PtxCstParser::atNumericStart() {
  const auto kind = token(peek()).kind;
  return atImmediateStart() || kind == TokenKind::LParen ||
         kind == TokenKind::Tilde || kind == TokenKind::Exclamation;
}

/** Parse a leaf, cast, parentheses or initializer-only call within the height budget. */
std::expected<PtxCstParser::ParsedConstantExpression, CstParseDiagnostic>
PtxCstParser::parseConstantPrimary(RemainingTreeDepth remaining_depth) {
  using namespace syntax_cst;

  if (remaining_depth.value == 0) {
    return std::unexpected(depthLimitExceeded(peek(), "constant expression"));
  }

  const TokenId first = peek();
  ParsedConstantExpression expression;
  if (isConstantLiteral(token(first).kind)) {
    const TokenId literal = consume();
    expression = ParsedConstantExpression{
        CstConstantExpression{CstConstantLiteral{literal},
                              {literal, literal + 1}},
        TreeHeight{1}};
  } else if (token(first).kind == TokenKind::Ident) {
    const TokenId name = consume();
    expression = ParsedConstantExpression{
        CstConstantExpression{CstConstantSymbol{name}, {name, name + 1}},
        TreeHeight{1}};
  } else if (token(first).kind == TokenKind::LParen) {
    const TokenId left_paren = consume();
    if (token(peek()).kind == TokenKind::DotIdent &&
        (token(peek()).text == ".s64" || token(peek()).text == ".u64")) {
      const TokenId type = consume();
      auto right_paren = expect(TokenKind::RParen, "')' after constant cast");
      if (!right_paren)
        return std::unexpected(right_paren.error());
      if (remaining_depth.value == 1) {
        return std::unexpected(
            depthLimitExceeded(left_paren, "constant expression"));
      }
      auto operand =
          parseConstantUnary(RemainingTreeDepth{remaining_depth.value - 1});
      if (!operand)
        return std::unexpected(operand.error());
      const TokenId last = operand->expression.token_range.last;
      return ParsedConstantExpression{
          CstConstantExpression{
              CstConstantCast{left_paren, type, *right_paren,
                              std::make_unique<CstConstantExpression>(
                                  std::move(operand->expression))},
              {left_paren, last}},
          TreeHeight{operand->height.value + 1}};
    }

    if (remaining_depth.value == 1) {
      return std::unexpected(
          depthLimitExceeded(left_paren, "constant expression"));
    }
    auto inner = parseConstantExpression(
        0, RemainingTreeDepth{remaining_depth.value - 1});
    if (!inner)
      return std::unexpected(inner.error());
    auto right_paren = expect(TokenKind::RParen, "')'");
    if (!right_paren)
      return std::unexpected(right_paren.error());
    expression = ParsedConstantExpression{
        CstConstantExpression{
            CstConstantParenthesized{left_paren,
                                     std::make_unique<CstConstantExpression>(
                                         std::move(inner->expression)),
                                     *right_paren},
            {left_paren, *right_paren + 1}},
        TreeHeight{inner->height.value + 1}};
  } else {
    return std::unexpected(
        CstParseDiagnostic{token(first).range, "expected constant expression"});
  }

  while (token(peek()).kind == TokenKind::LParen) {
    const TokenId left_paren = consume();
    if (expression.height.value >= remaining_depth.value) {
      return std::unexpected(
          depthLimitExceeded(left_paren, "constant expression"));
    }
    auto argument = parseConstantExpression(
        0, RemainingTreeDepth{remaining_depth.value - 1});
    if (!argument)
      return std::unexpected(argument.error());
    auto right_paren =
        expect(TokenKind::RParen, "')' after initializer operator");
    if (!right_paren)
      return std::unexpected(right_paren.error());
    const std::size_t depth =
        std::max(expression.height.value, argument->height.value) + 1;
    if (depth > remaining_depth.value) {
      return std::unexpected(
          depthLimitExceeded(left_paren, "constant expression"));
    }
    const TokenId expression_first = expression.expression.token_range.first;
    expression = ParsedConstantExpression{
        CstConstantExpression{
            CstConstantCall{std::make_unique<CstConstantExpression>(
                                std::move(expression.expression)),
                            left_paren,
                            std::make_unique<CstConstantExpression>(
                                std::move(argument->expression)),
                            *right_paren},
            {expression_first, *right_paren + 1}},
        TreeHeight{depth}};
  }

  return expression;
}

/** Parse a prefix operation, charging one tree level for each wrapper. */
std::expected<PtxCstParser::ParsedConstantExpression, CstParseDiagnostic>
PtxCstParser::parseConstantUnary(RemainingTreeDepth remaining_depth) {
  if (remaining_depth.value == 0) {
    return std::unexpected(depthLimitExceeded(peek(), "constant expression"));
  }
  if (!isConstantUnaryOperator(token(peek()).kind))
    return parseConstantPrimary(remaining_depth);

  const TokenId operator_token = consume();
  if (remaining_depth.value == 1) {
    return std::unexpected(
        depthLimitExceeded(operator_token, "constant expression"));
  }
  auto operand =
      parseConstantUnary(RemainingTreeDepth{remaining_depth.value - 1});
  if (!operand)
    return std::unexpected(operand.error());
  const TokenId last = operand->expression.token_range.last;
  return ParsedConstantExpression{
      syntax_cst::CstConstantExpression{
          syntax_cst::CstConstantUnary{
              operator_token,
              std::make_unique<syntax_cst::CstConstantExpression>(
                  std::move(operand->expression))},
          {operator_token, last}},
      TreeHeight{operand->height.value + 1}};
}

/** Parse precedence-climbing source operations under a copied height budget. */
std::expected<PtxCstParser::ParsedConstantExpression, CstParseDiagnostic>
PtxCstParser::parseConstantExpression(int minimum_precedence,
                                      RemainingTreeDepth remaining_depth) {
  if (remaining_depth.value == 0) {
    return std::unexpected(depthLimitExceeded(peek(), "constant expression"));
  }
  auto left = parseConstantUnary(remaining_depth);
  if (!left)
    return std::unexpected(left.error());

  for (;;) {
    const int precedence = constantBinaryPrecedence(token(peek()).kind);
    if (precedence < minimum_precedence)
      break;

    const TokenId operator_token = consume();
    if (left->height.value >= remaining_depth.value) {
      return std::unexpected(
          depthLimitExceeded(operator_token, "constant expression"));
    }
    auto right = parseConstantExpression(
        precedence + 1, RemainingTreeDepth{remaining_depth.value - 1});
    if (!right)
      return std::unexpected(right.error());
    const std::size_t depth =
        std::max(left->height.value, right->height.value) + 1;
    if (depth > remaining_depth.value) {
      return std::unexpected(
          depthLimitExceeded(operator_token, "constant expression"));
    }
    const TokenId first = left->expression.token_range.first;
    const TokenId last = right->expression.token_range.last;
    left = ParsedConstantExpression{
        syntax_cst::CstConstantExpression{
            syntax_cst::CstConstantBinary{
                std::make_unique<syntax_cst::CstConstantExpression>(
                    std::move(left->expression)),
                operator_token,
                std::make_unique<syntax_cst::CstConstantExpression>(
                    std::move(right->expression))},
            {first, last}},
        TreeHeight{depth}};
  }

  if (minimum_precedence == 0 && token(peek()).kind == TokenKind::Question) {
    const TokenId first = left->expression.token_range.first;
    const TokenId question = consume();
    if (left->height.value >= remaining_depth.value) {
      return std::unexpected(
          depthLimitExceeded(question, "constant expression"));
    }
    auto true_expression = parseConstantExpression(
        0, RemainingTreeDepth{remaining_depth.value - 1});
    if (!true_expression)
      return std::unexpected(true_expression.error());
    auto colon = expect(TokenKind::Colon, "':' in conditional expression");
    if (!colon)
      return std::unexpected(colon.error());
    auto false_expression = parseConstantExpression(
        0, RemainingTreeDepth{remaining_depth.value - 1});
    if (!false_expression)
      return std::unexpected(false_expression.error());
    const std::size_t depth =
        std::max(left->height.value, std::max(true_expression->height.value,
                                              false_expression->height.value)) +
        1;
    if (depth > remaining_depth.value) {
      return std::unexpected(
          depthLimitExceeded(question, "constant expression"));
    }
    const TokenId last = false_expression->expression.token_range.last;
    left = ParsedConstantExpression{
        syntax_cst::CstConstantExpression{
            syntax_cst::CstConstantConditional{
                std::make_unique<syntax_cst::CstConstantExpression>(
                    std::move(left->expression)),
                question,
                std::make_unique<syntax_cst::CstConstantExpression>(
                    std::move(true_expression->expression)),
                *colon,
                std::make_unique<syntax_cst::CstConstantExpression>(
                    std::move(false_expression->expression))},
            {first, last}},
        TreeHeight{depth}};
  }

  return left;
}

/** Parse scalar, named or brace-list initializers under their structural-height rules. */
std::expected<PtxCstParser::ParsedInitializer, CstParseDiagnostic>
PtxCstParser::parseInitializer(RemainingTreeDepth remaining_depth) {
  using namespace syntax_cst;

  if (remaining_depth.value == 0) {
    return std::unexpected(depthLimitExceeded(peek(), "initializer"));
  }

  if (isIdentifierToken(token(peek()).kind) &&
      token(peekNext()).kind == TokenKind::Eq) {
    const TokenId member = consume();
    const TokenId equals = consume();
    auto expression = parseConstantExpression(
        0, RemainingTreeDepth{remaining_depth.value - 1});
    if (!expression)
      return std::unexpected(expression.error());
    const CstTokenRange range{member, expression->expression.token_range.last};
    return ParsedInitializer{
        CstInitializer{
            CstNamedInitializer{member, equals,
                                std::move(expression->expression), range},
            range},
        TreeHeight{expression->height.value + 1}};
  }

  if (token(peek()).kind != TokenKind::LBrace) {
    auto expression = parseConstantExpression(0, remaining_depth);
    if (!expression)
      return std::unexpected(expression.error());
    const CstTokenRange range = expression->expression.token_range;
    return ParsedInitializer{
        CstInitializer{std::move(expression->expression), range},
        TreeHeight{expression->height.value}};
  }

  const TokenId left_brace = consume();
  std::vector<CstInitializer> elements;
  std::vector<TokenId> commas;
  std::size_t maximum_element_height = 0;
  while (token(peek()).kind != TokenKind::RBrace) {
    if (remaining_depth.value == 1) {
      return std::unexpected(depthLimitExceeded(left_brace, "initializer"));
    }
    auto element =
        parseInitializer(RemainingTreeDepth{remaining_depth.value - 1});
    if (!element)
      return std::unexpected(element.error());
    maximum_element_height =
        std::max(maximum_element_height, element->height.value);
    elements.push_back(std::move(element->initializer));
    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
    if (token(peek()).kind == TokenKind::RBrace)
      break;
  }
  auto right_brace = expect(TokenKind::RBrace, "'}' in initializer");
  if (!right_brace)
    return std::unexpected(right_brace.error());
  const CstTokenRange range{left_brace, *right_brace + 1};
  return ParsedInitializer{
      CstInitializer{CstInitializerList{left_brace, std::move(elements),
                                        std::move(commas), *right_brace, range},
                     range},
      TreeHeight{maximum_element_height + 1}};
}

/** Build a diagnostic for a source tree that exceeds the active depth budget. */
CstParseDiagnostic PtxCstParser::depthLimitExceeded(
    TokenId id, std::string_view tree_kind) const {
  return CstParseDiagnostic{
      .range = token(id).range,
      .message = std::string(tree_kind) + " tree depth limit exceeded",
  };
}

bool PtxCstParser::atImmediateStart() {
  const TokenKind kind = token(peek()).kind;
  return isImmediate(kind) || kind == TokenKind::Plus ||
         kind == TokenKind::Minus;
}

}  // namespace ptx_frontend
