#include "test_module_snapshot.hpp"
#include "test_module_projection.hpp"

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir::test_support {
namespace {

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

/** Copy only the function and storage facts used by module acceptance tests. */
ModuleSnapshot project(const ResolvedModule& module) {
  ModuleSnapshot snapshot;
  snapshot.symbols = module.symbols;
  snapshot.functions.reserve(module.functions.size());
  for (const auto& function : module.functions)
    snapshot.functions.push_back({function.symbol_id, function.name,
                                  function.is_prototype, function.body.size(),
                                  function.parameter_declarations});
  snapshot.storage_declarations = projectStorage(module);
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

namespace ptx_frontend::resolved_ir::test_support {

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
  snapshot.storage_declarations = projectStorage(*resolved);
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

template std::expected<TypedModuleSnapshot<Mov>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Mov>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Ld>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Ld>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<St>, std::vector<ResolveDiagnostic>>
resolveTypedModule<St>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Bar>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Bar>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Brx>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Brx>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Mul>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Mul>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Add>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Add>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Set>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Set>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Setp>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Setp>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Selp>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Selp>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Slct>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Slct>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Div>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Div>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Abs, Neg>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Abs, Neg>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Rcp, Sqrt, Rsqrt>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Rcp, Sqrt, Rsqrt>(const syntax_ast::AstModule&,
                                     ModulePipeline);
template std::expected<TypedModuleSnapshot<Mov, Add>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Mov, Add>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<
    TypedModuleSnapshot<Barrier, Clusterlaunchcontrol, Fence, Mbarrier>,
    std::vector<ResolveDiagnostic>>
resolveTypedModule<Barrier, Clusterlaunchcontrol, Fence, Mbarrier>(
    const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Atom, Cp, Ldmatrix, Mma, Vote>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Atom, Cp, Ldmatrix, Mma, Vote>(const syntax_ast::AstModule&,
                                                  ModulePipeline);
template std::expected<TypedModuleSnapshot<Call, Ld, St>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Call, Ld, St>(const syntax_ast::AstModule&, ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
