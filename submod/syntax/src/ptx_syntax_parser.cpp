#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend {
namespace {

template <typename D>
SyntaxParseDiagnostics mapDiagnostics(
    const DiagnosticCollection<D>& diagnostics) {
  SyntaxParseDiagnostics mapped;
  mapped.reserve(diagnostics.size());
  for (const auto& diagnostic : diagnostics) {
    mapped.push_back(
        SyntaxParseDiagnostic{diagnostic.range, diagnostic.message});
  }
  return mapped;
}

}  // namespace

PtxSyntaxParser::PtxSyntaxParser(std::string_view source) : source_(source) {}

SyntaxInstructionParseResult PtxSyntaxParser::parseInstruction() {
  PtxCstParser parser(source_);
  auto cst = parser.parseInstruction();
  if (!cst || !cst.diagnostics.empty()) {
    return {.value = std::nullopt,
            .diagnostics = mapDiagnostics(cst.diagnostics)};
  }

  auto ast = lowerSyntaxInstruction(*cst);
  SyntaxParseDiagnostics diagnostics = mapDiagnostics(cst.diagnostics);
  const auto lowered_diagnostics = mapDiagnostics(ast.diagnostics);
  diagnostics.insert(diagnostics.end(), lowered_diagnostics.begin(),
                     lowered_diagnostics.end());
  if (!ast) {
    return {.value = std::nullopt, .diagnostics = std::move(diagnostics)};
  }
  return {.value = std::move(*ast), .diagnostics = std::move(diagnostics)};
}

SyntaxModuleParseResult PtxSyntaxParser::parseModule() {
  PtxCstParser parser(source_);
  auto cst = parser.parseModule();
  if (!cst) {
    return {.value = std::nullopt,
            .diagnostics = mapDiagnostics(cst.diagnostics)};
  }

  auto ast = lowerSyntaxModule(*cst);
  SyntaxParseDiagnostics diagnostics = mapDiagnostics(cst.diagnostics);
  const auto lowered_diagnostics = mapDiagnostics(ast.diagnostics);
  diagnostics.insert(diagnostics.end(), lowered_diagnostics.begin(),
                     lowered_diagnostics.end());
  if (!ast) {
    return {.value = std::nullopt, .diagnostics = std::move(diagnostics)};
  }
  return {.value = std::move(*ast), .diagnostics = std::move(diagnostics)};
}

}  // namespace ptx_frontend
