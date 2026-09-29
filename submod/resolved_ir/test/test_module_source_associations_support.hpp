#pragma once

#include <gtest/gtest.h>

#include <optional>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir::module_source_test_support {

/** Parse a complete module and retain a useful failure in the test output. */
inline std::optional<syntax_ast::AstModule> parseModule(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    ADD_FAILURE() << (parsed.diagnostics.empty()
                          ? "PTX source did not parse."
                          : parsed.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*parsed);
}

/** Test whether a resolution diagnostic carries the requested checker kind. */
inline bool hasCheckerKind(const ModuleResolveDiagnostics& diagnostics,
                           checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.checker_kind && *diagnostic.checker_kind == kind)
      return true;
  }
  return false;
}

/** Test whether a checker result carries the requested diagnostic kind. */
inline bool hasCheckerKind(const checker::CheckDiagnostics& diagnostics,
                           checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.kind == kind)
      return true;
  }
  return false;
}

}  // namespace ptx_frontend::resolved_ir::module_source_test_support
