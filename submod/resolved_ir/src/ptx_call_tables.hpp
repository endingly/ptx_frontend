#pragma once

#include <expected>
#include <string>
#include <unordered_map>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>

namespace ptx_frontend::resolved_ir::detail {
/** Synchronous signature lookup keyed by bound or canonical function identity. */
using CallTableSignatures =
    std::unordered_map<uint32_t, declaration_semantics::FunctionSignature>;

/** Reconstruct a consumed table from actual owned storage and bound references. */
std::expected<ResolvedCallTableContract, std::string> build_call_table(
    const ResolvedStorageDeclaration& storage,
    const binding::SymbolTable& symbols, const CallTableSignatures& signatures);
}  // namespace ptx_frontend::resolved_ir::detail
