#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include "ptx_module_source_context.hpp"
#include "ptx_source_identity.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

namespace ptx_frontend::resolved_ir {
namespace {

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

/** Return source availability required by an owned resource contract. */
checker::AvailabilityDescriptor resource_availability(
    ResolvedKernelResourceKind kind) {
  using Kind = ResolvedKernelResourceKind;
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

/** Return the stable spelling used for an owned resource diagnostic. */
std::string_view resource_name(ResolvedKernelResourceKind kind) {
  using Kind = ResolvedKernelResourceKind;
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

/** Flatten syntax only to verify the owned source map, never to index checks. */
void collect_instructions(
    const std::vector<syntax_ast::AstFunctionBodyItem>& ast_body,
    std::vector<const syntax_ast::AstInstruction*>& instructions) {
  for (const auto& item : ast_body) {
    if (const auto* instruction =
            std::get_if<syntax_ast::AstInstruction>(&item)) {
      instructions.push_back(instruction);
    } else if (const auto* block =
                   std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
               block != nullptr && *block) {
      collect_instructions((*block)->body, instructions);
    }
  }
}

/** Match structural source identity; exact ranges only disambiguate duplicates. */
const ResolvedFunction* source_function(const syntax_ast::AstFunction& function,
                                        const ResolvedModule& module) {
  const std::string identity = detail::function_source_identity(function);
  const ResolvedFunction* match = nullptr;
  const ResolvedFunction* exact = nullptr;
  size_t count = 0;
  size_t exact_count = 0;
  for (const auto& candidate : module.functions) {
    if (candidate.source_identity != identity)
      continue;
    match = &candidate;
    ++count;
    if (candidate.range == function.range) {
      exact = &candidate;
      ++exact_count;
    }
  }
  return count == 1 ? match : exact_count == 1 ? exact : nullptr;
}

/** Require an unambiguous declaration and instruction association before checks. */
checker::CheckResult check_source_associations(const syntax_ast::AstModule& ast,
                                               const ResolvedModule& module) {
  checker::CheckDiagnostics diagnostics;
  /** Report malformed or unrelated IR as a structured checker failure. */
  const auto mismatch = [&](SourceRange range, std::string_view message) {
    diagnostics.push_back({
        .kind = checker::CheckDiagnosticKind::ModuleSourceMismatch,
        .range = range,
        .message = std::string(message),
    });
  };
  if (module.source_identity != detail::module_source_identity(ast)) {
    mismatch(ast.range, "Syntax and IR module identities differ.");
    return std::unexpected(std::move(diagnostics));
  }
  std::vector<bool> matched(module.functions.size(), false);
  for (const auto& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (!function)
      continue;
    const auto* candidate = source_function(*function, module);
    if (!candidate) {
      mismatch(function->range,
               "Syntax function has no unique resolved declaration.");
      continue;
    }
    const size_t match =
        static_cast<size_t>(candidate - module.functions.data());
    if (matched[match]) {
      mismatch(function->range,
               "Syntax declarations share the same IR function.");
      continue;
    }
    matched[match] = true;
    const auto& resolved = module.functions[match];
    const auto scope = module.symbols.functionScope(resolved.range);
    if (!scope || *scope != resolved.declaration_scope ||
        resolved.name != function->name.syntax.text ||
        resolved.is_entry != function->is_entry ||
        resolved.is_prototype != function->is_prototype ||
        module.symbols.scope(*scope).owner != resolved.symbol_id) {
      mismatch(function->range, "Syntax and IR function identities differ.");
      continue;
    }
    std::vector<const syntax_ast::AstInstruction*> instructions;
    collect_instructions(function->body, instructions);
    if (instructions.size() != resolved.body.size() ||
        resolved.instruction_ranges.size() != resolved.body.size() ||
        resolved.instruction_opcodes.size() != resolved.body.size()) {
      mismatch(function->range,
               "Syntax, IR, and instruction source-map counts differ.");
      continue;
    }
    for (size_t i = 0; i < instructions.size(); ++i) {
      const auto opcode = std::visit(
          [](const auto& instruction) {
            return instruction.get_resolved_descriptor().opcode_name;
          },
          resolved.body[i]);
      if (instructions[i]->opcode.syntax.text !=
              resolved.instruction_opcodes[i] ||
          opcode != resolved.instruction_opcodes[i]) {
        mismatch(instructions[i]->range,
                 "Syntax and IR instruction identities differ.");
      }
    }
  }
  if (std::ranges::find(matched, false) != matched.end())
    mismatch(module.range,
             "IR contains a function without a syntax declaration.");
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  return {};
}

/** Check every IR instruction using its owned source location. */
void check_instruction_body(const ResolvedFunction& function,
                            const checker::TargetInfo& target,
                            checker::CheckDiagnostics& diagnostics) {
  for (size_t i = 0; i < function.body.size(); ++i) {
    const checker::Context context{
        .target = target,
        .instruction_range = function.instruction_ranges[i],
    };
    const auto result = std::visit(
        [&context](const auto& resolved) {
          return checker::check(resolved, context);
        },
        function.body[i]);
    if (!result)
      diagnostics.insert(diagnostics.end(), result.error().begin(),
                         result.error().end());
  }
}

/** Report an owned-model invariant failure without relying on source syntax. */
void append_model_mismatch(checker::CheckDiagnostics& diagnostics,
                           SourceRange range, std::string message) {
  diagnostics.push_back({
      .kind = checker::CheckDiagnosticKind::ModuleSourceMismatch,
      .range = range,
      .message = std::move(message),
  });
}

/** One binding identity projected from a generated resolved operand payload. */
struct ModuleReferenceUse {
  std::optional<binding::SymbolId> symbol_id;
  std::optional<uint32_t> parameterized_index;
  std::optional<binding::SymbolKind> expected_kind;
  /** True when the binding must carry the .reg declaration state space. */
  bool requires_register_state{};
  bool function_local{};
  SourceRange range;
};

/** Return whether a symbol may back a register-typed instruction operand. */
bool is_register_operand_symbol(const binding::Symbol& symbol) {
  const bool register_declaration =
      symbol.kind == binding::SymbolKind::Variable ||
      symbol.kind == binding::SymbolKind::InputParameter ||
      symbol.kind == binding::SymbolKind::ReturnParameter;
  return register_declaration &&
         symbol.state_space == base::DeclarationStateSpace::Register;
}

/** Return the first owned location, falling back to the complete instruction. */
SourceRange reference_range(std::span<const SourceRange> locations,
                            SourceRange fallback) {
  return locations.empty() ? fallback : locations.front();
}

/** Append one declaration-bearing operand reference. */
void append_reference(std::vector<ModuleReferenceUse>& uses,
                      std::optional<binding::SymbolId> symbol_id,
                      std::optional<uint32_t> parameterized_index,
                      std::optional<binding::SymbolKind> expected_kind,
                      bool function_local,
                      std::span<const SourceRange> locations,
                      SourceRange fallback,
                      bool requires_register_state = false) {
  uses.push_back({.symbol_id = symbol_id,
                  .parameterized_index = parameterized_index,
                  .expected_kind = expected_kind,
                  .requires_register_state = requires_register_state,
                  .function_local = function_local,
                  .range = reference_range(locations, fallback)});
}

/**
 * A generator-selected payload whose contained declaration references are
 * modeled by the owned-module validator.
 *
 * The generator's explicit reference policy is the source of truth for this
 * set. Adding a reference-bearing payload requires both generator selection
 * and a collector branch, so new references cannot be silently ignored.
 */
template <typename Value>
concept ReferenceBearingOperandPayload =
    std::same_as<std::remove_cvref_t<Value>, ResolvedRegisterRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedMbarrierStateToken> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedRegisterOrSink> ||
    std::same_as<std::remove_cvref_t<Value>, RegOrImm> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedShflSyncDestination> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedPredicatePair> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedPredicatePairOrSink> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedPredicateOrSink> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedMovSource> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedPredicate> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedPredicateSource> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedBranchTarget> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedBranchTargetSet> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedVectorRegisterRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedSymbolRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedAddress> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedRegisterVector> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedTensorCoordinate> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedFunctionRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedIndirectCallee> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedCallParameterRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedCallArguments>;

/** Collect all nested binding identities from one generator-selected operand. */
template <ReferenceBearingOperandPayload Value>
void collect_operand_references(const Value& value,
                                std::span<const SourceRange> locations,
                                SourceRange fallback,
                                std::vector<ModuleReferenceUse>& uses) {
  const auto collect_register = [&](const ResolvedRegisterRef& register_ref) {
    append_reference(
        uses, register_ref.symbol_id, register_ref.parameterized_index,
        binding::SymbolKind::Variable, true, locations, fallback, true);
  };
  if constexpr (std::same_as<Value, ResolvedRegisterRef>) {
    collect_register(value);
  } else if constexpr (std::same_as<Value, ResolvedPredicate>) {
    collect_register(value.register_ref);
  } else if constexpr (std::same_as<Value, ResolvedBranchTarget>) {
    append_reference(uses, value.symbol_id, std::nullopt,
                     binding::SymbolKind::Label, true, locations, fallback);
  } else if constexpr (std::same_as<Value, ResolvedBranchTargetSet>) {
    append_reference(uses, value.symbol_id, std::nullopt,
                     binding::SymbolKind::BranchTargetSet, true, locations,
                     fallback);
  } else if constexpr (std::same_as<Value, ResolvedFunctionRef>) {
    append_reference(uses, value.symbol_id, std::nullopt,
                     binding::SymbolKind::Function, false, locations, fallback);
  } else if constexpr (std::same_as<Value, ResolvedCallParameterRef>) {
    append_reference(uses, value.symbol_id, value.parameterized_index,
                     std::nullopt, true, locations, fallback);
  } else if constexpr (std::same_as<Value, ResolvedSymbolRef>) {
    append_reference(uses, value.symbol_id, value.parameterized_index,
                     value.declaration_kind, false, locations, fallback);
  } else if constexpr (std::same_as<Value, ResolvedVectorRegisterRef>) {
    collect_register(value.register_ref);
  } else if constexpr (std::same_as<Value, ResolvedMbarrierStateToken>) {
    if (value.register_ref)
      collect_register(*value.register_ref);
  } else if constexpr (std::same_as<Value, ResolvedRegisterOrSink>) {
    if (value.register_ref)
      collect_register(*value.register_ref);
  } else if constexpr (std::same_as<Value, RegOrImm>) {
    if (const auto* register_ref = std::get_if<ResolvedRegisterRef>(&value))
      collect_register(*register_ref);
  } else if constexpr (std::same_as<Value, ResolvedPredicatePair>) {
    collect_register(value.first.register_ref);
    collect_register(value.second.register_ref);
  } else if constexpr (std::same_as<Value, ResolvedPredicatePairOrSink>) {
    if (value.first)
      collect_register(value.first->register_ref);
    if (value.second)
      collect_register(value.second->register_ref);
  } else if constexpr (std::same_as<Value, ResolvedPredicateOrSink>) {
    if (value.predicate)
      collect_register(value.predicate->register_ref);
  } else if constexpr (std::same_as<Value, ResolvedPredicateSource>) {
    if (const auto* predicate = std::get_if<ResolvedPredicate>(&value))
      collect_register(predicate->register_ref);
  } else if constexpr (std::same_as<Value, ResolvedIndirectCallee>) {
    if (const auto* metadata = std::get_if<ResolvedIndirectMetadataRef>(&value))
      append_reference(uses, metadata->symbol_id, std::nullopt,
                       metadata->declaration_kind, true, locations, fallback);
    else
      collect_register(std::get<ResolvedRegisterRef>(value));
  } else if constexpr (std::same_as<Value, ResolvedRegisterVector>) {
    for (const auto& element : value.elements)
      if (element)
        collect_register(*element);
  } else if constexpr (std::same_as<Value, ResolvedTensorCoordinate>) {
    for (const auto& element : value.elements)
      if (const auto* register_ref = std::get_if<ResolvedRegisterRef>(&element))
        collect_register(*register_ref);
  } else if constexpr (std::same_as<Value, ResolvedAddress>) {
    if (const auto* register_ref =
            std::get_if<ResolvedRegisterRef>(&value.base))
      collect_register(*register_ref);
    else if (const auto* symbol = std::get_if<ResolvedSymbolRef>(&value.base))
      collect_operand_references(*symbol, locations, fallback, uses);
  } else if constexpr (std::same_as<Value, ResolvedMovSource>) {
    std::visit(
        [&](const auto& source) {
          using Source = std::remove_cvref_t<decltype(source)>;
          if constexpr (std::same_as<Source, ResolvedRegisterRef> ||
                        std::same_as<Source, ResolvedFunctionRef> ||
                        std::same_as<Source, ResolvedSymbolRef> ||
                        std::same_as<Source, ResolvedAddress>)
            collect_operand_references(source, locations, fallback, uses);
        },
        value);
  } else if constexpr (std::same_as<Value, ResolvedShflSyncDestination>) {
    if (value.data)
      collect_register(value.data->value);
    if (value.predicate)
      collect_register(value.predicate->value.register_ref);
  } else if constexpr (std::same_as<Value, ResolvedCallArguments>) {
    for (const auto& argument : value.values) {
      if (const auto* parameter =
              std::get_if<ResolvedCallParameterRef>(&argument.value))
        collect_operand_references(*parameter, argument.locs, fallback, uses);
    }
  } else {
    static_assert(
        !sizeof(Value),
        "Reference-bearing generated operand needs collector support.");
  }
}

/** Return a symbol only when an externally supplied identity is in table bounds. */
const binding::Symbol* owned_symbol(const ResolvedModule& module,
                                    binding::SymbolId id) {
  if (id.value >= module.symbols.symbols().size())
    return nullptr;
  return &module.symbols.symbol(id);
}

/** Return whether a lexical scope belongs to a function's owned scope subtree. */
bool is_function_owned_scope(const ResolvedModule& module,
                             binding::ScopeId candidate,
                             binding::ScopeId function_scope) {
  while (candidate.value < module.symbols.scopes().size()) {
    if (candidate == function_scope)
      return true;
    const auto parent = module.symbols.scope(candidate).parent;
    if (!parent)
      return false;
    candidate = *parent;
  }
  return false;
}

/** Return whether a symbol is visible from a function-owned operand site. */
bool is_operand_scope(const ResolvedModule& module, binding::ScopeId candidate,
                      binding::ScopeId function_scope) {
  return candidate == module.symbols.moduleScope() ||
         is_function_owned_scope(module, candidate, function_scope);
}

/** Revalidate identities embedded in generated instruction operand payloads. */
void check_module_references(const ResolvedModule& module,
                             const ResolvedFunction& function,
                             checker::CheckDiagnostics& diagnostics) {
  std::vector<ModuleReferenceUse> uses;
  for (size_t index = 0; index < function.body.size(); ++index) {
    std::visit(
        [&](const auto& instruction) {
          detail::visit_instruction_references(
              instruction,
              [&](const auto& value, std::span<const SourceRange> locations) {
                collect_operand_references(
                    value, locations, function.instruction_ranges[index], uses);
              });
        },
        function.body[index]);
  }
  for (const auto& use : uses) {
    if (!use.symbol_id) {
      append_model_mismatch(diagnostics, use.range,
                            "Resolved module operand has no binding identity.");
      continue;
    }
    const auto* symbol = owned_symbol(module, *use.symbol_id);
    if (symbol == nullptr) {
      append_model_mismatch(
          diagnostics, use.range,
          "Resolved module operand has an out-of-range binding identity.");
      continue;
    }
    const bool compatible_kind =
        !use.expected_kind ||
        (use.requires_register_state ? is_register_operand_symbol(*symbol)
                                     : symbol->kind == *use.expected_kind);
    if (!compatible_kind ||
        !is_operand_scope(module, symbol->scope, function.declaration_scope) ||
        (use.function_local &&
         !is_function_owned_scope(module, symbol->scope,
                                  function.declaration_scope))) {
      append_model_mismatch(
          diagnostics, use.range,
          "Resolved module operand has an incompatible declaration identity.");
      continue;
    }
    if (use.parameterized_index &&
        (!symbol->parameterized_count ||
         *use.parameterized_index >= *symbol->parameterized_count)) {
      append_model_mismatch(
          diagnostics, use.range,
          "Resolved module operand has an invalid parameterized member index.");
    }
  }
}

/** Return whether a public provenance value is one of the modeled states. */
bool valid_provenance(SourceConfigurationProvenance provenance) {
  switch (provenance) {
    case SourceConfigurationProvenance::Missing:
    case SourceConfigurationProvenance::Explicit:
    case SourceConfigurationProvenance::Defaulted:
      return true;
  }
  return false;
}

/** Validate header payload/provenance consistency under public IR mutation. */
void check_header_region(const ResolvedSourceTargetRegion& region,
                         SourceRange range,
                         checker::CheckDiagnostics& diagnostics) {
  if (!valid_provenance(region.version_provenance) ||
      !valid_provenance(region.target_provenance) ||
      !valid_provenance(region.address_size_provenance)) {
    append_model_mismatch(
        diagnostics, range,
        "Resolved source header has an invalid provenance value.");
  }
  if (region.address_size_bits && *region.address_size_bits != 32 &&
      *region.address_size_bits != 64) {
    append_model_mismatch(
        diagnostics, range,
        "Resolved source header has an invalid address size.");
  }
  const auto check_provenance = [&](bool present,
                                    SourceConfigurationProvenance provenance,
                                    std::string_view subject) {
    if ((!present && provenance != SourceConfigurationProvenance::Missing) ||
        (present && provenance == SourceConfigurationProvenance::Missing)) {
      append_model_mismatch(
          diagnostics, range,
          fmt::format("Resolved source header has inconsistent {} provenance.",
                      subject));
    }
  };
  check_provenance(region.version.has_value(), region.version_provenance,
                   "version");
  check_provenance(!region.target_options.empty(), region.target_provenance,
                   "target");
  check_provenance(region.address_size_bits.has_value(),
                   region.address_size_provenance, "address-size");
  if (region.version_provenance == SourceConfigurationProvenance::Defaulted ||
      region.target_provenance == SourceConfigurationProvenance::Defaulted) {
    append_model_mismatch(diagnostics, range,
                          "Resolved source header defaulted a value that PTX "
                          "requires source to provide.");
  }
  if (region.address_size_provenance ==
          SourceConfigurationProvenance::Defaulted &&
      region.address_size_bits != 32) {
    append_model_mismatch(
        diagnostics, range,
        "Resolved source header defaulted an address size other than 32 bits.");
  }
}

/** Validate dimensional resource normalization and mutually exclusive contracts. */
void check_resource_contracts(const ResolvedFunction& function,
                              checker::CheckDiagnostics& diagnostics) {
  constexpr size_t resource_kind_count =
      static_cast<size_t>(ResolvedKernelResourceKind::MaxClusterRank) + 1;
  std::array<bool, resource_kind_count> seen{};
  bool has_max_ntid = false;
  bool has_req_ntid = false;
  bool has_req_cluster = false;
  bool has_max_cluster = false;
  for (const auto& resource : function.contract.resources) {
    const size_t kind = static_cast<size_t>(resource.kind);
    if (kind >= seen.size()) {
      append_model_mismatch(diagnostics, resource.range,
                            "Resolved kernel resource has an invalid kind.");
      continue;
    }
    if (seen[kind]) {
      append_model_mismatch(
          diagnostics, resource.range,
          "Resolved function repeats a kernel resource kind.");
    }
    seen[kind] = true;
    const bool dimensions =
        resource.kind == ResolvedKernelResourceKind::MaxNtid ||
        resource.kind == ResolvedKernelResourceKind::ReqNtid ||
        resource.kind == ResolvedKernelResourceKind::ReqNctaPerCluster;
    const size_t expected_values =
        dimensions                                                     ? 3
        : resource.kind == ResolvedKernelResourceKind::ExplicitCluster ? 0
                                                                       : 1;
    if (resource.values.size() != expected_values ||
        resource.explicit_value_count > resource.values.size() ||
        (dimensions && resource.explicit_value_count == 0)) {
      append_model_mismatch(
          diagnostics, resource.range,
          "Resolved kernel resource has an invalid dimensional payload.");
    }
    if (dimensions) {
      for (size_t i = resource.explicit_value_count; i < resource.values.size();
           ++i) {
        if (resource.values[i] != 1) {
          append_model_mismatch(
              diagnostics, resource.range,
              "Resolved kernel resource has a non-default inferred dimension.");
          break;
        }
      }
    }
    switch (resource.kind) {
      case ResolvedKernelResourceKind::MaxNtid:
        has_max_ntid = true;
        break;
      case ResolvedKernelResourceKind::ReqNtid:
        has_req_ntid = true;
        break;
      case ResolvedKernelResourceKind::ReqNctaPerCluster:
        has_req_cluster = true;
        break;
      case ResolvedKernelResourceKind::MaxClusterRank:
        has_max_cluster = true;
        break;
      default:
        break;
    }
  }
  if (has_max_ntid && has_req_ntid)
    append_model_mismatch(diagnostics, function.range,
                          "Resolved function combines .maxntid and .reqntid.");
  if (has_req_cluster && has_max_cluster)
    append_model_mismatch(
        diagnostics, function.range,
        "Resolved function combines .reqnctapercluster and .maxclusterrank.");
  if (function.contract.blocks_are_clusters &&
      (!has_req_ntid || !has_req_cluster)) {
    append_model_mismatch(diagnostics, function.range,
                          "Resolved .blocksareclusters lacks required "
                          "launch-resource contracts.");
  }
  if (!function.is_entry && (!function.contract.resources.empty() ||
                             function.contract.blocks_are_clusters)) {
    append_model_mismatch(diagnostics, function.range,
                          "Resolved device function carries entry-only "
                          "launch-resource contracts.");
  }
}

/** Validate function-only attribute, signature, and ABI suffix invariants. */
void check_function_contract_integrity(const ResolvedFunction& function,
                                       checker::CheckDiagnostics& diagnostics) {
  bool unified = false;
  for (const auto& attribute : function.contract.attributes) {
    if (attribute.kind == ResolvedFunctionAttributeKind::Managed) {
      append_model_mismatch(diagnostics, attribute.range,
                            ".managed is not a valid function attribute.");
      continue;
    }
    if (attribute.kind != ResolvedFunctionAttributeKind::Unified) {
      append_model_mismatch(diagnostics, attribute.range,
                            "Resolved function has an invalid attribute kind.");
      continue;
    }
    if (unified) {
      append_model_mismatch(
          diagnostics, attribute.range,
          "Resolved function repeats the .unified attribute.");
    }
    unified = true;
    if (!attribute.unified_id) {
      append_model_mismatch(
          diagnostics, attribute.range,
          "Resolved .unified attribute has no typed UUID payload.");
    }
  }
  if (function.contract.signature.is_entry != function.is_entry ||
      function.contract.signature.is_noreturn !=
          function.contract.is_noreturn) {
    append_model_mismatch(diagnostics, function.range,
                          "Resolved function and signature flags disagree.");
  }
  if (function.contract.is_noreturn &&
      !function.contract.signature.return_parameters.empty()) {
    append_model_mismatch(diagnostics, function.range,
                          "Resolved .noreturn function has return parameters.");
  }
  if (function.contract.abi_preserve &&
      function.contract.abi_preserve->control_registers) {
    append_model_mismatch(
        diagnostics, function.contract.abi_preserve->range,
        "Resolved .abi_preserve has a control-register variant.");
  }
  if (function.contract.abi_preserve_control &&
      !function.contract.abi_preserve_control->control_registers) {
    append_model_mismatch(
        diagnostics, function.contract.abi_preserve_control->range,
        "Resolved .abi_preserve_control lacks its control-register variant.");
  }
}

/** Validate one modeled scalar contract without accepting constructed enum junk. */
bool valid_scalar_contract(
    const declaration_semantics::FunctionParameterContract& parameter) {
  if (parameter.scalar_type == base::ScalarType::Invalid)
    return false;
  return base::find_scalar_type_metadata(parameter.scalar_type) != nullptr;
}

/** Check the normalized signature fields needed by contextual call validation. */
void check_signature(const declaration_semantics::FunctionSignature& signature,
                     SourceRange range,
                     checker::CheckDiagnostics& diagnostics) {
  const auto check_parameters = [&](const auto& parameters) {
    for (const auto& parameter : parameters) {
      if (!valid_scalar_contract(parameter)) {
        append_model_mismatch(
            diagnostics, range,
            "Resolved function signature has an invalid scalar type.");
      }
      if (parameter.state_space ==
          call_argument_compatibility::CallArgumentStateSpace::Invalid) {
        append_model_mismatch(
            diagnostics, range,
            "Resolved function signature has an invalid state space.");
      }
    }
  };
  check_parameters(signature.return_parameters);
  check_parameters(signature.parameters);
}

/** Check metadata declarations against their owning function's binding table. */
void check_control_contracts(const ResolvedModule& module,
                             const ResolvedFunction& function,
                             checker::CheckDiagnostics& diagnostics) {
  const auto expect = [&](binding::SymbolId id, binding::SymbolKind kind,
                          binding::ScopeId scope, SourceRange range,
                          std::string_view subject) -> const binding::Symbol* {
    const auto* symbol = owned_symbol(module, id);
    if (symbol == nullptr || symbol->kind != kind ||
        !is_function_owned_scope(module, symbol->scope, scope)) {
      append_model_mismatch(
          diagnostics, range,
          fmt::format("Resolved {} has an invalid bound declaration identity.",
                      subject));
      return nullptr;
    }
    return symbol;
  };
  for (const auto& branches : function.branch_target_sets) {
    const auto* metadata =
        expect(branches.symbol_id, binding::SymbolKind::BranchTargetSet,
               function.declaration_scope, branches.range, ".branchtargets");
    if (metadata != nullptr && branches.scope_id != metadata->scope) {
      append_model_mismatch(
          diagnostics, branches.range,
          "Resolved .branchtargets has a mismatched owner scope.");
    }
    for (const auto& target : branches.targets) {
      expect(target.symbol_id, binding::SymbolKind::Label,
             function.declaration_scope, target.range, "branch target");
    }
  }
  for (const auto& targets : function.call_target_sets) {
    const auto* metadata =
        expect(targets.symbol_id, binding::SymbolKind::CallTargetSet,
               function.declaration_scope, targets.range, ".calltargets");
    if (metadata != nullptr && targets.scope_id != metadata->scope) {
      append_model_mismatch(
          diagnostics, targets.range,
          "Resolved .calltargets has a mismatched owner scope.");
    }
    check_signature(targets.signature, targets.range, diagnostics);
    if (targets.targets.empty())
      append_model_mismatch(diagnostics, targets.range,
                            "Resolved .calltargets declaration has no target.");
    for (const auto& target : targets.targets) {
      const auto* symbol =
          expect(target.symbol_id, binding::SymbolKind::Function,
                 module.symbols.moduleScope(), target.range, "call target");
      if (symbol != nullptr && symbol->canonical_function.value_or(
                                   symbol->id) != target.canonical_function) {
        append_model_mismatch(
            diagnostics, target.range,
            "Resolved call target has a mismatched canonical identity.");
      }
    }
  }
  for (const auto& prototype : function.call_prototypes) {
    const auto* metadata =
        expect(prototype.symbol_id, binding::SymbolKind::CallPrototype,
               function.declaration_scope, prototype.range, ".callprototype");
    if (metadata != nullptr && prototype.scope_id != metadata->scope) {
      append_model_mismatch(
          diagnostics, prototype.range,
          "Resolved .callprototype has a mismatched owner scope.");
    }
    check_signature(prototype.signature, prototype.range, diagnostics);
  }
}

/** Per-validation O(1) lookup of direct and metadata call signatures. */
struct OwnedSignatureIndex {
  /** Canonical function identity to its retained ABI signature. */
  std::unordered_map<uint32_t, const declaration_semantics::FunctionSignature*>
      direct;
  /** Function-local metadata identity to its retained ABI signature. */
  std::unordered_map<uint32_t, const declaration_semantics::FunctionSignature*>
      metadata;
};

/** Build an ephemeral signature index and diagnose incompatible duplicate owners. */
OwnedSignatureIndex build_signature_index(
    const ResolvedModule& module, checker::CheckDiagnostics& diagnostics) {
  OwnedSignatureIndex index;
  const auto insert =
      [&](auto& destination, binding::SymbolId symbol_id,
          const declaration_semantics::FunctionSignature& signature,
          SourceRange range, std::string_view subject) {
        const auto [found, inserted] =
            destination.try_emplace(symbol_id.value, &signature);
        if (!inserted && *found->second != signature) {
          append_model_mismatch(
              diagnostics, range,
              fmt::format(
                  "Resolved {} declarations have incompatible signatures.",
                  subject));
        }
      };
  for (const auto& function : module.functions) {
    insert(index.direct, function.contract.canonical_function,
           function.contract.signature, function.range, "canonical function");
    for (const auto& prototype : function.call_prototypes) {
      insert(index.metadata, prototype.symbol_id, prototype.signature,
             prototype.range, ".callprototype");
    }
    for (const auto& targets : function.call_target_sets) {
      insert(index.metadata, targets.symbol_id, targets.signature,
             targets.range, ".calltargets");
    }
  }
  return index;
}

/** Return the O(1) signature selected by a direct function binding identity. */
const declaration_semantics::FunctionSignature* direct_signature(
    const ResolvedModule& module, const OwnedSignatureIndex& signatures,
    binding::SymbolId symbol_id) {
  const auto* symbol = owned_symbol(module, symbol_id);
  if (symbol == nullptr || symbol->kind != binding::SymbolKind::Function)
    return nullptr;
  const binding::SymbolId canonical =
      symbol->canonical_function.value_or(symbol->id);
  const auto found = signatures.direct.find(canonical.value);
  return found == signatures.direct.end() ? nullptr : found->second;
}

/** Return the O(1) signature selected by a local metadata identity. */
const declaration_semantics::FunctionSignature* metadata_signature(
    const OwnedSignatureIndex& signatures, binding::SymbolId symbol_id) {
  const auto found = signatures.metadata.find(symbol_id.value);
  return found == signatures.metadata.end() ? nullptr : found->second;
}

/** Return the metadata identity retained by either indirect call operand. */
std::optional<binding::SymbolId> indirect_metadata_identity(
    const ResolvedIndirectCallee& callee) {
  const auto* metadata = std::get_if<ResolvedIndirectMetadataRef>(&callee);
  return metadata == nullptr ? std::nullopt : metadata->symbol_id;
}

using call_argument_compatibility::CallArgumentCompatibility;
using call_argument_compatibility::CallArgumentProperties;
using call_argument_compatibility::CallArgumentStateSpace;
using call_argument_compatibility::CallArgumentVectorShape;

/** Convert an owned declaration state-space to its call ABI category. */
CallArgumentStateSpace call_state_space(
    base::DeclarationStateSpace state_space) {
  switch (state_space) {
    case base::DeclarationStateSpace::Register:
      return CallArgumentStateSpace::Register;
    case base::DeclarationStateSpace::Parameter:
      return CallArgumentStateSpace::Parameter;
    case base::DeclarationStateSpace::Local:
      return CallArgumentStateSpace::Local;
    case base::DeclarationStateSpace::Shared:
      return CallArgumentStateSpace::Shared;
    case base::DeclarationStateSpace::Global:
      return CallArgumentStateSpace::Global;
    case base::DeclarationStateSpace::Constant:
      return CallArgumentStateSpace::Constant;
  }
  return CallArgumentStateSpace::Invalid;
}

/** Convert retained vector lanes to the ABI shape accepted by call comparison. */
CallArgumentVectorShape call_vector_shape(uint32_t lanes) {
  switch (lanes) {
    case 1:
      return CallArgumentVectorShape::Scalar;
    case 2:
      return CallArgumentVectorShape::V2;
    case 4:
      return CallArgumentVectorShape::V4;
    default:
      return CallArgumentVectorShape::Invalid;
  }
}

/** Project a retained `.param` declaration into shared call-ABI properties. */
CallArgumentProperties call_argument_properties(
    const ResolvedParameterDeclaration& declaration) {
  CallArgumentProperties properties{
      .state_space = CallArgumentStateSpace::Parameter,
      .scalar_type = declaration.scalar_type,
      .vector_shape = call_vector_shape(declaration.vector_width),
      .array_alignment = declaration.alignment,
      .is_array = !declaration.array_extents.empty(),
      .pointer = declaration.pointer,
  };
  if (!properties.is_array)
    return properties;
  uint64_t extent = 1;
  for (const auto axis : declaration.array_extents) {
    if (!axis ||
        (*axis != 0 && extent > std::numeric_limits<uint64_t>::max() / *axis)) {
      return properties;
    }
    extent *= *axis;
  }
  properties.array_size = extent;
  return properties;
}

/** Build O(1) actual-parameter ABI properties from owned declaration records. */
std::unordered_map<uint32_t, CallArgumentProperties> build_parameter_properties(
    const ResolvedModule& module) {
  std::unordered_map<uint32_t, CallArgumentProperties> properties;
  for (const auto& function : module.functions) {
    for (const auto& declaration : function.parameter_declarations)
      properties.try_emplace(declaration.symbol_id.value,
                             call_argument_properties(declaration));
  }
  return properties;
}

/** Project a bound call-parameter operand, preserving its mutable type facts. */
CallArgumentProperties call_argument_properties(
    const ResolvedCallParameterRef& reference,
    const std::unordered_map<uint32_t, CallArgumentProperties>& declarations) {
  CallArgumentProperties properties{
      .state_space = reference.state_space
                         ? call_state_space(*reference.state_space)
                         : CallArgumentStateSpace::Invalid,
      .scalar_type =
          reference.declared_type.value_or(base::ScalarType::Invalid),
      .vector_shape = CallArgumentVectorShape::Scalar,
  };
  if (reference.symbol_id) {
    const auto found = declarations.find(reference.symbol_id->value);
    if (found != declarations.end())
      properties = found->second;
  }
  if (!reference.state_space || !reference.declared_type) {
    properties.state_space = CallArgumentStateSpace::Invalid;
    properties.scalar_type = base::ScalarType::Invalid;
    return properties;
  }
  properties.state_space = call_state_space(*reference.state_space);
  properties.scalar_type = *reference.declared_type;
  return properties;
}

/** Check one resolved literal against the signature that originally typed it. */
void check_typed_literal(
    const ResolvedCallLiteral& literal,
    const WithLocs<ResolvedCallArgument>& actual,
    const declaration_semantics::FunctionParameterContract& formal,
    checker::CheckDiagnostics& diagnostics, SourceRange fallback_range) {
  const SourceRange range =
      actual.locs.empty() ? fallback_range : actual.locs.front();
  if (!literal.value) {
    append_model_mismatch(
        diagnostics, range,
        "Resolved module call retains an untyped literal argument.");
    return;
  }
  const auto expected = resolve_call_literal(literal, range, formal);
  if (!expected || expected->value != *literal.value) {
    append_model_mismatch(diagnostics, range,
                          "Resolved module call literal no longer matches its "
                          "formal typed value.");
  }
}

/** Return whether a final unsized byte input may be omitted by an empty call. */
bool omits_unsized_byte_input(
    const std::vector<declaration_semantics::FunctionParameterContract>&
        formals,
    size_t actual_count) {
  return !formals.empty() && actual_count == formals.size() - 1 &&
         formals.back().is_array && !formals.back().array_extent &&
         formals.back().scalar_type == base::ScalarType::B8 &&
         formals.back().state_space == CallArgumentStateSpace::Parameter;
}

/** Check one bound parameter operand against its formal ABI contract. */
void check_call_parameter(
    const ResolvedCallParameterRef& actual,
    const declaration_semantics::FunctionParameterContract& formal,
    const std::unordered_map<uint32_t, CallArgumentProperties>& declarations,
    checker::CheckDiagnostics& diagnostics, SourceRange range) {
  const auto compatibility =
      call_argument_compatibility::checkCallArgumentCompatibility(
          declaration_semantics::call_argument_properties(formal),
          call_argument_properties(actual, declarations));
  if (compatibility != CallArgumentCompatibility::Compatible) {
    append_model_mismatch(diagnostics, range,
                          "Resolved module call parameter no longer satisfies "
                          "its formal ABI contract.");
  }
}

/** Check a complete input group against its formal arity and ABI contracts. */
void check_call_inputs(
    const ResolvedCallArguments* actuals,
    const std::vector<declaration_semantics::FunctionParameterContract>&
        formals,
    const std::unordered_map<uint32_t, CallArgumentProperties>& declarations,
    checker::CheckDiagnostics& diagnostics, SourceRange fallback_range) {
  const size_t actual_count = actuals == nullptr ? 0 : actuals->values.size();
  if (actual_count != formals.size() &&
      !omits_unsized_byte_input(formals, actual_count)) {
    append_model_mismatch(
        diagnostics, fallback_range,
        "Resolved module call input count no longer matches its signature.");
  }
  if (actuals == nullptr)
    return;
  const size_t count = std::min(actuals->values.size(), formals.size());
  for (size_t argument_index = 0; argument_index < count; ++argument_index) {
    const auto& argument = actuals->values[argument_index];
    const SourceRange range =
        argument.locs.empty() ? fallback_range : argument.locs.front();
    if (const auto* literal =
            std::get_if<ResolvedCallLiteral>(&argument.value)) {
      check_typed_literal(*literal, argument, formals[argument_index],
                          diagnostics, fallback_range);
    } else {
      check_call_parameter(std::get<ResolvedCallParameterRef>(argument.value),
                           formals[argument_index], declarations, diagnostics,
                           range);
    }
  }
}

/** Check the optional single return operand against the formal return ABI. */
void check_call_returns(
    const ResolvedCallParameterRef* actual,
    const std::vector<declaration_semantics::FunctionParameterContract>&
        formals,
    const std::unordered_map<uint32_t, CallArgumentProperties>& declarations,
    checker::CheckDiagnostics& diagnostics, SourceRange fallback_range) {
  const size_t actual_count = actual == nullptr ? 0 : 1;
  if (actual_count != formals.size()) {
    append_model_mismatch(
        diagnostics, fallback_range,
        "Resolved module call return count no longer matches its signature.");
  }
  if (actual != nullptr && !formals.empty()) {
    check_call_parameter(*actual, formals.front(), declarations, diagnostics,
                         fallback_range);
  }
}

/** Check all owned call layouts against O(1) direct or metadata signatures. */
void check_typed_call_literals(
    const ResolvedModule& module, const ResolvedFunction& function,
    const OwnedSignatureIndex& signatures,
    const std::unordered_map<uint32_t, CallArgumentProperties>& declarations,
    checker::CheckDiagnostics& diagnostics) {
  for (size_t index = 0; index < function.body.size(); ++index) {
    std::visit(
        [&](const auto& instruction) {
          if (instruction.get_resolved_descriptor().opcode_name != "call")
            return;
          std::visit(
              [&](const auto& selected) {
                if constexpr (requires { selected.operands; }) {
                  std::visit(
                      [&](const auto& operands) {
                        const declaration_semantics::FunctionSignature*
                            signature = nullptr;
                        if constexpr (requires { operands.metadata.value; }) {
                          if (const auto metadata = indirect_metadata_identity(
                                  operands.metadata.value)) {
                            signature =
                                metadata_signature(signatures, *metadata);
                          }
                        } else if constexpr (requires {
                                               operands.target.value.symbol_id;
                                             }) {
                          if (operands.target.value.symbol_id) {
                            signature = direct_signature(
                                module, signatures,
                                *operands.target.value.symbol_id);
                          }
                        }
                        if (signature == nullptr) {
                          append_model_mismatch(
                              diagnostics, function.instruction_ranges[index],
                              "Resolved module call has no retained formal "
                              "signature.");
                          return;
                        }
                        const ResolvedCallArguments* inputs = nullptr;
                        if constexpr (requires {
                                        operands.arguments.value.values;
                                      })
                          inputs = &operands.arguments.value;
                        const ResolvedCallParameterRef* returns = nullptr;
                        if constexpr (requires {
                                        operands.return_value.value.symbol_id;
                                      }) {
                          returns = &operands.return_value.value;
                        }
                        check_call_inputs(inputs, signature->parameters,
                                          declarations, diagnostics,
                                          function.instruction_ranges[index]);
                        check_call_returns(
                            returns, signature->return_parameters, declarations,
                            diagnostics, function.instruction_ranges[index]);
                      },
                      selected.operands);
                }
              },
              instruction.variant);
        },
        function.body[index]);
  }
}

/** Build checker target context from an owned source region rather than syntax. */
std::optional<checker::TargetInfo> owned_target_context(
    const ResolvedSourceTargetRegion& region,
    checker::CheckDiagnostics& diagnostics, SourceRange range) {
  if (!region.version || region.target_options.empty())
    return std::nullopt;
  const auto profile = base::find_target_profile(region.target_options.front());
  if (!profile) {
    diagnostics.push_back({
        .kind = checker::CheckDiagnosticKind::UnknownTarget,
        .range = range,
        .message = fmt::format("Unknown validation target '{}'.",
                               region.target_options.front()),
    });
    return std::nullopt;
  }
  return checker::TargetInfo{
      .ptx_version = *region.version,
      .sm_version = profile->identity.architecture.number,
      .enabled_family_features = profile->enabled_family_features,
      .identity = profile->identity,
      .capabilities = profile->capabilities,
  };
}

/** Check function-level availability using only owned source-level metadata. */
void check_function_contract_availability(
    const ResolvedFunction& function, const checker::TargetInfo& target,
    checker::CheckDiagnostics& diagnostics) {
  for (const auto& attribute : function.contract.attributes) {
    const bool managed =
        attribute.kind == ResolvedFunctionAttributeKind::Managed;
    append_requirement(
        diagnostics,
        availability(
            managed ? checker::PtxVersion{4, 0} : checker::PtxVersion{8, 0},
            managed ? 30 : 90),
        target, attribute.range,
        managed ? ".attribute(.managed)" : ".attribute(.unified)");
  }
  if (function.contract.is_noreturn)
    append_requirement(diagnostics,
                       directive_availability(DirectiveAvailability::Noreturn),
                       target, function.range, ".noreturn");
  if (function.contract.abi_preserve)
    append_requirement(
        diagnostics, directive_availability(DirectiveAvailability::AbiPreserve),
        target, function.contract.abi_preserve->range, ".abi_preserve");
  if (function.contract.abi_preserve_control)
    append_requirement(
        diagnostics,
        directive_availability(DirectiveAvailability::AbiPreserveControl),
        target, function.contract.abi_preserve_control->range,
        ".abi_preserve_control");
  if (function.contract.blocks_are_clusters)
    append_requirement(diagnostics, availability({9, 0}, 90, "cluster"), target,
                       function.range, ".blocksareclusters");
  if (function.contract.language_values)
    append_requirement(diagnostics, availability({9, 3}), target,
                       function.range, ".language");
  for (const auto& resource : function.contract.resources)
    append_requirement(diagnostics, resource_availability(resource.kind),
                       target, resource.range, resource_name(resource.kind));
}

}  // namespace

checker::CheckResult validateModule(const ResolvedModule& module,
                                    ModuleValidationPolicy policy) {
  checker::CheckDiagnostics diagnostics;
  const auto invalid = [&](SourceRange range, std::string message) {
    diagnostics.push_back(
        {.kind = checker::CheckDiagnosticKind::ModuleSourceMismatch,
         .range = range,
         .message = std::move(message)});
  };
  if (!module.header.invalid_directives.empty()) {
    for (const auto range : module.header.invalid_directives)
      invalid(range,
              "Owned module header contains an invalid source directive.");
  }
  if (module.header.regions.empty())
    invalid(module.range, "Resolved module has no owned source configuration.");
  for (const auto& region : module.header.regions) {
    check_header_region(region, region.range, diagnostics);
    if (!region.target_options.empty()) {
      const auto target =
          owned_target_context(region, diagnostics, region.range);
      if (policy == ModuleValidationPolicy::RequireCompleteContext &&
          (!region.version || !target)) {
        diagnostics.push_back({
            .kind = checker::CheckDiagnosticKind::MissingValidationContext,
            .range = region.range,
            .message = "Complete validation requires a version and recognized "
                       "target for every target region.",
        });
      }
    }
  }
  const OwnedSignatureIndex signatures =
      build_signature_index(module, diagnostics);
  const auto parameter_properties = build_parameter_properties(module);
  for (const auto& alias : module.function_aliases) {
    const auto* symbol = owned_symbol(module, alias.symbol_id);
    if (symbol == nullptr || symbol->kind != binding::SymbolKind::Function ||
        symbol->canonical_function != alias.canonical_function) {
      invalid(alias.range,
              "Resolved function alias has an invalid canonical identity.");
    }
    if (!alias.source_region ||
        *alias.source_region >= module.header.regions.size()) {
      invalid(
          alias.range,
          "Resolved function alias refers to a missing source configuration.");
    } else if (const auto target = owned_target_context(
                   module.header.regions[*alias.source_region], diagnostics,
                   alias.range);
               target) {
      append_requirement(diagnostics,
                         directive_availability(DirectiveAvailability::Alias),
                         *target, alias.range, ".alias");
    }
  }
  for (const auto& function : module.functions) {
    const auto* symbol = owned_symbol(module, function.symbol_id);
    if (symbol == nullptr || symbol->kind != binding::SymbolKind::Function ||
        symbol->name != function.name ||
        symbol->scope != module.symbols.moduleScope()) {
      invalid(function.range,
              "Resolved function identity does not match the symbol table.");
    } else if (symbol->canonical_function.value_or(symbol->id) !=
               function.contract.canonical_function) {
      invalid(
          function.range,
          "Resolved function contract has a mismatched canonical identity.");
    }
    if (function.declaration_scope.value >= module.symbols.scopes().size() ||
        module.symbols.scope(function.declaration_scope).kind !=
            binding::ScopeKind::Function ||
        module.symbols.scope(function.declaration_scope).owner !=
            function.symbol_id) {
      invalid(function.range,
              "Resolved function has an invalid declaration scope.");
    }
    check_signature(function.contract.signature, function.range, diagnostics);
    check_function_contract_integrity(function, diagnostics);
    const bool complete_instruction_provenance =
        function.instruction_ranges.size() == function.body.size() &&
        function.instruction_opcodes.size() == function.body.size();
    if (!complete_instruction_provenance) {
      invalid(
          function.range,
          "Resolved function instruction provenance does not match its body.");
    }
    for (const auto& label : function.label_positions) {
      const auto* label_symbol = owned_symbol(module, label.symbol_id);
      if (label.instruction_offset > function.body.size() ||
          label_symbol == nullptr ||
          label_symbol->kind != binding::SymbolKind::Label ||
          !is_function_owned_scope(module, label_symbol->scope,
                                   function.declaration_scope)) {
        invalid(function.range,
                "Resolved function has an invalid bound label position.");
      }
    }
    for (const auto& parameter : function.parameter_declarations) {
      const auto* parameter_symbol = owned_symbol(module, parameter.symbol_id);
      if (parameter_symbol == nullptr ||
          parameter_symbol->scope != parameter.scope_id ||
          !is_function_owned_scope(module, parameter.scope_id,
                                   function.declaration_scope)) {
        invalid(function.range,
                "Resolved parameter declaration has an invalid module-local "
                "identity.");
      }
      if (parameter.scalar_type == base::ScalarType::Invalid ||
          base::find_scalar_type_metadata(parameter.scalar_type) == nullptr) {
        invalid(function.range,
                "Resolved parameter declaration has an invalid scalar type.");
      }
    }
    check_resource_contracts(function, diagnostics);
    check_control_contracts(module, function, diagnostics);
    if (complete_instruction_provenance) {
      check_module_references(module, function, diagnostics);
      check_typed_call_literals(module, function, signatures,
                                parameter_properties, diagnostics);
    }
    if (function.source_region &&
        *function.source_region >= module.header.regions.size()) {
      invalid(
          function.range,
          "Resolved function refers to a missing owned source configuration.");
      continue;
    }
    const auto region = function.source_region
                            ? &module.header.regions[*function.source_region]
                            : nullptr;
    if (region != nullptr &&
        (function.source_version != region->version ||
         function.source_target !=
             (region->target_options.empty()
                  ? std::optional<std::string>{}
                  : std::optional<std::string>{
                        region->target_options.front()}))) {
      invalid(
          function.range,
          "Resolved function source configuration does not match its region.");
    }
    if (policy == ModuleValidationPolicy::RequireCompleteContext &&
        (!region || !region->version || region->target_options.empty())) {
      diagnostics.push_back({
          .kind = checker::CheckDiagnosticKind::MissingValidationContext,
          .range = function.range,
          .message = "Complete validation requires owned PTX version and "
                     "source target context.",
      });
    }
    if (region) {
      const auto target =
          owned_target_context(*region, diagnostics, function.range);
      if (target) {
        check_function_contract_availability(function, *target, diagnostics);
        if (complete_instruction_provenance)
          check_instruction_body(function, *target, diagnostics);
      }
    }
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

checker::CheckResult validateModule(const syntax_ast::AstModule& ast,
                                    const ResolvedModule& module,
                                    ModuleValidationPolicy policy) {
  if (auto associations = check_source_associations(ast, module); !associations)
    return associations;
  const auto version = detail::module_version(ast);
  checker::CheckDiagnostics diagnostics;
  // Retargeting must not bypass version/target-sensitive declaration rules.
  const auto rebound = binding::bindSymbols(ast);
  for (const auto& diagnostic : rebound.diagnostics) {
    diagnostics.push_back({
        .kind = checker::CheckDiagnosticKind::ModuleSourceMismatch,
        .range = diagnostic.range,
        .message = diagnostic.message,
    });
  }
  for (const auto& diagnostic :
       declaration_semantics::checkDeclarations(ast, rebound.table)) {
    using DeclarationKind = declaration_semantics::DeclarationDiagnosticKind;
    const bool availability =
        diagnostic.kind ==
            DeclarationKind::UnsupportedKernelResourcePtxVersion ||
        diagnostic.kind == DeclarationKind::UnsupportedDirectivePtxVersion ||
        diagnostic.kind == DeclarationKind::UnsupportedParameterDeclaration;
    diagnostics.push_back({
        .kind = availability
                    ? checker::CheckDiagnosticKind::UnsupportedAvailability
                    : checker::CheckDiagnosticKind::RuleViolation,
        .range = diagnostic.range,
        .message = diagnostic.message,
    });
  }
  std::optional<checker::TargetInfo> active_target;
  /** Strict validation may not silently skip a source region without context. */
  const auto require_context = [&](SourceRange range) {
    if (!active_target &&
        policy == ModuleValidationPolicy::RequireCompleteContext)
      diagnostics.push_back({
          .kind = checker::CheckDiagnosticKind::MissingValidationContext,
          .range = range,
          .message = "Complete validation requires a PTX version and a "
                     "recognized source target.",
      });
  };
  if (!version && policy == ModuleValidationPolicy::RequireCompleteContext)
    require_context(ast.range);

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
      require_context(variable->range);
      if (active_target)
        check_attributes(variable->attributes, *active_target, diagnostics);
    } else if (const auto* alias =
                   std::get_if<syntax_ast::AstAliasDirective>(&item)) {
      require_context(alias->range);
      if (active_target)
        append_requirement(diagnostics,
                           directive_availability(DirectiveAvailability::Alias),
                           *active_target, alias->range, ".alias");
    } else if (const auto* function =
                   std::get_if<syntax_ast::AstFunction>(&item)) {
      require_context(function->range);
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
      check_instruction_body(*source_function(*function, module),
                             *active_target, diagnostics);
    }
  }
  if (diagnostics.empty())
    return {};
  return std::unexpected(std::move(diagnostics));
}

checker::CheckResult checkModuleAvailability(const syntax_ast::AstModule& ast,
                                             const ResolvedModule& module) {
  return validateModule(ast, module, ModuleValidationPolicy::AvailableContext);
}

}  // namespace ptx_frontend::resolved_ir
