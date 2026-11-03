#pragma once

#include <optional>
#include <span>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>

namespace ptx_frontend::resolved_ir::detail {
/** Copy source-ordered owning section values without retaining AST borrows. */
std::vector<ResolvedDebugSection> debug_sections(
    const syntax_ast::AstModule& ast);
/** Check original numeric leaves, typed productions, ranges and logical locality. */
checker::CheckDiagnostics check_debug_sections(
    std::span<const ResolvedDebugSection> sections,
    std::optional<checker::PtxVersion> version, SourceRange module_range);
/** Rejoin current function-name references and prove known ASCII/NUL byte prefixes.
 * Numeric words contribute order-independent byte facts; ambiguous ordering or linked
 * symbolic bytes remain not-proven. No serialization or dense byte image is guessed.
 */
checker::CheckDiagnostics check_debug_section_references(
    std::span<const ResolvedDebugSection> sections,
    const binding::SymbolTable& symbols);
}  // namespace ptx_frontend::resolved_ir::detail
