#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend::detail {

/** Select the generated parser root without changing original source tokens. */
enum class EntryMode { Instruction, Module };

/**
 * Exclusive function under construction, shared only by moving between symbols.
 * Completed function symbols are nonnull; the generated uninitialized symbol
 * starts empty. No pointer survives materialization into the public module CST.
 */
using FunctionValue = std::unique_ptr<syntax_cst::CstFunction>;

/**
 * Private module-item value that retains the function's existing owner rather
 * than embedding its large payload or allocating a second wrapper.
 */
using ModuleItemValue = std::variant<
    syntax_cst::CstModuleDirective, syntax_cst::CstSectionDirective,
    syntax_cst::CstPragma, syntax_cst::CstVariableDeclaration,
    syntax_cst::CstAliasDirective, FunctionValue, syntax_cst::CstRecoveryNode>;

/** Move one completed private item into independently owned public CST storage. */
syntax_cst::CstModuleItem materializeModuleItem(ModuleItemValue&& item);

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

/** Initializer syntax with its bounded expression/list tree height. */
struct ParsedInitializer {
  /** Owns the initializer tree; token identities borrow the driver's buffer. */
  syntax_cst::CstInitializer initializer;
  /** Scalar wrappers add no level; lists and named members add one. */
  std::size_t depth{};
};

/** Source-ordered initializer elements with separators and maximum child height. */
struct InitializerElements {
  /** Owns children in written order, without synthesizing separators. */
  std::vector<syntax_cst::CstInitializer> elements;
  std::vector<syntax_cst::TokenId> commas;
  /** Maximum child height, or zero for an empty list. */
  std::size_t depth{};
};

/** Optional written two-token alignment or count prefix. */
using OptionalTokenPair =
    std::optional<std::pair<syntax_cst::TokenId, syntax_cst::TokenId>>;

class PtxParserDriver;

/** One active generated body frame; lifetime follows its grammar semantic value. */
struct BodyFrame {
  /** Borrowed driver outlives all generated semantic values. */
  PtxParserDriver* driver{};
  /** Identity of the written opening brace, never a synthetic source token. */
  syntax_cst::TokenId brace{};
  /** Function braces do not spend the nested block budget. */
  bool nested{};
  /** A private missing-close terminal belongs to this frame at most once. */
  bool close_issued{};
  /** Remove the frame on successful reduction or generated error unwinding. */
  ~BodyFrame();
};

/** A real close token or the exclusive endpoint at a retained missing-close anchor. */
using BodyClose =
    std::pair<std::optional<syntax_cst::TokenId>, syntax_cst::TokenId>;

/** Unwind generated standalone parsing while retaining a source diagnostic. */
struct GeneratedParseFailure {
  CstParseDiagnostic diagnostic;
};

/** Restore the enclosing expression budget and diagnostic expectation. */
struct ExpressionGuard {
  /** The driver outlives all generated semantic values. */
  PtxParserDriver* driver{};
  /** Remaining tree height at the enclosing production, in nodes. */
  std::size_t previous_budget{};
  std::optional<TokenKind> previous_expected;
  std::string previous_name;
  /** Initializer opening token retained independently of expression delimiters. */
  std::optional<syntax_cst::TokenId> previous_initializer_anchor;
  /** Written initializer brace when this guard owns a list. */
  syntax_cst::TokenId anchor{};
  /** Restore state even when the generated parser discards a partial tree. */
  ~ExpressionGuard();
};

/** Source-owned lexical stream and construction state for the generated parser. */
class PtxParserDriver {
 public:
  using TokenId = syntax_cst::TokenId;
  using Guard = std::unique_ptr<ExpressionGuard>;
  using BodyGuard = std::unique_ptr<BodyFrame>;

