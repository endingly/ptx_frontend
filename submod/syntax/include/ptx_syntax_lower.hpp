#pragma once

#include <expected>
#include <string>

#include <ptx_frontend/cst/ptx_cst.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend {

struct AstLowerDiagnostic {
  SourceRange range;
  std::string message;
};

/** Project a lossless CST instruction into the resolution-oriented AST. */
std::expected<syntax_ast::AstInstruction, AstLowerDiagnostic>
lowerSyntaxInstruction(const syntax_cst::CstFile& cst);

/** Project a lossless CST module into its semantic syntax representation. */
std::expected<syntax_ast::AstModule, AstLowerDiagnostic> lowerSyntaxModule(
    const syntax_cst::CstFile& cst);

}  // namespace ptx_frontend
