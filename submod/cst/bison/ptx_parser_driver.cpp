#include "ptx_parser_driver.hpp"
#include "ptx_parser.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ptx_frontend::detail {

ExpressionGuard::~ExpressionGuard() {
  driver->remaining_depth = previous_budget;
  driver->expected = previous_expected;
  driver->expected_name = std::move(previous_name);
}

PtxParserDriver::PtxParserDriver(std::string_view source, EntryMode mode)
    : entry_mode(mode), lexer_(source) {}

CstParseResult PtxParserDriver::parse() {
  try {
    GeneratedPtxParser parser(*this);
    const auto status = parser.parse();
    if (status != 0 || !root)
      return {.value = std::nullopt, .diagnostics = std::move(diagnostics)};
    return {.value = syntax_cst::CstFile{std::move(tokens_), std::move(*root)},
            .diagnostics = std::move(diagnostics)};
  } catch (GeneratedParseFailure& failure) {
    diagnostics.push_back(std::move(failure.diagnostic));
    return {.value = std::nullopt, .diagnostics = std::move(diagnostics)};
  }
}

PtxParserDriver::TokenId PtxParserDriver::nextToken() {
  if (next_ >= tokens_.size()) {
    if (!tokens_.empty() && tokens_.back().kind == TokenKind::Eof) {
      last_ = static_cast<TokenId>(tokens_.size() - 1);
      return last_;
    }
    tokens_.push_back(lexer_.consume());
  }
  last_ = next_++;
  return last_;
}

void PtxParserDriver::replayToken(TokenId id) {
  if (next_ != id + 1 || last_ != id)
    throw std::logic_error("only the last fetched token can be replayed");
  next_ = id;
}

const PtxToken& PtxParserDriver::token(TokenId id) const {
  return tokens_.at(id);
}

PtxParserDriver::TokenId PtxParserDriver::lookahead() const {
  return last_;
}

void PtxParserDriver::markShifted(TokenId id) {
  if (!shifted_through_ || id > *shifted_through_)
    shifted_through_ = id;
}

SourceRange PtxParserDriver::sourceRange(
    syntax_cst::CstTokenRange range) const {
  if (range.first > range.last || range.last > tokens_.size())
    throw std::out_of_range("invalid generated CST token range");
  if (range.first == range.last) {
    const auto anchor = range.first < tokens_.size()
                            ? token(range.first).range.start
                        : tokens_.empty() ? SourcePos{1, 1}
                                          : tokens_.back().range.end;
    return {anchor, anchor};
  }
  return {token(range.first).range.start, token(range.last - 1).range.end};
}

PtxParserDriver::Guard PtxParserDriver::enter(TokenId anchor,
                                              std::size_t left_depth,
                                              std::optional<TokenKind> closing,
                                              std::string name) {
  markShifted(anchor);
  if (remaining_depth <= 1 || left_depth >= remaining_depth)
    fail(anchor, "constant expression tree depth limit exceeded");
  auto guard =
      expectClosing(closing.value_or(expected.value_or(TokenKind::Eof)),
                    closing ? std::move(name) : expected_name);
  if (!closing)
    expected = guard->previous_expected;
  --remaining_depth;
  return guard;
}

PtxParserDriver::Guard PtxParserDriver::expectClosing(TokenKind closing,
                                                      std::string name) {
  auto guard = std::make_unique<ExpressionGuard>();
  guard->driver = this;
  guard->previous_budget = remaining_depth;
  guard->previous_expected = expected;
  guard->previous_name = expected_name;
  expected = closing;
  expected_name = std::move(name);
  return guard;
}

void PtxParserDriver::checkDepth(TokenId anchor, std::size_t height) const {
  if (height > remaining_depth)
    fail(anchor, "constant expression tree depth limit exceeded");
}

ParsedExpression PtxParserDriver::leaf(TokenId id, bool symbol) {
  markShifted(id);
  checkDepth(id, 1);
  syntax_cst::CstConstantExpressionNode node;
  if (symbol)
    node = syntax_cst::CstConstantSymbol{id};
  else
    node = syntax_cst::CstConstantLiteral{id};
  return {{std::move(node), {id, id + 1}}, 1};
}

