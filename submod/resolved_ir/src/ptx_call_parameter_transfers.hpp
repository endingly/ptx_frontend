#pragma once

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

namespace ptx_frontend::resolved_ir::detail {
/** Check the known logical global ceiling from current legacy input records.
 * This checked sum is not a host allocation, padding, or exact launch ABI proof.
 */
ModuleResolveDiagnostics check_legacy_entry_input_sizes(
    const ResolvedModule& module);
/** Check bound transfer identity/adjacency and optional construction-time readonly.
 * Module validators use the shared operand-domain path for readonly causes;
 * construction keeps this guard before publishing a legacy input handoff.
 */
ModuleResolveDiagnostics check_call_parameter_transfers(
    const ResolvedModule& module, bool include_legacy_readonly = true);
}  // namespace ptx_frontend::resolved_ir::detail
