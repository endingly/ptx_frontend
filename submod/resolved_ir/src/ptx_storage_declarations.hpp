#pragma once

#include <expected>
#include <vector>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::resolved_ir {

/**
 * Resolve source storage declarations into owned metadata after binding and
 * declaration validation have succeeded.
 */
[[nodiscard]] std::expected<
    std::vector<ResolvedStorageDeclaration>,
    std::vector<declaration_semantics::DeclarationDiagnostic>>
resolve_storage_declarations(const syntax_ast::AstModule& module,
                             const binding::SymbolTable& symbols);

}  // namespace ptx_frontend::resolved_ir
