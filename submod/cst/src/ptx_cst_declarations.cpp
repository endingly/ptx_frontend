#include "ptx_cst_parser_detail.hpp"

#include <algorithm>
#include <utility>

namespace ptx_frontend {
using namespace cst_detail;

/** Parse a declaration from its state-space token, retaining preceding qualifiers. */
std::expected<syntax_cst::CstVariableDeclaration, CstParseDiagnostic>
PtxCstParser::parseVariableDeclaration(std::vector<TokenId> qualifiers) {
  const TokenId first = qualifiers.empty() ? peek() : qualifiers.front();
  const TokenId state_space = consume();
  if (!isVariableStateSpace(token(state_space).kind)) {
    return std::unexpected(CstParseDiagnostic{token(state_space).range,
                                              "expected variable state space"});
  }

  std::optional<syntax_cst::CstAttributeList> attributes;
  if (token(peek()).kind == TokenKind::DotIdent &&
      token(peek()).text == ".attribute") {
    auto parsed = parseAttributeList();
    if (!parsed)
      return std::unexpected(parsed.error());
    attributes = std::move(*parsed);
  }

  std::optional<TokenId> align_directive;
  std::optional<TokenId> alignment;
  if (token(peek()).kind == TokenKind::DotAlign) {
    align_directive = consume();
    auto value = expect(TokenKind::Decimal, "variable alignment");
    if (!value)
      return std::unexpected(value.error());
    alignment = *value;
  }

  std::optional<TokenId> vector_type;
  if (token(peek()).kind == TokenKind::DotIdent &&
      (token(peek()).text == ".v2" || token(peek()).text == ".v4")) {
    vector_type = consume();
  }

  auto type = expect(TokenKind::DotIdent, "variable type");
  if (!type)
    return std::unexpected(type.error());

  std::vector<syntax_cst::CstVariableDeclarator> declarators;
  std::vector<TokenId> commas;
  for (;;) {
    auto name = expect(TokenKind::Ident, "variable name");
    if (!name)
      return std::unexpected(name.error());

    std::optional<TokenId> left_angle;
    std::optional<TokenId> parameterized_count;
    std::optional<TokenId> right_angle;
    std::vector<syntax_cst::CstArrayDimension> array_dimensions;
    TokenId last = *name;
    if (token(peek()).kind == TokenKind::Lt) {
      left_angle = consume();
      auto count_token = expect(TokenKind::Decimal, "parameterized count");
      if (!count_token)
        return std::unexpected(count_token.error());
      parameterized_count = *count_token;
      auto close = expect(TokenKind::Gt, "'>'");
      if (!close)
        return std::unexpected(close.error());
      right_angle = *close;
      last = *close;
    }

    while (token(peek()).kind == TokenKind::LBracket) {
      const TokenId left_bracket = consume();
      if (parameterized_count) {
        return std::unexpected(CstParseDiagnostic{
            token(left_bracket).range,
            "parameterized variable names cannot declare arrays"});
      }
      std::optional<syntax_cst::CstConstantExpression> size;
      if (token(peek()).kind != TokenKind::RBracket) {
        auto expression = parseConstantExpression(
            0, RemainingTreeDepth{maxConstantTreeDepth});
        if (!expression)
          return std::unexpected(expression.error());
        size = std::move(expression->expression);
      }
      auto right_bracket = expect(TokenKind::RBracket, "']'");
      if (!right_bracket)
        return std::unexpected(right_bracket.error());
      array_dimensions.push_back(
          syntax_cst::CstArrayDimension{left_bracket,
                                        std::move(size),
                                        *right_bracket,
                                        {left_bracket, *right_bracket + 1}});
      last = *right_bracket;
    }

    std::optional<TokenId> equals;
    std::optional<syntax_cst::CstInitializer> initializer;
    if (token(peek()).kind == TokenKind::Eq) {
      equals = consume();
      if (parameterized_count) {
        return std::unexpected(CstParseDiagnostic{
            token(*equals).range,
            "parameterized variable names cannot have an initializer"});
      }
      if (token(state_space).kind != TokenKind::DotGlobal &&
          token(state_space).kind != TokenKind::DotConst) {
        return std::unexpected(CstParseDiagnostic{
            token(*equals).range,
            "variable initializer requires '.global' or '.const' state space"});
      }
      const bool is_external =
          std::ranges::any_of(qualifiers, [this](TokenId qualifier) {
            return token(qualifier).kind == TokenKind::DotExtern;
          });
      if (is_external) {
        return std::unexpected(CstParseDiagnostic{
            token(*equals).range,
            "external variable declaration cannot have an initializer"});
      }
      auto parsed_initializer =
          parseInitializer(RemainingTreeDepth{maxConstantTreeDepth});
      if (!parsed_initializer)
        return std::unexpected(parsed_initializer.error());
      last = parsed_initializer->initializer.token_range.last - 1;
      initializer = std::move(parsed_initializer->initializer);
    }
    declarators.push_back(
        syntax_cst::CstVariableDeclarator{*name,
                                          left_angle,
                                          parameterized_count,
                                          right_angle,
                                          std::move(array_dimensions),
                                          equals,
                                          std::move(initializer),
                                          {*name, last + 1}});

    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
  }

  auto semicolon = expect(TokenKind::Semicolon, "';'");
  if (!semicolon)
    return std::unexpected(semicolon.error());
  return syntax_cst::CstVariableDeclaration{
      .qualifiers = std::move(qualifiers),
      .state_space = state_space,
      .attributes = std::move(attributes),
      .align_directive = align_directive,
      .alignment = alignment,
      .vector_type = vector_type,
      .type = *type,
      .declarators = std::move(declarators),
      .commas = std::move(commas),
      .semicolon = *semicolon,
      .token_range = {first, *semicolon + 1},
  };
}

/** Parse an unconsumed attribute directive and its parenthesized list. */
std::expected<syntax_cst::CstAttributeList, CstParseDiagnostic>
PtxCstParser::parseAttributeList() {
  const TokenId directive = consume();
  const TokenId first = directive;
  auto left_paren = expect(TokenKind::LParen, "'(' after .attribute");
  if (!left_paren)
    return std::unexpected(left_paren.error());
  std::vector<syntax_cst::CstAttribute> attributes;
  std::vector<TokenId> commas;
  for (;;) {
    auto name = expect(TokenKind::DotIdent, "attribute name");
    if (!name)
      return std::unexpected(name.error());
    if (token(*name).text != ".managed" && token(*name).text != ".unified") {
      return std::unexpected(CstParseDiagnostic{
          token(*name).range, "unsupported .attribute member"});
    }
    std::vector<TokenId> values;
    std::vector<TokenId> value_commas;
    TokenId last = *name;
    if (token(peek()).kind == TokenKind::LParen) {
      consume();
      auto first_value = expectIntegerLiteral("attribute integer value");
      if (!first_value)
        return std::unexpected(first_value.error());
      values.push_back(*first_value);
      auto comma = expect(TokenKind::Comma, "',' in .unified attribute");
      if (!comma)
        return std::unexpected(comma.error());
      value_commas.push_back(*comma);
      auto second_value = expectIntegerLiteral("attribute integer value");
      if (!second_value)
        return std::unexpected(second_value.error());
      values.push_back(*second_value);
      auto right = expect(TokenKind::RParen, "')' in .unified attribute");
      if (!right)
        return std::unexpected(right.error());
      last = *right;
    }
    if ((token(*name).text == ".managed" && !values.empty()) ||
        (token(*name).text == ".unified" && values.size() != 2)) {
      return std::unexpected(CstParseDiagnostic{
          token(*name).range, "invalid .attribute member arity"});
    }
    attributes.push_back(syntax_cst::CstAttribute{
        .name = *name,
        .values = std::move(values),
        .commas = std::move(value_commas),
        .token_range = {*name, last + 1},
    });
    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
  }
  auto right_paren = expect(TokenKind::RParen, "')' after .attribute");
  if (!right_paren)
    return std::unexpected(right_paren.error());
  return syntax_cst::CstAttributeList{
      .directive = directive,
      .left_paren = *left_paren,
      .attributes = std::move(attributes),
      .commas = std::move(commas),
      .right_paren = *right_paren,
      .token_range = {first, *right_paren + 1},
  };
}

/** Parse a formal parameter while retaining declaration punctuation. */
std::expected<syntax_cst::CstFunctionParameter, CstParseDiagnostic>
PtxCstParser::parseFunctionParameter() {
  const TokenId state_space = consume();
  if (token(state_space).kind != TokenKind::DotReg &&
      token(state_space).kind != TokenKind::DotParam) {
    return std::unexpected(CstParseDiagnostic{
        token(state_space).range, "expected '.reg' or '.param' parameter"});
  }

  std::optional<TokenId> align_directive;
  std::optional<TokenId> alignment;
  if (token(peek()).kind == TokenKind::DotAlign) {
    align_directive = consume();
    auto value = expect(TokenKind::Decimal, "parameter alignment");
    if (!value)
      return std::unexpected(value.error());
    alignment = *value;
  }

  std::optional<TokenId> vector_type;
  if (token(peek()).text == ".v2" || token(peek()).text == ".v4") {
    vector_type = consume();
    if (token(state_space).kind == TokenKind::DotParam)
      return std::unexpected(CstParseDiagnostic{
          token(*vector_type).range,
          "vector .param function parameters are not supported"});
  }
  auto type = expect(TokenKind::DotIdent, "parameter type");
  if (!type)
    return std::unexpected(type.error());

  std::optional<TokenId> pointer_directive;
  std::optional<TokenId> pointer_space;
  std::optional<TokenId> pointer_align_directive;
  std::optional<TokenId> pointer_alignment;
  if (token(peek()).kind == TokenKind::DotPtr) {
    pointer_directive = consume();
    const TokenKind space_kind = token(peek()).kind;
    if (space_kind == TokenKind::DotConst ||
        space_kind == TokenKind::DotGlobal ||
        space_kind == TokenKind::DotLocal ||
        space_kind == TokenKind::DotShared) {
      pointer_space = consume();
    }
    if (token(peek()).kind == TokenKind::DotAlign) {
      pointer_align_directive = consume();
      auto value = expect(TokenKind::Decimal, "pointer alignment");
      if (!value)
        return std::unexpected(value.error());
      pointer_alignment = *value;
    }
  }

  auto name = expect(TokenKind::Ident, "parameter name");
  if (!name)
    return std::unexpected(name.error());

  std::optional<TokenId> left_bracket;
  std::optional<syntax_cst::CstConstantExpression> array_size;
  std::optional<TokenId> right_bracket;
  TokenId last = *name;
  if (token(peek()).kind == TokenKind::LBracket) {
    left_bracket = consume();
    if (token(peek()).kind != TokenKind::RBracket) {
      auto size =
          parseConstantExpression(0, RemainingTreeDepth{maxConstantTreeDepth});
      if (!size)
        return std::unexpected(size.error());
      array_size = std::move(size->expression);
    }
    auto close = expect(TokenKind::RBracket, "']'");
    if (!close)
      return std::unexpected(close.error());
    right_bracket = *close;
    last = *close;
  }
  if (token(peek()).kind == TokenKind::LBracket) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "multidimensional function parameters are not supported"});
  }

  return syntax_cst::CstFunctionParameter{
      state_space,
      align_directive,
      alignment,
      vector_type,
      *type,
      pointer_directive,
      pointer_space,
      pointer_align_directive,
      pointer_alignment,
      *name,
      left_bracket,
      std::move(array_size),
      right_bracket,
      {state_space, last + 1},
  };
}

/** Parse formal parameters from the unconsumed `(` through `)`. */
std::expected<syntax_cst::CstFunctionParameterList, CstParseDiagnostic>
PtxCstParser::parseFunctionParameterList() {
  auto left = expect(TokenKind::LParen, "'('");
  if (!left)
    return std::unexpected(left.error());

  std::vector<syntax_cst::CstFunctionParameter> parameters;
  std::vector<TokenId> commas;
  if (token(peek()).kind != TokenKind::RParen) {
    for (;;) {
      auto parameter = parseFunctionParameter();
      if (!parameter)
        return std::unexpected(parameter.error());
      parameters.push_back(std::move(*parameter));
      if (token(peek()).kind != TokenKind::Comma)
        break;
      commas.push_back(consume());
    }
  }

  auto right = expect(TokenKind::RParen, "')'");
  if (!right)
    return std::unexpected(right.error());
  return syntax_cst::CstFunctionParameterList{*left,
                                              std::move(parameters),
                                              std::move(commas),
                                              *right,
                                              {*left, *right + 1}};
}

}  // namespace ptx_frontend
