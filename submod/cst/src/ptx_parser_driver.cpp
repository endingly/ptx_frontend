#include "ptx_parser_driver.hpp"
#include "ptx_parser.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ptx_frontend::detail {

namespace {
/** Classify the original narrower boundary that ends an unterminated body. */
bool bodyAnchor(TokenKind kind) {
  return kind == TokenKind::DotEntry || kind == TokenKind::DotFunc ||
         kind == TokenKind::DotExtern || kind == TokenKind::DotVisible ||
         kind == TokenKind::DotWeak || kind == TokenKind::DotVersion ||
         kind == TokenKind::DotTarget || kind == TokenKind::DotAddressSize ||
         kind == TokenKind::DotFile || kind == TokenKind::DotSection;
}
/** Module synchronization accepts declaration and pragma starts as well. */
bool moduleAnchor(TokenKind kind) {
  return bodyAnchor(kind) || kind == TokenKind::DotPragma ||
         kind == TokenKind::DotReg || kind == TokenKind::DotParam ||
         kind == TokenKind::DotLocal || kind == TokenKind::DotShared ||
         kind == TokenKind::DotGlobal || kind == TokenKind::DotConst ||
         kind == TokenKind::DotTex;
}
/** Preserve a valid following body item when only its predecessor lacks ';'. */
bool bodyItemStart(TokenKind kind) {
  return kind == TokenKind::LBrace || kind == TokenKind::At ||
         kind == TokenKind::Ident || kind == TokenKind::DotLoc ||
         kind == TokenKind::DotCallPrototype ||
         kind == TokenKind::DotCallTargets ||
         kind == TokenKind::DotBranchTargets ||
         (moduleAnchor(kind) && !bodyAnchor(kind));
}
}  // namespace

BodyFrame::~BodyFrame() {
  driver->leaveBody(this);
}

PtxParserDriver::TokenId PtxParserDriver::literal(TokenId id,
                                                  std::string_view name,
                                                  bool integer,
                                                  bool expected_decimal) const {
  const auto kind = token(id).kind;
  if (kind != TokenKind::Decimal && !(integer && kind == TokenKind::Hex))
    fail(id, "expected " + std::string(name),
         expected_decimal ? std::optional{TokenKind::Decimal} : std::nullopt);
  return id;
}

PtxParserDriver::BodyGuard PtxParserDriver::openBody(TokenId brace,
                                                     bool nested) {
  if (nested && nested_depth >= 256)
    fail(brace, "nested block depth exceeds parser limit");
  markShifted(brace);
  auto frame = std::make_unique<BodyFrame>();
  frame->driver = this;
  frame->brace = brace;
  frame->nested = nested;
  if (nested)
    ++nested_depth;
  bodies.push_back(frame.get());
  return frame;
}

void PtxParserDriver::leaveBody(BodyFrame* frame) {
  if (bodies.empty() || bodies.back() != frame)
    throw std::logic_error(
        "generated body frames must unwind in source nesting order");
  bodies.pop_back();
  if (frame->nested)
    --nested_depth;
  body_eligible = false;
}

void PtxParserDriver::itemBoundary() {
  next_item = true;
  if (shifted_through_ && last_ > *shifted_through_) {
    statement_first = last_;
    next_item = false;
  }
  recovery_error.reset();
  remaining_depth = PtxCstParser::maxConstantTreeDepth;
  expected.reset();
  expected_name.clear();
  operand_expected = true;
  unclassified_paren.reset();
  initializer_anchor.reset();
  initializer_child_pending = false;
}

void PtxParserDriver::bodyBoundary() {
  itemBoundary();
  body_eligible = true;
}

void PtxParserDriver::rejectItem(bool body) const {
  const auto id = lookahead();
  const auto kind = token(id).kind;
  if (kind == TokenKind::DotCallPrototype ||
      kind == TokenKind::DotCallTargets || kind == TokenKind::DotBranchTargets)
    fail(id, "'" + std::string(token(id).text) +
                 (body ? "' requires a preceding function-local label"
                       : "' is only valid inside a function body"));
  if (kind == TokenKind::DotMaxnreg || kind == TokenKind::DotMaxntid ||
      kind == TokenKind::DotReqntid || kind == TokenKind::DotMinnctapersm ||
      kind == TokenKind::DotReqnctapercluster ||
      kind == TokenKind::DotExplicitcluster ||
      kind == TokenKind::DotMaxclusterrank)
    fail(id,
         "kernel resource directives are only valid in an entry function "
         "header");
  if (body)
    fail(id, "expected instruction opcode", TokenKind::Ident);
  fail(id, "expected module directive, variable declaration, or function");
}

