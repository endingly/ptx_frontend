#include <ptx_frontend/cst/ptx_cst_parser.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ptx_frontend::syntax_cst {

const PtxToken& CstFile::token(TokenId id) const {
  return tokens.at(id);
}

SourceRange CstFile::sourceRange(CstTokenRange range) const {
  if (range.first >= range.last || range.last > tokens.size())
    throw std::out_of_range("invalid CST token range");
  return SourceRange{token(range.first).range.start,
                     token(range.last - 1).range.end};
}

std::string CstFile::sourceText() const {
  std::string result;
  for (const PtxToken& token : tokens) {
    for (const Trivia& trivia : token.leading_trivia)
      result += trivia.text;
    result += token.text;
  }
  return result;
}

const CstInstruction* CstFile::instruction() const noexcept {
  return std::get_if<CstInstruction>(&root);
}

const CstModule* CstFile::module() const noexcept {
  return std::get_if<CstModule>(&root);
}

}  // namespace ptx_frontend::syntax_cst

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

bool isIntegerLiteral(TokenKind kind) {
  return kind == TokenKind::Decimal || kind == TokenKind::Hex;
}

bool isIdentifierToken(TokenKind kind) {
  return kind == TokenKind::Ident || kind == TokenKind::DotIdent;
}

bool isModifier(TokenKind kind) {
  return kind == TokenKind::DotIdent || kind == TokenKind::DotGlobal ||
         kind == TokenKind::DotConst || kind == TokenKind::DotShared ||
         kind == TokenKind::DotLocal || kind == TokenKind::DotParam ||
         kind == TokenKind::DotWeak;
}

bool isModuleDirective(TokenKind kind) {
  return kind == TokenKind::DotVersion || kind == TokenKind::DotTarget ||
         kind == TokenKind::DotAddressSize || kind == TokenKind::DotFile;
}

bool isFunctionQualifier(TokenKind kind);
bool isVariableStateSpace(TokenKind kind);

bool isKernelResourceDirective(TokenKind kind) {
  return kind == TokenKind::DotMaxnreg || kind == TokenKind::DotMaxntid ||
         kind == TokenKind::DotReqntid ||
         kind == TokenKind::DotMinnctapersm;
}

bool isFunctionBoundary(TokenKind kind) {
  return kind == TokenKind::DotEntry || kind == TokenKind::DotFunc ||
         isFunctionQualifier(kind);
}

bool isFunctionOrModuleBoundary(TokenKind kind) {
  return isFunctionBoundary(kind) || isModuleDirective(kind) ||
         kind == TokenKind::DotSection;
}

bool isSupportedModuleItemStart(TokenKind kind) {
  return isModuleDirective(kind) || kind == TokenKind::DotPragma ||
         kind == TokenKind::DotSection || kind == TokenKind::DotEntry ||
         kind == TokenKind::DotFunc || isFunctionQualifier(kind) ||
         isVariableStateSpace(kind);
}

bool isFunctionBodyItemStart(TokenKind kind) {
  return kind == TokenKind::LBrace || kind == TokenKind::At ||
         kind == TokenKind::Ident ||
         isVariableStateSpace(kind) || kind == TokenKind::DotLoc ||
         kind == TokenKind::DotPragma || kind == TokenKind::DotCallPrototype ||
         kind == TokenKind::DotCallTargets || kind == TokenKind::DotBranchTargets;
}

CstParseResult parseFailure(CstParseDiagnostic diagnostic) {
  return {.value = std::nullopt, .diagnostics = {std::move(diagnostic)}};
}

bool isFunctionQualifier(TokenKind kind) {
  return kind == TokenKind::DotExtern || kind == TokenKind::DotVisible ||
         kind == TokenKind::DotWeak;
}

bool isVariableStateSpace(TokenKind kind) {
  return kind == TokenKind::DotReg || kind == TokenKind::DotParam ||
         kind == TokenKind::DotLocal || kind == TokenKind::DotShared ||
         kind == TokenKind::DotGlobal || kind == TokenKind::DotConst;
}

bool isConstantLiteral(TokenKind kind) {
  return isImmediate(kind) || kind == TokenKind::WarpSz;
}

bool isConstantUnaryOperator(TokenKind kind) {
  return kind == TokenKind::Plus || kind == TokenKind::Minus ||
         kind == TokenKind::Exclamation || kind == TokenKind::Tilde;
}

int constantBinaryPrecedence(TokenKind kind) {
  switch (kind) {
    case TokenKind::PipePipe:
      return 1;
    case TokenKind::AmpAmp:
      return 2;
    case TokenKind::Pipe:
      return 3;
    case TokenKind::Caret:
      return 4;
    case TokenKind::Amp:
      return 5;
    case TokenKind::EqEq:
    case TokenKind::NotEq:
      return 6;
    case TokenKind::Lt:
    case TokenKind::LtEq:
    case TokenKind::Gt:
    case TokenKind::GtEq:
      return 7;
    case TokenKind::ShiftLeft:
    case TokenKind::ShiftRight:
      return 8;
    case TokenKind::Plus:
    case TokenKind::Minus:
      return 9;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
      return 10;
    default:
      return -1;
  }
}

}  // namespace

PtxCstParser::PtxCstParser(std::string_view source) : lexer_(source) {}

PtxCstParser::TokenId PtxCstParser::peek() {
  if (!peeked_) {
    tokens_.push_back(lexer_.consume());
    peeked_ = static_cast<TokenId>(tokens_.size() - 1);
  }
  return *peeked_;
}

PtxCstParser::TokenId PtxCstParser::consume() {
  const TokenId id = peek();
  peeked_.reset();
  return id;
}

const PtxToken& PtxCstParser::token(TokenId id) const {
  return tokens_.at(id);
}

bool PtxCstParser::atImmediateStart() {
  const TokenKind kind = token(peek()).kind;
  return isImmediate(kind) || kind == TokenKind::Plus ||
         kind == TokenKind::Minus;
}

std::expected<PtxCstParser::TokenId, CstParseDiagnostic> PtxCstParser::expect(
    TokenKind kind, std::string_view name) {
  const TokenId id = peek();
  if (token(id).kind == kind)
    return consume();
  return std::unexpected(CstParseDiagnostic{
      token(id).range, "expected " + std::string(name), kind});
}