ParsedExpression PtxParserDriver::binary(ParsedExpression left, TokenId op,
                                         ParsedExpression right, Guard guard) {
  guard.reset();
  const auto height = std::max(left.depth, right.depth) + 1;
  checkDepth(op, height);
  const syntax_cst::CstTokenRange range{left.expression.token_range.first,
                                        right.expression.token_range.last};
  return {{syntax_cst::CstConstantBinary{
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(left.expression)),
               op,
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(right.expression))},
           range},
          height};
}

ParsedExpression PtxParserDriver::unary(TokenId op, ParsedExpression operand,
                                        Guard guard) {
  guard.reset();
  const auto height = operand.depth + 1;
  checkDepth(op, height);
  const syntax_cst::CstTokenRange range{op,
                                        operand.expression.token_range.last};
  return {{syntax_cst::CstConstantUnary{
               op, std::make_unique<syntax_cst::CstConstantExpression>(
                       std::move(operand.expression))},
           range},
          height};
}

ParsedExpression PtxParserDriver::cast(TokenId open, TokenId type,
                                       TokenId close, ParsedExpression operand,
                                       Guard guard) {
  markShifted(close);
  guard.reset();
  const auto height = operand.depth + 1;
  checkDepth(open, height);
  const syntax_cst::CstTokenRange range{open,
                                        operand.expression.token_range.last};
  return {{syntax_cst::CstConstantCast{
               open, type, close,
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(operand.expression))},
           range},
          height};
}

ParsedExpression PtxParserDriver::parenthesized(TokenId open,
                                                ParsedExpression inner,
                                                TokenId close, Guard guard) {
  markShifted(close);
  guard.reset();
  const auto height = inner.depth + 1;
  checkDepth(open, height);
  return {{syntax_cst::CstConstantParenthesized{
               open,
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(inner.expression)),
               close},
           {open, close + 1}},
          height};
}

ParsedExpression PtxParserDriver::call(ParsedExpression callee, TokenId open,
                                       ParsedExpression argument, TokenId close,
                                       Guard guard) {
  markShifted(close);
  guard.reset();
  const auto height = std::max(callee.depth, argument.depth) + 1;
  checkDepth(open, height);
  const syntax_cst::CstTokenRange range{callee.expression.token_range.first,
                                        close + 1};
  return {{syntax_cst::CstConstantCall{
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(callee.expression)),
               open,
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(argument.expression)),
               close},
           range},
          height};
}

ParsedExpression PtxParserDriver::conditional(
    ParsedExpression condition, TokenId question, ParsedExpression yes,
    TokenId colon, ParsedExpression no, Guard guard) {
  guard.reset();
  const auto height = std::max({condition.depth, yes.depth, no.depth}) + 1;
  checkDepth(question, height);
  const syntax_cst::CstTokenRange range{condition.expression.token_range.first,
                                        no.expression.token_range.last};
  return {{syntax_cst::CstConstantConditional{
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(condition.expression)),
               question,
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(yes.expression)),
               colon,
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(no.expression))},
           range},
          height};
}

syntax_cst::CstInstruction PtxParserDriver::instruction(
    InstructionHead head, std::vector<syntax_cst::CstOperandElement> operands,
    TokenId semicolon) const {
  return {std::move(head.predicate), head.opcode, std::move(head.modifiers),
          std::move(operands),       semicolon,   {head.first, semicolon + 1}};
}

syntax_cst::CstNumericOperand PtxParserDriver::numeric(
    ParsedExpression expression) const {
  using namespace syntax_cst;
  const auto range = expression.expression.token_range;
  for (auto i = range.first; i < range.last; ++i)
    if (token(i).kind == TokenKind::Ident && token(i).text.starts_with('%'))
      fail(i, "registers cannot participate in constant expressions");
  const auto& node = expression.expression.node;
  if (const auto* literal = std::get_if<CstConstantLiteral>(&node))
    return CstImmediate{std::nullopt, literal->literal, range};
  if (const auto* unary = std::get_if<CstConstantUnary>(&node)) {
    const auto kind = token(unary->operator_token).kind;
    if (kind == TokenKind::Plus || kind == TokenKind::Minus)
      if (const auto* literal =
              std::get_if<CstConstantLiteral>(&unary->operand->node))
        return CstImmediate{unary->operator_token, literal->literal, range};
  }
  return CstConstantOperand{
      std::make_unique<CstConstantExpression>(std::move(expression.expression)),
      range};
}

