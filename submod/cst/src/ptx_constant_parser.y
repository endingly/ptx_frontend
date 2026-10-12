%require "3.8"
%language "c++"
%define api.namespace {ptx_frontend::detail}
%define api.parser.class {BisonConstantParser}
%define api.value.type variant
%define api.token.constructor
%define parse.error custom
%define parse.lac full
%parse-param {BisonExpressionDriver& driver}
%lex-param {BisonExpressionDriver& driver}

%code requires {
#include "ptx_constant_driver.hpp"
}
%code provides {
namespace ptx_frontend::detail {
/** Return borrowed token identities, retaining the enclosing parser lookahead. */
BisonConstantParser::symbol_type yylex(BisonExpressionDriver& driver);
}
}
%code {
#include <algorithm>
using namespace ptx_frontend::syntax_cst;
}

%token <syntax_cst::TokenId> LITERAL SYMBOL CAST
%token <syntax_cst::TokenId> LP RP QUESTION COLON
%token <syntax_cst::TokenId> PLUS MINUS NOT TILDE STAR SLASH PERCENT
%token <syntax_cst::TokenId> SHL SHR LT LE GT GE EQ NE AMP CARET PIPE AND OR
%type <BisonExpression> expression logical_or logical_and bit_or bit_xor bit_and
%type <BisonExpression> equality relation shift sum product unary postfix atom
%type <syntax_cst::TokenId> unary_op add_op mul_op shift_op relation_op equality_op
%type <BisonExpressionDriver::Guard> paren_guard call_guard cast_guard cast_close_guard

%%
input: expression { driver.result = std::move($1); };
expression:
  logical_or { $$ = std::move($1); }
| logical_or QUESTION <BisonExpressionDriver::Guard>{
    $$ = driver.enter($2, $1.depth, TokenKind::Colon, "':' in conditional expression");
  } expression COLON {
    driver.acceptThrough($5);
    driver.expected = $3->previous_expected;
    driver.expected_name = $3->previous_name;
  } expression {
    $3.reset();
    const auto depth = std::max({$1.depth, $4.depth, $7.depth}) + 1;
    driver.checkDepth($2, depth);
    const CstTokenRange range{$1.expression.token_range.first, $7.expression.token_range.last};
    $$ = {CstConstantExpression{CstConstantConditional{
        std::make_unique<CstConstantExpression>(std::move($1.expression)), $2,
        std::make_unique<CstConstantExpression>(std::move($4.expression)), $5,
        std::make_unique<CstConstantExpression>(std::move($7.expression))}, range}, depth};
  };
logical_or:
  logical_and { $$ = std::move($1); }
| logical_or OR <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } logical_and
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
logical_and:
  bit_or { $$ = std::move($1); }
| logical_and AND <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } bit_or
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
bit_or:
  bit_xor { $$ = std::move($1); }
| bit_or PIPE <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } bit_xor
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
bit_xor:
  bit_and { $$ = std::move($1); }
| bit_xor CARET <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } bit_and
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
bit_and:
  equality { $$ = std::move($1); }
| bit_and AMP <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } equality
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
equality:
  relation { $$ = std::move($1); }
| equality equality_op <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } relation
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
relation:
  shift { $$ = std::move($1); }
| relation relation_op <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } shift
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
shift:
  sum { $$ = std::move($1); }
| shift shift_op <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } sum
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
sum:
  product { $$ = std::move($1); }
| sum add_op <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } product
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
product:
  unary { $$ = std::move($1); }
| product mul_op <BisonExpressionDriver::Guard>{ $$ = driver.enter($2, $1.depth); } unary
  { $$ = driver.binary(std::move($1), $2, std::move($4), std::move($3)); };
unary:
  postfix { $$ = std::move($1); }
| unary_op <BisonExpressionDriver::Guard>{ $$ = driver.enter($1); } unary {
    $2.reset();
    const auto depth = $3.depth + 1;
    driver.checkDepth($1, depth);
    const CstTokenRange range{$1, $3.expression.token_range.last};
    $$ = {CstConstantExpression{CstConstantUnary{$1,
        std::make_unique<CstConstantExpression>(std::move($3.expression))}, range}, depth};
  }
