#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ptx_frontend/cst/ptx_cst.hpp>
#include <ptx_frontend/lexer/ptx_lexer.hpp>

namespace ptx_frontend {

struct CstParseDiagnostic {
  SourceRange range;
  std::string message;
  std::optional<TokenKind> expected_kind;
};

using CstParseDiagnostics = DiagnosticCollection<CstParseDiagnostic>;
using CstParseResult =
    ResultWithDiagnostics<syntax_cst::CstFile, CstParseDiagnostic>;

/** Parses a PTX instruction fragment or module into a lossless CST. */
class PtxCstParser {
 public:
  /** Maximum permitted height of a constant expression or initializer tree. */
  static constexpr std::size_t maxConstantTreeDepth = 128;

  explicit PtxCstParser(std::string_view source);

  CstParseResult parseInstruction();
  CstParseResult parseModule();

 private:
  using TokenId = syntax_cst::TokenId;

  /** Nesting budget for lexical blocks, independent of expression trees. */
  struct BlockDepth {
    /** Number of nested braces below the function body; the function is zero. */
    std::size_t value{};
  };
  /** Selects the diagnostic spelling at a missing body terminator. */
  enum class BodyContext { Function, NestedBlock };
  /** A body whose closing brace has been consumed. */
  struct ClosedBody {
    /** Real consumed delimiter in the parser-owned token buffer. */
    TokenId right_brace;
  };
  /** A body ending before a boundary token, which remains unconsumed. */
  struct MissingClosingBrace {
    /** Unconsumed boundary token; its start anchors the zero-width insertion. */
    TokenId anchor;
  };
  /** Exhaustive body termination states, with no optional-payload ambiguity. */
  using BodyOutcome = std::variant<ClosedBody, MissingClosingBrace>;
  /** Parse body items after the opening brace; append each recovery only once. */
  BodyOutcome parseBody(std::vector<syntax_cst::CstFunctionBodyItem>& body,
                        CstParseDiagnostics& diagnostics, BlockDepth depth,
                        BodyContext context);

  enum class RecoveryStop {
    Semicolon,
    RightBrace,
    FunctionBoundary,
    ModuleItem,
    Eof,
  };

  enum class RecoveryContext {
    FunctionBody,
    Module,
  };

  struct RecoveryResult {
    std::vector<syntax_cst::CstRecoveryNode> nodes;
    std::optional<TokenId> last;
    RecoveryStop stop{};
  };

  /** Allowed height including the current root; siblings receive copies. */
  struct RemainingTreeDepth {
    /** Maximum allowed height including this root; zero forbids a tree. */
    std::size_t value;
  };
  /** Actual structural height; scalar initializer wrappers add no level. */
  struct TreeHeight {
    /** Constructed node height; a leaf is one and an empty accumulator is zero. */
    std::size_t value{};
  };

  /** A parsed expression together with its private structural tree height. */
  struct ParsedConstantExpression {
    syntax_cst::CstConstantExpression expression;
    /** Leaf expressions have height one; each expression wrapper adds one. */
    TreeHeight height{};
  };

  /** A parsed initializer together with its private structural tree height. */
  struct ParsedInitializer {
    syntax_cst::CstInitializer initializer;
    /** Scalar initializer wrappers add no height; initializer lists add one. */
    TreeHeight height{};
  };

  /** Numeric entry grammar: address bases stop before additive operators. */
  enum class ExpressionBoundary { Full, AboveAdditive };

  [[nodiscard]] TokenId peek();
  /** Read one token beyond the current lookahead without consuming either. */
  [[nodiscard]] TokenId peekNext();
  TokenId consume();
  [[nodiscard]] const PtxToken& token(TokenId id) const;
  /** Build a diagnostic for a source tree that exceeds the active depth budget. */
  [[nodiscard]] CstParseDiagnostic depthLimitExceeded(
      TokenId id, std::string_view tree_kind) const;
  [[nodiscard]] bool atImmediateStart();
  /** Test a numeric expression prefix without consuming register/address syntax. */
  [[nodiscard]] bool atNumericStart();
  /** Parse a numeric leaf while preserving the legacy single-literal shape. */
  std::expected<syntax_cst::CstNumericOperand, CstParseDiagnostic>
  parseNumericOperand(ExpressionBoundary boundary = ExpressionBoundary::Full);
  [[nodiscard]] RecoveryResult recover(TokenId first,
                                       const CstParseDiagnostic& diagnostic,
                                       RecoveryContext context);