syntax_cst::CstOperand PtxParserDriver::numericOperand(
    ParsedExpression expression) const {
  using namespace syntax_cst;
  auto value = numeric(std::move(expression));
  if (const auto* tree = std::get_if<CstConstantOperand>(&value)) {
    if (const auto* unary =
            std::get_if<CstConstantUnary>(&tree->expression->node);
        unary && token(unary->operator_token).kind == TokenKind::Exclamation) {
      const CstConstantExpression* child = unary->operand.get();
      std::optional<TokenId> sign;
      if (const auto* signed_child =
              std::get_if<CstConstantUnary>(&child->node);
          signed_child &&
          (token(signed_child->operator_token).kind == TokenKind::Plus ||
           token(signed_child->operator_token).kind == TokenKind::Minus)) {
        sign = signed_child->operator_token;
        child = signed_child->operand.get();
      }
      if (const auto* literal = std::get_if<CstConstantLiteral>(&child->node)) {
        const auto kind = token(literal->literal).kind;
        if (kind != TokenKind::Decimal && kind != TokenKind::Hex &&
            kind != TokenKind::WarpSz)
          fail(literal->literal,
               "expected integer predicate constant after '!'");
        return CstNegatedImmediate{
            unary->operator_token,
            CstImmediate{
                sign,
                literal->literal,
                {sign.value_or(literal->literal), literal->literal + 1}},
            tree->token_range};
      }
    }
  }
  return std::visit([](auto&& leaf) -> CstOperand { return std::move(leaf); },
                    std::move(value));
}

syntax_cst::CstAddressBase PtxParserDriver::numericBase(
    ParsedExpression expression) const {
  return std::visit(
      [](auto&& leaf) -> syntax_cst::CstAddressBase { return std::move(leaf); },
      numeric(std::move(expression)));
}

void PtxParserDriver::fail(TokenId id, std::string message,
                           std::optional<TokenKind> expected_kind) const {
  CstParseDiagnostic diagnostic{token(id).range, std::move(message),
                                expected_kind};
  if (entry_mode == EntryMode::Instruction)
    throw GeneratedParseFailure{std::move(diagnostic)};
  pending_diagnostic_ = std::move(diagnostic);
  throw GeneratedPtxParser::syntax_error(pending_diagnostic_->message);
}

void PtxParserDriver::diagnose(CstParseDiagnostic diagnostic) {
  if (entry_mode == EntryMode::Instruction)
    throw GeneratedParseFailure{std::move(diagnostic)};
  recovery_error = shifted_through_ && lookahead() <= *shifted_through_
                       ? *shifted_through_ + 1
                       : lookahead();
  diagnostics.push_back(std::move(diagnostic));
}

void PtxParserDriver::generatedError(std::string message) {
  if (pending_diagnostic_) {
    auto diagnostic = std::move(*pending_diagnostic_);
    pending_diagnostic_.reset();
    diagnose(std::move(diagnostic));
  } else {
    diagnose({token(lookahead()).range, std::move(message), std::nullopt});
  }
}

void PtxParserDriver::beginStatement(TokenId first) {
  statement_first = first;
  remaining_depth = PtxCstParser::maxConstantTreeDepth;
  operand_expected = true;
  unclassified_paren.reset();
  // The generated parser may have already fetched the first operand while
  // deciding that the modifier sequence is complete; retain that prefix state.
  const auto kind = token(lookahead()).kind;
  if (kind == TokenKind::LParen)
    unclassified_paren = lookahead();
  else if (kind == TokenKind::Ident || kind == TokenKind::Decimal ||
           kind == TokenKind::Hex || kind == TokenKind::F32Hex ||
           kind == TokenKind::F64Hex || kind == TokenKind::F64 ||
           kind == TokenKind::WarpSz)
    operand_expected = false;
  recovery_error.reset();
  expected.reset();
  expected_name.clear();
}