| LP CAST cast_close_guard RP cast_guard unary {
    $5.reset();
    const auto depth = $6.depth + 1;
    driver.checkDepth($1, depth);
    const CstTokenRange range{$1, $6.expression.token_range.last};
    $$ = {CstConstantExpression{CstConstantCast{$1, $2, $4,
        std::make_unique<CstConstantExpression>(std::move($6.expression))}, range}, depth};
  };
postfix:
  atom { $$ = std::move($1); }
| postfix LP call_guard expression RP {
    driver.acceptThrough($5);
    $3.reset();
    const auto depth = std::max($1.depth, $4.depth) + 1;
    driver.checkDepth($2, depth);
    const CstTokenRange range{$1.expression.token_range.first, $5 + 1};
    $$ = {CstConstantExpression{CstConstantCall{
        std::make_unique<CstConstantExpression>(std::move($1.expression)), $2,
        std::make_unique<CstConstantExpression>(std::move($4.expression)), $5}, range}, depth};
  };
atom:
  LITERAL { $$ = driver.leaf($1, false); }
| SYMBOL { $$ = driver.leaf($1, true); }
| LP paren_guard expression RP {
    driver.acceptThrough($4);
    $2.reset();
    const auto depth = $3.depth + 1;
    driver.checkDepth($1, depth);
    $$ = {CstConstantExpression{CstConstantParenthesized{$1,
        std::make_unique<CstConstantExpression>(std::move($3.expression)), $4}, {$1, $4 + 1}}, depth};
  };
paren_guard: %empty {
  $$ = driver.enter($<syntax_cst::TokenId>0, 0, TokenKind::RParen, "')'");
};
call_guard: %empty {
  $$ = driver.enter($<syntax_cst::TokenId>0, $<BisonExpression>-1.depth,
                    TokenKind::RParen, "')' after initializer operator");
};
cast_guard: %empty {
  driver.acceptThrough($<syntax_cst::TokenId>0);
  $<BisonExpressionDriver::Guard>-1.reset();
  $$ = driver.enter($<syntax_cst::TokenId>-3);
};
cast_close_guard: %empty {
  $$ = driver.expectClosing($<syntax_cst::TokenId>0, TokenKind::RParen,
                            "')' after constant cast");
};
unary_op: PLUS | MINUS | NOT | TILDE;
add_op: PLUS | MINUS;
mul_op: STAR | SLASH | PERCENT;
shift_op: SHL | SHR;
relation_op: LT | LE | GT | GE;
equality_op: EQ | NE;
%%

/** Map generated syntax failures to the established enclosing CST diagnostics. */
void ptx_frontend::detail::BisonConstantParser::report_syntax_error(const context& ctx) const {
  symbol_kind_type tokens[64];
  const int count = ctx.expected_tokens(tokens, 64);
  bool needs_operand = false;
  bool needs_right_paren = false;
  bool needs_colon = false;
  for (int i = 0; i < count; ++i) {
    needs_operand |= tokens[i] == symbol_kind::S_LITERAL || tokens[i] == symbol_kind::S_SYMBOL;
    needs_right_paren |= tokens[i] == symbol_kind::S_RP;
    needs_colon |= tokens[i] == symbol_kind::S_COLON;
  }
  if (!needs_operand && needs_colon)
    driver.fail(driver.peek(), "expected ':' in conditional expression", TokenKind::Colon);
  if (!needs_operand && needs_right_paren) {
    driver.fail(driver.peek(), "expected " + driver.expected_name, TokenKind::RParen);
  }
  driver.fail(driver.peek(), "expected constant expression");
}

/** Generated parser errors abort this expression without generated recovery. */
void ptx_frontend::detail::BisonConstantParser::error(const std::string& message) {
  driver.fail(driver.peek(), message);
}