PtxCstParser::RecoveryResult PtxCstParser::recover(
    TokenId first,
    const CstParseDiagnostic& diagnostic,
    RecoveryContext context) {
  using syntax_cst::CstRecoveryKind;
  using syntax_cst::CstRecoveryNode;
  using syntax_cst::CstTokenRange;

  RecoveryResult result;
  TokenId current = peek();
  const auto append_span = [this, &result](CstRecoveryKind kind,
                                           TokenId begin,
                                           TokenId end) {
    if (begin >= end)
      return;
    result.nodes.push_back(CstRecoveryNode{
        .kind = kind,
        .expected_kind = std::nullopt,
        .token_range = CstTokenRange{begin, end},
        .range = SourceRange{token(begin).range.start,
                             token(end - 1).range.end},
    });
    result.last = end - 1;
  };
  const auto append_inserted = [this, &result](TokenKind expected,
                                                TokenId position) {
    result.nodes.push_back(CstRecoveryNode{
        .kind = CstRecoveryKind::Inserted,
        .expected_kind = expected,
        .token_range = std::nullopt,
        .range = SourceRange{token(position).range.start,
                             token(position).range.start},
    });
  };

  const auto is_anchor = [context](TokenKind kind) {
    if (context == RecoveryContext::FunctionBody)
      return kind == TokenKind::RBrace || isFunctionOrModuleBoundary(kind);
    return isSupportedModuleItemStart(kind);
  };
  const auto stop_at_anchor = [context](TokenKind kind) {
    if (context == RecoveryContext::FunctionBody) {
      if (kind == TokenKind::RBrace)
        return RecoveryStop::RightBrace;
      return RecoveryStop::FunctionBoundary;
    }
    return RecoveryStop::ModuleItem;
  };
  const auto append_missing_semicolon = [&] {
    if (diagnostic.expected_kind != TokenKind::Semicolon)
      return;
    append_inserted(TokenKind::Semicolon, current);
  };

  append_span(CstRecoveryKind::Skipped, first, current);
  if (token(current).kind == TokenKind::Eof) {
    result.nodes.push_back(CstRecoveryNode{
        .kind = CstRecoveryKind::Error,
        .expected_kind = std::nullopt,
        .token_range = std::nullopt,
        .range = token(current).range,
    });
    append_missing_semicolon();
    result.stop = RecoveryStop::Eof;
    return result;
  }

  if ((context == RecoveryContext::FunctionBody || current != first) &&
      is_anchor(token(current).kind)) {
    append_missing_semicolon();
    result.stop = stop_at_anchor(token(current).kind);
    return result;
  }
  if (context == RecoveryContext::FunctionBody &&
      diagnostic.expected_kind == TokenKind::Semicolon &&
      isFunctionBodyItemStart(token(current).kind)) {
    append_inserted(TokenKind::Semicolon, current);
    result.stop = RecoveryStop::Semicolon;
    return result;
  }

  const TokenId error = consume();
  append_span(CstRecoveryKind::Error, error, error + 1);
  const TokenId skipped_first = peek();
  while (token(peek()).kind != TokenKind::Eof &&
         !is_anchor(token(peek()).kind)) {
    const TokenId skipped = consume();
    if (token(skipped).kind == TokenKind::Semicolon) {
      append_span(CstRecoveryKind::Skipped, skipped_first, skipped + 1);
      result.stop = RecoveryStop::Semicolon;
      return result;
    }
  }
  append_span(CstRecoveryKind::Skipped, skipped_first, peek());
  result.stop = token(peek()).kind == TokenKind::Eof
                    ? RecoveryStop::Eof
                    : stop_at_anchor(token(peek()).kind);
  return result;
}

std::expected<syntax_cst::CstImmediate, CstParseDiagnostic>
PtxCstParser::parseImmediate(bool allow_sign) {
  std::optional<TokenId> sign;
  TokenId first = peek();
  if (allow_sign && (token(first).kind == TokenKind::Plus ||
                     token(first).kind == TokenKind::Minus)) {
    sign = consume();
    first = *sign;
  }

  const TokenId literal = consume();
  if (!isImmediate(token(literal).kind)) {
    return std::unexpected(
        CstParseDiagnostic{token(literal).range, "expected immediate literal"});
  }
  return syntax_cst::CstImmediate{sign, literal, {first, literal + 1}};
}

std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseBracketedAddress(TokenId open) {
  const TokenId first = open;
  syntax_cst::CstAddressBase base;
  TokenId last = first;

  if (atImmediateStart()) {
    auto immediate = parseImmediate();
    if (!immediate)
      return std::unexpected(immediate.error());
    last = immediate->token_range.last - 1;
    base = std::move(*immediate);
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
    auto magnitude = parseImmediate(false);
    if (!magnitude)
      return std::unexpected(magnitude.error());
    last = magnitude->token_range.last - 1;
    offset =
        syntax_cst::CstAddressOffset{op, std::move(*magnitude), {op, last + 1}};
  }

  auto close = expect(TokenKind::RBracket, "']'");
  if (!close)
    return std::unexpected(close.error());
  last = *close;

  return syntax_cst::CstOperand{syntax_cst::CstAddress{
      open, std::move(base), std::move(offset), *close, {first, last + 1}}};
}

std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseVectorPack(TokenId open) {
  std::vector<syntax_cst::CstVectorElement> elements;
  std::vector<TokenId> commas;

  if (token(peek()).kind == TokenKind::RBrace) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "vector operand cannot be empty"});
  }

  for (;;) {
    if (token(peek()).kind == TokenKind::Ident) {
      elements.emplace_back(syntax_cst::CstIdentifier{consume()});
    } else if (atImmediateStart()) {
      auto immediate = parseImmediate();
      if (!immediate)
        return std::unexpected(immediate.error());
      elements.emplace_back(std::move(*immediate));
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
  return syntax_cst::CstOperand{syntax_cst::CstVectorPack{open,
                                                          std::move(elements),
                                                          std::move(commas),
                                                          *close,
                                                          {open, *close + 1}}};
}

std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
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
      } else if (atImmediateStart()) {
        auto parameter = parseImmediate();
        if (!parameter)
          return std::unexpected(parameter.error());
        parameters.emplace_back(std::move(*parameter));
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
  return syntax_cst::CstOperand{CstCallParameterList{
      .kind = kind,
      .left_paren = *open,
      .parameters = std::move(parameters),
      .commas = std::move(commas),
      .right_paren = *close,
      .token_range = {*open, *close + 1},
  }};
}

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
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "brx.idx accepts exactly an index and target list"});
  }
  return std::vector<syntax_cst::CstOperandElement>{
      {syntax_cst::CstIdentifier{*index}, *comma},
      {syntax_cst::CstBranchTargetSet{
           syntax_cst::CstIdentifier{*target_set}, {*target_set, *target_set + 1}},
       std::nullopt}};
}

