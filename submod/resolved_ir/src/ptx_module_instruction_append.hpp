#pragma once

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include <optional>
#include <vector>

namespace ptx_frontend::resolved_ir::detail {

/** Synchronous, borrowed callback that may inspect and update an owned instruction. */
using BeforeAppendInstruction = void (*)(OwnedInstruction&, void*);

/**
 * Resolve one instruction and append it after the caller's synchronous callback.
 *
 * The callback and its context are borrowed only for this call. A resolution
 * failure skips both the callback and append and returns an owned diagnostic.
 * Callback diagnostics do not prevent appending the successfully resolved
 * instruction; exceptions propagate to the caller.
 */
std::optional<ResolveDiagnostic> resolve_and_append_instruction(
    std::vector<OwnedInstruction>& body,
    const syntax_ast::AstInstruction& instruction,
    const ResolveContext& context, BeforeAppendInstruction before_append,
    void* user_data);

}  // namespace ptx_frontend::resolved_ir::detail