bool PtxParserDriver::missingClose(TokenId anchor) {
  if (!body_eligible || bodies.empty() || bodies.back()->close_issued)
    return false;
  body_eligible = false;
  if (token(anchor).kind != TokenKind::Eof && !bodyAnchor(token(anchor).kind))
    return false;
  bodies.back()->close_issued = true;
  replayToken(anchor);
  return true;
}

syntax_cst::CstRecoveryNode PtxParserDriver::inserted(TokenKind kind,
                                                      TokenId anchor) const {
  const auto position = token(anchor).range.start;
  return {syntax_cst::CstRecoveryKind::Inserted,
          kind,
          std::nullopt,
          {position, position}};
}

std::vector<PtxParserDriver::TokenId> PtxParserDriver::tokenSpan(
    TokenId first, TokenId last) const {
  std::vector<TokenId> result;
  for (auto id = first; id < last; ++id)
    result.push_back(id);
  return result;
}

void PtxParserDriver::initializerAllowed(TokenId equals) const {
  if (parameterized_declarator)
    fail(equals, "parameterized variable names cannot have an initializer");
  if (declaration_space != TokenKind::DotGlobal &&
      declaration_space != TokenKind::DotConst)
    fail(equals,
         "variable initializer requires '.global' or '.const' state space");
  if (declaration_external)
    fail(equals, "external variable declaration cannot have an initializer");
}

std::vector<syntax_cst::CstRecoveryNode> PtxParserDriver::synchronize(
    bool body) {
  using namespace syntax_cst;
  std::vector<CstRecoveryNode> nodes;
  auto current = recovery_error.value_or(lookahead());
  while (tokens_.size() <= current)
    nextToken();
  next_ = current;
  nextToken();
  const auto span = [&](CstRecoveryKind kind, TokenId first, TokenId last) {
    if (first < last)
      nodes.push_back({kind, std::nullopt, CstTokenRange{first, last},
                       sourceRange({first, last})});
  };
  const auto anchor = [&](TokenKind kind) {
    return body ? kind == TokenKind::RBrace || bodyAnchor(kind)
                : moduleAnchor(kind);
  };
  const bool missing_semi =
      !diagnostics.empty() &&
      diagnostics.back().expected_kind == TokenKind::Semicolon;
  span(CstRecoveryKind::Skipped, statement_first, current);
  if (token(current).kind == TokenKind::Eof) {
    nodes.push_back({CstRecoveryKind::Error, std::nullopt, std::nullopt,
                     token(current).range});
    if (missing_semi)
      nodes.push_back(inserted(TokenKind::Semicolon, current));
    replayToken(current);
  } else if (((body || current != statement_first) &&
              anchor(token(current).kind)) ||
             (body && missing_semi && bodyItemStart(token(current).kind))) {
    if (missing_semi)
      nodes.push_back(inserted(TokenKind::Semicolon, current));
    replayToken(current);
  } else {
    const auto error = current;
    span(CstRecoveryKind::Error, error, error + 1);
    markShifted(error);
    if (token(error).kind != TokenKind::Semicolon) {
      current = nextToken();
      const auto first = current;
      while (token(current).kind != TokenKind::Eof &&
             !anchor(token(current).kind)) {
        if (token(current).kind == TokenKind::Semicolon)
          break;
        current = nextToken();
      }
      const bool semicolon = token(current).kind == TokenKind::Semicolon;
      span(CstRecoveryKind::Skipped, first, current + (semicolon ? 1 : 0));
      if (semicolon)
        markShifted(current);
      else if (current > first)
        markShifted(current - 1);
      if (!semicolon)
        replayToken(current);
    }
  }
  body_eligible = false;
  return nodes;
}

ExpressionGuard::~ExpressionGuard() {
  driver->remaining_depth = previous_budget;
  driver->expected = previous_expected;
  driver->expected_name = std::move(previous_name);
  driver->initializer_anchor = previous_initializer_anchor;
}

PtxParserDriver::PtxParserDriver(std::string_view source) : lexer_(source) {}

