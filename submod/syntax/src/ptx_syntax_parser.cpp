#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend {

PtxSyntaxParser::PtxSyntaxParser(std::string_view source) : source_(source) {}

std::expected<syntax_ast::AstInstruction, SyntaxParseDiagnostic>
PtxSyntaxParser::parseInstruction() {
  PtxCstParser parser(source_);
  auto cst = parser.parseInstruction();
  if (!cst) {
    return std::unexpected(
        SyntaxParseDiagnostic{cst.error().range, cst.error().message});
  }

  auto ast = lowerSyntaxInstruction(*cst);
  if (!ast) {
    return std::unexpected(
        SyntaxParseDiagnostic{ast.error().range, ast.error().message});
  }
  return std::move(*ast);
}

std::expected<syntax_ast::AstModule, SyntaxParseDiagnostic>
PtxSyntaxParser::parseModule() {
  PtxCstParser parser(source_);
  auto cst = parser.parseModule();
  if (!cst) {
    return std::unexpected(
        SyntaxParseDiagnostic{cst.error().range, cst.error().message});
  }

  auto ast = lowerSyntaxModule(*cst);
  if (!ast) {
    return std::unexpected(
        SyntaxParseDiagnostic{ast.error().range, ast.error().message});
  }
  return std::move(*ast);
}

}  // namespace ptx_frontend