std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseOperand() {
  if (token(peek()).kind == TokenKind::Exclamation) {
    const TokenId exclamation = consume();
    auto name = expect(TokenKind::Ident, "predicate operand");
    if (!name)
      return std::unexpected(name.error());
    return syntax_cst::CstOperand{syntax_cst::CstPredicateOperand{
        exclamation, *name, {exclamation, *name + 1}}};
  }
  if (token(peek()).kind == TokenKind::LBracket)
    return parseBracketedAddress(consume());
  if (token(peek()).kind == TokenKind::LBrace)
    return parseVectorPack(consume());
  if (atImmediateStart()) {
    auto immediate = parseImmediate();
    if (!immediate)
      return std::unexpected(immediate.error());
    return syntax_cst::CstOperand{std::move(*immediate)};
  }

  auto identifier = expect(TokenKind::Ident, "operand");
  if (!identifier)
    return std::unexpected(identifier.error());
  const syntax_cst::CstIdentifier base{*identifier};

  if (token(peek()).kind == TokenKind::DotIdent) {
    const TokenId selector = consume();
    return syntax_cst::CstOperand{syntax_cst::CstVectorMember{
        base, selector, {*identifier, selector + 1}}};
  }
  if (token(peek()).kind == TokenKind::Plus ||
      token(peek()).kind == TokenKind::Minus) {
    const TokenId op = consume();
    auto magnitude = parseImmediate(false);
    if (!magnitude)
      return std::unexpected(magnitude.error());
    const TokenId last = magnitude->token_range.last - 1;
    syntax_cst::CstAddressOffset offset{
        op, std::move(*magnitude), {op, last + 1}};
    return syntax_cst::CstOperand{
        syntax_cst::CstAddress{std::nullopt,
                               base,
                               std::move(offset),
                               std::nullopt,
                               {*identifier, last + 1}}};
  }
  return syntax_cst::CstOperand{base};
}

