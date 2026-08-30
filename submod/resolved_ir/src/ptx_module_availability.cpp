#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>

#include <fmt/format.h>

namespace ptx_frontend::resolved_ir {
namespace {

std::optional<checker::PtxVersion> module_version(
    const syntax_ast::AstModule& module) {
  for (const auto& item : module.items) {
    const auto* version = std::get_if<syntax_ast::AstVersionDirective>(&item);
    if (version == nullptr)
      continue;
    const std::string_view text = version->version.text;
    const size_t dot = text.find('.');
    if (dot == std::string_view::npos)
      return std::nullopt;
    checker::PtxVersion result;
    const auto parse = [](std::string_view value, uint16_t& output) {
      const auto [end, error] =
          std::from_chars(value.data(), value.data() + value.size(), output);
      return !value.empty() && error == std::errc{} &&
             end == value.data() + value.size();
    };
    if (!parse(text.substr(0, dot), result.major) ||
        !parse(text.substr(dot + 1), result.minor))
      return std::nullopt;
    return result;
  }
  return std::nullopt;
}

checker::AvailabilityDescriptor availability(checker::PtxVersion minimum_ptx,
                                             uint32_t minimum_sm = 0,
                                             std::string_view capability = {}) {
  checker::AvailabilityDescriptor result{
      .minimum_ptx_version = minimum_ptx,
      .minimum_sm_version = minimum_sm,
  };
  if (capability.empty())
    return result;
  result.any_of[0] = checker::AvailabilityClause{
      .minimum_ptx_version = minimum_ptx,
      .minimum_sm_version = minimum_sm,
      .capabilities = {capability},
      .capability_count = 1,
  };
  result.any_of_count = 1;
  return result;
}

enum class DirectiveAvailability {
  Alias,
  Noreturn,
  AbiPreserve,
  AbiPreserveControl,
};

checker::AvailabilityDescriptor directive_availability(
    DirectiveAvailability directive) {
  switch (directive) {
    case DirectiveAvailability::Alias:
      return availability({6, 3}, 30);
    case DirectiveAvailability::Noreturn:
      return availability({6, 4}, 30);
    case DirectiveAvailability::AbiPreserve:
    case DirectiveAvailability::AbiPreserveControl:
      return availability({9, 0}, 80);
  }
  return {};
}

void append_requirement(checker::CheckDiagnostics& diagnostics,
                        const checker::AvailabilityDescriptor& requirement,
                        const checker::TargetInfo& target, SourceRange range,
                        std::string_view subject) {
  if (checker::is_available(requirement, target))
    return;
  diagnostics.push_back(checker::CheckDiagnostic{
      .kind = checker::CheckDiagnosticKind::UnsupportedAvailability,
      .range = range,
      .message = fmt::format("{} is unavailable for module target '{}'.",
                             subject, target.identity->source_spelling),
  });
}

checker::AvailabilityDescriptor resource_availability(
    syntax_ast::AstKernelResourceKind kind) {
  using Kind = syntax_ast::AstKernelResourceKind;
  switch (kind) {
    case Kind::MaxNreg:
    case Kind::MaxNtid:
      return availability({1, 3});
    case Kind::ReqNtid:
      return availability({2, 1});
    case Kind::MinNctaPerSm:
      return availability({2, 0});
    case Kind::ReqNctaPerCluster:
    case Kind::ExplicitCluster:
    case Kind::MaxClusterRank:
      return availability({7, 8}, 90, "cluster");
  }
  return {};
}

std::string_view resource_name(syntax_ast::AstKernelResourceKind kind) {
  using Kind = syntax_ast::AstKernelResourceKind;
  switch (kind) {
    case Kind::MaxNreg:
      return ".maxnreg";
    case Kind::MaxNtid:
      return ".maxntid";
    case Kind::ReqNtid:
      return ".reqntid";
    case Kind::MinNctaPerSm:
      return ".minnctapersm";
    case Kind::ReqNctaPerCluster:
      return ".reqnctapercluster";
    case Kind::ExplicitCluster:
      return ".explicitcluster";
    case Kind::MaxClusterRank:
      return ".maxclusterrank";
  }
  return "kernel resource";
}

void check_attributes(const std::vector<syntax_ast::AstAttribute>& attributes,
                      const checker::TargetInfo& target,
                      checker::CheckDiagnostics& diagnostics) {
  for (const auto& attribute : attributes) {
    const bool managed =
        attribute.kind == syntax_ast::AstAttributeKind::Managed;
    append_requirement(
        diagnostics,
        availability(
            managed ? checker::PtxVersion{4, 0} : checker::PtxVersion{8, 0},
            managed ? 30 : 90),
        target, attribute.range,
        managed ? ".attribute(.managed)" : ".attribute(.unified)");
  }
}

void check_function_directives(
    const std::optional<syntax_ast::AstSyntax>& noreturn_directive,
    const std::optional<syntax_ast::AstCallPrototypeAbiSuffix>& abi_preserve,
    const std::optional<syntax_ast::AstCallPrototypeAbiSuffix>&
        abi_preserve_control,
    const checker::TargetInfo& target, checker::CheckDiagnostics& diagnostics) {
  if (noreturn_directive)
    append_requirement(diagnostics,
                       directive_availability(DirectiveAvailability::Noreturn),
                       target, noreturn_directive->range, ".noreturn");
  if (abi_preserve)
    append_requirement(
        diagnostics, directive_availability(DirectiveAvailability::AbiPreserve),
        target, abi_preserve->range, ".abi_preserve");
  if (abi_preserve_control)
    append_requirement(
        diagnostics,
        directive_availability(DirectiveAvailability::AbiPreserveControl),
        target, abi_preserve_control->range, ".abi_preserve_control");
}

void check_body_directives(
    const std::vector<syntax_ast::AstFunctionBodyItem>& body,
    const checker::TargetInfo& target, checker::CheckDiagnostics& diagnostics) {
  for (const auto& item : body) {
    if (const auto* declaration =
            std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
      check_attributes(declaration->attributes, target, diagnostics);
    } else if (const auto* prototype =
                   std::get_if<syntax_ast::AstCallPrototype>(&item)) {
      check_function_directives(
          prototype->noreturn_directive, prototype->abi_preserve,
          prototype->abi_preserve_control, target, diagnostics);
    } else if (const auto* block =
                   std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
               block != nullptr && *block) {
      check_body_directives((*block)->body, target, diagnostics);
    }
  }
}

void check_instruction_body(
    const std::vector<syntax_ast::AstFunctionBodyItem>& ast_body,
    const ResolvedFunction& function, const checker::TargetInfo& target,
    size_t& resolved_index, checker::CheckDiagnostics& diagnostics) {
  for (const auto& item : ast_body) {
    if (const auto* instruction =
            std::get_if<syntax_ast::AstInstruction>(&item)) {
      if (resolved_index >= function.body.size())
        return;
      const checker::Context context{
          .target = target,
          .instruction_range = instruction->range,
      };
      const auto result = std::visit(
          [&context](const auto& resolved) {
            return checker::check(resolved, context);
          },
          function.body[resolved_index++]);
      if (!result)
        diagnostics.insert(diagnostics.end(), result.error().begin(),
                           result.error().end());
    } else if (const auto* block =
                   std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
               block != nullptr && *block) {
      check_instruction_body((*block)->body, function, target, resolved_index,
                             diagnostics);
    }
  }
}

}  // namespace

checker::CheckResult checkModuleAvailability(const syntax_ast::AstModule& ast,
                                             const ResolvedModule& module) {
  const auto version = module_version(ast);
  checker::CheckDiagnostics diagnostics;
  std::optional<checker::TargetInfo> active_target;
  size_t function_index = 0;

  for (const auto& item : ast.items) {
    if (const auto* target =
            std::get_if<syntax_ast::AstTargetDirective>(&item)) {
      active_target.reset();
      if (target->targets.empty())
        continue;
      const auto profile =
          base::find_target_profile(target->targets.front().text);
      if (!profile) {
        diagnostics.push_back(checker::CheckDiagnostic{
            .kind = checker::CheckDiagnosticKind::UnknownTarget,
            .range = target->range,
            .message = fmt::format("Unknown validation target '{}'.",
                                   target->targets.front().text),
        });
      } else if (version) {
        active_target = checker::TargetInfo{
            .ptx_version = *version,
            .sm_version = profile->identity.architecture.number,
            .enabled_family_features = profile->enabled_family_features,
            .identity = profile->identity,
            .capabilities = profile->capabilities,
        };
      }
    } else if (const auto* variable =
                   std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
      if (active_target)
        check_attributes(variable->attributes, *active_target, diagnostics);
    } else if (const auto* alias =
                   std::get_if<syntax_ast::AstAliasDirective>(&item)) {
      if (active_target)
        append_requirement(diagnostics,
                           directive_availability(DirectiveAvailability::Alias),
                           *active_target, alias->range, ".alias");
    } else if (const auto* function =
                   std::get_if<syntax_ast::AstFunction>(&item)) {
      const size_t resolved_index = function_index++;
      if (!active_target)
        continue;
      check_attributes(function->attributes, *active_target, diagnostics);
      check_function_directives(
          function->noreturn_directive, function->abi_preserve,
          function->abi_preserve_control, *active_target, diagnostics);
      if (function->blocks_are_clusters)
        append_requirement(diagnostics, availability({9, 0}, 90, "cluster"),
                           *active_target, function->blocks_are_clusters->range,
                           ".blocksareclusters");
      if (function->language)
        append_requirement(diagnostics, availability({9, 3}), *active_target,
                           function->language->range, ".language");
      for (const auto& resource : function->resources)
        append_requirement(diagnostics, resource_availability(resource.kind),
                           *active_target, resource.range,
                           resource_name(resource.kind));
      check_body_directives(function->body, *active_target, diagnostics);
      if (resolved_index < module.functions.size()) {
        size_t instruction_index = 0;
        check_instruction_body(function->body, module.functions[resolved_index],
                               *active_target, instruction_index, diagnostics);
      }
    }
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

}  // namespace ptx_frontend::resolved_ir