  /** Copy input into the existing reentrant lexer; no source view is retained. */
  explicit PtxParserDriver(std::string_view source);
  /** Parse the selected root and transfer the complete token buffer on success. */
  CstParseResult parse(EntryMode mode);
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
  /** Locate the first forbidden register in a completed numeric tree, read-only. */
  std::optional<TokenId> numericRegister(syntax_cst::CstTokenRange range) const;
  /** Normalize predicate constants while preserving ordinary numeric operands. */
  syntax_cst::CstOperand numericOperand(ParsedExpression expression) const;
  /** Convert a numeric leaf/tree into the existing address-base alternative. */
  syntax_cst::CstAddressBase numericBase(ParsedExpression expression) const;
  /** Emit a source-located syntax failure without invoking another parser. */
  [[noreturn]] void fail(
      TokenId id, std::string message,
      std::optional<TokenKind> expected = std::nullopt) const;
  /** Emit a diagnostic for a complete written construct rather than one token. */
  [[noreturn]] void failRange(
      SourceRange range, std::string message,
      std::optional<TokenKind> expected = std::nullopt) const;
  /** Append one generated diagnostic in source order for module recovery. */
  void diagnose(CstParseDiagnostic diagnostic);
  /** Consume an action failure once when Bison enters its normal error path. */
  void generatedError(std::string message);
  /** Reset statement-local resource and lexical state after a grammar boundary. */
  void beginStatement(TokenId first);
  /** Validate a literal at a grammar-defined integer/decimal-only position. */
  TokenId literal(TokenId id, std::string_view name, bool integer = false,
                  bool expected_decimal = true) const;
  /** Validate one consumed architecture or option at its directive position. */
  TokenId targetDirectiveItem(TokenId id, bool architecture);
  /** Install one grammar-owned body frame before fetching its first item. */
  BodyGuard openBody(TokenId brace, bool nested);
  /** Restore body frame/depth state during ordinary or error destruction. */
  void leaveBody(BodyFrame* frame);
  /** Mark the exact generated boundary before the next body item. */
  void bodyBoundary();
  /** Start the next source item even when Bison already owns its lookahead. */
  void itemBoundary();
  /** Report an unsupported item without shifting its offending lookahead. */
  void rejectItem(bool body) const;
  /** Emit one missing-close terminal only at an eligible boundary. */
  bool missingClose(TokenId anchor);
  /** Construct the existing zero-width CST recovery marker at a real anchor. */
  syntax_cst::CstRecoveryNode inserted(TokenKind kind, TokenId anchor) const;
  /** Recover one failed module/body item, replaying its preserved real anchor. */
  std::vector<syntax_cst::CstRecoveryNode> synchronize(bool body);
  /** Preserve header token ordering without duplicating source tokens. */
  std::vector<TokenId> tokenSpan(TokenId first, TokenId last) const;
  /** Validate declaration-only initializer admission before parsing its value. */
  void initializerAllowed(TokenId equals) const;

  /** Synthetic entry mode is returned once, before the uninterrupted source. */
  EntryMode entry_mode{EntryMode::Instruction};
  bool entry_pending{true};
  /** Logical source start of this entry; earlier tokens remain owned as a prefix. */
  TokenId entry_first{};
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
  /** Logical recovery cursor; an expect-style rejection may retain its token. */
  std::optional<TokenId> recovery_error;
  /** Prefix lexical state is structural, independent of opcode legality. */
  bool operand_expected{true};
  std::optional<TokenId> unclassified_paren;
  /** Innermost list brace used for depth diagnostics before entering a child. */
  std::optional<TokenId> initializer_anchor;
  /** One scanner delivery is pending after a generated initializer opening. */
  bool initializer_child_pending{};
  /** Current declaration admission state, set by generated declaration heads. */
  TokenKind declaration_space{TokenKind::DotGlobal};
  bool declaration_external{};
  bool parameterized_declarator{};
  /** Generated boundaries alone enable missing-close detection. */
  bool body_eligible{};
  /** The next fetched real token starts an item unless already prefetched. */
  bool next_item{true};
  /** Borrowed frame pointers in source nesting order; semantic values own them. */
  std::vector<BodyFrame*> bodies;
  /** Number of active nested blocks, excluding the enclosing function. */
  std::size_t nested_depth{};

 private:
  /** Owns scanner input and trivia until tokens are moved into the CST file. */
  PtxLexer lexer_;
  std::vector<PtxToken> tokens_;
  /** Logical cursor into stable cached tokens; only one fetched token is replayed. */
  TokenId next_{};
  /** Most recently fetched real token, including a retained EOF/anchor. */
  TokenId last_{};
  /** Largest token known consumed, distinguished from eager Bison lookahead. */
  std::optional<TokenId> shifted_through_;
  /** Full action diagnostic carried through Bison's private syntax_error type. */
  mutable std::optional<CstParseDiagnostic> pending_diagnostic_;
  /** True only after a value transfers tokens; later entries use cached EOF. */
  bool exhausted_{};
  /** Original EOF location survives transfer independently of the token buffer. */
  SourceRange eof_range_;
  /** Reset generated per-entry state without restarting the owned source stream. */
  void beginEntry(EntryMode mode);
  /** Restore the logical unconsumed position after generated failure unwinding. */
  void retainFailureCursor();
};

/** Parse a fresh private generated entry for differential and construction tests. */
CstParseResult parseGenerated(std::string_view source, EntryMode mode);

}  // namespace ptx_frontend::detail