std::expected<syntax_cst::CstInstruction, CstParseDiagnostic>
PtxCstParser::parseInstructionNode(std::optional<TokenId> supplied_opcode) {
  std::optional<syntax_cst::CstPredicate> predicate;
  TokenId first = supplied_opcode.value_or(peek());
  if (!supplied_opcode && token(peek()).kind == TokenKind::At) {
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

  std::expected<TokenId, CstParseDiagnostic> opcode =
      supplied_opcode
          ? std::expected<TokenId, CstParseDiagnostic>{*supplied_opcode}
          : expect(TokenKind::Ident, "instruction opcode");
  if (!opcode)
    return std::unexpected(opcode.error());
  if (!predicate)
    first = *opcode;

  std::vector<TokenId> modifiers;
  while (isModifier(token(peek()).kind))
    modifiers.push_back(consume());

  std::vector<syntax_cst::CstOperandElement> operands;
  if (token(*opcode).text == "call") {
    auto parsed_operands = parseCallOperands();
    if (!parsed_operands)
      return std::unexpected(parsed_operands.error());
    operands = std::move(*parsed_operands);
  } else if (token(*opcode).text == "bra") {
    auto parsed_operands = parseBranchOperands();
    if (!parsed_operands)
      return std::unexpected(parsed_operands.error());
    operands = std::move(*parsed_operands);
  } else if (token(*opcode).text == "brx") {
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
      std::move(predicate), *opcode,    std::move(modifiers),
      std::move(operands),  *semicolon, {first, *semicolon + 1}};
}

std::expected<syntax_cst::CstConstantExpression, CstParseDiagnostic>
PtxCstParser::parseConstantPrimary() {
  using namespace syntax_cst;

  const TokenId first = peek();
  CstConstantExpression expression;
  if (isConstantLiteral(token(first).kind)) {
    const TokenId literal = consume();
    expression = CstConstantExpression{CstConstantLiteral{literal},
                                       {literal, literal + 1}};
  } else if (token(first).kind == TokenKind::Ident) {
    const TokenId name = consume();
    expression =
        CstConstantExpression{CstConstantSymbol{name}, {name, name + 1}};
  } else if (token(first).kind == TokenKind::LParen) {
    const TokenId left_paren = consume();
    if (token(peek()).kind == TokenKind::DotIdent &&
        (token(peek()).text == ".s64" || token(peek()).text == ".u64")) {
      const TokenId type = consume();
      auto right_paren = expect(TokenKind::RParen, "')' after constant cast");
      if (!right_paren)
        return std::unexpected(right_paren.error());
      auto operand = parseConstantUnary();
      if (!operand)
        return std::unexpected(operand.error());
      const TokenId last = operand->token_range.last;
      return CstConstantExpression{
          CstConstantCast{
              left_paren, type, *right_paren,
              std::make_unique<CstConstantExpression>(std::move(*operand))},
          {left_paren, last}};
    }

    auto inner = parseConstantExpression();
    if (!inner)
      return std::unexpected(inner.error());
    auto right_paren = expect(TokenKind::RParen, "')'");
    if (!right_paren)
      return std::unexpected(right_paren.error());
    expression = CstConstantExpression{
        CstConstantParenthesized{
            left_paren,
            std::make_unique<CstConstantExpression>(std::move(*inner)),
            *right_paren},
        {left_paren, *right_paren + 1}};
  } else {
    return std::unexpected(
        CstParseDiagnostic{token(first).range, "expected constant expression"});
  }

  while (token(peek()).kind == TokenKind::LParen) {
    const TokenId left_paren = consume();
    auto argument = parseConstantExpression();
    if (!argument)
      return std::unexpected(argument.error());
    auto right_paren =
        expect(TokenKind::RParen, "')' after initializer operator");
    if (!right_paren)
      return std::unexpected(right_paren.error());
    const TokenId expression_first = expression.token_range.first;
    expression = CstConstantExpression{
        CstConstantCall{
            std::make_unique<CstConstantExpression>(std::move(expression)),
            left_paren,
            std::make_unique<CstConstantExpression>(std::move(*argument)),
            *right_paren},
        {expression_first, *right_paren + 1}};
  }

  return expression;
}

std::expected<syntax_cst::CstConstantExpression, CstParseDiagnostic>
PtxCstParser::parseConstantUnary() {
  if (!isConstantUnaryOperator(token(peek()).kind))
    return parseConstantPrimary();

  const TokenId operator_token = consume();
  auto operand = parseConstantUnary();
  if (!operand)
    return std::unexpected(operand.error());
  const TokenId last = operand->token_range.last;
  return syntax_cst::CstConstantExpression{
      syntax_cst::CstConstantUnary{
          operator_token, std::make_unique<syntax_cst::CstConstantExpression>(
                              std::move(*operand))},
      {operator_token, last}};
}

std::expected<syntax_cst::CstConstantExpression, CstParseDiagnostic>
PtxCstParser::parseConstantExpression(int minimum_precedence) {
  auto left = parseConstantUnary();
  if (!left)
    return std::unexpected(left.error());

  for (;;) {
    const int precedence = constantBinaryPrecedence(token(peek()).kind);
    if (precedence < minimum_precedence)
      break;

    const TokenId operator_token = consume();
    auto right = parseConstantExpression(precedence + 1);
    if (!right)
      return std::unexpected(right.error());
    const TokenId first = left->token_range.first;
    const TokenId last = right->token_range.last;
    left = syntax_cst::CstConstantExpression{
        syntax_cst::CstConstantBinary{
            std::make_unique<syntax_cst::CstConstantExpression>(
                std::move(*left)),
            operator_token,
            std::make_unique<syntax_cst::CstConstantExpression>(
                std::move(*right))},
        {first, last}};
  }

  if (minimum_precedence == 0 && token(peek()).kind == TokenKind::Question) {
    const TokenId first = left->token_range.first;
    const TokenId question = consume();
    auto true_expression = parseConstantExpression();
    if (!true_expression)
      return std::unexpected(true_expression.error());
    auto colon = expect(TokenKind::Colon, "':' in conditional expression");
    if (!colon)
      return std::unexpected(colon.error());
    auto false_expression = parseConstantExpression();
    if (!false_expression)
      return std::unexpected(false_expression.error());
    const TokenId last = false_expression->token_range.last;
    left = syntax_cst::CstConstantExpression{
        syntax_cst::CstConstantConditional{
            std::make_unique<syntax_cst::CstConstantExpression>(
                std::move(*left)),
            question,
            std::make_unique<syntax_cst::CstConstantExpression>(
                std::move(*true_expression)),
            *colon,
            std::make_unique<syntax_cst::CstConstantExpression>(
                std::move(*false_expression))},
        {first, last}};
  }

  return left;
}

std::expected<syntax_cst::CstInitializer, CstParseDiagnostic>
PtxCstParser::parseInitializer() {
  using namespace syntax_cst;

  if (token(peek()).kind != TokenKind::LBrace) {
    auto expression = parseConstantExpression();
    if (!expression)
      return std::unexpected(expression.error());
    const CstTokenRange range = expression->token_range;
    return CstInitializer{std::move(*expression), range};
  }

  const TokenId left_brace = consume();
  std::vector<CstInitializer> elements;
  std::vector<TokenId> commas;
  while (token(peek()).kind != TokenKind::RBrace) {
    auto element = parseInitializer();
    if (!element)
      return std::unexpected(element.error());
    elements.push_back(std::move(*element));
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
  return CstInitializer{
      CstInitializerList{left_brace, std::move(elements), std::move(commas),
                         *right_brace, range},
      range};
}

std::expected<syntax_cst::CstVariableDeclaration, CstParseDiagnostic>
PtxCstParser::parseVariableDeclaration(std::vector<TokenId> qualifiers,
                                       std::optional<TokenId> first_token) {
  const TokenId first = first_token.value_or(peek());
  const TokenId state_space = consume();
  if (!isVariableStateSpace(token(state_space).kind)) {
    return std::unexpected(CstParseDiagnostic{token(state_space).range,
                                              "expected variable state space"});
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
        auto expression = parseConstantExpression();
        if (!expression)
          return std::unexpected(expression.error());
        size = std::move(*expression);
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
      auto parsed_initializer = parseInitializer();
      if (!parsed_initializer)
        return std::unexpected(parsed_initializer.error());
      last = parsed_initializer->token_range.last - 1;
      initializer = std::move(*parsed_initializer);
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
      auto size = parseConstantExpression();
      if (!size)
        return std::unexpected(size.error());
      array_size = std::move(*size);
    }
    auto close = expect(TokenKind::RBracket, "']'");
    if (!close)
      return std::unexpected(close.error());
    right_bracket = *close;
    last = *close;
  }

  return syntax_cst::CstFunctionParameter{
      state_space,
      align_directive,
      alignment,
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

std::expected<syntax_cst::CstCallPrototype, CstParseDiagnostic>
PtxCstParser::parseCallPrototype(TokenId label, TokenId colon) {
  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotCallPrototype) {
    return std::unexpected(CstParseDiagnostic{
        token(directive).range, "expected '.callprototype'"});
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
          ".callprototype return parameter list must contain exactly one parameter"});
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
    auto count = expect(TokenKind::Decimal, "ABI-preserve register count");
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

  const auto semicolon = expect(TokenKind::Semicolon, "';' after .callprototype");
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

std::expected<syntax_cst::CstCallTargets, CstParseDiagnostic>
PtxCstParser::parseCallTargets(TokenId label, TokenId colon) {
  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotCallTargets) {
    return std::unexpected(CstParseDiagnostic{
        token(directive).range, "expected '.calltargets'"});
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

std::expected<syntax_cst::CstBranchTargets, CstParseDiagnostic>
PtxCstParser::parseBranchTargets(TokenId label, TokenId colon) {
  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotBranchTargets) {
    return std::unexpected(CstParseDiagnostic{
        token(directive).range, "expected '.branchtargets'"});
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
      auto parsed_count = expect(TokenKind::Decimal, "branch target count");
      if (!parsed_count)
        return std::unexpected(parsed_count.error());
      count = *parsed_count;
      auto parsed_right_angle = expect(TokenKind::Gt, "'>' after branch target count");
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

std::expected<syntax_cst::CstLocDirective, CstParseDiagnostic>
PtxCstParser::parseLocDirective() {
  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotLoc) {
    return std::unexpected(CstParseDiagnostic{token(directive).range,
                                              "expected '.loc'"});
  }
  auto file_index = expect(TokenKind::Decimal, "source file index");
  if (!file_index)
    return std::unexpected(file_index.error());
  auto line_number = expect(TokenKind::Decimal, "source line number");
  if (!line_number)
    return std::unexpected(line_number.error());
  auto column_position =
      expect(TokenKind::Decimal, "source column position");
  if (!column_position)
    return std::unexpected(column_position.error());

  std::optional<syntax_cst::CstLocInlineContext> inline_context;
  TokenId last = *column_position;
  if (token(peek()).kind == TokenKind::Comma) {
    const TokenId function_name_comma = consume();
    const TokenId function_name_keyword = consume();
    if (token(function_name_keyword).kind != TokenKind::Ident ||
        token(function_name_keyword).text != "function_name") {
      return std::unexpected(CstParseDiagnostic{
          token(function_name_keyword).range, "expected 'function_name'"});
    }
    const TokenId function_name_label = consume();
    if (!isIdentifierToken(token(function_name_label).kind)) {
      return std::unexpected(CstParseDiagnostic{
          token(function_name_label).range, "expected function name label"});
    }
    std::optional<TokenId> plus;
    std::optional<TokenId> function_name_offset;
    if (token(peek()).kind == TokenKind::Plus) {
      plus = consume();
      const TokenId offset = consume();
      if (!isIntegerLiteral(token(offset).kind)) {
        return std::unexpected(CstParseDiagnostic{
            token(offset).range, "expected integer function name offset"});
      }
      function_name_offset = offset;
    }
    auto inlined_at_comma = expect(TokenKind::Comma, "comma before inlined_at");
    if (!inlined_at_comma)
      return std::unexpected(inlined_at_comma.error());
    const TokenId inlined_at_keyword = consume();
    if (token(inlined_at_keyword).kind != TokenKind::Ident ||
        token(inlined_at_keyword).text != "inlined_at") {
      return std::unexpected(CstParseDiagnostic{
          token(inlined_at_keyword).range, "expected 'inlined_at'"});
    }
    auto inline_file_index = expect(TokenKind::Decimal, "inlined source file index");
    if (!inline_file_index)
      return std::unexpected(inline_file_index.error());
    auto inline_line_number = expect(TokenKind::Decimal, "inlined source line number");
    if (!inline_line_number)
      return std::unexpected(inline_line_number.error());
    auto inline_column_position =
        expect(TokenKind::Decimal, "inlined source column position");
    if (!inline_column_position)
      return std::unexpected(inline_column_position.error());
    last = *inline_column_position;
    inline_context = syntax_cst::CstLocInlineContext{
        .function_name_comma = function_name_comma,
        .function_name_keyword = function_name_keyword,
        .function_name_label = function_name_label,
        .plus = plus,
        .function_name_offset = function_name_offset,
        .inlined_at_comma = *inlined_at_comma,
        .inlined_at_keyword = inlined_at_keyword,
        .file_index = *inline_file_index,
        .line_number = *inline_line_number,
        .column_position = *inline_column_position,
        .token_range = {function_name_comma, last + 1},
    };
  }
  std::optional<TokenId> terminator;
  if (token(peek()).kind == TokenKind::Semicolon) {
    terminator = consume();
    last = *terminator;
  }
  return syntax_cst::CstLocDirective{
      .directive = directive,
      .file_index = *file_index,
      .line_number = *line_number,
      .column_position = *column_position,
      .inline_context = std::move(inline_context),
      .terminator = terminator,
      .token_range = {directive, last + 1},
  };
}

std::expected<syntax_cst::CstPragma, CstParseDiagnostic>
PtxCstParser::parsePragma() {
  const TokenId directive = consume();
  auto first = expect(TokenKind::String, "pragma string");
  if (!first)
    return std::unexpected(first.error());

  std::vector<TokenId> strings{*first};
  std::vector<TokenId> commas;
  while (token(peek()).kind == TokenKind::Comma) {
    commas.push_back(consume());
    auto string = expect(TokenKind::String, "pragma string after comma");
    if (!string)
      return std::unexpected(string.error());
    strings.push_back(*string);
  }
  auto terminator = expect(TokenKind::Semicolon, "pragma terminator");
  if (!terminator)
    return std::unexpected(terminator.error());
  return syntax_cst::CstPragma{
      .directive = directive,
      .strings = std::move(strings),
      .commas = std::move(commas),
      .terminator = *terminator,
      .token_range = {directive, *terminator + 1},
  };
}

std::expected<syntax_cst::CstKernelResourceDirective, CstParseDiagnostic>
PtxCstParser::parseKernelResourceDirective() {
  const TokenId directive = consume();
  const TokenKind kind = token(directive).kind;
  if (!isKernelResourceDirective(kind)) {
    return std::unexpected(CstParseDiagnostic{
        token(directive).range, "expected kernel resource directive"});
  }

  auto first = expect(TokenKind::Decimal, "kernel resource value");
  if (!first)
    return std::unexpected(first.error());
  std::vector<TokenId> values{*first};
  std::vector<TokenId> commas;

  if (kind == TokenKind::DotMaxnreg || kind == TokenKind::DotMinnctapersm) {
    if (token(peek()).kind == TokenKind::Comma) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range, "this kernel resource directive accepts one value"});
    }
  } else {
    while (token(peek()).kind == TokenKind::Comma) {
      commas.push_back(consume());
      if (values.size() == 3) {
        return std::unexpected(CstParseDiagnostic{
            token(peek()).range,
            "thread-count kernel resource directives accept at most three values"});
      }
      auto value = expect(TokenKind::Decimal, "kernel resource value after comma");
      if (!value)
        return std::unexpected(value.error());
      values.push_back(*value);
    }
  }
  const TokenId last = values.back();
  return syntax_cst::CstKernelResourceDirective{
      .directive = directive,
      .values = std::move(values),
      .commas = std::move(commas),
      .token_range = {directive, last + 1},
  };
}

