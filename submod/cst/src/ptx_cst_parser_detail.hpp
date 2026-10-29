#pragma once

#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <utility>

namespace ptx_frontend::cst_detail {

/** Maximum nested lexical blocks inside a function body. */
inline constexpr std::size_t kMaxNestedBlockDepth = 256;

/** Recognize source literal tokens without interpreting their values. */
inline bool isImmediate(TokenKind kind) {
  switch (kind) {
    case TokenKind::Decimal:
    case TokenKind::Hex:
    case TokenKind::F32Hex:
    case TokenKind::F64Hex:
    case TokenKind::F64:
    case TokenKind::WarpSz:
      return true;
    default:
      return false;
  }
}

/** Recognize integer literal source tokens. */
inline bool isIntegerLiteral(TokenKind kind) {
  return kind == TokenKind::Decimal || kind == TokenKind::Hex;
}

/** Recognize plain and dotted identifier source tokens. */
inline bool isIdentifierToken(TokenKind kind) {
  return kind == TokenKind::Ident || kind == TokenKind::DotIdent;
}

/** Recognize lexical modifier forms without ISA legality checks. */
inline bool isModifier(TokenKind kind) {
  return kind == TokenKind::DotIdent || kind == TokenKind::DotGlobal ||
         kind == TokenKind::DotConst || kind == TokenKind::DotShared ||
         kind == TokenKind::DotLocal || kind == TokenKind::DotParam ||
         kind == TokenKind::DotWeak;
}

/** Recognize dedicated module-directive token kinds. */
inline bool isModuleDirective(TokenKind kind) {
  return kind == TokenKind::DotVersion || kind == TokenKind::DotTarget ||
         kind == TokenKind::DotAddressSize || kind == TokenKind::DotFile;
}

/** Recognize linkage prefixes shared by functions and declarations. */
inline bool isFunctionQualifier(TokenKind kind);
/** Recognize variable-declaration state-space prefixes. */
inline bool isVariableStateSpace(TokenKind kind);

/** Recognize entry-header resource directive prefixes. */
inline bool isKernelResourceDirective(TokenKind kind) {
  return kind == TokenKind::DotMaxnreg || kind == TokenKind::DotMaxntid ||
         kind == TokenKind::DotReqntid || kind == TokenKind::DotMinnctapersm ||
         kind == TokenKind::DotReqnctapercluster ||
         kind == TokenKind::DotExplicitcluster ||
         kind == TokenKind::DotMaxclusterrank;
}

/** Recognize a function or linkage boundary for body recovery. */
inline bool isFunctionBoundary(TokenKind kind) {
  return kind == TokenKind::DotEntry || kind == TokenKind::DotFunc ||
         isFunctionQualifier(kind);
}

/** Recognize body recovery boundaries; this is narrower than all module starts. */
inline bool isFunctionOrModuleBoundary(TokenKind kind) {
  return isFunctionBoundary(kind) || isModuleDirective(kind) ||
         kind == TokenKind::DotSection;
}

/** Module source forms, independent of instruction or target legality. */
enum class ModuleSourceForm {
  Directive,
  Pragma,
  Section,
  Alias,
  Function,
  Qualifier,
  Variable,
  Unsupported
};

/** Classify supported module prefixes, including spellings lexed as DotIdent. */
inline ModuleSourceForm classifyModuleSource(const PtxToken& token) {
  const auto kind = token.kind;
  if (isModuleDirective(kind))
    return ModuleSourceForm::Directive;
  if (kind == TokenKind::DotPragma)
    return ModuleSourceForm::Pragma;
  if (kind == TokenKind::DotSection)
    return ModuleSourceForm::Section;
  if (kind == TokenKind::DotIdent && token.text == ".alias")
    return ModuleSourceForm::Alias;
  if (kind == TokenKind::DotEntry || kind == TokenKind::DotFunc)
    return ModuleSourceForm::Function;
  if (isFunctionQualifier(kind))
    return ModuleSourceForm::Qualifier;
  if (isVariableStateSpace(kind))
    return ModuleSourceForm::Variable;
  return ModuleSourceForm::Unsupported;
}

/** Keep the established opaque-section boundary set, distinct from module starts.
 * Dotted `.alias` spellings can occur in section payload and are not sync anchors.
 */
inline bool isSectionRecoveryBoundary(const PtxToken& token) {
  const auto form = classifyModuleSource(token);
  return form != ModuleSourceForm::Unsupported &&
         form != ModuleSourceForm::Alias;
}

/** Recognize potential successors when inserting a missing body semicolon. */
inline bool isFunctionBodyItemStart(TokenKind kind) {
  return kind == TokenKind::LBrace || kind == TokenKind::At ||
         kind == TokenKind::Ident || isVariableStateSpace(kind) ||
         kind == TokenKind::DotLoc || kind == TokenKind::DotPragma ||
         kind == TokenKind::DotCallPrototype ||
         kind == TokenKind::DotCallTargets ||
         kind == TokenKind::DotBranchTargets;
}

/** Return a fail-fast fragment result without publishing a partial root. */
inline CstParseResult parseFailure(CstParseDiagnostic diagnostic) {
  return {.value = std::nullopt, .diagnostics = {std::move(diagnostic)}};
}

/** Recognize linkage prefixes shared by functions and declarations. */
inline bool isFunctionQualifier(TokenKind kind) {
  return kind == TokenKind::DotExtern || kind == TokenKind::DotVisible ||
         kind == TokenKind::DotWeak;
}

/** Recognize variable-declaration state-space prefixes. */
inline bool isVariableStateSpace(TokenKind kind) {
  return kind == TokenKind::DotReg || kind == TokenKind::DotParam ||
         kind == TokenKind::DotLocal || kind == TokenKind::DotShared ||
         kind == TokenKind::DotGlobal || kind == TokenKind::DotConst ||
         kind == TokenKind::DotTex;
}

/** Recognize constant-expression literal leaves. */
inline bool isConstantLiteral(TokenKind kind) {
  return isImmediate(kind);
}

/** Recognize supported prefix constant-expression operators. */
inline bool isConstantUnaryOperator(TokenKind kind) {
  return kind == TokenKind::Plus || kind == TokenKind::Minus ||
         kind == TokenKind::Exclamation || kind == TokenKind::Tilde;
}

/** Return handwritten source precedence, or minus one for a nonoperator. */
inline int constantBinaryPrecedence(TokenKind kind) {
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

}  // namespace ptx_frontend::cst_detail

namespace ptx_frontend {

/** Return the current token id without consuming it; lex into the owned buffer only on a cache miss. */
inline PtxCstParser::TokenId PtxCstParser::peek() {
  if (!peeked_) {
    if (buffered_next_) {
      peeked_ = *buffered_next_;
      buffered_next_.reset();
    } else {
      tokens_.push_back(lexer_.consume());
      peeked_ = static_cast<TokenId>(tokens_.size() - 1);
    }
  }
  return *peeked_;
}

/** Retain one true successor for source-prefix prediction without consuming either lookahead token. */
inline PtxCstParser::TokenId PtxCstParser::peekNext() {
  (void)peek();
  if (!buffered_next_) {
    tokens_.push_back(lexer_.consume());
    buffered_next_ = static_cast<TokenId>(tokens_.size() - 1);
  }
  return *buffered_next_;
}

/** Consume the current lookahead while retaining its token and trivia in the parser-owned buffer. */
inline PtxCstParser::TokenId PtxCstParser::consume() {
  const TokenId id = peek();
  peeked_.reset();
  return id;
}

/** Read a checked reference into the parser-owned buffer; subsequent lexing can invalidate the reference. */
inline const PtxToken& PtxCstParser::token(TokenId id) const {
  return tokens_.at(id);
}

/** Consume only the requested token kind; a mismatch leaves lookahead untouched and returns its diagnostic. */
inline std::expected<PtxCstParser::TokenId, CstParseDiagnostic>
PtxCstParser::expect(TokenKind kind, std::string_view name) {
  const TokenId id = peek();
  if (token(id).kind == kind)
    return consume();
  return std::unexpected(CstParseDiagnostic{
      token(id).range, "expected " + std::string(name), kind});
}

/** Consume one decimal or hexadecimal integer token, preserving mismatch lookahead. */
inline std::expected<PtxCstParser::TokenId, CstParseDiagnostic>
PtxCstParser::expectIntegerLiteral(std::string_view name) {
  const TokenId id = peek();
  if (cst_detail::isIntegerLiteral(token(id).kind))
    return consume();
  return std::unexpected(
      CstParseDiagnostic{token(id).range, "expected " + std::string(name)});
}

}  // namespace ptx_frontend
