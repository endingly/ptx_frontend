#pragma once

#include "test_module_projection.hpp"

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

namespace ptx_frontend::resolved_ir::test_support {
namespace detail {

/** Copy storage facts shared by ordinary and typed module test projections. */
std::vector<StorageSnapshot> projectStorage(const ResolvedModule& module);

}  // namespace detail

template <PtxOperator... Instructions>
std::expected<TypedModuleSnapshot<Instructions...>,
              std::vector<ResolveDiagnostic>>
resolveTypedModule(const syntax_ast::AstModule& ast, ModulePipeline pipeline) {
  std::expected<ResolvedModule, ModuleResolveDiagnostics> resolved =
      [&]() -> std::expected<ResolvedModule, ModuleResolveDiagnostics> {
    switch (pipeline) {
      case ModulePipeline::ResolveOnly:
        return resolveModuleOnly(ast);
      case ModulePipeline::AvailableContext:
        return resolveModule(ast);
      case ModulePipeline::CompleteContext:
        return resolveAndValidateModule(ast);
    }
    __builtin_unreachable();
  }();
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));

  TypedModuleSnapshot<Instructions...> snapshot;
  snapshot.symbols = resolved->symbols;
  snapshot.storage_declarations = detail::projectStorage(*resolved);
  snapshot.functions.reserve(resolved->functions.size());
  for (const auto& function : resolved->functions) {
    TypedFunctionSnapshot<Instructions...> projected;
    projected.symbol_id = function.symbol_id;
    projected.name = function.name;
    projected.instruction_ranges = function.instruction_ranges;
    projected.body.reserve(function.body.size());
    for (const auto& instruction : function.body) {
      std::variant<std::monostate, Instructions...> selected;
      (
          [&] {
            if (const auto* value = std::get_if<Instructions>(&instruction))
              selected = *value;
          }(),
          ...);
      projected.body.push_back(std::move(selected));
    }
    snapshot.functions.push_back(std::move(projected));
  }
  return snapshot;
}

}  // namespace ptx_frontend::resolved_ir::test_support