std::expected<syntax_cst::CstFunctionBodyItem, CstParseDiagnostic>
PtxCstParser::parseFunctionBodyItem(CstParseDiagnostics& diagnostics) {
  if (token(peek()).kind == TokenKind::LBrace) {
    auto block = parseBlock(diagnostics);
    if (!block)
      return std::unexpected(block.error());
    return std::make_unique<syntax_cst::CstBlock>(std::move(*block));
  }
  if (isVariableStateSpace(token(peek()).kind)) {
    auto declaration = parseVariableDeclaration();
    if (!declaration)
      return std::unexpected(declaration.error());
    return std::move(*declaration);
  }

  if (token(peek()).kind == TokenKind::DotLoc) {
    auto location = parseLocDirective();
    if (!location)
      return std::unexpected(location.error());
    return std::move(*location);
  }

  if (token(peek()).kind == TokenKind::DotPragma) {
    auto pragma = parsePragma();
    if (!pragma)
      return std::unexpected(pragma.error());
    return std::move(*pragma);
  }

  if (isKernelResourceDirective(token(peek()).kind)) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "kernel resource directives are only valid in an entry function header"});
  }

  if (token(peek()).kind == TokenKind::Ident) {
    const TokenId first_token = consume();
    if (token(peek()).kind == TokenKind::Colon) {
      const TokenId colon = consume();
      if (token(peek()).kind == TokenKind::DotCallPrototype) {
        auto prototype = parseCallPrototype(first_token, colon);
        if (!prototype)
          return std::unexpected(prototype.error());
        return std::move(*prototype);
      }
      if (token(peek()).kind == TokenKind::DotCallTargets) {
        auto targets = parseCallTargets(first_token, colon);
        if (!targets)
          return std::unexpected(targets.error());
        return std::move(*targets);
      }
      if (token(peek()).kind == TokenKind::DotBranchTargets) {
        auto targets = parseBranchTargets(first_token, colon);
        if (!targets)
          return std::unexpected(targets.error());
        return std::move(*targets);
      }
      return syntax_cst::CstLabel{first_token, colon,
                                  {first_token, colon + 1}};
    }
    auto instruction = parseInstructionNode(first_token);
    if (!instruction)
      return std::unexpected(instruction.error());
    return std::move(*instruction);
  }

  if (token(peek()).kind == TokenKind::DotCallPrototype) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "'.callprototype' requires a preceding function-local label"});
  }
  if (token(peek()).kind == TokenKind::DotCallTargets) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "'.calltargets' requires a preceding function-local label"});
  }
  if (token(peek()).kind == TokenKind::DotBranchTargets) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "'.branchtargets' requires a preceding function-local label"});
  }

  auto instruction = parseInstructionNode();
  if (!instruction)
    return std::unexpected(instruction.error());
  return std::move(*instruction);
}

