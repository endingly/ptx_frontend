#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
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
  explicit PtxCstParser(std::string_view source);

  CstParseResult parseInstruction();
  CstParseResult parseModule();

 private:
  using TokenId = syntax_cst::TokenId;

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

  [[nodiscard]] TokenId peek();
  TokenId consume();
  [[nodiscard]] const PtxToken& token(TokenId id) const;
  [[nodiscard]] bool atImmediateStart();
  [[nodiscard]] RecoveryResult recover(
      TokenId first,
      const CstParseDiagnostic& diagnostic,
      RecoveryContext context);

  std::expected<TokenId, CstParseDiagnostic> expect(TokenKind kind,
                                                    std::string_view name);
  std::expected<TokenId, CstParseDiagnostic> expectIntegerLiteral(
      std::string_view name);
  std::expected<syntax_cst::CstImmediate, CstParseDiagnostic> parseImmediate(
      bool allow_sign = true);
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic> parseOperand();
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
  parseBracketedAddress(TokenId open);
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic> parseVectorPack(
      TokenId open);
  std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
  parseCallParameterList(syntax_cst::CstCallParameterListKind kind);
  std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
  parseCallOperands();
  std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
  parseBranchOperands();
  std::expected<std::vector<syntax_cst::CstOperandElement>, CstParseDiagnostic>
  parseIndexedBranchOperands();
  std::expected<syntax_cst::CstInstruction, CstParseDiagnostic>
  parseInstructionNode(std::optional<TokenId> opcode = std::nullopt);
  std::expected<syntax_cst::CstConstantExpression, CstParseDiagnostic>
  parseConstantExpression(int minimum_precedence = 0);
  std::expected<syntax_cst::CstConstantExpression, CstParseDiagnostic>
  parseConstantUnary();
  std::expected<syntax_cst::CstConstantExpression, CstParseDiagnostic>
  parseConstantPrimary();
  std::expected<syntax_cst::CstInitializer, CstParseDiagnostic>
  parseInitializer();
  std::expected<syntax_cst::CstVariableDeclaration, CstParseDiagnostic>
  parseVariableDeclaration(std::vector<TokenId> qualifiers = {},
                           std::optional<TokenId> first_token = std::nullopt);
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
                        std::size_t block_depth = 0);
  std::expected<syntax_cst::CstBlock, CstParseDiagnostic> parseBlock(
      CstParseDiagnostics& diagnostics, std::size_t block_depth);
  std::expected<syntax_cst::CstModuleDirective, CstParseDiagnostic>
  parseModuleDirective();
  std::expected<syntax_cst::CstSectionDirective, CstParseDiagnostic>
  parseSectionDirective();
  std::expected<syntax_cst::CstFunction, CstParseDiagnostic> parseFunction(
      std::vector<TokenId> qualifiers,
      TokenId first_token,
      CstParseDiagnostics& diagnostics);

  PtxLexer lexer_;
  std::vector<PtxToken> tokens_;
  std::optional<TokenId> peeked_;
};

}  // namespace ptx_frontend
