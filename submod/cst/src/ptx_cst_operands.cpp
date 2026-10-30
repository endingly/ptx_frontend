#include "ptx_cst_parser_detail.hpp"

#include <algorithm>
#include <utility>

namespace ptx_frontend {
using namespace cst_detail;

/** Parse the address/tensor/fabric tail after an already consumed `[`; own `]`. */
std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseBracketedAddress(TokenId open) {
  const TokenId first = open;
  syntax_cst::CstAddressBase base;
  TokenId last = first;

  if (atNumericStart()) {
    // Top-level additive operators separate the base and offset; parentheses
    // permit the complete expression grammar inside either numeric component.
    auto immediate = parseNumericOperand(ExpressionBoundary::AboveAdditive);
    if (!immediate)
      return std::unexpected(immediate.error());
    last = std::visit(
        [](const auto& leaf) { return leaf.token_range.last - 1; }, *immediate);
    base = std::visit(
        [](auto&& leaf) -> syntax_cst::CstAddressBase {
          return std::move(leaf);
        },
        std::move(*immediate));
  } else {
    auto identifier = expect(TokenKind::Ident, "address base");
    if (!identifier)
      return std::unexpected(identifier.error());
    last = *identifier;
    base = syntax_cst::CstIdentifier{*identifier};
  }

  std::optional<syntax_cst::CstAddressOffset> offset;
  if (token(peek()).kind == TokenKind::Plus ||
      token(peek()).kind == TokenKind::Minus) {
    const TokenId op = consume();
    auto magnitude = parseNumericOperand();
    if (!magnitude)
      return std::unexpected(magnitude.error());
    last = std::visit(
        [](const auto& leaf) { return leaf.token_range.last - 1; }, *magnitude);
    offset =
        syntax_cst::CstAddressOffset{op, std::move(*magnitude), {op, last + 1}};
  }

  if (token(peek()).kind == TokenKind::Comma) {
    const TokenId comma = consume();
    if (token(peek()).kind == TokenKind::LBrace) {
      auto parsed = parseVectorPack(consume());
      if (!parsed)
        return std::unexpected(parsed.error());
      auto close = expect(TokenKind::RBracket, "']' after tensor coordinates");
      if (!close)
        return std::unexpected(close.error());
      return syntax_cst::CstOperand{syntax_cst::CstTensorOperand{
          open,
          syntax_cst::CstAddress{std::nullopt,
                                 std::move(base),
                                 std::move(offset),
                                 std::nullopt,
                                 std::nullopt,
                                 {open + 1, comma}},
          comma,
          std::move(*parsed),
          *close,
          {open, *close + 1}}};
    }
    if (offset)
      return std::unexpected(CstParseDiagnostic{
          token(comma).range, "fabric handle endpoint cannot have an offset"});
    std::vector<syntax_cst::CstAddressBase> elements;
    elements.push_back(std::move(base));
    std::vector<TokenId> commas{comma};
    do {
      if (token(peek()).kind == TokenKind::LBrace) {
        auto coordinates = parseVectorPack(consume());
        if (!coordinates)
          return std::unexpected(coordinates.error());
        auto close = expect(TokenKind::RBracket,
                            "']' after compound bracket coordinates");
        if (!close)
          return std::unexpected(close.error());
        return syntax_cst::CstOperand{syntax_cst::CstCompoundBracket{
            .heads = std::move(elements),
            .commas = std::move(commas),
            .coordinates = std::move(*coordinates),
            .left_bracket = open,
            .right_bracket = *close,
            .token_range = {open, *close + 1},
        }};
      }
      if (atNumericStart()) {
        auto immediate = parseNumericOperand();
        if (!immediate)
          return std::unexpected(immediate.error());
        elements.push_back(std::visit(
            [](auto&& leaf) -> syntax_cst::CstAddressBase {
              return std::move(leaf);
            },
            std::move(*immediate)));
      } else {
        auto identifier = expect(TokenKind::Ident, "fabric handle component");
        if (!identifier)
          return std::unexpected(identifier.error());
        elements.emplace_back(syntax_cst::CstIdentifier{*identifier});
      }
      if (token(peek()).kind != TokenKind::Comma)
        break;
      commas.push_back(consume());
    } while (true);
    auto close = expect(TokenKind::RBracket, "']' after fabric handle");
    if (!close)
      return std::unexpected(close.error());
    return syntax_cst::CstOperand{
        syntax_cst::CstFabricHandle{std::move(elements),
                                    std::move(commas),
                                    open,
                                    *close,
                                    {open, *close + 1}}};
  }

  auto close = expect(TokenKind::RBracket, "']'");
  if (!close)
    return std::unexpected(close.error());
  last = *close;
  std::optional<TokenId> unified;
  if (token(peek()).kind == TokenKind::DotIdent &&
      token(peek()).text == ".unified") {
    unified = consume();
    last = *unified;
  }

  return syntax_cst::CstOperand{syntax_cst::CstAddress{open,
                                                       std::move(base),
                                                       std::move(offset),
                                                       *close,
                                                       unified,
                                                       {first, last + 1}}};
}

/** Parse vector elements after an already consumed `{`; own `}` and its half-open range. */
std::expected<syntax_cst::CstVectorPack, CstParseDiagnostic>
PtxCstParser::parseVectorPack(TokenId open) {
  std::vector<syntax_cst::CstVectorElement> elements;
  std::vector<TokenId> commas;

  if (token(peek()).kind == TokenKind::RBrace) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "vector operand cannot be empty"});
  }

  for (;;) {
    if (token(peek()).kind == TokenKind::Ident) {
      const TokenId identifier = consume();
      if (token(peek()).kind == TokenKind::DotIdent) {
        const TokenId selector = consume();
        elements.emplace_back(syntax_cst::CstVectorMember{
            {identifier}, selector, {identifier, selector + 1}});
      } else {
        elements.emplace_back(syntax_cst::CstIdentifier{identifier});
      }
    } else if (atNumericStart()) {
      auto immediate = parseNumericOperand();
      if (!immediate)
        return std::unexpected(immediate.error());
      elements.push_back(std::visit(
          [](auto&& leaf) -> syntax_cst::CstVectorElement {
            return std::move(leaf);
          },
          std::move(*immediate)));
    } else {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          "expected identifier or immediate vector element"});
    }

    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
  }

  auto close = expect(TokenKind::RBrace, "'}'");
  if (!close)
    return std::unexpected(close.error());
  return syntax_cst::CstVectorPack{
      open, std::move(elements), std::move(commas), *close, {open, *close + 1}};
}