std::expected<syntax_cst::CstBlock, CstParseDiagnostic>
PtxCstParser::parseBlock(CstParseDiagnostics& diagnostics) {
  const TokenId left_brace = consume();
  std::vector<syntax_cst::CstFunctionBodyItem> body;
  const auto finish_missing_right_brace = [&]() {
    diagnostics.push_back(CstParseDiagnostic{
        token(peek()).range, "expected '}' at end of nested block",
        TokenKind::RBrace});
    body.emplace_back(syntax_cst::CstRecoveryNode{
        .kind = syntax_cst::CstRecoveryKind::Inserted,
        .expected_kind = TokenKind::RBrace,
        .token_range = std::nullopt,
        .range = SourceRange{token(peek()).range.start,
                             token(peek()).range.start},
    });
    return syntax_cst::CstBlock{
        .left_brace = left_brace,
        .body = std::move(body),
        .right_brace = std::nullopt,
        .token_range = {left_brace, peek()},
    };
  };
  while (token(peek()).kind != TokenKind::RBrace) {
    if (token(peek()).kind == TokenKind::Eof ||
        isFunctionOrModuleBoundary(token(peek()).kind))
      return finish_missing_right_brace();
    const TokenId item_first = peek();
    auto item = parseFunctionBodyItem(diagnostics);
    if (!item) {
      diagnostics.push_back(item.error());
      auto recovery =
          recover(item_first, item.error(), RecoveryContext::FunctionBody);
      for (auto& node : recovery.nodes)
        body.emplace_back(std::move(node));
      if (recovery.stop == RecoveryStop::Semicolon)
        continue;
      if (recovery.stop == RecoveryStop::RightBrace)
        continue;
      return finish_missing_right_brace();
    }
    body.push_back(std::move(*item));
  }
  const TokenId right_brace = consume();
  return syntax_cst::CstBlock{
      .left_brace = left_brace,
      .body = std::move(body),
      .right_brace = right_brace,
      .token_range = {left_brace, right_brace + 1},
  };
}

CstParseResult PtxCstParser::parseInstruction() {
  auto root = parseInstructionNode();
  if (!root)
    return parseFailure(std::move(root.error()));

  const TokenId eof = peek();
  if (token(eof).kind != TokenKind::Eof) {
    return parseFailure(
        CstParseDiagnostic{token(eof).range, "expected end of input"});
  }
  return {.value = syntax_cst::CstFile{std::move(tokens_), std::move(*root)},
          .diagnostics = {}};
}

std::expected<syntax_cst::CstModuleDirective, CstParseDiagnostic>
PtxCstParser::parseModuleDirective() {
  const TokenId keyword = consume();
  std::vector<TokenId> arguments;
  std::vector<TokenId> separators;

  switch (token(keyword).kind) {
    case TokenKind::DotVersion: {
      auto version = expect(TokenKind::F64, "PTX version");
      if (!version)
        return std::unexpected(version.error());
      arguments.push_back(*version);
      break;
    }
    case TokenKind::DotTarget: {
      auto target = expect(TokenKind::Ident, "target architecture");
      if (!target)
        return std::unexpected(target.error());
      arguments.push_back(*target);
      while (token(peek()).kind == TokenKind::Comma) {
        separators.push_back(consume());
        auto option = expect(TokenKind::Ident, "target option");
        if (!option)
          return std::unexpected(option.error());
        arguments.push_back(*option);
      }
      break;
    }
    case TokenKind::DotAddressSize: {
      auto size = expect(TokenKind::Decimal, "address size");
      if (!size)
        return std::unexpected(size.error());
      arguments.push_back(*size);
      break;
    }
    case TokenKind::DotFile: {
      auto index = expect(TokenKind::Decimal, "file index");
      if (!index)
        return std::unexpected(index.error());
      auto filename = expect(TokenKind::String, "file name");
      if (!filename)
        return std::unexpected(filename.error());
      arguments.push_back(*index);
      arguments.push_back(*filename);
      if (token(peek()).kind != TokenKind::Comma)
        break;
      separators.push_back(consume());
      auto timestamp = expect(TokenKind::Decimal, "file timestamp");
      if (!timestamp)
        return std::unexpected(timestamp.error());
      auto comma = expect(TokenKind::Comma, "comma before file size");
      if (!comma)
        return std::unexpected(comma.error());
      auto size = expect(TokenKind::Decimal, "file size");
      if (!size)
        return std::unexpected(size.error());
      arguments.push_back(*timestamp);
      separators.push_back(*comma);
      arguments.push_back(*size);
      break;
    }
    default:
      return std::unexpected(CstParseDiagnostic{
          token(keyword).range, "expected supported module directive"});
  }

  std::optional<TokenId> terminator;
  if (token(peek()).kind == TokenKind::Semicolon)
    terminator = consume();
  const TokenId last = terminator.value_or(arguments.back());
  return syntax_cst::CstModuleDirective{keyword,
                                        std::move(arguments),
                                        std::move(separators),
                                        terminator,
                                        {keyword, last + 1}};
}

