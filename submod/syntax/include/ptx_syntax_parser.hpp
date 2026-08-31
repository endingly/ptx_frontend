#pragma once

#include <string>
#include <string_view>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/syntax/ptx_syntax_lower.hpp>

namespace ptx_frontend {

struct SyntaxParseDiagnostic {
  SourceRange range;
  std::string message;
};

using SyntaxParseDiagnostics = DiagnosticCollection<SyntaxParseDiagnostic>;
using SyntaxInstructionParseResult =
    ResultWithDiagnostics<syntax_ast::AstInstruction, SyntaxParseDiagnostic>;
using SyntaxModuleParseResult =
    ResultWithDiagnostics<syntax_ast::AstModule, SyntaxParseDiagnostic>;

/** Convenience facade for source -> CST -> Syntax AST. */
class PtxSyntaxParser {
 public:
  explicit PtxSyntaxParser(std::string_view source);

  SyntaxInstructionParseResult parseInstruction();
  SyntaxModuleParseResult parseModule();

 private:
  std::string source_;
};

}  // namespace ptx_frontend
