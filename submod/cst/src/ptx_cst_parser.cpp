#include <ptx_frontend/cst/ptx_cst_parser.hpp>

#include "ptx_parser_driver.hpp"

#include <stdexcept>

namespace ptx_frontend::syntax_cst {

/** Resolve a stable file-owned token identity with checked bounds. */
const PtxToken& CstFile::token(TokenId id) const {
  return tokens.at(id);
}

/** Resolve a nonempty half-open token span into its complete source range. */
SourceRange CstFile::sourceRange(CstTokenRange range) const {
  if (range.first >= range.last || range.last > tokens.size())
    throw std::out_of_range("invalid CST token range");
  return {token(range.first).range.start, token(range.last - 1).range.end};
}

/** Serialize every original token and trivia byte, independently of recovery nodes. */
std::string CstFile::sourceText() const {
  std::string result;
  for (const PtxToken& token : tokens) {
    for (const Trivia& trivia : token.leading_trivia)
      result += trivia.text;
    result += token.text;
  }
  return result;
}

/** Access an instruction fragment root without reinterpreting a module. */
const CstInstruction* CstFile::instruction() const noexcept {
  return std::get_if<CstInstruction>(&root);
}

/** Access a module root without reinterpreting an instruction fragment. */
const CstModule* CstFile::module() const noexcept {
  return std::get_if<CstModule>(&root);
}

}  // namespace ptx_frontend::syntax_cst

namespace ptx_frontend {

/** Keep one scanner and cached source buffer across public entry calls. */
struct PtxCstParser::Impl {
  /** Copy input once; parsing entries never reconstruct or restart the scanner. */
  explicit Impl(std::string_view source) : driver(source) {}
  /** Owns token identities/cursor until a result transfers the complete buffer. */
  detail::PtxParserDriver driver;
};

PtxCstParser::PtxCstParser(std::string_view source)
    : impl_(std::make_unique<Impl>(source)) {}

PtxCstParser::~PtxCstParser() = default;

CstParseResult PtxCstParser::parseInstruction() {
  return impl_->driver.parse(detail::EntryMode::Instruction);
}

CstParseResult PtxCstParser::parseModule() {
  return impl_->driver.parse(detail::EntryMode::Module);
}

}  // namespace ptx_frontend
