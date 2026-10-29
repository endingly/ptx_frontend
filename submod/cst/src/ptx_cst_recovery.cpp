#include "ptx_cst_parser_detail.hpp"

#include <algorithm>
#include <utility>

namespace ptx_frontend {
using namespace cst_detail;

/** Retain malformed source spans and stop before the selected context boundary. */
PtxCstParser::RecoveryResult PtxCstParser::recover(
    TokenId first, const CstParseDiagnostic& diagnostic,
    RecoveryContext context) {
  using syntax_cst::CstRecoveryKind;
  using syntax_cst::CstRecoveryNode;
  using syntax_cst::CstTokenRange;

  RecoveryResult result;
  TokenId current = peek();
  const auto append_span = [this, &result](CstRecoveryKind kind, TokenId begin,
                                           TokenId end) {
    if (begin >= end)
      return;
    result.nodes.push_back(CstRecoveryNode{
        .kind = kind,
        .expected_kind = std::nullopt,
        .token_range = CstTokenRange{begin, end},
        .range =
            SourceRange{token(begin).range.start, token(end - 1).range.end},
    });
    result.last = end - 1;
  };
  const auto append_inserted = [this, &result](TokenKind expected,
                                               TokenId position) {
    result.nodes.push_back(CstRecoveryNode{
        .kind = CstRecoveryKind::Inserted,
        .expected_kind = expected,
        .token_range = std::nullopt,
        .range = SourceRange{token(position).range.start,
                             token(position).range.start},
    });
  };

  const auto is_anchor = [this, context](TokenId id) {
    const auto kind = token(id).kind;
    if (context == RecoveryContext::FunctionBody)
      return kind == TokenKind::RBrace || isFunctionOrModuleBoundary(kind);
    return classifyModuleSource(token(id)) != ModuleSourceForm::Unsupported;
  };
  const auto stop_at_anchor = [context](TokenKind kind) {
    if (context == RecoveryContext::FunctionBody) {
      if (kind == TokenKind::RBrace)
        return RecoveryStop::RightBrace;
      return RecoveryStop::FunctionBoundary;
    }
    return RecoveryStop::ModuleItem;
  };
  const auto append_missing_semicolon = [&] {
    if (diagnostic.expected_kind != TokenKind::Semicolon)
      return;
    append_inserted(TokenKind::Semicolon, current);
  };

  append_span(CstRecoveryKind::Skipped, first, current);
  if (token(current).kind == TokenKind::Eof) {
    result.nodes.push_back(CstRecoveryNode{
        .kind = CstRecoveryKind::Error,
        .expected_kind = std::nullopt,
        .token_range = std::nullopt,
        .range = token(current).range,
    });
    append_missing_semicolon();
    result.stop = RecoveryStop::Eof;
    return result;
  }

  if ((context == RecoveryContext::FunctionBody || current != first) &&
      is_anchor(current)) {
    append_missing_semicolon();
    result.stop = stop_at_anchor(token(current).kind);
    return result;
  }
  if (context == RecoveryContext::FunctionBody &&
      diagnostic.expected_kind == TokenKind::Semicolon &&
      isFunctionBodyItemStart(token(current).kind)) {
    append_inserted(TokenKind::Semicolon, current);
    result.stop = RecoveryStop::Semicolon;
    return result;
  }

  const TokenId error = consume();
  append_span(CstRecoveryKind::Error, error, error + 1);
  // An offending semicolon already terminates this malformed source item.
  if (token(error).kind == TokenKind::Semicolon) {
    result.stop = RecoveryStop::Semicolon;
    return result;
  }
  const TokenId skipped_first = peek();
  while (token(peek()).kind != TokenKind::Eof && !is_anchor(peek())) {
    const TokenId skipped = consume();
    if (token(skipped).kind == TokenKind::Semicolon) {
      append_span(CstRecoveryKind::Skipped, skipped_first, skipped + 1);
      result.stop = RecoveryStop::Semicolon;
      return result;
    }
  }
  append_span(CstRecoveryKind::Skipped, skipped_first, peek());
  result.stop = token(peek()).kind == TokenKind::Eof
                    ? RecoveryStop::Eof
                    : stop_at_anchor(token(peek()).kind);
  return result;
}

/** Shared body driver; the opener belongs to the caller, the closer to this driver. */
PtxCstParser::BodyOutcome PtxCstParser::parseBody(
    std::vector<syntax_cst::CstFunctionBodyItem>& body,
    CstParseDiagnostics& diagnostics, BlockDepth depth, BodyContext context) {
  const auto missing = [&]() -> BodyOutcome {
    const TokenId anchor = peek();
    diagnostics.push_back(
        CstParseDiagnostic{token(anchor).range,
                           context == BodyContext::Function
                               ? "expected '}' at end of function body"
                               : "expected '}' at end of nested block",
                           TokenKind::RBrace});
    body.emplace_back(syntax_cst::CstRecoveryNode{
        .kind = syntax_cst::CstRecoveryKind::Inserted,
        .expected_kind = TokenKind::RBrace,
        .token_range = std::nullopt,
        .range =
            SourceRange{token(anchor).range.start, token(anchor).range.start},
    });
    return MissingClosingBrace{anchor};
  };
  while (token(peek()).kind != TokenKind::RBrace) {
    if (token(peek()).kind == TokenKind::Eof ||
        isFunctionOrModuleBoundary(token(peek()).kind))
      return missing();
    const TokenId first = peek();
    auto item = parseFunctionBodyItem(diagnostics, depth);
    if (item) {
      body.push_back(std::move(*item));
      continue;
    }
    diagnostics.push_back(item.error());
    auto recovery = recover(first, item.error(), RecoveryContext::FunctionBody);
    for (auto& node : recovery.nodes)
      body.emplace_back(std::move(node));
    if (recovery.stop != RecoveryStop::Semicolon &&
        recovery.stop != RecoveryStop::RightBrace)
      return missing();
  }
  return ClosedBody{consume()};
}

}  // namespace ptx_frontend