void PtxParserDriver::beginEntry(EntryMode mode) {
  entry_mode = mode;
  entry_pending = true;
  entry_first = next_;
  root.reset();
  diagnostics.clear();
  remaining_depth = PtxCstParser::maxConstantTreeDepth;
  expected.reset();
  expected_name.clear();
  statement_first = next_;
  recovery_error.reset();
  operand_expected = true;
  unclassified_paren.reset();
  initializer_anchor.reset();
  initializer_child_pending = false;
  declaration_space = TokenKind::DotGlobal;
  declaration_external = false;
  parameterized_declarator = false;
  body_eligible = false;
  next_item = true;
  bodies.clear();
  nested_depth = 0;
  shifted_through_ = next_ == 0 ? std::nullopt : std::optional{next_ - 1};
  last_ = shifted_through_.value_or(0);
  pending_diagnostic_.reset();
}

void PtxParserDriver::retainFailureCursor() {
  // Syntax failures retain Bison's eager lookahead; action failures can happen
  // after their last fetched token was consumed, before a successor was fetched.
  next_ = shifted_through_ && last_ <= *shifted_through_ ? *shifted_through_ + 1
                                                         : last_;
}

CstParseResult PtxParserDriver::parse(EntryMode mode) {
  if (exhausted_) {
    CstParseDiagnostic diagnostic{
        eof_range_,
        mode == EntryMode::Instruction ? "expected instruction opcode"
                                       : "expected module item",
        mode == EntryMode::Instruction ? std::optional{TokenKind::Ident}
                                       : std::nullopt};
    return {.value = std::nullopt, .diagnostics = {std::move(diagnostic)}};
  }
  beginEntry(mode);
  try {
    int status;
    {
      GeneratedPtxParser parser(*this);
      status = parser.parse();
    }
    if (status != 0 || !root) {
      retainFailureCursor();
      return {.value = std::nullopt, .diagnostics = std::move(diagnostics)};
    }
    eof_range_ = tokens_.back().range;
    exhausted_ = true;
    return {.value = syntax_cst::CstFile{std::move(tokens_), std::move(*root)},
            .diagnostics = std::move(diagnostics)};
  } catch (GeneratedParseFailure& failure) {
    retainFailureCursor();
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
  guard->previous_initializer_anchor = initializer_anchor;
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

std::optional<PtxParserDriver::TokenId> PtxParserDriver::numericRegister(
    syntax_cst::CstTokenRange range) const {
  for (auto i = range.first; i < range.last; ++i)
    if (token(i).kind == TokenKind::Ident && token(i).text.starts_with('%'))
      return i;
  return std::nullopt;
}

syntax_cst::CstNumericOperand PtxParserDriver::numeric(
    ParsedExpression expression) const {
  using namespace syntax_cst;
  const auto range = expression.expression.token_range;
  if (const auto reg = numericRegister(range))
    fail(*reg, "registers cannot participate in constant expressions");
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
  failRange(token(id).range, std::move(message), expected_kind);
}

void PtxParserDriver::failRange(SourceRange range, std::string message,
                                std::optional<TokenKind> expected_kind) const {
  CstParseDiagnostic diagnostic{range, std::move(message), expected_kind};
  if (entry_mode == EntryMode::Instruction)
    throw GeneratedParseFailure{std::move(diagnostic)};
  pending_diagnostic_ = std::move(diagnostic);
  throw GeneratedPtxParser::syntax_error(pending_diagnostic_->message);
}

void PtxParserDriver::diagnose(CstParseDiagnostic diagnostic) {
  if (entry_mode == EntryMode::Instruction)
    throw GeneratedParseFailure{std::move(diagnostic)};
  if (!recovery_error)
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

CstParseResult parseGenerated(std::string_view source, EntryMode mode) {
  return PtxParserDriver(source).parse(mode);
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
  if (driver.next_item) {
    driver.statement_first = id;
    driver.next_item = false;
  }
  if (driver.missingClose(id))
    return Parser::make_MISSING_RBRACE(id);
  if (driver.initializer_child_pending) {
    driver.initializer_child_pending = false;
    if (driver.remaining_depth == 0 && kind != Kind::RBrace) {
      driver.replayToken(id);
      driver.fail(*driver.initializer_anchor,
                  "initializer tree depth limit exceeded");
    }
  }
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
      if (token.text == ".attribute")
        return Parser::make_ATTRIBUTE(id);
      if (token.text == ".alias")
        return Parser::make_ALIAS(id);
      if (token.text == ".language")
        return Parser::make_LANGUAGE(id);
      if (token.text == ".blocksareclusters")
        return Parser::make_BLOCKS_CLUSTERS(id);
      if (token.text == ".abi_preserve")
        return Parser::make_ABI_PRESERVE(id);
      if (token.text == ".abi_preserve_control")
        return Parser::make_ABI_CONTROL(id);
      if (token.text == ".v2" || token.text == ".v4")
        return Parser::make_VECTOR_TYPE(id);
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
