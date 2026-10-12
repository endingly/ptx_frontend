#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend::detail {

/** A generated expression and its bounded structural height. */
struct BisonExpression {
  /** Owns the existing CST operation tree; token IDs borrow the enclosing file. */
  syntax_cst::CstConstantExpression expression;
  /** Leaves have height one; each operation adds one to its deepest child. */
  std::size_t depth{};
};

/** Private failure used to unwind the generated parser without recovery. */
struct BisonExpressionFailure {
  CstParseDiagnostic diagnostic;
};

struct BisonExpressionDriver;

/** Restores a recursive expression budget and expected delimiter on unwind. */
struct BisonExpressionGuard {
  /** Driver outlives the generated parser and all its semantic values. */
  BisonExpressionDriver* driver{};
  /** Enclosing budget measured in remaining expression nodes on one path. */
  std::size_t previous_budget{};
  /** Enclosing delimiter expectation, restored independently of tree ownership. */
  std::optional<TokenKind> previous_expected;
  std::string previous_name;
  /** Restore the enclosing grammar context, including on parse failure. */
  ~BisonExpressionGuard();
};

/** Adapts the existing parser cursor without owning or duplicating its tokens. */
struct BisonExpressionDriver {
  using TokenId = syntax_cst::TokenId;
  using Guard = std::unique_ptr<BisonExpressionGuard>;
  /** Callbacks borrow one PtxCstParser for the duration of a single parse. */
  std::function<TokenId()> peek;
  std::function<TokenId()> consume;
  std::function<const PtxToken&(TokenId)> token;
  /** Public callers use zero, or ten for a multiplicative-only address base. */
  int minimum_precedence{};
  /** Active child budget, reduced before recursive grammar descent. */
  std::size_t remaining_depth{};
  std::optional<BisonExpression> result;
  /** Returned lookahead stays unconsumed until the next scan or an action. */
  std::optional<TokenId> pending;
  /** Question counts per parenthesis level distinguish enclosing colons. */
  std::vector<std::size_t> questions{0};
  /** Prefix state distinguishes unary signs and adjacent outer-grammar tokens. */
  bool operand_expected{true};
  /** A prefix '(' needs one lookahead to distinguish a cast from parentheses. */
  std::optional<TokenId> unclassified_paren;
  /** Innermost required delimiter and its established diagnostic spelling. */
  std::optional<TokenKind> expected;
  std::string expected_name;

  /** Commit only a token that the grammar has already shifted. */
  void acceptThrough(TokenId id);
  /** Track a required cast delimiter without charging its operand budget yet. */
  Guard expectClosing(TokenId shifted, TokenKind closing, std::string name);
  /** Enter a child production after checking its parent can fit the budget. */
  Guard enter(TokenId anchor, std::size_t left_depth = 0,
              std::optional<TokenKind> closing = std::nullopt,
              std::string name = {});
  /** Reject a parent before allocating a tree taller than the active budget. */
  void checkDepth(TokenId anchor, std::size_t depth) const;
  /** Construct one leaf using the original token's identity. */
  BisonExpression leaf(TokenId id, bool symbol);
  /** Construct a binary node after its child budget has been restored. */
  BisonExpression binary(BisonExpression left, TokenId op,
                         BisonExpression right, Guard guard);
  /** Produce an existing source-located diagnostic and unwind immediately. */
  [[noreturn]] void fail(TokenId id, std::string message,
                         std::optional<TokenKind> kind = std::nullopt) const;
};

/** Parse a complete expression slice; failures never invoke the old backend. */
std::expected<BisonExpression, CstParseDiagnostic> parseBisonExpression(
    BisonExpressionDriver driver);

}  // namespace ptx_frontend::detail
