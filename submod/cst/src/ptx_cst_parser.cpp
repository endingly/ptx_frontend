#include "ptx_cst_parser_detail.hpp"

#include <algorithm>
#include <utility>

namespace ptx_frontend {
using namespace cst_detail;

PtxCstParser::PtxCstParser(std::string_view source) : lexer_(source) {}

/** Parse an instruction fragment with fail-fast diagnostics and real EOF trivia. */
CstParseResult PtxCstParser::parseInstruction() {
  auto root = parseInstructionNode();
  if (!root)
    return parseFailure(std::move(root.error()));

  const TokenId eof = peek();
  if (token(eof).kind != TokenKind::Eof) {
    return parseFailure(
        CstParseDiagnostic{token(eof).range, "expected end of input"});
  }
  return {.value = syntax_cst::CstFile{std::move(tokens_), std::move(*root)},
          .diagnostics = {}};
}

}  // namespace ptx_frontend
