#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "ptx_ir/cst/ptx_cst_parser.hpp"
#include "ptx_ir/syntax/ptx_syntax_lower.hpp"

namespace ptx_frontend {

struct SyntaxParseDiagnostic {
  SourceRange range;
  std::string message;
};

/** Convenience facade for source -> CST -> Syntax AST. */
class PtxSyntaxParser {
 public:
  explicit PtxSyntaxParser(std::string_view source);

  std::expected<syntax_ast::AstInstruction, SyntaxParseDiagnostic>
  parseInstruction();
  std::expected<syntax_ast::AstModule, SyntaxParseDiagnostic> parseModule();

 private:
  std::string source_;
};

}  // namespace ptx_frontend
