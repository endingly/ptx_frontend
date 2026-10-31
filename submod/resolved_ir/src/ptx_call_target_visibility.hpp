#pragma once

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

namespace ptx_frontend::resolved_ir::detail {
/** Require prior lexical declarations for owned direct device-call targets. */
ModuleResolveDiagnostics check_direct_call_visibility(
    const ResolvedModule& module);
}  // namespace ptx_frontend::resolved_ir::detail
