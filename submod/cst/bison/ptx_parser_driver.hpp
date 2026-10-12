#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend::detail {

/** Select the generated parser root without changing original source tokens. */
enum class EntryMode { Instruction, Module };

/** One bounded expression built exclusively by generated productions. */
struct ParsedExpression {
  /** Owns children; token identities refer to the driver's eventual CstFile. */
  syntax_cst::CstConstantExpression expression;
  /** Height in expression nodes; leaves have height one. */
  std::size_t depth{};
};

/** Parsed prefix shared by ordinary and structurally specialized instructions. */
struct InstructionHead {
  std::optional<syntax_cst::CstPredicate> predicate;
  syntax_cst::TokenId opcode{};
  std::vector<syntax_cst::TokenId> modifiers;
  /** Includes the predicate when present, otherwise starts at the opcode. */
  syntax_cst::TokenId first{};
};

/** Bracket base and its optional additive offset before a closing suffix. */
struct AddressContent {
  syntax_cst::CstAddressBase base;
  std::optional<syntax_cst::CstAddressOffset> offset;
  /** Covers the interior only; enclosing bracket tokens remain separate. */
  syntax_cst::CstTokenRange token_range;
};

/** Scalar heads and punctuation preceding an optional compound coordinate pack. */
struct BracketHeads {
  std::vector<syntax_cst::CstAddressBase> heads;
  std::vector<syntax_cst::TokenId> commas;
};

/** Unwind generated standalone parsing while retaining a source diagnostic. */
struct GeneratedParseFailure {
  CstParseDiagnostic diagnostic;
};

class PtxParserDriver;

/** Restore the enclosing expression budget and diagnostic expectation. */
struct ExpressionGuard {
  /** The driver outlives all generated semantic values. */
  PtxParserDriver* driver{};
  /** Remaining tree height at the enclosing production, in nodes. */
  std::size_t previous_budget{};
  std::optional<TokenKind> previous_expected;
  std::string previous_name;
  /** Restore state even when the generated parser discards a partial tree. */
  ~ExpressionGuard();
};

/** Source-owned lexical stream and construction state for the generated parser. */
class PtxParserDriver {
 public:
  using TokenId = syntax_cst::TokenId;
  using Guard = std::unique_ptr<ExpressionGuard>;

