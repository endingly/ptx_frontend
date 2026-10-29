#include <ptx_frontend/cst/ptx_cst.hpp>

#include <stdexcept>

namespace ptx_frontend::syntax_cst {

const PtxToken& CstFile::token(TokenId id) const {
  return tokens.at(id);
}

SourceRange CstFile::sourceRange(CstTokenRange range) const {
  if (range.first >= range.last || range.last > tokens.size())
    throw std::out_of_range("invalid CST token range");
  return SourceRange{token(range.first).range.start,
                     token(range.last - 1).range.end};
}

std::string CstFile::sourceText() const {
  std::string result;
  for (const PtxToken& token : tokens) {
    for (const Trivia& trivia : token.leading_trivia)
      result += trivia.text;
    result += token.text;
  }
  return result;
}

const CstInstruction* CstFile::instruction() const noexcept {
  return std::get_if<CstInstruction>(&root);
}

const CstModule* CstFile::module() const noexcept {
  return std::get_if<CstModule>(&root);
}

}  // namespace ptx_frontend::syntax_cst