/** Parse an unconsumed parenthesized return/input list through its closing `)`. */
std::expected<syntax_cst::CstCallParameterList, CstParseDiagnostic>
PtxCstParser::parseCallParameterList(
    syntax_cst::CstCallParameterListKind kind) {
  using syntax_cst::CstCallParameterList;
  using syntax_cst::CstCallParameterListKind;

  auto open = expect(TokenKind::LParen, "'(' in call parameter list");
  if (!open)
    return std::unexpected(open.error());

  std::vector<syntax_cst::CstCallParameter> parameters;
  std::vector<TokenId> commas;
  if (kind == CstCallParameterListKind::Return) {
    auto parameter = expect(TokenKind::Ident, "call return parameter");
    if (!parameter)
      return std::unexpected(parameter.error());
    parameters.emplace_back(syntax_cst::CstIdentifier{*parameter});
    if (token(peek()).kind == TokenKind::Comma) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          "a call return parameter list must contain exactly one name"});
    }
  } else {
    while (token(peek()).kind != TokenKind::RParen) {
      if (token(peek()).kind == TokenKind::Ident) {
        parameters.emplace_back(syntax_cst::CstIdentifier{consume()});
      } else if (atNumericStart()) {
        auto parameter = parseNumericOperand();
        if (!parameter)
          return std::unexpected(parameter.error());
        parameters.push_back(std::visit(
            [](auto&& leaf) -> syntax_cst::CstCallParameter {
              return std::move(leaf);
            },
            std::move(*parameter)));
      } else {
        return std::unexpected(CstParseDiagnostic{
            token(peek()).range,
            "expected identifier or immediate call argument"});
      }
      if (token(peek()).kind != TokenKind::Comma)
        break;
      commas.push_back(consume());
      if (token(peek()).kind == TokenKind::RParen) {
        return std::unexpected(CstParseDiagnostic{
            token(peek()).range,
            "call argument list cannot end with a trailing comma"});
      }
    }
  }

  auto close = expect(TokenKind::RParen, "')' in call parameter list");
  if (!close)
    return std::unexpected(close.error());
  return CstCallParameterList{
      .kind = kind,
      .left_paren = *open,
      .parameters = std::move(parameters),
      .commas = std::move(commas),
      .right_paren = *close,
      .token_range = {*open, *close + 1},
  };
}

