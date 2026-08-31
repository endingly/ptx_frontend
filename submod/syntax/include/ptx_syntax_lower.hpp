#pragma once

#include <string>

#include <ptx_frontend/cst/ptx_cst.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend {

struct AstLowerDiagnostic {
  SourceRange range;
  std::string message;
};

using AstLowerDiagnostics = DiagnosticCollection<AstLowerDiagnostic>;
using AstInstructionLowerResult =
    ResultWithDiagnostics<syntax_ast::AstInstruction, AstLowerDiagnostic>;
using AstModuleLowerResult =
    ResultWithDiagnostics<syntax_ast::AstModule, AstLowerDiagnostic>;

/** Project a lossless CST instruction into the resolution-oriented AST. */
AstInstructionLowerResult lowerSyntaxInstruction(
    const syntax_cst::CstFile& cst);

/** Project a lossless CST module into its semantic syntax representation. */
AstModuleLowerResult lowerSyntaxModule(const syntax_cst::CstFile& cst);

}  // namespace ptx_frontend
