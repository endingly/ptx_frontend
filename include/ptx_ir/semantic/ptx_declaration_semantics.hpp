#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ptx_ir/bind/ptx_symbol_table.hpp"
#include "ptx_ir/source_loc.hpp"
#include "ptx_ir/syntax/ptx_syntax_ast.hpp"

namespace ptx_frontend::declaration_semantics {

enum class DeclarationDiagnosticKind : uint8_t {
  InvalidArrayDimension,
  UnsizedArrayDimension,
  InitializerShapeMismatch,
  ExcessInitializerElements,
  InitializerTypeMismatch,
  InvalidInitializerExpression,
  IncompatibleRedeclaration,
  MultipleDefinitions,
  InvalidLinkage,
};

struct DeclarationDiagnostic {
  DeclarationDiagnosticKind kind{};
  SourceRange range;
  std::optional<SourceRange> previous_range;
  std::string message;
};

/**
 * Check declaration rules that require structured AST or cross-declaration
 * information. Lexical name lookup remains the responsibility of binding.
 */
[[nodiscard]] std::vector<DeclarationDiagnostic> checkDeclarations(
    const syntax_ast::AstModule& module, const binding::SymbolTable& symbols);

}  // namespace ptx_frontend::declaration_semantics
