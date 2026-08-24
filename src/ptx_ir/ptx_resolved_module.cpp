#include "ptx_ir/resolved/ptx_resolved_ir.hpp"

#include "ptx_ir/semantic/ptx_declaration_semantics.hpp"

#include <utility>

namespace ptx_frontend::resolved_ir {

std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveModule(
    const syntax_ast::AstModule& ast) {
  binding::SymbolBinding binding_result = binding::bindSymbols(ast);

  ModuleResolveDiagnostics diagnostics;
  diagnostics.reserve(binding_result.diagnostics.size());
  for (const binding::BindDiagnostic& diagnostic : binding_result.diagnostics) {
    diagnostics.push_back(ResolveDiagnostic{
        .range = diagnostic.range,
        .message = diagnostic.message,
    });
  }
  for (const auto& diagnostic :
       declaration_semantics::checkDeclarations(ast, binding_result.table)) {
    diagnostics.push_back(ResolveDiagnostic{
        .range = diagnostic.range,
        .message = diagnostic.message,
    });
  }
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));

  std::vector<ResolvedFunction> functions;
  for (const syntax_ast::AstModuleItem& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (function == nullptr)
      continue;

    const auto lookup = binding_result.table.lookup(
        binding_result.table.moduleScope(), function->name.syntax.text);
    if (!lookup) {
      throw ResolveException(
          "Bound module has no symbol for a syntax function.");
    }
    const binding::Symbol& symbol = binding_result.table.symbol(lookup->symbol);
    if (symbol.kind != binding::SymbolKind::Function || !symbol.owned_scope) {
      throw ResolveException(
          "Bound function symbol has no associated function scope.");
    }

    ResolveContext context{
        .symbols = binding_result.table,
        .scope = *symbol.owned_scope,
        .function_is_entry = function->is_entry,
    };
    ResolvedFunction resolved_function{
        .symbol_id = symbol.id,
        .name = symbol.name,
        .is_entry = function->is_entry,
        .is_prototype = function->is_prototype,
        .range = function->range,
    };
    for (const syntax_ast::AstFunctionBodyItem& body_item : function->body) {
      const auto* instruction =
          std::get_if<syntax_ast::AstInstruction>(&body_item);
      if (instruction == nullptr)
        continue;

      auto resolved = resolveInstruction(*instruction, context);
      if (!resolved) {
        diagnostics.push_back(std::move(resolved.error()));
        continue;
      }
      resolved_function.body.push_back(std::move(*resolved));
    }
    functions.push_back(std::move(resolved_function));
  }

  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  return ResolvedModule{
      .symbols = std::move(binding_result.table),
      .functions = std::move(functions),
      .range = ast.range,
  };
}

}  // namespace ptx_frontend::resolved_ir