/** Parse the handwritten call return/target/input/metadata source shape. */
std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
PtxCstParser::parseCallOperands() {
  using syntax_cst::CstCallParameterListKind;
  std::vector<syntax_cst::CstOperandElement> operands;
  const bool has_return_parameters = token(peek()).kind == TokenKind::LParen;
  if (has_return_parameters) {
    auto returns = parseCallParameterList(CstCallParameterListKind::Return);
    if (!returns)
      return std::unexpected(returns.error());
    operands.push_back({std::move(*returns), std::nullopt});
    auto comma = expect(TokenKind::Comma, "',' after call return parameter");
    if (!comma)
      return std::unexpected(comma.error());
    operands.back().trailing_comma = *comma;
  }

  auto target = expect(TokenKind::Ident, "call target");
  if (!target)
    return std::unexpected(target.error());
  operands.push_back(
      {syntax_cst::CstCallTarget{syntax_cst::CstIdentifier{*target},
                                 {*target, *target + 1}},
       std::nullopt});

  if (token(peek()).kind != TokenKind::Comma) {
    if (has_return_parameters) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          "a call with a return parameter requires an input parameter list"});
    }
    return operands;
  }

  operands.back().trailing_comma = consume();
  if (token(peek()).kind == TokenKind::LParen) {
    auto inputs = parseCallParameterList(CstCallParameterListKind::Input);
    if (!inputs)
      return std::unexpected(inputs.error());
    operands.push_back({std::move(*inputs), std::nullopt});
    if (token(peek()).kind != TokenKind::Comma)
      return operands;
    operands.back().trailing_comma = consume();
  } else if (has_return_parameters) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "expected call input parameter list after the target"});
  }

  auto target_set = expect(TokenKind::Ident, "call target set or prototype");
  if (!target_set)
    return std::unexpected(target_set.error());
  operands.push_back(
      {syntax_cst::CstCallTargetSet{syntax_cst::CstIdentifier{*target_set},
                                    {*target_set, *target_set + 1}},
       std::nullopt});
  return operands;
}

/** Parse a direct branch target without assigning instruction legality. */
std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
PtxCstParser::parseBranchOperands() {
  auto target = expect(TokenKind::Ident, "branch label target");
  if (!target)
    return std::unexpected(target.error());
  if (token(peek()).kind == TokenKind::Comma) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "direct branch accepts exactly one label target"});
  }
  std::vector<syntax_cst::CstOperandElement> operands;
  operands.push_back(
      {syntax_cst::CstBranchTarget{syntax_cst::CstIdentifier{*target},
                                   {*target, *target + 1}},
       std::nullopt});
  return operands;
}

/** Parse the indexed branch index and target-set source shape. */
std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
PtxCstParser::parseIndexedBranchOperands() {
  auto index = expect(TokenKind::Ident, "brx.idx index register");
  if (!index)
    return std::unexpected(index.error());
  auto comma = expect(TokenKind::Comma, "',' after brx.idx index register");
  if (!comma)
    return std::unexpected(comma.error());
  auto target_set = expect(TokenKind::Ident, "brx.idx branch target list");
  if (!target_set)
    return std::unexpected(target_set.error());
  if (token(peek()).kind == TokenKind::Comma) {
    return std::unexpected(
        CstParseDiagnostic{token(peek()).range,
                           "brx.idx accepts exactly an index and target list"});
  }
  return std::vector<syntax_cst::CstOperandElement>{
      {syntax_cst::CstIdentifier{*index}, *comma},
      {syntax_cst::CstBranchTargetSet{syntax_cst::CstIdentifier{*target_set},
                                      {*target_set, *target_set + 1}},
       std::nullopt}};
}

/** Predict operand punctuation without consuming optional absent source forms. */
std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseOperand() {
  if (token(peek()).kind == TokenKind::Minus &&
      token(peekNext()).kind == TokenKind::Ident) {
    auto operand = parseNegatedRegisterOperand();
    if (!operand)
      return std::unexpected(operand.error());
    return syntax_cst::CstOperand{std::move(*operand)};
  }
  if (token(peek()).kind == TokenKind::Exclamation &&
      token(peekNext()).kind == TokenKind::Ident) {
    auto operand = parsePredicateOperand();
    if (!operand)
      return std::unexpected(operand.error());
    return syntax_cst::CstOperand{std::move(*operand)};
  }
  if (token(peek()).kind == TokenKind::LBracket)
    return parseBracketedAddress(consume());
  if (token(peek()).kind == TokenKind::LBrace)
    return parseVectorOperand();
  if (atNumericStart())
    return parseNumericSourceOperand();
  return parseIdentifierOperand();
}