std::expected<syntax_cst::CstSectionDirective, CstParseDiagnostic>
PtxCstParser::parseSectionDirective() {
  const TokenId directive = consume();
  const TokenId name = consume();
  if (!isIdentifierToken(token(name).kind)) {
    return std::unexpected(CstParseDiagnostic{token(name).range,
                                              "expected section name"});
  }
  auto left_brace = expect(TokenKind::LBrace, "section opening brace");
  if (!left_brace)
    return std::unexpected(left_brace.error());

  std::vector<TokenId> payload;
  size_t brace_depth = 1;
  TokenId right_brace{};
  while (brace_depth != 0) {
    const TokenId next = consume();
    if (token(next).kind == TokenKind::Eof) {
      return std::unexpected(CstParseDiagnostic{token(next).range,
                                                "expected section closing brace"});
    }
    if (token(next).kind == TokenKind::LBrace) {
      ++brace_depth;
      payload.push_back(next);
      continue;
    }
    if (token(next).kind == TokenKind::RBrace) {
      --brace_depth;
      if (brace_depth == 0) {
        right_brace = next;
        break;
      }
    }
    payload.push_back(next);
  }

  std::optional<TokenId> terminator;
  if (token(peek()).kind == TokenKind::Semicolon)
    terminator = consume();
  const TokenId last = terminator.value_or(right_brace);
  return syntax_cst::CstSectionDirective{
      .directive = directive,
      .name = name,
      .left_brace = *left_brace,
      .payload = std::move(payload),
      .right_brace = right_brace,
      .terminator = terminator,
      .token_range = {directive, last + 1},
  };
}

std::expected<syntax_cst::CstFunction, CstParseDiagnostic>
PtxCstParser::parseFunction(std::vector<TokenId> qualifiers,
                            TokenId first_token,
                            CstParseDiagnostics& diagnostics) {
  const TokenId first = first_token;
  std::vector<TokenId> header_tokens = qualifiers;

  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotEntry &&
      token(directive).kind != TokenKind::DotFunc) {
    return std::unexpected(CstParseDiagnostic{token(directive).range,
                                              "expected '.entry' or '.func'"});
  }
  header_tokens.push_back(directive);

  const auto append_range = [&header_tokens](syntax_cst::CstTokenRange range) {
    for (TokenId id = range.first; id < range.last; ++id)
      header_tokens.push_back(id);
  };

  std::optional<syntax_cst::CstFunctionParameterList> return_parameters;
  if (token(directive).kind == TokenKind::DotFunc &&
      token(peek()).kind == TokenKind::LParen) {
    auto parameters = parseFunctionParameterList();
    if (!parameters)
      return std::unexpected(parameters.error());
    append_range(parameters->token_range);
    return_parameters = std::move(*parameters);
  }

  auto name = expect(TokenKind::Ident, "function name");
  if (!name)
    return std::unexpected(name.error());
  header_tokens.push_back(*name);

  std::optional<syntax_cst::CstFunctionParameterList> parameters;
  if (token(peek()).kind == TokenKind::LParen) {
    auto parsed = parseFunctionParameterList();
    if (!parsed)
      return std::unexpected(parsed.error());
    append_range(parsed->token_range);
    parameters = std::move(*parsed);
  }

  std::optional<TokenId> noreturn_directive;
  if (token(peek()).kind == TokenKind::DotNoreturn) {
    noreturn_directive = consume();
    header_tokens.push_back(*noreturn_directive);
  }

  std::vector<syntax_cst::CstPragma> pragmas;
  std::vector<syntax_cst::CstKernelResourceDirective> resources;
  while (token(peek()).kind == TokenKind::DotPragma ||
         isKernelResourceDirective(token(peek()).kind)) {
    if (token(directive).kind != TokenKind::DotEntry) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          token(peek()).kind == TokenKind::DotPragma
              ? "'.pragma' is only valid in an entry function header"
              : "kernel resource directives are only valid in an entry "
                "function header"});
    }
    if (token(peek()).kind == TokenKind::DotPragma) {
      auto pragma = parsePragma();
      if (!pragma)
        return std::unexpected(pragma.error());
      append_range(pragma->token_range);
      pragmas.push_back(std::move(*pragma));
    } else {
      auto resource = parseKernelResourceDirective();
      if (!resource)
        return std::unexpected(resource.error());
      append_range(resource->token_range);
      resources.push_back(std::move(*resource));
    }
  }

  if (token(peek()).kind == TokenKind::Eof) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "expected function body or prototype terminator"});
  }
  if (token(peek()).kind != TokenKind::LBrace &&
      token(peek()).kind != TokenKind::Semicolon) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "unsupported function header token '" +
                                 std::string{token(peek()).text} + "'"});
  }

  if (token(peek()).kind == TokenKind::Semicolon) {
    const TokenId terminator = consume();
    if (token(directive).kind != TokenKind::DotFunc) {
      return std::unexpected(CstParseDiagnostic{
          token(terminator).range, "an entry function must have a body"});
    }
    return syntax_cst::CstFunction{
        .qualifiers = std::move(qualifiers),
        .directive = directive,
        .return_parameters = std::move(return_parameters),
        .name = *name,
        .parameters = std::move(parameters),
        .noreturn_directive = noreturn_directive,
        .pragmas = std::move(pragmas),
        .resources = std::move(resources),
        .header_tokens = std::move(header_tokens),
        .left_brace = std::nullopt,
        .body = {},
        .right_brace = std::nullopt,
        .terminator = terminator,
        .token_range = {first, terminator + 1},
    };
  }

  const TokenId left_brace = consume();

  std::vector<syntax_cst::CstFunctionBodyItem> body;
  const auto finish_missing_body_brace = [&]() {
    return syntax_cst::CstFunction{
        .qualifiers = std::move(qualifiers),
        .directive = directive,
        .return_parameters = std::move(return_parameters),
        .name = *name,
        .parameters = std::move(parameters),
        .noreturn_directive = noreturn_directive,
        .pragmas = std::move(pragmas),
        .resources = std::move(resources),
        .header_tokens = std::move(header_tokens),
        .left_brace = left_brace,
        .body = std::move(body),
        .right_brace = std::nullopt,
        .terminator = std::nullopt,
        .token_range = {first, peek()},
    };
  };
  while (token(peek()).kind != TokenKind::RBrace) {
    if (token(peek()).kind == TokenKind::Eof ||
        isFunctionOrModuleBoundary(token(peek()).kind)) {
      diagnostics.push_back(CstParseDiagnostic{
          token(peek()).range, "expected '}' at end of function body",
          TokenKind::RBrace});
      body.emplace_back(syntax_cst::CstRecoveryNode{
          .kind = syntax_cst::CstRecoveryKind::Inserted,
          .expected_kind = TokenKind::RBrace,
          .token_range = std::nullopt,
          .range = SourceRange{token(peek()).range.start,
                               token(peek()).range.start},
      });
      return finish_missing_body_brace();
    }
    const TokenId item_first = peek();
    auto item = parseFunctionBodyItem(diagnostics);
    if (!item) {
      diagnostics.push_back(item.error());
      auto recovery =
          recover(item_first, item.error(), RecoveryContext::FunctionBody);
      for (auto& node : recovery.nodes)
        body.emplace_back(std::move(node));
      if (recovery.stop == RecoveryStop::Semicolon ||
          recovery.stop == RecoveryStop::RightBrace) {
        continue;
      }
      diagnostics.push_back(CstParseDiagnostic{
          token(peek()).range, "expected '}' at end of function body",
          TokenKind::RBrace});
      body.emplace_back(syntax_cst::CstRecoveryNode{
          .kind = syntax_cst::CstRecoveryKind::Inserted,
          .expected_kind = TokenKind::RBrace,
          .token_range = std::nullopt,
          .range = SourceRange{token(peek()).range.start,
                               token(peek()).range.start},
      });
      return finish_missing_body_brace();
    }
    body.push_back(std::move(*item));
  }
  const TokenId right_brace = consume();

  return syntax_cst::CstFunction{
      .qualifiers = std::move(qualifiers),
      .directive = directive,
      .return_parameters = std::move(return_parameters),
      .name = *name,
      .parameters = std::move(parameters),
      .noreturn_directive = noreturn_directive,
      .pragmas = std::move(pragmas),
      .resources = std::move(resources),
      .header_tokens = std::move(header_tokens),
      .left_brace = left_brace,
      .body = std::move(body),
      .right_brace = right_brace,
      .terminator = std::nullopt,
      .token_range = {first, right_brace + 1},
  };
}

