#include "ptx_module_instruction_append.hpp"

#include <utility>

namespace ptx_frontend::resolved_ir::detail {

std::optional<ResolveDiagnostic> resolve_and_append_instruction(
    std::vector<ResolvedInstruction>& body,
    const syntax_ast::AstInstruction& instruction,
    const ResolveContext& context, BeforeAppendInstruction before_append,
    void* user_data) {
  auto resolved = resolveInstruction(instruction, context);
  if (!resolved)
    return std::move(resolved.error());
  if (before_append)
    before_append(*resolved, user_data);
  body.push_back(std::move(*resolved));
  return std::nullopt;
}

}  // namespace ptx_frontend::resolved_ir::detail
