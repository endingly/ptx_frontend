#pragma once

#include <optional>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>

namespace ptx_frontend::resolved_ir::detail {
/** Validate current ordered locations against actual owned definition occurrences. */
checker::CheckDiagnostics check_debug_locations(
    const binding::SymbolTable& symbols, const ResolvedModule& module,
    std::optional<checker::PtxVersion> version);
/** Validate replacement locations against that source's own definition occurrences. */
checker::CheckDiagnostics check_debug_locations(
    const binding::SymbolTable& symbols, const syntax_ast::AstModule& ast,
    std::optional<checker::PtxVersion> version);
}  // namespace ptx_frontend::resolved_ir::detail
