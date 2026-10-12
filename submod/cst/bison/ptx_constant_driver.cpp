#include "ptx_constant_driver.hpp"
#include "ptx_constant_parser.hpp"

#include <algorithm>
#include <utility>

namespace ptx_frontend::detail {

BisonExpressionGuard::~BisonExpressionGuard() {
  driver->remaining_depth = previous_budget;
  driver->expected = previous_expected;
  driver->expected_name = std::move(previous_name);
}

void BisonExpressionDriver::acceptThrough(TokenId id) {
  if (pending && *pending <= id) {
    consume();
    pending.reset();
  }
}

BisonExpressionDriver::Guard BisonExpressionDriver::enter(
    TokenId anchor, std::size_t left_depth, std::optional<TokenKind> closing,
    std::string name) {
  acceptThrough(anchor);
  if (remaining_depth <= 1 || left_depth >= remaining_depth)
    fail(anchor, "constant expression tree depth limit exceeded");
  auto guard = std::make_unique<BisonExpressionGuard>();
  guard->driver = this;
  guard->previous_budget = remaining_depth;
  guard->previous_expected = expected;
  guard->previous_name = expected_name;
  --remaining_depth;
  if (closing) {
    expected = closing;
    expected_name = std::move(name);
  }
  return guard;
}

BisonExpressionDriver::Guard BisonExpressionDriver::expectClosing(
    TokenId shifted, TokenKind closing, std::string name) {
  acceptThrough(shifted);
  auto guard = std::make_unique<BisonExpressionGuard>();
  guard->driver = this;
  guard->previous_budget = remaining_depth;
  guard->previous_expected = expected;
  guard->previous_name = expected_name;
  expected = closing;
  expected_name = std::move(name);
  return guard;
}

void BisonExpressionDriver::checkDepth(TokenId anchor,
                                       std::size_t depth) const {
  if (depth > remaining_depth)
    fail(anchor, "constant expression tree depth limit exceeded");
}

BisonExpression BisonExpressionDriver::leaf(TokenId id, bool symbol) {
  checkDepth(id, 1);
  acceptThrough(id);
  syntax_cst::CstConstantExpressionNode node;
  if (symbol)
    node = syntax_cst::CstConstantSymbol{id};
  else
    node = syntax_cst::CstConstantLiteral{id};
  return {{std::move(node), {id, id + 1}}, 1};
}

BisonExpression BisonExpressionDriver::binary(BisonExpression left, TokenId op,
                                              BisonExpression right,
                                              Guard guard) {
  guard.reset();
  const auto depth = std::max(left.depth, right.depth) + 1;
  checkDepth(op, depth);
  const syntax_cst::CstTokenRange range{left.expression.token_range.first,
                                        right.expression.token_range.last};
  return {{syntax_cst::CstConstantBinary{
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(left.expression)),
               op,
               std::make_unique<syntax_cst::CstConstantExpression>(
                   std::move(right.expression))},
           range},
          depth};
}

void BisonExpressionDriver::fail(TokenId id, std::string message,
                                 std::optional<TokenKind> kind) const {
  throw BisonExpressionFailure{{token(id).range, std::move(message), kind}};
}

std::expected<BisonExpression, CstParseDiagnostic> parseBisonExpression(
    BisonExpressionDriver driver) {
  try {
    if (driver.remaining_depth == 0)
      driver.fail(driver.peek(),
                  "constant expression tree depth limit exceeded");
    BisonConstantParser parser(driver);
    parser.parse();
    return std::move(*driver.result);
  } catch (BisonExpressionFailure& failure) {
    return std::unexpected(std::move(failure.diagnostic));
  }
}

