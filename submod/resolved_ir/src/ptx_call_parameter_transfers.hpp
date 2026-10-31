#pragma once

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

namespace ptx_frontend::resolved_ir::detail {
/** Check bound parameter-transfer identity and adjacency over owned instructions. */
ModuleResolveDiagnostics check_call_parameter_transfers(
    const ResolvedModule& module);
}  // namespace ptx_frontend::resolved_ir::detail