  /** Copy input into the existing reentrant lexer; no source view is retained. */
  explicit PtxParserDriver(std::string_view source, EntryMode mode);
  /** Parse the selected root and transfer the complete token buffer on success. */
  CstParseResult parse();
  /** Return the next stable source token; repeated EOF does not duplicate it. */
  TokenId nextToken();
  /** Replay a fetched token when a scanner-side failure precedes its delivery. */
  void replayToken(TokenId id);
  /** Access token spelling/location without changing the lexical cursor. */
  const PtxToken& token(TokenId id) const;
  /** Last token returned to Bison, including its retained offending lookahead. */
  TokenId lookahead() const;
  /** Record source tokens reduced or shifted by an executed grammar action. */
  void markShifted(TokenId id);
  /** Build a half-open source range from real source token identities. */
  SourceRange sourceRange(syntax_cst::CstTokenRange range) const;
  /** Enter a recursive child only after its parent fits the active budget. */
  Guard enter(TokenId anchor, std::size_t left_depth = 0,
              std::optional<TokenKind> closing = std::nullopt,
              std::string name = {});
  /** Track a required delimiter without spending an expression level. */
  Guard expectClosing(TokenKind closing, std::string name);
  /** Check before constructing an operation taller than the current budget. */
  void checkDepth(TokenId anchor, std::size_t height) const;
  /** Build a literal or symbol leaf from the original token identity. */
  ParsedExpression leaf(TokenId id, bool symbol);
  /** Assemble a binary operation after restoring its parent budget. */
  ParsedExpression binary(ParsedExpression left, TokenId op,
                          ParsedExpression right, Guard guard);
  /** Assemble one unary operation after restoring the enclosing budget. */
  ParsedExpression unary(TokenId op, ParsedExpression operand, Guard guard);
  /** Assemble a cast after its type-closing delimiter and operand are parsed. */
  ParsedExpression cast(TokenId open, TokenId type, TokenId close,
                        ParsedExpression operand, Guard guard);
  /** Assemble parentheses or a postfix initializer operation. */
  ParsedExpression parenthesized(TokenId open, ParsedExpression inner,
                                 TokenId close, Guard guard);
  /** Assemble a postfix call, retaining both delimiters and bounded children. */
  ParsedExpression call(ParsedExpression callee, TokenId open,
                        ParsedExpression argument, TokenId close, Guard guard);
  /** Assemble a right-associative conditional after restoring its child budget. */
  ParsedExpression conditional(ParsedExpression condition, TokenId question,
                               ParsedExpression yes, TokenId colon,
                               ParsedExpression no, Guard guard);
  /** Build the instruction node without instruction-family legality checks. */
  syntax_cst::CstInstruction instruction(
      InstructionHead head, std::vector<syntax_cst::CstOperandElement> operands,
      TokenId semicolon) const;
  /** Normalize single signed literals and reject register-bearing numeric trees. */
  syntax_cst::CstNumericOperand numeric(ParsedExpression expression) const;
  /** Normalize predicate constants while preserving ordinary numeric operands. */
  syntax_cst::CstOperand numericOperand(ParsedExpression expression) const;
  /** Convert a numeric leaf/tree into the existing address-base alternative. */
  syntax_cst::CstAddressBase numericBase(ParsedExpression expression) const;
  /** Emit a source-located syntax failure without invoking another parser. */
  [[noreturn]] void fail(
      TokenId id, std::string message,
      std::optional<TokenKind> expected = std::nullopt) const;
  /** Append one generated diagnostic in source order for module recovery. */
  void diagnose(CstParseDiagnostic diagnostic);
  /** Consume an action failure once when Bison enters its normal error path. */
  void generatedError(std::string message);
  /** Reset statement-local resource and lexical state after a grammar boundary. */
  void beginStatement(TokenId first);
  /** Record source spans skipped by a generated error-to-semicolon production. */
  std::vector<syntax_cst::CstRecoveryNode> recoverStatement(TokenId semicolon);

  /** Synthetic entry mode is returned once, before the uninterrupted source. */
  EntryMode entry_mode;
  bool entry_pending{true};
  /** Generated actions supply one root; tokens are transferred only afterward. */
  std::optional<syntax_cst::CstRoot> root;
  CstParseDiagnostics diagnostics;
  /** Active expression height allowance, shared with future initializer guards. */
  std::size_t remaining_depth{PtxCstParser::maxConstantTreeDepth};
  /** Innermost required delimiter and its established diagnostic spelling. */
  std::optional<TokenKind> expected;
  std::string expected_name;
  /** Source start of the current statement and its first offending lookahead. */
  TokenId statement_first{};
  std::optional<TokenId> recovery_error;
  /** Prefix lexical state is structural, independent of opcode legality. */
  bool operand_expected{true};
  std::optional<TokenId> unclassified_paren;

 private:
  /** Owns scanner input and trivia until tokens are moved into the CST file. */
  PtxLexer lexer_;
  std::vector<PtxToken> tokens_;
  /** Monotonic token index; Bison owns any earlier retained lookahead. */
  TokenId next_{};
  TokenId last_{};
  /** Largest token known consumed, distinguished from eager Bison lookahead. */
  std::optional<TokenId> shifted_through_;
  /** Full action diagnostic carried through Bison's private syntax_error type. */
  mutable std::optional<CstParseDiagnostic> pending_diagnostic_;
};

/** Private migration entry used by differential tests before facade cutover. */
CstParseResult parseGenerated(std::string_view source, EntryMode mode);

}  // namespace ptx_frontend::detail
