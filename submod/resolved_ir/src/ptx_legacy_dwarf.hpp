#pragma once

#include <optional>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>
#include <span>
#include <vector>

namespace ptx_frontend::resolved_ir::detail {
/** Copy lexical metadata order with actual module/definition/block declaration scopes. */
std::vector<ResolvedLegacyDwarfRecord> legacy_dwarf_records(
    const syntax_ast::AstModule& ast, const binding::SymbolTable& symbols);
/** Check current typed serialization and actual owning definition occurrences. */
checker::CheckDiagnostics check_legacy_dwarf(
    const ResolvedModule& module, std::optional<checker::PtxVersion> version);
/** Check supplied serialization using only its freshly rebound lexical scope table. */
checker::CheckDiagnostics check_legacy_dwarf(
    const syntax_ast::AstModule& ast, const binding::SymbolTable& symbols,
    std::span<const ResolvedLegacyDwarfRecord> records,
    std::optional<checker::PtxVersion> version);
}  // namespace ptx_frontend::resolved_ir::detail