/** Parse NegatedRegisterOperand from its unconsumed source prefix. */
std::expected<syntax_cst::CstNegatedRegisterOperand, CstParseDiagnostic>
PtxCstParser::parseNegatedRegisterOperand() {
  const TokenId minus = consume();
  const TokenId identifier = consume();
  const syntax_cst::CstIdentifier base{identifier};
  if (token(peek()).kind == TokenKind::DotIdent) {
    const TokenId selector = consume();
    return syntax_cst::CstNegatedRegisterOperand{
        minus,
        syntax_cst::CstVectorMember{base, selector, {identifier, selector + 1}},
        {minus, selector + 1}};
  }
  return syntax_cst::CstNegatedRegisterOperand{
      minus, base, {minus, identifier + 1}};
}

/** Parse PredicateOperand from its unconsumed source prefix. */
std::expected<syntax_cst::CstPredicateOperand, CstParseDiagnostic>
PtxCstParser::parsePredicateOperand() {
  const TokenId exclamation = consume();
  auto name = expect(TokenKind::Ident, "predicate operand");
  if (!name)
    return std::unexpected(name.error());
  return syntax_cst::CstPredicateOperand{
      exclamation, *name, {exclamation, *name + 1}};
}

/** Parse VectorOperand from its unconsumed source prefix. */
std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseVectorOperand() {
  auto vector = parseVectorPack(consume());
  if (!vector)
    return std::unexpected(vector.error());
  if (token(peek()).kind != TokenKind::Pipe)
    return syntax_cst::CstOperand{std::move(*vector)};
  const TokenId pipe = consume();
  auto predicate = expect(TokenKind::Ident, "predicate after '|'");
  if (!predicate)
    return std::unexpected(predicate.error());
  auto pack = std::move(*vector);
  const TokenId first = pack.token_range.first;
  return syntax_cst::CstOperand{syntax_cst::CstVectorPredicatePair{
      .data = std::move(pack),
      .pipe = pipe,
      .predicate = syntax_cst::CstIdentifier{*predicate},
      .token_range = {first, *predicate + 1}}};
}

/** Parse NumericSourceOperand from its unconsumed source prefix. */
std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseNumericSourceOperand() {
  auto immediate = parseNumericOperand();
  if (!immediate)
    return std::unexpected(immediate.error());
  if (const auto* expression =
          std::get_if<syntax_cst::CstConstantOperand>(&*immediate)) {
    if (const auto* unary = std::get_if<syntax_cst::CstConstantUnary>(
            &expression->expression->node);
        unary && token(unary->operator_token).kind == TokenKind::Exclamation) {
      const syntax_cst::CstConstantExpression* child = unary->operand.get();
      std::optional<TokenId> sign;
      if (const auto* signed_child =
              std::get_if<syntax_cst::CstConstantUnary>(&child->node);
          signed_child &&
          (token(signed_child->operator_token).kind == TokenKind::Plus ||
           token(signed_child->operator_token).kind == TokenKind::Minus)) {
        sign = signed_child->operator_token;
        child = signed_child->operand.get();
      }
      if (const auto* literal =
              std::get_if<syntax_cst::CstConstantLiteral>(&child->node)) {
        if (!isIntegerLiteral(token(literal->literal).kind) &&
            token(literal->literal).kind != TokenKind::WarpSz)
          return std::unexpected(CstParseDiagnostic{
              token(literal->literal).range,
              "expected integer predicate constant after '!'"});
        return syntax_cst::CstOperand{syntax_cst::CstNegatedImmediate{
            unary->operator_token,
            syntax_cst::CstNumericOperand{syntax_cst::CstImmediate{
                sign,
                literal->literal,
                {sign.value_or(literal->literal), literal->literal + 1}}},
            expression->token_range}};
      }
    }
  }
  return std::visit(
      [](auto&& leaf) -> syntax_cst::CstOperand { return std::move(leaf); },
      std::move(*immediate));
}