CstParseResult PtxCstParser::parseModule() {
  std::vector<syntax_cst::CstModuleItem> items;
  CstParseDiagnostics diagnostics;
  const TokenId first = peek();
  TokenId last = first;

  const auto recover_item = [this, &diagnostics, &items, &last](
                                TokenId item_first,
                                CstParseDiagnostic diagnostic) {
    diagnostics.push_back(diagnostic);
    auto recovery = recover(item_first, diagnostic, RecoveryContext::Module);
    for (auto& node : recovery.nodes)
      items.emplace_back(std::move(node));
    if (recovery.last)
      last = *recovery.last;
  };

  while (token(peek()).kind != TokenKind::Eof) {
    const TokenId item_first = peek();
    std::vector<TokenId> qualifiers;
    while (isFunctionQualifier(token(peek()).kind))
      qualifiers.push_back(consume());

    if (qualifiers.empty() && token(peek()).kind == TokenKind::DotPragma) {
      auto pragma = parsePragma();
      if (!pragma) {
        recover_item(item_first, std::move(pragma.error()));
        continue;
      }
      last = pragma->token_range.last - 1;
      items.emplace_back(std::move(*pragma));
      continue;
    }

    if (qualifiers.empty() && token(peek()).kind == TokenKind::DotSection) {
      auto section = parseSectionDirective();
      if (!section) {
        recover_item(item_first, std::move(section.error()));
        continue;
      }
      last = section->token_range.last - 1;
      items.emplace_back(std::move(*section));
      continue;
    }

    if (qualifiers.empty() && isModuleDirective(token(peek()).kind)) {
      auto directive = parseModuleDirective();
      if (!directive) {
        recover_item(item_first, std::move(directive.error()));
        continue;
      }
      last = directive->token_range.last - 1;
      items.emplace_back(std::move(*directive));
      continue;
    }

    if (token(peek()).kind == TokenKind::DotEntry ||
        token(peek()).kind == TokenKind::DotFunc) {
      auto function =
          parseFunction(std::move(qualifiers), item_first, diagnostics);
      if (!function) {
        recover_item(item_first, std::move(function.error()));
        continue;
      }
      last = function->token_range.last - 1;
      items.emplace_back(std::move(*function));
      continue;
    }

    if (isVariableStateSpace(token(peek()).kind)) {
      auto declaration =
          parseVariableDeclaration(std::move(qualifiers), item_first);
      if (!declaration) {
        recover_item(item_first, std::move(declaration.error()));
        continue;
      }
      last = declaration->token_range.last - 1;
      items.emplace_back(std::move(*declaration));
      continue;
    }

    if (token(peek()).kind == TokenKind::DotCallPrototype) {
      recover_item(item_first, CstParseDiagnostic{
          token(peek()).range,
          "'.callprototype' is only valid inside a function body"});
      continue;
    }
    if (token(peek()).kind == TokenKind::DotCallTargets) {
      recover_item(item_first, CstParseDiagnostic{
          token(peek()).range,
          "'.calltargets' is only valid inside a function body"});
      continue;
    }
    if (token(peek()).kind == TokenKind::DotBranchTargets) {
      recover_item(item_first, CstParseDiagnostic{
          token(peek()).range,
          "'.branchtargets' is only valid inside a function body"});
      continue;
    }
    if (isKernelResourceDirective(token(peek()).kind)) {
      recover_item(item_first, CstParseDiagnostic{
          token(peek()).range,
          "kernel resource directives are only valid in an entry function header"});
      continue;
    }

    if (token(peek()).kind == TokenKind::Ident) {
      consume();
      if (token(peek()).kind == TokenKind::Colon) {
        consume();
        std::string_view message;
        switch (token(peek()).kind) {
          case TokenKind::DotCallPrototype:
            message = "'.callprototype' is only valid inside a function body";
            break;
          case TokenKind::DotCallTargets:
            message = "'.calltargets' is only valid inside a function body";
            break;
          case TokenKind::DotBranchTargets:
            message = "'.branchtargets' is only valid inside a function body";
            break;
          default:
            break;
        }
        if (!message.empty()) {
          recover_item(item_first,
                       CstParseDiagnostic{token(peek()).range,
                                          std::string(message)});
          continue;
        }
      }
    }

    recover_item(item_first, CstParseDiagnostic{
        token(peek()).range,
        "expected module directive, variable declaration, or function"});
  }

  if (items.empty()) {
    return parseFailure(
        CstParseDiagnostic{token(first).range, "expected module item"});
  }
  syntax_cst::CstModule module{std::move(items), {first, last + 1}};
  return {.value = syntax_cst::CstFile{std::move(tokens_), std::move(module)},
          .diagnostics = std::move(diagnostics)};
}

}  // namespace ptx_frontend
