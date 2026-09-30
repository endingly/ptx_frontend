#pragma once

#include "test_module_projection.hpp"

#include <functional>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

namespace ptx_frontend::resolved_ir::test_support {
namespace detail {

/** Copy storage facts shared by ordinary and typed module test projections. */
std::vector<StorageSnapshot> projectStorage(const ResolvedModule& module);

/** Resolve and destroy the full module outside typed projection instantiations. */
std::expected<void, std::vector<ResolveDiagnostic>> withResolvedModule(
    const syntax_ast::AstModule& ast, ModulePipeline pipeline,
    const std::function<void(const ResolvedModule&)>& project);

}  // namespace detail

template <PtxOperator... Instructions>
std::expected<TypedModuleSnapshot<Instructions...>,
              std::vector<ResolveDiagnostic>>
resolveTypedModule(const syntax_ast::AstModule& ast, ModulePipeline pipeline) {
  TypedModuleSnapshot<Instructions...> snapshot;
  auto status = detail::withResolvedModule(
      ast, pipeline, [&](const ResolvedModule& module) {
        snapshot.symbols = module.symbols;
        snapshot.storage_declarations = detail::projectStorage(module);
        snapshot.functions.reserve(module.functions.size());
        for (const auto& function : module.functions) {
          TypedFunctionSnapshot<Instructions...> projected;
          projected.symbol_id = function.symbol_id;
          projected.name = function.name;
          projected.instruction_ranges = function.instruction_ranges;
          projected.body.reserve(function.body.size());
          for (const auto& instruction : function.body) {
            std::variant<std::monostate, Instructions...> selected;
            (
                [&] {
                  if (const auto* value =
                          std::get_if<Instructions>(&instruction))
                    selected = *value;
                }(),
                ...);
            projected.body.push_back(std::move(selected));
          }
          snapshot.functions.push_back(std::move(projected));
        }
      });
  if (!status)
    return std::unexpected(std::move(status.error()));
  return snapshot;
}

}  // namespace ptx_frontend::resolved_ir::test_support