/** Parse an identifier and its optional index, selector, predicate or offset suffix. */
std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseIdentifierOperand() {
  auto identifier = expect(TokenKind::Ident, "operand");
  if (!identifier)
    return std::unexpected(identifier.error());
  const syntax_cst::CstIdentifier base{*identifier};

  if (token(peek()).kind == TokenKind::LBracket) {
    const TokenId open = consume();
    syntax_cst::CstAddressBase index;
    std::optional<syntax_cst::CstAddressOffset> displacement;
    if (token(peek()).kind == TokenKind::Ident &&
        token(peek()).text != "WARP_SZ") {
      index = syntax_cst::CstIdentifier{consume()};
      if (token(peek()).kind == TokenKind::Plus ||
          token(peek()).kind == TokenKind::Minus) {
        const TokenId operation = consume();
        auto term = parseNumericOperand();
        if (!term)
          return std::unexpected(term.error());
        const TokenId end = std::visit(
            [](const auto& value) { return value.token_range.last; }, *term);
        displacement = syntax_cst::CstAddressOffset{
            operation, std::move(*term), {operation, end}};
      }
    } else {
      auto numeric = parseNumericOperand();
      if (!numeric)
        return std::unexpected(numeric.error());
      index = std::visit(
          [](auto&& value) -> syntax_cst::CstAddressBase {
            return std::move(value);
          },
          std::move(*numeric));
    }
    auto close = expect(TokenKind::RBracket, "']' after named-array index");
    if (!close)
      return std::unexpected(close.error());
    syntax_cst::CstAddress address{.base = base,
                                   .token_range = {*identifier, *close + 1}};
    address.named_index =
        syntax_cst::CstNamedArrayIndex{open,
                                       std::move(index),
                                       std::move(displacement),
                                       *close,
                                       {open, *close + 1}};
    return syntax_cst::CstOperand{std::move(address)};
  }

  if (token(peek()).kind == TokenKind::Pipe) {
    const TokenId pipe = consume();
    auto predicate = expect(TokenKind::Ident, "predicate after '|'");
    if (!predicate)
      return std::unexpected(predicate.error());
    return syntax_cst::CstOperand{syntax_cst::CstRegisterPredicatePair{
        base,
        pipe,
        syntax_cst::CstIdentifier{*predicate},
        {*identifier, *predicate + 1}}};
  }

  if (token(peek()).kind == TokenKind::DotIdent) {
    const TokenId selector = consume();
    return syntax_cst::CstOperand{syntax_cst::CstVectorMember{
        base, selector, {*identifier, selector + 1}}};
  }
  if (token(peek()).kind == TokenKind::Plus ||
      token(peek()).kind == TokenKind::Minus) {
    const TokenId op = consume();
    auto magnitude = parseNumericOperand();
    if (!magnitude)
      return std::unexpected(magnitude.error());
    const TokenId last = std::visit(
        [](const auto& leaf) { return leaf.token_range.last - 1; }, *magnitude);
    syntax_cst::CstAddressOffset offset{
        op, std::move(*magnitude), {op, last + 1}};
    return syntax_cst::CstOperand{
        syntax_cst::CstAddress{std::nullopt,
                               base,
                               std::move(offset),
                               std::nullopt,
                               std::nullopt,
                               {*identifier, last + 1}}};
  }
  return syntax_cst::CstOperand{base};
}

/** Parse an optional predicate and opcode, then delegate the consumed head. */
std::expected<syntax_cst::CstInstruction, CstParseDiagnostic>
PtxCstParser::parseInstructionNode() {
  std::optional<syntax_cst::CstPredicate> predicate;
  if (token(peek()).kind == TokenKind::At) {
    const TokenId at = consume();
    std::optional<TokenId> exclamation;
    if (token(peek()).kind == TokenKind::Exclamation)
      exclamation = consume();
    auto name = expect(TokenKind::Ident, "predicate identifier");
    if (!name)
      return std::unexpected(name.error());
    predicate =
        syntax_cst::CstPredicate{at, exclamation, *name, {at, *name + 1}};
  }

  auto opcode = expect(TokenKind::Ident, "instruction opcode");
  if (!opcode)
    return std::unexpected(opcode.error());
  return parseInstructionAfterHead(*opcode, std::move(predicate));
}