std::vector<syntax_cst::CstRecoveryNode> PtxParserDriver::recoverStatement(
    TokenId semicolon) {
  using namespace syntax_cst;
  std::vector<CstRecoveryNode> nodes;
  const auto error = recovery_error.value_or(statement_first);
  if (statement_first < error)
    nodes.push_back({CstRecoveryKind::Skipped, std::nullopt,
                     CstTokenRange{statement_first, error},
                     sourceRange({statement_first, error})});
  nodes.push_back({CstRecoveryKind::Error, std::nullopt,
                   CstTokenRange{error, error + 1}, token(error).range});
  if (error < semicolon)
    nodes.push_back({CstRecoveryKind::Skipped, std::nullopt,
                     CstTokenRange{error + 1, semicolon + 1},
                     sourceRange({error + 1, semicolon + 1})});
  recovery_error.reset();
  operand_expected = true;
  unclassified_paren.reset();
  remaining_depth = PtxCstParser::maxConstantTreeDepth;
  expected.reset();
  expected_name.clear();
  return nodes;
}

CstParseResult parseGenerated(std::string_view source, EntryMode mode) {
  return PtxParserDriver(source, mode).parse();
}

GeneratedPtxParser::symbol_type yylex(PtxParserDriver& driver) {
  using Parser = GeneratedPtxParser;
  using Kind = TokenKind;
  if (driver.entry_pending) {
    driver.entry_pending = false;
    return driver.entry_mode == EntryMode::Instruction
               ? Parser::make_MODE_INSTRUCTION()
               : Parser::make_MODE_MODULE();
  }
  const auto id = driver.nextToken();
  const auto& token = driver.token(id);
  const auto kind = token.kind;
  if (driver.unclassified_paren) {
    const auto anchor = *driver.unclassified_paren;
    driver.unclassified_paren.reset();
    const bool cast = kind == Kind::DotIdent &&
                      (token.text == ".s64" || token.text == ".u64");
    if (!cast && driver.remaining_depth <= 1) {
      driver.replayToken(id);
      driver.fail(anchor, "constant expression tree depth limit exceeded");
    }
  }
  if (kind == Kind::Eof)
    return Parser::make_END();
  switch (kind) {
    case Kind::Decimal:
    case Kind::Hex:
    case Kind::F32Hex:
    case Kind::F64Hex:
    case Kind::F64:
    case Kind::WarpSz:
      driver.operand_expected = false;
      return Parser::make_LITERAL(id);
    case Kind::Ident:
      driver.operand_expected = false;
      if (token.text == "call")
        return Parser::make_CALL(id);
      if (token.text == "bra")
        return Parser::make_BRA(id);
      if (token.text == "brx")
        return Parser::make_BRX(id);
      return Parser::make_IDENT(id);
    case Kind::DotIdent:
      if (token.text == ".s64" || token.text == ".u64")
        return Parser::make_CAST(id);
      if (token.text == ".unified")
        return Parser::make_UNIFIED(id);
      return Parser::make_DOT_IDENT(id);
    case Kind::LParen:
      if (driver.operand_expected)
        driver.unclassified_paren = id;
      driver.operand_expected = true;
      return Parser::make_LP(id);
    case Kind::RParen:
      driver.operand_expected = id > 0 &&
                                driver.token(id - 1).kind == Kind::DotIdent &&
                                (driver.token(id - 1).text == ".s64" ||
                                 driver.token(id - 1).text == ".u64");
      return Parser::make_RP(id);
#define PTX_SOURCE_TOKEN(source, terminal) \
  case Kind::source:                       \
    driver.operand_expected = true;        \
    return Parser::make_##terminal(id)
      PTX_SOURCE_TOKEN(Question, QUESTION);
      PTX_SOURCE_TOKEN(Colon, COLON);
      PTX_SOURCE_TOKEN(Semicolon, SEMI);
      PTX_SOURCE_TOKEN(Comma, COMMA);
      PTX_SOURCE_TOKEN(At, AT);
      PTX_SOURCE_TOKEN(LBracket, LBRACKET);
      PTX_SOURCE_TOKEN(RBracket, RBRACKET);
      PTX_SOURCE_TOKEN(LBrace, LBRACE);
      PTX_SOURCE_TOKEN(RBrace, RBRACE);
      PTX_SOURCE_TOKEN(Plus, PLUS);
      PTX_SOURCE_TOKEN(Minus, MINUS);
      PTX_SOURCE_TOKEN(Exclamation, NOT);
      PTX_SOURCE_TOKEN(Tilde, TILDE);
      PTX_SOURCE_TOKEN(Star, STAR);
      PTX_SOURCE_TOKEN(Slash, SLASH);
      PTX_SOURCE_TOKEN(Percent, PERCENT);
      PTX_SOURCE_TOKEN(ShiftLeft, SHL);
      PTX_SOURCE_TOKEN(ShiftRight, SHR);
      PTX_SOURCE_TOKEN(Lt, LT);
      PTX_SOURCE_TOKEN(LtEq, LE);
      PTX_SOURCE_TOKEN(Gt, GT);
      PTX_SOURCE_TOKEN(GtEq, GE);
      PTX_SOURCE_TOKEN(EqEq, EQ);
      PTX_SOURCE_TOKEN(NotEq, NE);
      PTX_SOURCE_TOKEN(Amp, AMP);
      PTX_SOURCE_TOKEN(Caret, CARET);
      PTX_SOURCE_TOKEN(Pipe, PIPE);
      PTX_SOURCE_TOKEN(AmpAmp, AND);
      PTX_SOURCE_TOKEN(PipePipe, OR);
      PTX_SOURCE_TOKEN(DotGlobal, DOT_GLOBAL);
      PTX_SOURCE_TOKEN(DotConst, DOT_CONST);
      PTX_SOURCE_TOKEN(DotShared, DOT_SHARED);
      PTX_SOURCE_TOKEN(DotLocal, DOT_LOCAL);
      PTX_SOURCE_TOKEN(DotParam, DOT_PARAM);
      PTX_SOURCE_TOKEN(DotWeak, DOT_WEAK);
      PTX_SOURCE_TOKEN(DotEntry, ENTRY);
      PTX_SOURCE_TOKEN(DotVersion, DOT_VERSION);
      PTX_SOURCE_TOKEN(DotTarget, DOT_TARGET);
      PTX_SOURCE_TOKEN(DotAddressSize, DOT_ADDRESS_SIZE);
      PTX_SOURCE_TOKEN(DotLoc, DOT_LOC);
      PTX_SOURCE_TOKEN(DotPragma, DOT_PRAGMA);
      PTX_SOURCE_TOKEN(DotFile, DOT_FILE);
      PTX_SOURCE_TOKEN(DotSection, DOT_SECTION);
      PTX_SOURCE_TOKEN(DotExtern, DOT_EXTERN);
      PTX_SOURCE_TOKEN(DotVisible, DOT_VISIBLE);
      PTX_SOURCE_TOKEN(DotFunc, DOT_FUNC);
      PTX_SOURCE_TOKEN(DotCallPrototype, DOT_CALL_PROTOTYPE);
      PTX_SOURCE_TOKEN(DotCallTargets, DOT_CALL_TARGETS);
      PTX_SOURCE_TOKEN(DotBranchTargets, DOT_BRANCH_TARGETS);
      PTX_SOURCE_TOKEN(DotMaxnreg, DOT_MAXNREG);
      PTX_SOURCE_TOKEN(DotMaxntid, DOT_MAXNTID);
      PTX_SOURCE_TOKEN(DotReqntid, DOT_REQNTID);
      PTX_SOURCE_TOKEN(DotMinnctapersm, DOT_MINNCTAPERSM);
      PTX_SOURCE_TOKEN(DotReqnctapercluster, DOT_REQNCTAPERCLUSTER);
      PTX_SOURCE_TOKEN(DotExplicitcluster, DOT_EXPLICITCLUSTER);
      PTX_SOURCE_TOKEN(DotMaxclusterrank, DOT_MAXCLUSTERRANK);
      PTX_SOURCE_TOKEN(DotNoreturn, DOT_NORETURN);
      PTX_SOURCE_TOKEN(DotReg, DOT_REG);
      PTX_SOURCE_TOKEN(DotAlign, DOT_ALIGN);
      PTX_SOURCE_TOKEN(DotPtr, DOT_PTR);
      PTX_SOURCE_TOKEN(DotTex, DOT_TEX);
      PTX_SOURCE_TOKEN(String, STRING);
      PTX_SOURCE_TOKEN(Dot, DOT);
      PTX_SOURCE_TOKEN(Eq, EQ_ASSIGN);
#undef PTX_SOURCE_TOKEN
    default:
      return Parser::make_INVALID(id);
  }
}

}  // namespace ptx_frontend::detail
