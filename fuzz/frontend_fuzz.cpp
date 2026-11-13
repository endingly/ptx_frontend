#include "frontend_fuzz.hpp"

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::fuzz {
namespace {
/** Turn a violated successful-resolution invariant into a reproducible crash. */
void require(bool condition) {
  if (!condition)
    std::abort();
}
/** Parse with a private source copy; the returned AST must own all spelling. */
SyntaxModuleParseResult parseOwnedModule(const uint8_t* data,
                                         std::size_t size) {
  const std::string source(size == 0 ? "" : reinterpret_cast<const char*>(data),
                           size);
  PtxSyntaxParser parser(source);
  return parser.parseModule();
}
/** Exercise standalone syntax and resolution without imposing module context. */
void exerciseInstruction(const uint8_t* data, std::size_t size) {
  auto ast = [&] {
    const std::string source(
        size == 0 ? "" : reinterpret_cast<const char*>(data), size);
    PtxSyntaxParser parser(source);
    return parser.parseInstruction();
  }();
  if (ast && ast.diagnostics.empty())
    (void)resolved_ir::resolveInstruction(*ast);
}
}  // namespace

FrontendStages exerciseFrontend(const uint8_t* data, std::size_t size) {
  FrontendStages stages;
  if (size > 64 * 1024) {
    stages.oversized = true;
    return stages;
  }
  exerciseInstruction(data, size);
  std::optional<resolved_ir::ResolvedModule> owned;
  {
    auto ast = parseOwnedModule(data, size);
    if (!ast || !ast.diagnostics.empty())
      return stages;
    stages.clean_module = true;
    auto result = resolved_ir::resolveAndValidateModule(*ast);
    if (!result)
      return stages;
    stages.strict_success = true;
    require(static_cast<bool>(resolved_ir::validateModule(
        *ast, *result,
        resolved_ir::ModuleValidationPolicy::RequireCompleteContext)));
    owned.emplace(std::move(*result));
  }
  // Every check below runs after both source and AST destruction.
  constexpr auto policy =
      resolved_ir::ModuleValidationPolicy::RequireCompleteContext;
  require(static_cast<bool>(resolved_ir::validateModule(*owned, policy)));
  resolved_ir::ResolvedModule copied(*owned);
  owned.reset();
  require(static_cast<bool>(resolved_ir::validateModule(copied, policy)));
  resolved_ir::ResolvedModule assigned;
  assigned = copied;
  copied = {};
  require(static_cast<bool>(resolved_ir::validateModule(assigned, policy)));
  resolved_ir::ResolvedModule moved(std::move(assigned));
  assigned = {};
  require(static_cast<bool>(resolved_ir::validateModule(moved, policy)));
  resolved_ir::ResolvedModule move_assigned;
  move_assigned = std::move(moved);
  moved = {};
  require(
      static_cast<bool>(resolved_ir::validateModule(move_assigned, policy)));
  for (auto& function : move_assigned.functions) {
    ++stages.functions_checked;
    resolved_ir::ResolvedFunction function_copy(function), function_assigned;
    function_assigned = function_copy;
    function_copy = {};
    resolved_ir::ResolvedFunction function_moved(std::move(function_assigned));
    function = std::move(function_moved);
    for (auto& instruction : function.body) {
      require(instruction != nullptr);
      const auto kind = instruction->instruction_kind();
      auto clone = instruction->clone();
      require(clone && clone.get() != instruction.get() &&
              clone->instruction_kind() == kind);
      instruction = std::move(clone);
      ++stages.cloned_instructions;
    }
  }
  require(
      static_cast<bool>(resolved_ir::validateModule(move_assigned, policy)));
  stages.ownership_checked = true;
  return stages;
}
}  // namespace ptx_frontend::fuzz