  std::expected<TokenId, CstParseDiagnostic> expect(TokenKind kind,
                                                    std::string_view name);
  std::expected<TokenId, CstParseDiagnostic> expectIntegerLiteral(
      std::string_view name);
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic> parseOperand();
  /** Parse the predicted operand form from its unconsumed prefix. */
  std::expected<syntax_cst::CstNegatedRegisterOperand, CstParseDiagnostic>
  parseNegatedRegisterOperand();
  /** Parse the predicted operand form from its unconsumed prefix. */
  std::expected<syntax_cst::CstPredicateOperand, CstParseDiagnostic>
  parsePredicateOperand();
  /** Parse the predicted operand form from its unconsumed prefix. */
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
  parseVectorOperand();
  /** Parse the predicted operand form from its unconsumed prefix. */
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
  parseNumericSourceOperand();
  /** Parse the predicted operand form from its unconsumed prefix. */
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
  parseIdentifierOperand();

  /** Parse after consuming `[`, including its closing `]` and optional suffix. */
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
  parseBracketedAddress(TokenId open);
  /** Parse after consuming `{`; consume `}` and retain both in a half-open range. */
  std::expected<syntax_cst::CstVectorPack, CstParseDiagnostic> parseVectorPack(
      TokenId open);
  /** Parse an unconsumed `(` through `)` using the selected parameter grammar. */
  std::expected<syntax_cst::CstCallParameterList, CstParseDiagnostic>
  parseCallParameterList(syntax_cst::CstCallParameterListKind kind);
  std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
  parseCallOperands();
  std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
  parseBranchOperands();
  std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
  parseIndexedBranchOperands();
  std::expected<syntax_cst::CstInstruction, CstParseDiagnostic>
  parseInstructionNode();
  /** Parse the tail after the opcode and optional predicate have been consumed. */
  std::expected<syntax_cst::CstInstruction, CstParseDiagnostic>
  parseInstructionAfterHead(
      TokenId opcode,
      std::optional<syntax_cst::CstPredicate> predicate = std::nullopt);
  std::expected<ParsedConstantExpression, CstParseDiagnostic>
  parseConstantExpression(int minimum_precedence,
                          RemainingTreeDepth remaining_depth);
  std::expected<ParsedConstantExpression, CstParseDiagnostic>
  parseConstantUnary(RemainingTreeDepth remaining_depth);
  std::expected<ParsedConstantExpression, CstParseDiagnostic>
  parseConstantPrimary(RemainingTreeDepth remaining_depth);
  std::expected<ParsedInitializer, CstParseDiagnostic> parseInitializer(
      RemainingTreeDepth remaining_depth);
  std::expected<syntax_cst::CstVariableDeclaration, CstParseDiagnostic>
  parseVariableDeclaration(std::vector<TokenId> qualifiers = {});
  std::expected<syntax_cst::CstAttributeList, CstParseDiagnostic>
  parseAttributeList();
  std::expected<syntax_cst::CstFunctionParameter, CstParseDiagnostic>
  parseFunctionParameter();
  std::expected<syntax_cst::CstFunctionParameterList, CstParseDiagnostic>
  parseFunctionParameterList();
  std::expected<syntax_cst::CstCallPrototype, CstParseDiagnostic>
  parseCallPrototype(TokenId label, TokenId colon);
  std::expected<syntax_cst::CstCallTargets, CstParseDiagnostic>
  parseCallTargets(TokenId label, TokenId colon);
  std::expected<syntax_cst::CstBranchTargets, CstParseDiagnostic>
  parseBranchTargets(TokenId label, TokenId colon);
  std::expected<syntax_cst::CstLocDirective, CstParseDiagnostic>
  parseLocDirective();
  std::expected<syntax_cst::CstPragma, CstParseDiagnostic> parsePragma();
  std::expected<syntax_cst::CstKernelResourceDirective, CstParseDiagnostic>
  parseKernelResourceDirective();
  std::expected<syntax_cst::CstLanguageDirective, CstParseDiagnostic>
  parseLanguageDirective();
  std::expected<syntax_cst::CstAliasDirective, CstParseDiagnostic>
  parseAliasDirective();
  std::expected<syntax_cst::CstFunctionBodyItem, CstParseDiagnostic>
  parseFunctionBodyItem(CstParseDiagnostics& diagnostics,
                        BlockDepth block_depth);
  std::expected<syntax_cst::CstBlock, CstParseDiagnostic> parseBlock(
      CstParseDiagnostics& diagnostics, BlockDepth block_depth);
  std::expected<syntax_cst::CstModuleDirective, CstParseDiagnostic>
  parseModuleDirective();
  std::expected<syntax_cst::CstSectionDirective, CstParseDiagnostic>
  parseSectionDirective();
  std::expected<syntax_cst::CstFunction, CstParseDiagnostic> parseFunction(
      std::vector<TokenId> qualifiers, CstParseDiagnostics& diagnostics);

  PtxLexer lexer_;
  std::vector<PtxToken> tokens_;
  std::optional<TokenId> peeked_;
  /** One lexed successor retained until the current token is consumed. */
  std::optional<TokenId> buffered_next_;
};

}  // namespace ptx_frontend
