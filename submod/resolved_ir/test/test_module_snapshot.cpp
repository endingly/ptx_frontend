#include "test_module_snapshot.hpp"
#include "test_module_projection_detail.hpp"

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir::test_support {
namespace detail {

/** Copy storage facts without exposing the full module instruction variant. */
std::vector<StorageSnapshot> projectStorage(const ResolvedModule& module) {
  std::vector<StorageSnapshot> projected;
  projected.reserve(module.storage_declarations.size());
  for (const auto& storage : module.storage_declarations)
    projected.push_back({
        .name = module.symbols.symbol(storage.symbol_id).name,
        .initializer_count = storage.initializer.size(),
        .first_constant_bits = [&]() -> std::optional<uint64_t> {
          if (storage.initializer.empty())
            return std::nullopt;
          if (const auto* value = std::get_if<StorageConstant>(
                  &storage.initializer.front().value))
            return value->bits;
          return std::nullopt;
        }(),
    });
  return projected;
}

}  // namespace detail

namespace {

/** Copy only the function and storage facts used by module acceptance tests. */
ModuleSnapshot project(const ResolvedModule& module) {
  ModuleSnapshot snapshot;
  snapshot.symbols = module.symbols;
  snapshot.functions.reserve(module.functions.size());
  for (const auto& function : module.functions)
    snapshot.functions.push_back({function.symbol_id, function.name,
                                  function.is_prototype, function.body.size(),
                                  function.parameter_declarations});
  snapshot.storage_declarations = detail::projectStorage(module);
  snapshot.storage_metadata = module.storage_declarations;
  return snapshot;
}

}  // namespace

std::expected<ModuleSnapshot, std::vector<ResolveDiagnostic>>
resolveModuleSnapshot(const syntax_ast::AstModule& ast) {
  auto resolved = resolveModule(ast);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  return project(*resolved);
}

std::expected<ModuleSnapshot, std::vector<ResolveDiagnostic>>
resolveAndValidateModuleSnapshot(const syntax_ast::AstModule& ast) {
  auto resolved = resolveAndValidateModule(ast);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  return project(*resolved);
}

std::expected<checker::CheckResult, std::vector<ResolveDiagnostic>>
resolveOnlyAndCheckModule(const syntax_ast::AstModule& ast) {
  auto resolved = resolveModuleOnly(ast);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  return validateModule(*resolved);
}

std::expected<ModuleSnapshot, std::vector<ResolveDiagnostic>>
resolveAndCheckAvailableModuleSnapshot(const syntax_ast::AstModule& ast) {
  auto resolved = resolveModule(ast);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  const auto checked = checkModuleAvailability(ast, *resolved);
  if (!checked) {
    std::vector<ResolveDiagnostic> diagnostics;
    diagnostics.reserve(checked.error().size());
    for (const auto& diagnostic : checked.error())
      diagnostics.push_back({.range = diagnostic.range,
                             .message = diagnostic.message,
                             .checker_kind = diagnostic.kind});
    return std::unexpected(std::move(diagnostics));
  }
  return project(*resolved);
}

std::expected<ModuleSnapshot, std::vector<ResolveDiagnostic>>
resolveAndCheckInstructionSnapshot(const syntax_ast::AstModule& ast,
                                   const checker::Context& context) {
  auto resolved = resolveModule(ast);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  for (const auto& function : resolved->functions) {
    for (const auto& instruction : function.body) {
      const auto checked = std::visit(
          [&](const auto& value) { return checker::check(value, context); },
          instruction);
      if (!checked) {
        std::vector<ResolveDiagnostic> diagnostics;
        diagnostics.reserve(checked.error().size());
        for (const auto& diagnostic : checked.error())
          diagnostics.push_back({.range = diagnostic.range,
                                 .message = diagnostic.message,
                                 .checker_kind = diagnostic.kind});
        return std::unexpected(std::move(diagnostics));
      }
    }
  }
  return project(*resolved);
}

std::expected<RetargetedAvailability, std::vector<ResolveDiagnostic>>
checkRetargetedModuleAvailability(const syntax_ast::AstModule& original,
                                  const syntax_ast::AstModule& retargeted) {
  auto resolved = resolveModule(original);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  return RetargetedAvailability{
      .checked = checkModuleAvailability(retargeted, *resolved),
      .original_instruction_range =
          resolved->functions.front().instruction_ranges.front(),
  };
}

std::expected<UnifiedLoadMutationCheck, std::vector<ResolveDiagnostic>>
checkUnifiedLoadMutation(const syntax_ast::AstModule& ast) {
  auto resolved = resolveModule(ast);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  auto before = validateModule(*resolved);
  auto& load = std::get<Ld>(resolved->functions.front().body.front());
  std::get<Ld::ExplicitScalar>(load.variant).address.value.unified = false;
  return UnifiedLoadMutationCheck{std::move(before), validateModule(*resolved)};
}

std::expected<OwnedMutationCheck, std::vector<ResolveDiagnostic>>
checkOwnedModuleMutation(std::string source, OwnedMutationScenario scenario) {
  // The moved source, parser, and syntax tree die before either validation.
  auto resolved =
      [&]() -> std::expected<ResolvedModule, ModuleResolveDiagnostics> {
    std::string owned_source = std::move(source);
    PtxSyntaxParser parser(owned_source);
    auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::unexpected(ModuleResolveDiagnostics{{
          .message = ast.diagnostics.empty() ? "PTX source did not parse."
                                             : ast.diagnostics.front().message,
      }});
    return resolveModuleOnly(*ast);
  }();
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));

  auto before =
      validateModule(*resolved, ModuleValidationPolicy::RequireCompleteContext);
  auto& body = resolved->functions.front().body;
  switch (scenario) {
    case OwnedMutationScenario::DivSourceWidth: {
      auto& f32 = std::get<Div::RnF32>(std::get<Div>(body[0]).variant);
      f32.src2.value =
          std::get<Div::RnF64>(std::get<Div>(body[1]).variant).src2.value;
      break;
    }
    case OwnedMutationScenario::AbsSourceWidth: {
      auto& f32 = std::get<Abs::F32>(std::get<Abs>(body[0]).variant);
      f32.src.value =
          std::get<Neg::F64>(std::get<Neg>(body[1]).variant).src.value;
      break;
    }
    case OwnedMutationScenario::RcpSourceWidth: {
      auto& f32 = std::get<Rcp::RnF32>(std::get<Rcp>(body[0]).variant);
      f32.src.value =
          std::get<Rcp::RnF64>(std::get<Rcp>(body[1]).variant).src.value;
      break;
    }
  }
  return OwnedMutationCheck{
      std::move(before),
      validateModule(*resolved, ModuleValidationPolicy::RequireCompleteContext),
  };
}

}  // namespace ptx_frontend::resolved_ir::test_support
