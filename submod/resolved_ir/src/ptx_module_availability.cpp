#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvta.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/model/tensor_memory/tcgen05.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_instruction_catalogue.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "ptx_module_source_context.hpp"
#include "ptx_resolved_ir_private.hpp"
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
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

namespace ptx_frontend::resolved_ir {
namespace {

/** Borrow an exact final class only after its dynamic type has been checked. */
template <typename T>
  requires std::derived_from<T, Instruction> && std::is_final_v<T>
const T* instruction_if(const std::unique_ptr<Instruction>& instruction) {
  return dynamic_cast<const T*>(instruction.get());
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
      if (!resolved.body[i]) {
        mismatch(instructions[i]->range,
                 "Resolved function contains a null instruction entry.");
        continue;
      }
      const auto opcode = resolved.body[i]->opcode_name();
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

/** Validate the owned texture payload after CST and AST have been discarded. */
void check_texture_instruction_payload(const Instruction& instruction,
                                       TextureMode mode,
                                       const checker::TargetInfo& target,
                                       SourceRange range,
                                       checker::CheckDiagnostics& diagnostics);

/** Check every IR instruction using its owned source location. */
void check_instruction_body(const ResolvedFunction& function,
                            const checker::TargetInfo& target,
                            TextureMode texture_mode,
                            checker::CheckDiagnostics& diagnostics) {
  for (size_t i = 0; i < function.body.size(); ++i) {
    if (!function.body[i])
      continue;
    const checker::Context context{
        .target = target,
        .instruction_range = function.instruction_ranges[i],
    };
    const auto result = function.body[i]->check(context);
    if (!result)
      diagnostics.insert(diagnostics.end(), result.error().begin(),
                         result.error().end());
    check_texture_instruction_payload(*function.body[i], texture_mode, target,
                                      function.instruction_ranges[i],
                                      diagnostics);
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

/** Borrow the texture operands of one final form for immediate validation. */
struct TexturePayloadObserver final : detail::IReferenceObserver {
  /** Access payload, present for tex and tld4 forms. */
  const ResolvedTextureAccess* access = nullptr;
  /** Data result, including a possible residency predicate. */
  const ResolvedTextureResult* result = nullptr;
  /** Bracketed query resource, present for txq forms. */
  const ResolvedTextureQueryResource* query_resource = nullptr;
  /** Capture one borrowed access without retaining it beyond this check. */
  void texture_access(const ResolvedTextureAccess& value,
                      std::span<const SourceRange>,
                      checker::AddressSymbolResolutionPolicy) override {
    access = &value;
  }
  /** Capture one borrowed result without retaining it beyond this check. */
  void texture_result(const ResolvedTextureResult& value,
                      std::span<const SourceRange>,
                      checker::AddressSymbolResolutionPolicy) override {
    result = &value;
  }
  /** Capture one borrowed query resource for kind and mode checks. */
  void texture_query_resource(const ResolvedTextureQueryResource& value,
                              std::span<const SourceRange>,
                              checker::AddressSymbolResolutionPolicy) override {
    query_resource = &value;
  }
};

/** Check source-independent texture topology and feature-specific gates. */
void check_texture_instruction_payload(const Instruction& instruction,
                                       TextureMode mode,
                                       const checker::TargetInfo& target,
                                       SourceRange range,
                                       checker::CheckDiagnostics& diagnostics) {
  const auto* descriptor = instruction.texture_descriptor();
  if (!descriptor)
    return;
  TexturePayloadObserver payload;
  instruction.visit_references(payload);
  const auto mismatch = [&](std::string message) {
    append_model_mismatch(diagnostics, range, std::move(message));
  };
  const bool independent = mode == TextureMode::Independent;
  if (descriptor->geometry) {
    if (!payload.access || !payload.result || payload.query_resource) {
      mismatch("Texture form has an invalid resource/result payload topology.");
      return;
    }
    const auto& access = *payload.access;
    const auto& result = *payload.result;
    if (access.texture.expected_kind != base::OpaqueResourceKind::Texture ||
        static_cast<bool>(access.sampler) != independent ||
        (access.sampler &&
         access.sampler->expected_kind != base::OpaqueResourceKind::Sampler))
      mismatch(
          "Texture resource kinds or sampler presence disagree with mode.");
    if (independent) {
      if (!std::holds_alternative<ResolvedOpaqueSymbolRef>(
              access.texture.value) ||
          (access.sampler && !std::holds_alternative<ResolvedOpaqueSymbolRef>(
                                 access.sampler->value)))
        mismatch(
            "Independent texture access requires direct resource symbols.");
    } else if (std::holds_alternative<ResolvedRegisterRef>(
                   access.texture.value)) {
      if (descriptor->indirect_availability)
        append_requirement(diagnostics, *descriptor->indirect_availability,
                           target, range, "indirect texture resource");
      else
        mismatch(
            "Texture form lacks an indirect resource availability contract.");
    }
    const checker::Context context{.target = target,
                                   .instruction_range = range};
    const auto static_check = checker::check_texture_static_payload(
        *descriptor, instruction.texture_selected_types(), access, result,
        instruction.texture_layout_requires_residency(), context);
    if (!static_check)
      diagnostics.insert(diagnostics.end(), static_check.error().begin(),
                         static_check.error().end());
  } else if (descriptor->query) {
    if (!payload.query_resource || payload.access || payload.result) {
      mismatch("Texture query has an invalid resource payload topology.");
      return;
    }
    const bool force =
        *descriptor->query == TextureQuery::ForceUnnormalizedCoords;
    if (force && !independent)
      mismatch("Force-unnormalized query requires independent texturing mode.");
    const bool sampler_query =
        force || *descriptor->query == TextureQuery::FilterMode ||
        *descriptor->query == TextureQuery::AddressMode0 ||
        *descriptor->query == TextureQuery::AddressMode1 ||
        *descriptor->query == TextureQuery::AddressMode2;
    const auto expected_kind = sampler_query && independent
                                   ? base::OpaqueResourceKind::Sampler
                                   : base::OpaqueResourceKind::Texture;
    const auto& resource = payload.query_resource->resource;
    if (resource.expected_kind != expected_kind ||
        !payload.query_resource->bracketed)
      mismatch("Query resource kind or bracket topology disagrees with mode.");
    if (independent &&
        !std::holds_alternative<ResolvedOpaqueSymbolRef>(resource.value))
      mismatch("Independent query requires a direct resource symbol.");
    if (std::holds_alternative<ResolvedRegisterRef>(resource.value))
      if (descriptor->indirect_availability)
        append_requirement(diagnostics, *descriptor->indirect_availability,
                           target, range, "indirect texture query");
      else
        mismatch(
            "Texture query lacks an indirect resource availability contract.");
  } else if (!descriptor->tested_kind) {
    mismatch("Texture-family descriptor has no operation identity.");
  }
}

/** One binding identity projected from a generated resolved operand payload. */
struct ModuleReferenceUse {
  std::optional<binding::SymbolId> symbol_id;
  std::optional<uint32_t> parameterized_index;
  std::optional<binding::SymbolKind> expected_kind;
  /** Owned snapshot of a register carrier's cached declaration metadata. */
  std::optional<ResolvedRegisterRef> register_ref;
  /** Copied address-symbol metadata; no visitor borrow escapes its callback. */
  std::optional<ResolvedSymbolRef> address_symbol;
  /** Cached direct resource identity, including its expected declared kind. */
  std::optional<ResolvedOpaqueSymbolRef> opaque_symbol;
  std::optional<base::OpaqueResourceKind> opaque_use_kind;
  /** Copied enclosing function context for an offset address. */
  std::optional<EnclosingFunctionKind> enclosing_address_function_kind;
  /** Immutable generated policy for parameter-address materialization. */
  checker::AddressSymbolResolutionPolicy address_resolution_policy{
      checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace};
  /** True when the binding must carry the .reg declaration state space. */
  bool requires_register_state{};
  /** True when the binding must name a scalar .reg .pred declaration. */
  bool requires_predicate_register{};
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
void append_reference(
    std::vector<ModuleReferenceUse>& uses,
    std::optional<binding::SymbolId> symbol_id,
    std::optional<uint32_t> parameterized_index,
    std::optional<binding::SymbolKind> expected_kind, bool function_local,
    std::span<const SourceRange> locations, SourceRange fallback,
    bool requires_register_state = false,
    bool requires_predicate_register = false,
    const ResolvedSymbolRef* address_symbol = nullptr,
    const ResolvedAddress* enclosing_address = nullptr,
    checker::AddressSymbolResolutionPolicy address_resolution_policy =
        checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace) {
  uses.push_back(
      {.symbol_id = symbol_id,
       .parameterized_index = parameterized_index,
       .expected_kind = expected_kind,
       .address_symbol =
           address_symbol ? std::optional{*address_symbol} : std::nullopt,
       .enclosing_address_function_kind =
           enclosing_address
               ? std::optional{enclosing_address->enclosing_function_kind}
               : std::nullopt,
       .address_resolution_policy = address_resolution_policy,
       .requires_register_state = requires_register_state,
       .requires_predicate_register = requires_predicate_register,
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
    std::same_as<std::remove_cvref_t<Value>, ResolvedCpAsyncSourceControl> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedPredicate> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedPredicateSource> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedBranchTarget> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedBranchTargetSet> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedVectorRegisterRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedSymbolRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedAddress> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedRegisterVector> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedValueVector> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedTensorCoordinate> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedTensorIm2colInfo> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedTensorOperand> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedFabricHandle> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedTextureAccess> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedTextureQueryResource> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedTextureResult> ||
    std::same_as<std::remove_cvref_t<Value>, TensorMemoryAddress> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedMatrixScaleSelector> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedSharedMatrixDescriptor> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedFunctionRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedIndirectCallee> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedCallParameterRef> ||
    std::same_as<std::remove_cvref_t<Value>, ResolvedCallArguments>;

/** Collect all nested binding identities from one generator-selected operand. */
template <ReferenceBearingOperandPayload Value>
void collect_operand_references(
    const Value& value, std::span<const SourceRange> locations,
    SourceRange fallback, std::vector<ModuleReferenceUse>& uses,
    checker::AddressSymbolResolutionPolicy address_resolution_policy,
    const ResolvedAddress* enclosing_address = nullptr) {
  const auto collect_register = [&](const ResolvedRegisterRef& register_ref,
                                    bool requires_predicate_register = false) {
    append_reference(uses, register_ref.symbol_id,
                     register_ref.parameterized_index,
                     binding::SymbolKind::Variable, true, locations, fallback,
                     true, requires_predicate_register);
    uses.back().register_ref = register_ref;
  };
  const auto collect_resource = [&](const ResolvedOpaqueResourceRef& resource,
                                    SourceRange resource_range) {
    const std::array<SourceRange, 1> range{resource_range};
    if (const auto* direct =
            std::get_if<ResolvedOpaqueSymbolRef>(&resource.value)) {
      const std::array<SourceRange, 1> direct_range{direct->range};
      append_reference(uses, direct->symbol_id, direct->parameterized_index,
                       std::nullopt, false, direct_range, fallback);
      uses.back().opaque_symbol = *direct;
      uses.back().opaque_use_kind = resource.expected_kind;
    } else if (const auto* indirect =
                   std::get_if<ResolvedRegisterRef>(&resource.value)) {
      collect_operand_references(*indirect, range, fallback, uses,
                                 address_resolution_policy);
      uses.back().opaque_use_kind = resource.expected_kind;
    }
  };
  if constexpr (std::same_as<Value, ResolvedRegisterRef>) {
    collect_register(value);
  } else if constexpr (std::same_as<Value, ResolvedPredicate>) {
    collect_register(value.register_ref, true);
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
                     value.declaration_kind, false, locations, fallback, false,
                     false, &value, enclosing_address,
                     address_resolution_policy);
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
  } else if constexpr (std::same_as<Value, ResolvedCpAsyncSourceControl>) {
    if (const auto* register_ref = std::get_if<ResolvedRegisterRef>(&value))
      collect_register(*register_ref);
    if (const auto* predicate = std::get_if<ResolvedPredicate>(&value))
      collect_register(predicate->register_ref, true);
    if (const auto* policy = std::get_if<ResolvedCpAsyncCachePolicy>(&value))
      collect_register(policy->register_ref);
  } else if constexpr (std::same_as<Value, ResolvedPredicatePair>) {
    collect_register(value.first.register_ref, true);
    collect_register(value.second.register_ref, true);
  } else if constexpr (std::same_as<Value, ResolvedPredicatePairOrSink>) {
    if (value.first)
      collect_register(value.first->register_ref, true);
    if (value.second)
      collect_register(value.second->register_ref, true);
  } else if constexpr (std::same_as<Value, ResolvedPredicateOrSink>) {
    if (value.predicate)
      collect_register(value.predicate->register_ref, true);
  } else if constexpr (std::same_as<Value, ResolvedPredicateSource>) {
    if (const auto* predicate = std::get_if<ResolvedPredicate>(&value))
      collect_register(predicate->register_ref, true);
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
  } else if constexpr (std::same_as<Value, ResolvedValueVector>) {
    for (size_t index = 0; index < value.elements.size(); ++index) {
      const auto* register_ref =
          std::get_if<ResolvedRegisterRef>(&value.elements[index]);
      if (!register_ref)
        continue;
      const std::array<SourceRange, 1> lane_range{
          index < locations.size() ? locations[index] : fallback};
      collect_operand_references(*register_ref, lane_range, fallback, uses,
                                 address_resolution_policy);
    }
  } else if constexpr (std::same_as<Value, ResolvedTensorCoordinate>) {
    for (const auto& element : value.elements)
      if (const auto* register_ref = std::get_if<ResolvedRegisterRef>(&element))
        collect_register(*register_ref);
  } else if constexpr (std::same_as<Value, ResolvedTensorIm2colInfo>) {
    for (const auto& element : value.elements)
      if (const auto* register_ref = std::get_if<ResolvedRegisterRef>(&element))
        collect_register(*register_ref);
  } else if constexpr (std::same_as<Value, TensorMemoryAddress>) {
    collect_operand_references(value.value, locations, fallback, uses,
                               address_resolution_policy);
  } else if constexpr (std::same_as<Value, ResolvedMatrixScaleSelector>) {
    collect_operand_references(value.byte_id, locations, fallback, uses,
                               address_resolution_policy);
    collect_operand_references(value.thread_id, locations, fallback, uses,
                               address_resolution_policy);
  } else if constexpr (std::same_as<Value, ResolvedSharedMatrixDescriptor>) {
    collect_register(value.register_ref);
  } else if constexpr (std::same_as<Value, ResolvedTensorOperand>) {
    const std::array<SourceRange, 1> map_range{value.tensor_map.range};
    collect_operand_references(value.tensor_map.address, map_range, fallback,
                               uses, address_resolution_policy);
    for (size_t index = 0; index < value.coordinates.elements.size(); ++index) {
      const auto* register_ref =
          std::get_if<ResolvedRegisterRef>(&value.coordinates.elements[index]);
      if (!register_ref)
        continue;
      const std::array<SourceRange, 1> coordinate_range{
          index < value.coordinate_ranges.size()
              ? value.coordinate_ranges[index]
              : fallback};
      collect_operand_references(*register_ref, coordinate_range, fallback,
                                 uses, address_resolution_policy);
    }
  } else if constexpr (std::same_as<Value, ResolvedFabricHandle>) {
    collect_operand_references(value.endpoint.value, value.endpoint.locs,
                               fallback, uses, address_resolution_policy);
    collect_operand_references(value.data_offset.value, value.data_offset.locs,
                               fallback, uses, address_resolution_policy);
    if (value.counter_offset)
      collect_operand_references(value.counter_offset->value,
                                 value.counter_offset->locs, fallback, uses,
                                 address_resolution_policy);
  } else if constexpr (std::same_as<Value, ResolvedTextureAccess>) {
    collect_resource(value.texture, value.texture.source_range);
    if (value.sampler)
      collect_resource(*value.sampler, value.sampler->source_range);
    for (const auto& lane : value.coordinates) {
      const std::array<SourceRange, 1> range{lane.range};
      collect_operand_references(lane.value, range, fallback, uses,
                                 address_resolution_policy);
    }
  } else if constexpr (std::same_as<Value, ResolvedTextureQueryResource>) {
    collect_resource(value.resource, value.resource.source_range);
  } else if constexpr (std::same_as<Value, ResolvedTextureResult>) {
    for (size_t index = 0; index < value.data.elements.size(); ++index) {
      if (!value.data.elements[index])
        continue;
      const std::array<SourceRange, 1> range{index < value.data_ranges.size()
                                                 ? value.data_ranges[index]
                                                 : fallback};
      collect_operand_references(*value.data.elements[index], range, fallback,
                                 uses, address_resolution_policy);
    }
    if (value.residency)
      collect_operand_references(
          *value.residency, std::array<SourceRange, 1>{value.residency_range},
          fallback, uses, address_resolution_policy);
  } else if constexpr (std::same_as<Value, ResolvedAddress>) {
    if (const auto* register_ref =
            std::get_if<ResolvedRegisterRef>(&value.base))
      collect_register(*register_ref);
    else if (const auto* symbol = std::get_if<ResolvedSymbolRef>(&value.base))
      collect_operand_references(*symbol, locations, fallback, uses,
                                 address_resolution_policy, &value);
  } else if constexpr (std::same_as<Value, ResolvedMovSource>) {
    std::visit(
        [&](const auto& source) {
          using Source = std::remove_cvref_t<decltype(source)>;
          if constexpr (std::same_as<Source, ResolvedRegisterRef> ||
                        std::same_as<Source, ResolvedFunctionRef> ||
                        std::same_as<Source, ResolvedSymbolRef> ||
                        std::same_as<Source, ResolvedAddress>)
            collect_operand_references(source, locations, fallback, uses,
                                       address_resolution_policy);
          else if constexpr (std::same_as<Source, ResolvedOpaqueSymbolRef>) {
            append_reference(uses, source.symbol_id, source.parameterized_index,
                             std::nullopt, false, locations, fallback);
            uses.back().opaque_symbol = source;
            uses.back().opaque_use_kind = source.kind;
          }
        },
        value);
  } else if constexpr (std::same_as<Value, ResolvedShflSyncDestination>) {
    if (value.data)
      collect_register(value.data->value);
    if (value.predicate)
      collect_register(value.predicate->value.register_ref, true);
  } else if constexpr (std::same_as<Value, ResolvedCallArguments>) {
    for (const auto& argument : value.values) {
      if (const auto* parameter =
              std::get_if<ResolvedCallParameterRef>(&argument.value))
        collect_operand_references(*parameter, argument.locs, fallback, uses,
                                   address_resolution_policy);
    }
  } else {
    static_assert(
        !sizeof(Value),
        "Reference-bearing generated operand needs collector support.");
  }
}

/** Collect every fixed foundation domain without RTTI or erased payload casts. */
class ReferenceCollector final : public detail::IReferenceObserver {
 public:
  /** Borrow destination and instruction fallback only for one synchronous visit. */
  ReferenceCollector(std::vector<ModuleReferenceUse>& uses,
                     SourceRange fallback)
      : uses_(uses), fallback_(fallback) {}
  /** Collect declaration identities from a borrowed RegOrImm. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange> locations,
                  checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedAddress. */
  void address(const ResolvedAddress& value,
               std::span<const SourceRange> locations,
               checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedBranchTarget. */
  void branch_target(const ResolvedBranchTarget& value,
                     std::span<const SourceRange> locations,
                     checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedBranchTargetSet. */
  void branch_target_set(
      const ResolvedBranchTargetSet& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedCallArguments. */
  void call_arguments(const ResolvedCallArguments& value,
                      std::span<const SourceRange> locations,
                      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedCallParameterRef. */
  void call_parameter_ref(
      const ResolvedCallParameterRef& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedCpAsyncSourceControl. */
  void cp_async_source_control(
      const ResolvedCpAsyncSourceControl& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedFunctionRef. */
  void function_ref(const ResolvedFunctionRef& value,
                    std::span<const SourceRange> locations,
                    checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedIndirectCallee. */
  void indirect_callee(const ResolvedIndirectCallee& value,
                       std::span<const SourceRange> locations,
                       checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedMbarrierStateToken. */
  void mbarrier_state_token(
      const ResolvedMbarrierStateToken& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedMovSource. */
  void mov_source(const ResolvedMovSource& value,
                  std::span<const SourceRange> locations,
                  checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedPredicate. */
  void predicate(const ResolvedPredicate& value,
                 std::span<const SourceRange> locations,
                 checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedPredicateOrSink. */
  void predicate_or_sink(
      const ResolvedPredicateOrSink& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedPredicatePair. */
  void predicate_pair(const ResolvedPredicatePair& value,
                      std::span<const SourceRange> locations,
                      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedPredicatePairOrSink. */
  void predicate_pair_or_sink(
      const ResolvedPredicatePairOrSink& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedPredicateSource. */
  void predicate_source(
      const ResolvedPredicateSource& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedRegisterOrSink. */
  void register_or_sink(
      const ResolvedRegisterOrSink& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedRegisterRef. */
  void reg(const ResolvedRegisterRef& value,
           std::span<const SourceRange> locations,
           checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedRegisterVector. */
  void register_vector(const ResolvedRegisterVector& value,
                       std::span<const SourceRange> locations,
                       checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect bound register lanes from a borrowed source value vector. */
  void value_vector(const ResolvedValueVector& value,
                    std::span<const SourceRange> locations,
                    checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedShflSyncDestination. */
  void shfl_sync_destination(
      const ResolvedShflSyncDestination& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedSymbolRef. */
  void symbol_ref(const ResolvedSymbolRef& value,
                  std::span<const SourceRange> locations,
                  checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedTensorCoordinate. */
  void tensor_coordinate(
      const ResolvedTensorCoordinate& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect register elements in a borrowed im2col information pack. */
  void tensor_im2col_info(
      const ResolvedTensorIm2colInfo& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect descriptor and coordinate references with their owned source ranges. */
  void tensor_operand(const ResolvedTensorOperand& value,
                      std::span<const SourceRange> locations,
                      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect each bound register in a borrowed transport handle. */
  void fabric_handle(const ResolvedFabricHandle& value,
                     std::span<const SourceRange> locations,
                     checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect bound resource heads and coordinate registers in an access. */
  void texture_access(const ResolvedTextureAccess& value,
                      std::span<const SourceRange> locations,
                      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect the direct or indirect bracketed query resource. */
  void texture_query_resource(
      const ResolvedTextureQueryResource& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect data and optional residency destination identities. */
  void texture_result(const ResolvedTextureResult& value,
                      std::span<const SourceRange> locations,
                      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect a borrowed Tensor Memory address register, when present. */
  void tensor_memory_address(
      const TensorMemoryAddress& value, std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect both borrowed matrix scale selector registers, when present. */
  void matrix_scale_selector(
      const ResolvedMatrixScaleSelector& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect the bound register carrying a shared matrix descriptor. */
  void shared_matrix_descriptor(
      const ResolvedSharedMatrixDescriptor& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }
  /** Collect declaration identities from a borrowed ResolvedVectorRegisterRef. */
  void vector_register_ref(
      const ResolvedVectorRegisterRef& value,
      std::span<const SourceRange> locations,
      checker::AddressSymbolResolutionPolicy policy) override {
    collect_operand_references(value, locations, fallback_, uses_, policy);
  }

 private:
  /** Borrowed ordered destination, valid for this visitor's stack lifetime. */
  std::vector<ModuleReferenceUse>& uses_;
  /** Owned instruction source range used when an operand has no explicit range. */
  SourceRange fallback_;
};
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

/** Return the owned role required for a bound formal parameter symbol. */
std::optional<ParameterDeclarationRole> expected_parameter_role(
    const binding::Symbol& symbol, const ResolvedFunction& function) {
  if (symbol.kind == binding::SymbolKind::InputParameter) {
    return function.is_entry ? ParameterDeclarationRole::EntryInput
                             : ParameterDeclarationRole::DeviceInput;
  }
  if (symbol.kind == binding::SymbolKind::ReturnParameter &&
      !function.is_entry) {
    return ParameterDeclarationRole::DeviceReturn;
  }
  return std::nullopt;
}

/** Cross-check cached address metadata against its immutable symbol binding. */
void check_address_symbol_binding(const binding::Symbol& bound_symbol,
                                  const ModuleReferenceUse& use,
                                  const ResolvedFunction& function,
                                  checker::CheckDiagnostics& diagnostics) {
  if (!use.address_symbol)
    return;

  const ResolvedSymbolRef& cached = *use.address_symbol;
  const EnclosingFunctionKind expected_function_kind =
      function.is_entry ? EnclosingFunctionKind::Entry
                        : EnclosingFunctionKind::Device;
  const bool materialized_device_parameter =
      use.address_resolution_policy ==
          checker::AddressSymbolResolutionPolicy::MaterializeDeviceParameter &&
      !function.is_entry &&
      (bound_symbol.kind == binding::SymbolKind::InputParameter ||
       bound_symbol.kind == binding::SymbolKind::ReturnParameter) &&
      bound_symbol.state_space == syntax_ast::AstStateSpace::Parameter;
  const auto expected_address_space = materialized_device_parameter
                                          ? syntax_ast::AstStateSpace::Local
                                          : bound_symbol.state_space;
  const auto expected_declared_type =
      bound_symbol.type ? detail::scalar_type_from_ptx_name(*bound_symbol.type)
                        : std::nullopt;
  const bool matching_declaration =
      cached.declaration_kind == bound_symbol.kind &&
      cached.declaration_state_space == bound_symbol.state_space &&
      cached.address_state_space == expected_address_space &&
      cached.declared_type == expected_declared_type &&
      cached.address_alignment == bound_symbol.address_alignment &&
      cached.enclosing_function_kind == expected_function_kind;
  const bool matching_address_context =
      !use.enclosing_address_function_kind ||
      *use.enclosing_address_function_kind == expected_function_kind;
  if (!matching_declaration || !matching_address_context) {
    append_model_mismatch(diagnostics, use.range,
                          "Resolved address operand metadata disagrees with "
                          "its bound declaration.");
  }

  const auto role = expected_parameter_role(bound_symbol, function);
  if (!role)
    return;
  const auto declaration = std::ranges::find_if(
      function.parameter_declarations, [&](const auto& candidate) {
        return candidate.symbol_id == bound_symbol.id;
      });
  if (declaration == function.parameter_declarations.end() ||
      declaration->role != *role) {
    append_model_mismatch(diagnostics, use.range,
                          "Resolved address operand has an incompatible "
                          "parameter declaration role.");
  }
}

/** Revalidate identities embedded in generated instruction operand payloads. */
void check_module_references(const ResolvedModule& module,
                             const ResolvedFunction& function,
                             checker::CheckDiagnostics& diagnostics) {
  std::vector<ModuleReferenceUse> uses;
  for (size_t index = 0; index < function.body.size(); ++index) {
    if (!function.body[index])
      continue;
    ReferenceCollector collector(uses, function.instruction_ranges[index]);
    function.body[index]->visit_references(collector);
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
    const bool compatible_predicate =
        !use.requires_predicate_register ||
        (symbol->type && *symbol->type == ".pred" && !symbol->vector_width);
    if (!compatible_kind || !compatible_predicate ||
        !is_operand_scope(module, symbol->scope, function.declaration_scope) ||
        (use.function_local &&
         !is_function_owned_scope(module, symbol->scope,
                                  function.declaration_scope))) {
      append_model_mismatch(
          diagnostics, use.range,
          "Resolved module operand has an incompatible declaration identity.");
      continue;
    }
    if (use.register_ref) {
      const auto declared =
          symbol->type ? detail::scalar_type_from_ptx_name(*symbol->type)
                       : std::nullopt;
      const auto& cached = *use.register_ref;
      if (!declared || cached.declared_type != declared ||
          cached.vector_width != symbol->vector_width ||
          cached.register_class != (*declared == ScalarType::Pred
                                        ? ResolvedRegisterClass::Predicate
                                        : ResolvedRegisterClass::General)) {
        append_model_mismatch(
            diagnostics, use.range,
            "Resolved register metadata disagrees with its owned declaration.");
      }
    }
    if (use.opaque_use_kind &&
        *use.opaque_use_kind != base::OpaqueResourceKind::Texture &&
        *use.opaque_use_kind != base::OpaqueResourceKind::Sampler &&
        *use.opaque_use_kind != base::OpaqueResourceKind::Surface)
      append_model_mismatch(
          diagnostics, use.range,
          "Resolved resource use has an invalid opaque kind.");
    if (use.opaque_symbol) {
      const auto& cached = *use.opaque_symbol;
      const bool global =
          symbol->kind == binding::SymbolKind::Variable &&
          symbol->scope == module.symbols.moduleScope() &&
          symbol->state_space == base::DeclarationStateSpace::Global;
      const bool entry =
          function.is_entry &&
          symbol->kind == binding::SymbolKind::InputParameter &&
          symbol->scope == function.declaration_scope &&
          symbol->state_space == base::DeclarationStateSpace::Parameter;
      if ((!global && !entry) || !symbol->type ||
          base::opaque_resource_kind(*symbol->type) != cached.kind ||
          cached.kind != use.opaque_use_kind ||
          cached.symbol_id != symbol->id || cached.scope_id != symbol->scope ||
          cached.entry_input != entry || cached.range != use.range)
        append_model_mismatch(diagnostics, use.range,
                              "Resolved direct resource metadata disagrees "
                              "with its bound declaration.");
    }
    if (use.opaque_use_kind && use.register_ref) {
      const auto type = use.register_ref->declared_type;
      if (!type || base::scalar_size_of(*type) != 8 ||
          (base::scalar_kind(*type) != base::ScalarKind::Bit &&
           base::scalar_kind(*type) != base::ScalarKind::Signed &&
           base::scalar_kind(*type) != base::ScalarKind::Unsigned))
        append_model_mismatch(diagnostics, use.range,
                              "Indirect resource carrier is not a scalar "
                              "64-bit integer or bit register.");
    }
    if (use.parameterized_index &&
        (!symbol->parameterized_count ||
         *use.parameterized_index >= *symbol->parameterized_count)) {
      append_model_mismatch(
          diagnostics, use.range,
          "Resolved module operand has an invalid parameterized member index.");
    }
    check_address_symbol_binding(*symbol, use, function, diagnostics);
  }
}

/** Return a written TCGEN CTA group from the exact owned form, if any. */
template <typename T>
  requires std::derived_from<T, Instruction> && std::is_final_v<T>
std::optional<TcgenCtaGroup> tcgen_group_if(const Instruction& instruction) {
  const auto* form = dynamic_cast<const T*>(&instruction);
  return form ? std::optional{form->cta_group.value} : std::nullopt;
}

/** Select the group only from a matching final TCGEN instruction class. */
std::optional<TcgenCtaGroup> tcgen_cta_group(const Instruction& instruction) {
  switch (instruction.instruction_kind()) {
    case InstructionKind::Tcgen05AllocGeneric:
      return tcgen_group_if<Tcgen05AllocGeneric>(instruction);
    case InstructionKind::Tcgen05AllocSharedCta:
      return tcgen_group_if<Tcgen05AllocSharedCta>(instruction);
    case InstructionKind::Tcgen05Dealloc:
      return tcgen_group_if<Tcgen05Dealloc>(instruction);
    case InstructionKind::Tcgen05RelinquishAllocPermit:
      return tcgen_group_if<Tcgen05RelinquishAllocPermit>(instruction);
    case InstructionKind::Tcgen05CommitGroup1GenericSingle:
      return tcgen_group_if<Tcgen05CommitGroup1GenericSingle>(instruction);
    case InstructionKind::Tcgen05CommitGroup1GenericMulticast:
      return tcgen_group_if<Tcgen05CommitGroup1GenericMulticast>(instruction);
    case InstructionKind::Tcgen05CommitGroup1SharedClusterSingle:
      return tcgen_group_if<Tcgen05CommitGroup1SharedClusterSingle>(
          instruction);
    case InstructionKind::Tcgen05CommitGroup1SharedClusterMulticast:
      return tcgen_group_if<Tcgen05CommitGroup1SharedClusterMulticast>(
          instruction);
    case InstructionKind::Tcgen05CommitGroup2GenericSingle:
      return tcgen_group_if<Tcgen05CommitGroup2GenericSingle>(instruction);
    case InstructionKind::Tcgen05CommitGroup2GenericMulticast:
      return tcgen_group_if<Tcgen05CommitGroup2GenericMulticast>(instruction);
    case InstructionKind::Tcgen05CommitGroup2SharedClusterSingle:
      return tcgen_group_if<Tcgen05CommitGroup2SharedClusterSingle>(
          instruction);
    case InstructionKind::Tcgen05CommitGroup2SharedClusterMulticast:
      return tcgen_group_if<Tcgen05CommitGroup2SharedClusterMulticast>(
          instruction);
    case InstructionKind::Tcgen05Cp:
      return tcgen_group_if<Tcgen05Cp>(instruction);
    case InstructionKind::Tcgen05MmaF16:
      return tcgen_group_if<Tcgen05MmaF16>(instruction);
    case InstructionKind::Tcgen05MmaTf32:
      return tcgen_group_if<Tcgen05MmaTf32>(instruction);
    case InstructionKind::Tcgen05MmaI8:
      return tcgen_group_if<Tcgen05MmaI8>(instruction);
    case InstructionKind::Tcgen05Shift:
      return tcgen_group_if<Tcgen05Shift>(instruction);
    default:
      return std::nullopt;
  }
}

/** Compare only written TCGEN groups within one owned function body. */
void check_tcgen_cta_groups(const ResolvedFunction& function,
                            checker::CheckDiagnostics& diagnostics) {
  std::optional<TcgenCtaGroup> group;
  for (size_t index = 0; index < function.body.size(); ++index) {
    if (!function.body[index])
      continue;
    const auto current = tcgen_cta_group(*function.body[index]);
    if (!current)
      continue;
    if (!group) {
      group = current;
    } else if (*group != *current) {
      diagnostics.push_back({
          .kind = checker::CheckDiagnosticKind::RuleViolation,
          .range = function.instruction_ranges[index],
          .message = "Tensor Memory CTA group conflicts with another "
                     "instruction in this function body.",
      });
    }
  }
}

/** Return whether an entry input declares a pointer to constant storage. */
bool has_kernel_constant_pointer_parameter(const ResolvedModule& module) {
  for (const auto& function : module.functions) {
    if (!function.is_entry)
      continue;
    for (const auto& parameter : function.parameter_declarations) {
      if (parameter.role != ParameterDeclarationRole::EntryInput ||
          !parameter.pointer ||
          parameter.pointer->pointed_state_space !=
              call_argument_compatibility::PointedStateSpace::Constant) {
        continue;
      }
      return true;
    }
  }
  return false;
}

/** Reject generic constant-address creation with a kernel constant pointer. */
void check_cvta_constant_pointer_restriction(
    const ResolvedModule& module, checker::CheckDiagnostics& diagnostics) {
  if (!has_kernel_constant_pointer_parameter(module))
    return;
  for (const auto& function : module.functions) {
    for (size_t index = 0; index < function.body.size(); ++index) {
      if (!instruction_if<CvtaConstU32>(function.body[index]) &&
          !instruction_if<CvtaConstU64>(function.body[index])) {
        continue;
      }
      diagnostics.push_back({
          .kind = checker::CheckDiagnosticKind::RuleViolation,
          .range = index < function.instruction_ranges.size()
                       ? function.instruction_ranges[index]
                       : function.range,
          .message = "cvta.const cannot create a generic constant pointer in a "
                     "module with a kernel .ptr.const parameter.",
      });
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
  size_t mode_count = 0;
  for (const auto& option : region.target_options) {
    if (option == "texmode_unified" || option == "texmode_independent") {
      ++mode_count;
    }
  }
  if (mode_count > 1)
    append_model_mismatch(
        diagnostics, range,
        "Resolved target region selects multiple texturing modes.");
}

/** Check the single effective texturing mode across all owned target regions. */
void check_module_texture_mode(const ResolvedModuleHeader& header,
                               checker::CheckDiagnostics& diagnostics) {
  std::optional<TextureMode> explicit_mode;
  for (const auto& region : header.regions) {
    for (const auto& option : region.target_options) {
      if (option != "texmode_unified" && option != "texmode_independent")
        continue;
      const auto mode = option == "texmode_independent"
                            ? TextureMode::Independent
                            : TextureMode::Unified;
      if (explicit_mode && *explicit_mode != mode)
        append_model_mismatch(
            diagnostics, region.range,
            "PTX module selects conflicting texturing modes.");
      else
        explicit_mode = mode;
    }
  }
  const auto effective = explicit_mode.value_or(TextureMode::Unified);
  for (const auto& region : header.regions)
    if (region.texture_mode != effective)
      append_model_mismatch(
          diagnostics, region.range,
          "Resolved texturing mode disagrees with the module selection.");
  if (effective == TextureMode::Independent && !header.regions.empty() &&
      header.regions.front().version &&
      *header.regions.front().version < checker::PtxVersion{1, 5})
    diagnostics.push_back({
        .kind = checker::CheckDiagnosticKind::UnsupportedAvailability,
        .range = header.regions.front().range,
        .message = "Independent texturing mode requires PTX ISA >= 1.5.",
    });
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
  const auto check_parameters = [&](const auto& parameters, bool entry_inputs) {
    for (const auto& parameter : parameters) {
      const bool valid_opaque =
          parameter.opaque_kind && entry_inputs &&
          parameter.scalar_type == base::ScalarType::Invalid &&
          parameter.state_space ==
              call_argument_compatibility::CallArgumentStateSpace::Parameter &&
          !parameter.is_pointer && !parameter.pointed_state_space &&
          !parameter.pointer_alignment;
      if (!valid_opaque &&
          (parameter.opaque_kind || !valid_scalar_contract(parameter))) {
        append_model_mismatch(
            diagnostics, range,
            "Resolved function signature has an invalid parameter type.");
      }
      if (parameter.state_space ==
          call_argument_compatibility::CallArgumentStateSpace::Invalid) {
        append_model_mismatch(
            diagnostics, range,
            "Resolved function signature has an invalid state space.");
      }
    }
  };
  check_parameters(signature.return_parameters, false);
  check_parameters(signature.parameters, signature.is_entry);
}

/** Revalidate name-only entry inputs against both the ABI and bound symbols. */
void check_opaque_entry_parameters(const ResolvedModule& module,
                                   const ResolvedFunction& function,
                                   std::optional<checker::PtxVersion> version,
                                   checker::CheckDiagnostics& diagnostics) {
  std::vector<base::OpaqueResourceKind> expected;
  for (const auto& parameter : function.contract.signature.parameters)
    if (parameter.opaque_kind)
      expected.push_back(*parameter.opaque_kind);
  if (!function.is_entry &&
      (!expected.empty() || !function.opaque_entry_parameters.empty())) {
    append_model_mismatch(diagnostics, function.range,
                          "Opaque parameters belong only to entry inputs.");
    return;
  }
  if (expected.size() != function.opaque_entry_parameters.size())
    append_model_mismatch(
        diagnostics, function.range,
        "Opaque entry parameter records do not match the signature.");

  const auto function_scope = module.symbols.functionScope(function.range);
  std::vector<binding::SymbolId> bound;
  if (function_scope) {
    for (const auto& symbol : module.symbols.symbols()) {
      if (symbol.scope == *function_scope &&
          symbol.kind == binding::SymbolKind::InputParameter && symbol.type &&
          base::opaque_resource_kind(*symbol.type))
        bound.push_back(symbol.id);
    }
  }
  if (bound.size() != function.opaque_entry_parameters.size())
    append_model_mismatch(
        diagnostics, function.range,
        "Bound opaque entry inputs do not match owned records.");
  for (size_t index = 0; index < function.opaque_entry_parameters.size();
       ++index) {
    const auto& record = function.opaque_entry_parameters[index];
    if (version && *version < checker::PtxVersion{1, 5})
      diagnostics.push_back({
          .kind = checker::CheckDiagnosticKind::RuleViolation,
          .range = record.range,
          .message = "Opaque entry parameter requires PTX ISA >= 1.5.",
      });
    const auto* symbol = owned_symbol(module, record.symbol_id);
    const auto* contract =
        [&]() -> const declaration_semantics::FunctionParameterContract* {
      size_t ordinal = 0;
      for (const auto& parameter : function.contract.signature.parameters) {
        if (!parameter.opaque_kind)
          continue;
        if (ordinal++ == index)
          return &parameter;
      }
      return nullptr;
    }();
    if (symbol == nullptr || !function_scope ||
        symbol->scope != *function_scope ||
        record.scope_id != *function_scope ||
        symbol->kind != binding::SymbolKind::InputParameter ||
        symbol->state_space != base::DeclarationStateSpace::Parameter ||
        !symbol->type ||
        base::opaque_resource_kind(*symbol->type) != record.kind ||
        (index < bound.size() && bound[index] != record.symbol_id) ||
        (index < expected.size() && expected[index] != record.kind) ||
        (contract &&
         (record.explicit_alignment !=
              declaration_semantics::contract_constant(contract->alignment) ||
          record.is_array != contract->is_array ||
          record.array_extent != declaration_semantics::contract_constant(
                                     contract->array_extent))) ||
        record.explicit_alignment != symbol->address_alignment) {
      append_model_mismatch(diagnostics, record.range,
                            "Opaque entry input identity or kind disagrees "
                            "with its declaration.");
    }
  }
}

/** Check whether a named resource field belongs to its declaration kind. */
bool opaque_field_for_kind(OpaqueStaticField field, StorageOpaqueType kind) {
  if (kind == StorageOpaqueType::Sampler)
    return field == OpaqueStaticField::ForceUnnormalizedCoords ||
           field == OpaqueStaticField::FilterMode ||
           field == OpaqueStaticField::AddressMode0 ||
           field == OpaqueStaticField::AddressMode1 ||
           field == OpaqueStaticField::AddressMode2;
  if (kind == StorageOpaqueType::Surface)
    return field == OpaqueStaticField::Width ||
           field == OpaqueStaticField::Height ||
           field == OpaqueStaticField::Depth ||
           field == OpaqueStaticField::ChannelDataType ||
           field == OpaqueStaticField::ChannelOrder ||
           field == OpaqueStaticField::ArraySize ||
           field == OpaqueStaticField::MemoryLayout;
  switch (field) {
    case OpaqueStaticField::Width:
    case OpaqueStaticField::Height:
    case OpaqueStaticField::Depth:
    case OpaqueStaticField::ChannelDataType:
    case OpaqueStaticField::ChannelOrder:
    case OpaqueStaticField::NormalizedCoords:
    case OpaqueStaticField::FilterMode:
    case OpaqueStaticField::AddressMode0:
    case OpaqueStaticField::AddressMode1:
    case OpaqueStaticField::AddressMode2:
    case OpaqueStaticField::ArraySize:
    case OpaqueStaticField::NumMipmapLevels:
    case OpaqueStaticField::NumSamples:
      return kind == StorageOpaqueType::Texture;
    case OpaqueStaticField::ForceUnnormalizedCoords:
    case OpaqueStaticField::MemoryLayout:
      return false;
  }
  return false;
}

/** Validate owned opaque declarations without consulting the source AST. */
void check_opaque_storage(const ResolvedModule& module,
                          checker::CheckDiagnostics& diagnostics) {
  const auto version = module.header.regions.empty()
                           ? std::optional<checker::PtxVersion>{}
                           : module.header.regions.front().version;
  for (const auto& symbol : module.symbols.symbols()) {
    if (symbol.scope != module.symbols.moduleScope() ||
        symbol.kind != binding::SymbolKind::Variable || !symbol.type ||
        !base::opaque_resource_kind(*symbol.type))
      continue;
    if (std::ranges::none_of(module.storage_declarations,
                             [&](const auto& declaration) {
                               return declaration.symbol_id == symbol.id &&
                                      std::holds_alternative<StorageOpaqueType>(
                                          declaration.element_type);
                             }))
      append_model_mismatch(
          diagnostics, symbol.declaration_range,
          "Bound opaque global has no owned storage declaration.");
  }
  for (const auto& declaration : module.storage_declarations) {
    const auto* kind =
        std::get_if<StorageOpaqueType>(&declaration.element_type);
    if (!kind) {
      if (declaration.legacy_texture ||
          !declaration.opaque_static_objects.empty() ||
          declaration.initialization == StorageInitializationKind::OpaqueStatic)
        append_model_mismatch(
            diagnostics, declaration.range,
            "Scalar storage carries opaque resource metadata.");
      continue;
    }
    if (!declaration.legacy_texture && version &&
        *version < checker::PtxVersion{1, 5})
      diagnostics.push_back({
          .kind = checker::CheckDiagnosticKind::RuleViolation,
          .range = declaration.range,
          .message =
              "Modern opaque resource declarations require PTX ISA >= 1.5.",
      });
    const auto* symbol = owned_symbol(module, declaration.symbol_id);
    const bool valid_kind = *kind == StorageOpaqueType::Texture ||
                            *kind == StorageOpaqueType::Sampler ||
                            *kind == StorageOpaqueType::Surface;
    const bool valid_symbol =
        valid_kind && symbol && symbol->kind == binding::SymbolKind::Variable &&
        symbol->scope == declaration.scope_id && symbol->type &&
        base::opaque_resource_kind(*symbol->type) == *kind &&
        symbol->state_space == base::DeclarationStateSpace::Global &&
        symbol->vector_width == std::nullopt &&
        symbol->address_alignment == declaration.explicit_alignment &&
        symbol->parameterized_count == declaration.parameterized_count;
    const bool valid_declaration =
        valid_symbol &&
        (declaration.declaration_kind == StorageDeclarationKind::External ||
         declaration.declaration_kind == StorageDeclarationKind::Definition) &&
        declaration.scope_id == module.symbols.moduleScope() &&
        !declaration.owner_function &&
        declaration.space == StorageSpace::Global &&
        declaration.vector_width == 1 && !declaration.byte_extent &&
        declaration.alignment == declaration.explicit_alignment &&
        (!declaration.legacy_texture || *kind == StorageOpaqueType::Texture) &&
        declaration.initializer.empty() && !declaration.is_dynamic_shared &&
        !declaration.is_managed && !declaration.unified_id &&
        (std::ranges::all_of(
             declaration.array_extents,
             [](const auto extent) { return extent && *extent > 0; }) ||
         (declaration.declaration_kind == StorageDeclarationKind::External &&
          !declaration.array_extents.empty() &&
          !declaration.array_extents.front() &&
          std::ranges::all_of(
              declaration.array_extents | std::views::drop(1),
              [](const auto extent) { return extent && *extent > 0; })));
    if (!valid_declaration) {
      append_model_mismatch(
          diagnostics, declaration.range,
          "Opaque storage declaration disagrees with its bound identity.");
      continue;
    }
    const bool static_initialization =
        declaration.initialization == StorageInitializationKind::OpaqueStatic;
    if ((declaration.declaration_kind == StorageDeclarationKind::External &&
         (static_initialization ||
          !declaration.opaque_static_objects.empty())) ||
        (static_initialization && declaration.opaque_static_objects.empty()) ||
        (!static_initialization &&
         !declaration.opaque_static_objects.empty()) ||
        (declaration.declaration_kind == StorageDeclarationKind::External &&
         declaration.initialization != StorageInitializationKind::External) ||
        (declaration.declaration_kind == StorageDeclarationKind::Definition &&
         declaration.initialization !=
             StorageInitializationKind::Uninitialized &&
         !static_initialization)) {
      append_model_mismatch(
          diagnostics, declaration.range,
          "Opaque storage has an invalid initialization state.");
    }
    std::vector<std::vector<uint64_t>> paths;
    for (const auto& object : declaration.opaque_static_objects) {
      bool valid_path =
          object.indices.size() == declaration.array_extents.size();
      for (size_t axis = 0; valid_path && axis < object.indices.size(); ++axis)
        valid_path = declaration.array_extents[axis] &&
                     object.indices[axis] < *declaration.array_extents[axis];
      if (!valid_path ||
          std::ranges::find(paths, object.indices) != paths.end() ||
          object.members.empty()) {
        append_model_mismatch(
            diagnostics, object.range,
            "Opaque static object has an invalid array position.");
      }
      paths.push_back(object.indices);
      std::vector<OpaqueStaticField> seen;
      for (const auto& member : object.members) {
        const bool duplicate =
            std::ranges::find(seen, member.field) != seen.end();
        seen.push_back(member.field);
        const bool field_valid = opaque_field_for_kind(member.field, *kind);
        const auto* enumeration = std::get_if<OpaqueStaticEnum>(&member.value);
        const auto* number = std::get_if<uint64_t>(&member.value);
        const bool channel_type =
            member.field == OpaqueStaticField::ChannelDataType;
        const bool channel_order =
            member.field == OpaqueStaticField::ChannelOrder;
        const bool filter = member.field == OpaqueStaticField::FilterMode;
        const bool address = member.field == OpaqueStaticField::AddressMode0 ||
                             member.field == OpaqueStaticField::AddressMode1 ||
                             member.field == OpaqueStaticField::AddressMode2;
        const bool enum_valid =
            enumeration &&
            ((channel_type && *enumeration >= OpaqueStaticEnum::ClSnormInt8 &&
              *enumeration <= OpaqueStaticEnum::ClFloat) ||
             (channel_order && *enumeration >= OpaqueStaticEnum::ClR &&
              *enumeration <= OpaqueStaticEnum::ClLuminance) ||
             (filter && (*enumeration == OpaqueStaticEnum::Nearest ||
                         *enumeration == OpaqueStaticEnum::Linear)) ||
             (address && *enumeration >= OpaqueStaticEnum::Wrap &&
              *enumeration <= OpaqueStaticEnum::ClampToBorder));
        const bool numeric_valid =
            number && !channel_type && !channel_order && !filter && !address &&
            ((member.field != OpaqueStaticField::NormalizedCoords &&
              member.field != OpaqueStaticField::ForceUnnormalizedCoords &&
              member.field != OpaqueStaticField::MemoryLayout) ||
             *number <= 1);
        if (duplicate || !field_valid || (!enum_valid && !numeric_valid))
          append_model_mismatch(
              diagnostics, member.range,
              "Opaque static member has an invalid field or value.");
      }
    }
  }
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
/** Check all owned call layouts against direct or metadata signatures. */
void check_typed_call_literals(
    const ResolvedModule& module, const ResolvedFunction& function,
    const OwnedSignatureIndex& signatures,
    const std::unordered_map<uint32_t, CallArgumentProperties>& declarations,
    checker::CheckDiagnostics& diagnostics) {
  /** Copy only values needed after the synchronous borrowed reference visit. */
  struct CallContractObserver final : detail::IReferenceObserver {
    /** Retained direct callee identity, when the selected layout binds one. */
    std::optional<binding::SymbolId> direct_target;
    /** Retained indirect metadata identity, when the layout binds one. */
    std::optional<binding::SymbolId> indirect_metadata;
    /** Owned argument group; no borrowed reference escapes a callback. */
    std::optional<ResolvedCallArguments> inputs;
    /** Owned return parameter; no borrowed reference escapes a callback. */
    std::optional<ResolvedCallParameterRef> returns;
    /** Whether a valid selected layout exposed a callee operand. */
    bool saw_target{};
    /** Capture a direct callee's bound identity. */
    void function_ref(const ResolvedFunctionRef& value,
                      std::span<const SourceRange>,
                      checker::AddressSymbolResolutionPolicy) override {
      direct_target = value.symbol_id;
      saw_target = true;
    }
    /** Capture indirect metadata without retaining the borrowed value. */
    void indirect_callee(const ResolvedIndirectCallee& value,
                         std::span<const SourceRange>,
                         checker::AddressSymbolResolutionPolicy) override {
      if (const auto metadata = indirect_metadata_identity(value))
        indirect_metadata = metadata;
      saw_target = true;
    }
    /** Copy resolved call inputs for later ABI comparison. */
    void call_arguments(const ResolvedCallArguments& value,
                        std::span<const SourceRange>,
                        checker::AddressSymbolResolutionPolicy) override {
      inputs = value;
    }
    /** Copy the resolved return operand for later ABI comparison. */
    void call_parameter_ref(const ResolvedCallParameterRef& value,
                            std::span<const SourceRange>,
                            checker::AddressSymbolResolutionPolicy) override {
      returns = value;
    }
  };
  for (size_t index = 0; index < function.body.size(); ++index) {
    const auto* call = instruction_if<CallDirect>(function.body[index]);
    if (!call)
      continue;
    CallContractObserver observer;
    call->visit_references(observer);
    if (!observer.saw_target) {
      append_model_mismatch(
          diagnostics, function.instruction_ranges[index],
          "Resolved call has no valid selected layout target.");
      continue;
    }
    const declaration_semantics::FunctionSignature* signature = nullptr;
    if (observer.indirect_metadata)
      signature = metadata_signature(signatures, *observer.indirect_metadata);
    else if (observer.direct_target)
      signature = direct_signature(module, signatures, *observer.direct_target);
    if (!signature) {
      append_model_mismatch(
          diagnostics, function.instruction_ranges[index],
          "Resolved module call has no retained formal signature.");
      continue;
    }
    check_call_inputs(observer.inputs ? &*observer.inputs : nullptr,
                      signature->parameters, declarations, diagnostics,
                      function.instruction_ranges[index]);
    check_call_returns(observer.returns ? &*observer.returns : nullptr,
                       signature->return_parameters, declarations, diagnostics,
                       function.instruction_ranges[index]);
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
  check_module_texture_mode(module.header, diagnostics);
  if (!module.header.regions.empty()) {
    const auto version = module.header.regions.front().version;
    for (const auto& region : module.header.regions)
      if (region.version != version)
        invalid(region.range,
                "Resolved target regions disagree on module PTX version.");
  }
  const OwnedSignatureIndex signatures =
      build_signature_index(module, diagnostics);
  const auto parameter_properties = build_parameter_properties(module);
  check_opaque_storage(module, diagnostics);
  check_cvta_constant_pointer_restriction(module, diagnostics);
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
    const auto entry_version =
        function.source_region &&
                *function.source_region < module.header.regions.size()
            ? module.header.regions[*function.source_region].version
            : std::optional<checker::PtxVersion>{};
    check_opaque_entry_parameters(module, function, entry_version, diagnostics);
    check_function_contract_integrity(function, diagnostics);
    const bool complete_instruction_provenance =
        function.instruction_ranges.size() == function.body.size() &&
        function.instruction_opcodes.size() == function.body.size();
    if (!complete_instruction_provenance) {
      invalid(
          function.range,
          "Resolved function instruction provenance does not match its body.");
    }
    if (complete_instruction_provenance) {
      for (size_t index = 0; index < function.body.size(); ++index) {
        if (!function.body[index])
          invalid(function.instruction_ranges[index],
                  "Resolved function contains an empty instruction owner.");
      }
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
      check_tcgen_cta_groups(function, diagnostics);
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
          check_instruction_body(function, *target, region->texture_mode,
                                 diagnostics);
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
  /** Replacement source selects one texturing mode for every target region. */
  std::optional<TextureMode> explicit_texture_mode;
  for (const auto& item : ast.items) {
    const auto* target = std::get_if<syntax_ast::AstTargetDirective>(&item);
    if (!target)
      continue;
    bool seen_in_target = false;
    for (const auto& option : target->targets) {
      if (option.text != "texmode_unified" &&
          option.text != "texmode_independent")
        continue;
      const auto mode = option.text == "texmode_independent"
                            ? TextureMode::Independent
                            : TextureMode::Unified;
      if (seen_in_target ||
          (explicit_texture_mode && *explicit_texture_mode != mode))
        diagnostics.push_back({
            .kind = checker::CheckDiagnosticKind::RuleViolation,
            .range = target->range,
            .message =
                "PTX module selects conflicting or repeated texturing modes.",
        });
      seen_in_target = true;
      explicit_texture_mode = mode;
    }
  }
  const auto texture_mode =
      explicit_texture_mode.value_or(TextureMode::Unified);
  if (texture_mode == TextureMode::Independent && version &&
      *version < checker::PtxVersion{1, 5})
    diagnostics.push_back({
        .kind = checker::CheckDiagnosticKind::UnsupportedAvailability,
        .range = ast.range,
        .message = "Independent texturing mode requires PTX ISA >= 1.5.",
    });
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
      const auto* owned = source_function(*function, module);
      check_instruction_body(*owned, *active_target, texture_mode, diagnostics);
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