/** The opcode is already consumed; this entry owns modifiers and the semicolon. */
std::expected<syntax_cst::CstInstruction, CstParseDiagnostic>
PtxCstParser::parseInstructionAfterHead(
    TokenId opcode, std::optional<syntax_cst::CstPredicate> predicate) {
  const TokenId first = predicate ? predicate->token_range.first : opcode;

  std::vector<TokenId> modifiers;
  while (isModifier(token(peek()).kind))
    modifiers.push_back(consume());

  std::vector<syntax_cst::CstOperandElement> operands;
  if (token(opcode).text == "call") {
    auto parsed_operands = parseCallOperands();
    if (!parsed_operands)
      return std::unexpected(parsed_operands.error());
    operands = std::move(*parsed_operands);
  } else if (token(opcode).text == "bra") {
    auto parsed_operands = parseBranchOperands();
    if (!parsed_operands)
      return std::unexpected(parsed_operands.error());
    operands = std::move(*parsed_operands);
  } else if (token(opcode).text == "brx") {
    auto parsed_operands = parseIndexedBranchOperands();
    if (!parsed_operands)
      return std::unexpected(parsed_operands.error());
    operands = std::move(*parsed_operands);
  } else if (token(peek()).kind != TokenKind::Semicolon) {
    for (;;) {
      auto operand = parseOperand();
      if (!operand)
        return std::unexpected(operand.error());
      syntax_cst::CstOperandElement element{std::move(*operand), std::nullopt};
      if (token(peek()).kind == TokenKind::Comma)
        element.trailing_comma = consume();
      const bool has_comma = element.trailing_comma.has_value();
      operands.push_back(std::move(element));
      if (!has_comma)
        break;
    }
  }

  auto semicolon = expect(TokenKind::Semicolon, "';'");
  if (!semicolon)
    return std::unexpected(semicolon.error());

  return syntax_cst::CstInstruction{
      std::move(predicate), opcode,     std::move(modifiers),
      std::move(operands),  *semicolon, {first, *semicolon + 1}};
}

/** Parse labeled call metadata after the label and colon have been consumed. */
std::expected<syntax_cst::CstCallPrototype, CstParseDiagnostic>
PtxCstParser::parseCallPrototype(TokenId label, TokenId colon) {
  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotCallPrototype) {
    return std::unexpected(CstParseDiagnostic{token(directive).range,
                                              "expected '.callprototype'"});
  }

  std::optional<syntax_cst::CstFunctionParameterList> return_parameters;
  if (token(peek()).kind == TokenKind::LParen) {
    auto parsed = parseFunctionParameterList();
    if (!parsed)
      return std::unexpected(parsed.error());
    if (parsed->parameters.size() != 1) {
      return std::unexpected(CstParseDiagnostic{
          SourceRange{token(parsed->token_range.first).range.start,
                      token(parsed->token_range.last - 1).range.end},
          ".callprototype return parameter list must contain exactly one "
          "parameter"});
    }
    return_parameters = std::move(*parsed);
  }

  const auto sink = expect(TokenKind::Ident, "'_' in .callprototype");
  if (!sink)
    return std::unexpected(sink.error());
  if (token(*sink).text != "_") {
    return std::unexpected(CstParseDiagnostic{
        token(*sink).range, "expected '_' in .callprototype"});
  }

  std::optional<syntax_cst::CstFunctionParameterList> parameters;
  if (token(peek()).kind == TokenKind::LParen) {
    auto parsed = parseFunctionParameterList();
    if (!parsed)
      return std::unexpected(parsed.error());
    parameters = std::move(*parsed);
  }

  std::optional<TokenId> noreturn_directive;
  if (token(peek()).kind == TokenKind::DotNoreturn)
    noreturn_directive = consume();

  const auto parse_abi_suffix = [this](std::string_view spelling)
      -> std::expected<std::optional<syntax_cst::CstCallPrototypeAbiSuffix>,
                       CstParseDiagnostic> {
    if (token(peek()).kind != TokenKind::DotIdent ||
        token(peek()).text != spelling) {
      return std::optional<syntax_cst::CstCallPrototypeAbiSuffix>{};
    }
    const TokenId suffix = consume();
    auto count = expectIntegerLiteral("ABI-preserve register count");
    if (!count)
      return std::unexpected(count.error());
    return syntax_cst::CstCallPrototypeAbiSuffix{
        suffix, *count, {suffix, *count + 1}};
  };

  auto abi_preserve = parse_abi_suffix(".abi_preserve");
  if (!abi_preserve)
    return std::unexpected(abi_preserve.error());
  auto abi_preserve_control = parse_abi_suffix(".abi_preserve_control");
  if (!abi_preserve_control)
    return std::unexpected(abi_preserve_control.error());

  const auto semicolon =
      expect(TokenKind::Semicolon, "';' after .callprototype");
  if (!semicolon)
    return std::unexpected(semicolon.error());
  return syntax_cst::CstCallPrototype{
      .label = label,
      .colon = colon,
      .directive = directive,
      .return_parameters = std::move(return_parameters),
      .sink = *sink,
      .parameters = std::move(parameters),
      .noreturn_directive = noreturn_directive,
      .abi_preserve = std::move(*abi_preserve),
      .abi_preserve_control = std::move(*abi_preserve_control),
      .semicolon = *semicolon,
      .token_range = {label, *semicolon + 1},
  };
}