BisonConstantParser::symbol_type yylex(BisonExpressionDriver& driver) {
  using Parser = BisonConstantParser;
  using Kind = TokenKind;
  if (driver.pending)
    driver.acceptThrough(*driver.pending);
  const auto id = driver.peek();
  const auto& token = driver.token(id);
  const auto kind = token.kind;
  if (driver.unclassified_paren) {
    const auto anchor = *driver.unclassified_paren;
    driver.unclassified_paren.reset();
    const bool cast = kind == Kind::DotIdent &&
                      (token.text == ".s64" || token.text == ".u64");
    // This check precedes LAC: malformed child lookahead must not bypass the
    // parenthesis budget merely because its empty guard cannot reduce.
    if (!cast && driver.remaining_depth <= 1)
      driver.fail(anchor, "constant expression tree depth limit exceeded");
  }
  const bool outer = driver.questions.size() == 1;
  // A boundary is borrowed lookahead: the enclosing handwritten grammar owns it.
  if (outer && driver.minimum_precedence == 10 &&
      (kind == Kind::Plus || kind == Kind::Minus || kind == Kind::Question ||
       kind == Kind::ShiftLeft || kind == Kind::ShiftRight ||
       kind == Kind::Lt || kind == Kind::LtEq || kind == Kind::Gt ||
       kind == Kind::GtEq || kind == Kind::EqEq || kind == Kind::NotEq ||
       kind == Kind::Amp || kind == Kind::Caret || kind == Kind::Pipe ||
       kind == Kind::AmpAmp || kind == Kind::PipePipe) &&
      !driver.operand_expected)
    return Parser::make_YYEOF();
  if ((kind == Kind::RParen && outer) ||
      (kind == Kind::Colon && driver.questions.back() == 0))
    return Parser::make_YYEOF();

  driver.pending = id;
  switch (kind) {
    case Kind::Decimal:
    case Kind::Hex:
    case Kind::F32Hex:
    case Kind::F64Hex:
    case Kind::F64:
    case Kind::WarpSz:
      if (!driver.operand_expected)
        break;
      driver.operand_expected = false;
      return Parser::make_LITERAL(id);
    case Kind::Ident:
      if (!driver.operand_expected)
        break;
      driver.operand_expected = false;
      return Parser::make_SYMBOL(id);
    case Kind::DotIdent:
      if (driver.operand_expected && driver.questions.size() > 1 &&
          (token.text == ".s64" || token.text == ".u64"))
        return Parser::make_CAST(id);
      break;
    case Kind::LParen:
      if (driver.operand_expected)
        driver.unclassified_paren = id;
      driver.questions.push_back(0);
      driver.operand_expected = true;
      return Parser::make_LP(id);
    case Kind::RParen:
      driver.questions.pop_back();
      driver.operand_expected = id > 0 &&
                                driver.token(id - 1).kind == Kind::DotIdent &&
                                (driver.token(id - 1).text == ".s64" ||
                                 driver.token(id - 1).text == ".u64");
      return Parser::make_RP(id);
    case Kind::Question:
      ++driver.questions.back();
      driver.operand_expected = true;
      return Parser::make_QUESTION(id);
    case Kind::Colon:
      --driver.questions.back();
      driver.operand_expected = true;
      return Parser::make_COLON(id);
#define PTX_BINARY_TOKEN(kind_name, terminal) \
  case Kind::kind_name:                       \
    driver.operand_expected = true;           \
    return Parser::make_##terminal(id)
      PTX_BINARY_TOKEN(Plus, PLUS);
      PTX_BINARY_TOKEN(Minus, MINUS);
      PTX_BINARY_TOKEN(Star, STAR);
      PTX_BINARY_TOKEN(Slash, SLASH);
      PTX_BINARY_TOKEN(Percent, PERCENT);
      PTX_BINARY_TOKEN(ShiftLeft, SHL);
      PTX_BINARY_TOKEN(ShiftRight, SHR);
      PTX_BINARY_TOKEN(Lt, LT);
      PTX_BINARY_TOKEN(LtEq, LE);
      PTX_BINARY_TOKEN(Gt, GT);
      PTX_BINARY_TOKEN(GtEq, GE);
      PTX_BINARY_TOKEN(EqEq, EQ);
      PTX_BINARY_TOKEN(NotEq, NE);
      PTX_BINARY_TOKEN(Amp, AMP);
      PTX_BINARY_TOKEN(Caret, CARET);
      PTX_BINARY_TOKEN(Pipe, PIPE);
      PTX_BINARY_TOKEN(AmpAmp, AND);
      PTX_BINARY_TOKEN(PipePipe, OR);
#undef PTX_BINARY_TOKEN
    case Kind::Exclamation:
      if (driver.operand_expected)
        return Parser::make_NOT(id);
      break;
    case Kind::Tilde:
      if (driver.operand_expected)
        return Parser::make_TILDE(id);
      break;
    default:
      break;
  }
  driver.pending.reset();
  return Parser::make_YYEOF();
}

}  // namespace ptx_frontend::detail