/** Parse a labeled call-target list after the label and colon have been consumed. */
std::expected<syntax_cst::CstCallTargets, CstParseDiagnostic>
PtxCstParser::parseCallTargets(TokenId label, TokenId colon) {
  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotCallTargets) {
    return std::unexpected(
        CstParseDiagnostic{token(directive).range, "expected '.calltargets'"});
  }
  if (token(peek()).kind == TokenKind::Semicolon) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        ".calltargets requires at least one function target"});
  }

  std::vector<TokenId> targets;
  std::vector<TokenId> commas;
  for (;;) {
    auto target = expect(TokenKind::Ident, "call target function");
    if (!target)
      return std::unexpected(target.error());
    targets.push_back(*target);
    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
    if (token(peek()).kind == TokenKind::Semicolon) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          "call target list cannot end with a trailing comma"});
    }
  }

  const auto semicolon = expect(TokenKind::Semicolon, "';' after .calltargets");
  if (!semicolon)
    return std::unexpected(semicolon.error());
  return syntax_cst::CstCallTargets{
      .label = label,
      .colon = colon,
      .directive = directive,
      .targets = std::move(targets),
      .commas = std::move(commas),
      .semicolon = *semicolon,
      .token_range = {label, *semicolon + 1},
  };
}

/** Parse labeled branch-target metadata after its label and colon. */
std::expected<syntax_cst::CstBranchTargets, CstParseDiagnostic>
PtxCstParser::parseBranchTargets(TokenId label, TokenId colon) {
  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotBranchTargets) {
    return std::unexpected(CstParseDiagnostic{token(directive).range,
                                              "expected '.branchtargets'"});
  }
  if (token(peek()).kind == TokenKind::Semicolon) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        ".branchtargets requires at least one label target"});
  }

  std::vector<syntax_cst::CstBranchTargetEntry> targets;
  std::vector<TokenId> commas;
  for (;;) {
    auto name = expect(TokenKind::Ident, "branch target label");
    if (!name)
      return std::unexpected(name.error());
    std::optional<TokenId> left_angle;
    std::optional<TokenId> count;
    std::optional<TokenId> right_angle;
    TokenId last = *name;
    if (token(peek()).kind == TokenKind::Lt) {
      left_angle = consume();
      auto parsed_count = expectIntegerLiteral("branch target count");
      if (!parsed_count)
        return std::unexpected(parsed_count.error());
      count = *parsed_count;
      auto parsed_right_angle =
          expect(TokenKind::Gt, "'>' after branch target count");
      if (!parsed_right_angle)
        return std::unexpected(parsed_right_angle.error());
      right_angle = *parsed_right_angle;
      last = *right_angle;
    }
    targets.push_back(syntax_cst::CstBranchTargetEntry{
        .name = *name,
        .left_angle = left_angle,
        .count = count,
        .right_angle = right_angle,
        .token_range = {*name, last + 1},
    });
    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
    if (token(peek()).kind == TokenKind::Semicolon) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          "branch target list cannot end with a trailing comma"});
    }
  }

  const auto semicolon =
      expect(TokenKind::Semicolon, "';' after .branchtargets");
  if (!semicolon)
    return std::unexpected(semicolon.error());
  return syntax_cst::CstBranchTargets{
      .label = label,
      .colon = colon,
      .directive = directive,
      .targets = std::move(targets),
      .commas = std::move(commas),
      .semicolon = *semicolon,
      .token_range = {label, *semicolon + 1},
  };
}

}  // namespace ptx_frontend
