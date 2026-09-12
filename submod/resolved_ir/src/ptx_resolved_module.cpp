#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include <ptx_frontend/semantic/ptx_call_argument_compatibility.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>

#include "ptx_module_source_context.hpp"
#include "ptx_source_identity.hpp"
#include "ptx_storage_declarations.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <variant>

#include <fmt/format.h>

namespace ptx_frontend::resolved_ir {
namespace {

using call_argument_compatibility::CallArgumentCompatibility;
using call_argument_compatibility::CallArgumentProperties;
using call_argument_compatibility::CallArgumentStateSpace;
using call_argument_compatibility::CallArgumentVectorShape;

using CallArgumentPropertyIndex =
    std::unordered_map<uint32_t, CallArgumentProperties>;
using FunctionSignatureIndex =
    std::unordered_map<uint32_t, declaration_semantics::FunctionSignature>;

/** Translate AST state space without accepting invalid constructed values. */
CallArgumentStateSpace call_state_space(syntax_ast::AstStateSpace state_space) {
  switch (state_space) {
    case syntax_ast::AstStateSpace::Register:
      return CallArgumentStateSpace::Register;
    case syntax_ast::AstStateSpace::Parameter:
      return CallArgumentStateSpace::Parameter;
    case syntax_ast::AstStateSpace::Local:
      return CallArgumentStateSpace::Local;
    case syntax_ast::AstStateSpace::Shared:
      return CallArgumentStateSpace::Shared;
    case syntax_ast::AstStateSpace::Global:
      return CallArgumentStateSpace::Global;
    case syntax_ast::AstStateSpace::Constant:
      return CallArgumentStateSpace::Constant;
  }
  return CallArgumentStateSpace::Invalid;
}

/** Translate an optional AST vector modifier into a semantic ABI shape. */
CallArgumentVectorShape call_vector_shape(
    const std::optional<syntax_ast::AstSyntax>& vector_type) {
  if (!vector_type)
    return CallArgumentVectorShape::Scalar;
  if (vector_type->text == ".v2")
    return CallArgumentVectorShape::V2;
  if (vector_type->text == ".v4")
    return CallArgumentVectorShape::V4;
  return CallArgumentVectorShape::Invalid;
}

/** Return a modeled scalar identity without treating unknown source as valid. */
base::ScalarType call_scalar_type(std::string_view spelling) {
  const auto* metadata = base::find_scalar_type_metadata(spelling);
  return metadata ? metadata->type : base::ScalarType::Invalid;
}

/** Parse a source address-size directive without retaining syntax ownership. */
std::optional<uint32_t> address_size_bits(std::string_view text) {
  uint32_t result{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size())
    return std::nullopt;
  return result == 32 || result == 64 ? std::optional{result} : std::nullopt;
}

/** Parse a PTX major.minor token without borrowing its source storage. */
std::optional<checker::PtxVersion> parse_version(std::string_view text) {
  const size_t dot = text.find('.');
  if (dot == std::string_view::npos)
    return std::nullopt;
  checker::PtxVersion result;
  const auto parse = [](std::string_view digits, uint16_t& output) {
    const auto [end, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), output);
    return !digits.empty() && error == std::errc{} &&
           end == digits.data() + digits.size();
  };
  if (!parse(text.substr(0, dot), result.major) ||
      !parse(text.substr(dot + 1), result.minor))
    return std::nullopt;
  return result;
}

/** Parse one validated unsigned directive operand into owned semantic storage. */
std::optional<uint32_t> normalized_u32(std::string_view text) {
  uint32_t value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size())
    return std::nullopt;
  return value;
}

/** Convert a syntax-only resource category to the public semantic category. */
ResolvedKernelResourceKind resource_kind(
    syntax_ast::AstKernelResourceKind kind) {
  using AstKind = syntax_ast::AstKernelResourceKind;
  using ResolvedKind = ResolvedKernelResourceKind;
  switch (kind) {
    case AstKind::MaxNreg:
      return ResolvedKind::MaxNreg;
    case AstKind::MaxNtid:
      return ResolvedKind::MaxNtid;
    case AstKind::ReqNtid:
      return ResolvedKind::ReqNtid;
    case AstKind::MinNctaPerSm:
      return ResolvedKind::MinNctaPerSm;
    case AstKind::ReqNctaPerCluster:
      return ResolvedKind::ReqNctaPerCluster;
    case AstKind::ExplicitCluster:
      return ResolvedKind::ExplicitCluster;
    case AstKind::MaxClusterRank:
      return ResolvedKind::MaxClusterRank;
  }
  throw ResolveException("Unknown kernel resource kind.");
}

/** Return whether this resource's omitted trailing dimensions normalize to one. */
bool has_implicit_resource_dimensions(ResolvedKernelResourceKind kind) {
  return kind == ResolvedKernelResourceKind::MaxNtid ||
         kind == ResolvedKernelResourceKind::ReqNtid ||
         kind == ResolvedKernelResourceKind::ReqNctaPerCluster;
}

/** Own ordered source configuration regions rather than collapsing target history. */
ResolvedModuleHeader resolve_module_header(const syntax_ast::AstModule& ast) {
  ResolvedModuleHeader header;
  std::optional<checker::PtxVersion> version;
  /** PTX specifies a 32-bit address-size default independent of the host. */
  std::optional<uint32_t> address_size{32};
  bool address_size_explicit = false;
  /** Region zero models declarations before the first .target directive. */
  header.regions.push_back({.range = ast.range});
  for (const auto& item : ast.items) {
    if (const auto* directive =
            std::get_if<syntax_ast::AstVersionDirective>(&item)) {
      const auto parsed = parse_version(directive->version.text);
      if (!parsed || version)
        header.invalid_directives.push_back(directive->range);
      else
        version = *parsed;
    } else if (const auto* directive =
                   std::get_if<syntax_ast::AstAddressSizeDirective>(&item)) {
      const auto parsed = address_size_bits(directive->bit_width.text);
      if (!parsed || address_size_explicit)
        header.invalid_directives.push_back(directive->range);
      else {
        address_size = *parsed;
        address_size_explicit = true;
      }
    } else if (const auto* directive =
                   std::get_if<syntax_ast::AstTargetDirective>(&item)) {
      std::vector<std::string> targets;
      for (const auto& target : directive->targets)
        targets.emplace_back(target.text);
      if (targets.empty())
        header.invalid_directives.push_back(directive->range);
      header.regions.push_back({
          .range = directive->range,
          .target_options = targets,
          .target_provenance = targets.empty()
                                   ? SourceConfigurationProvenance::Missing
                                   : SourceConfigurationProvenance::Explicit,
      });
    }
  }
  const auto version_provenance = version
                                      ? SourceConfigurationProvenance::Explicit
                                      : SourceConfigurationProvenance::Missing;
  const auto address_provenance =
      address_size_explicit ? SourceConfigurationProvenance::Explicit
                            : SourceConfigurationProvenance::Defaulted;
  for (auto& region : header.regions) {
    region.version = version;
    region.version_provenance = version_provenance;
    region.address_size_bits = address_size;
    region.address_size_provenance = address_provenance;
  }
  return header;
}

/** Copy source resource values into an owned semantic contract. */
ResolvedKernelResourceContract resolve_resource_contract(
    const syntax_ast::AstKernelResourceDirective& resource) {
  ResolvedKernelResourceContract resolved{
      .kind = resource_kind(resource.kind),
      .explicit_value_count = static_cast<uint8_t>(resource.values.size()),
      .range = resource.range};
  for (const auto& value : resource.values) {
    const auto parsed = normalized_u32(value.text);
    if (!parsed)
      throw ResolveException(
          "Validated kernel resource has a non-numeric value.");
    resolved.values.push_back(*parsed);
  }
  if (has_implicit_resource_dimensions(resolved.kind)) {
    while (resolved.values.size() < 3)
      resolved.values.push_back(1);
  }
  return resolved;
}

/** Copy a validated source ABI suffix into an owned numeric contract. */
std::optional<ResolvedAbiPreservationContract> resolve_abi_contract(
    const std::optional<syntax_ast::AstCallPrototypeAbiSuffix>& suffix,
    bool control_registers) {
  if (!suffix)
    return std::nullopt;
  const auto count = normalized_u32(suffix->count.text);
  if (!count)
    throw ResolveException(
        "Validated ABI preservation suffix has an invalid count.");
  return ResolvedAbiPreservationContract{
      .count = *count,
      .control_registers = control_registers,
      .range = suffix->range,
  };
}

/** Convert a function attribute into owned semantic data without AST lifetime. */
ResolvedFunctionAttribute resolve_function_attribute(
    const syntax_ast::AstAttribute& attribute) {
  ResolvedFunctionAttribute resolved{
      .kind = attribute.kind == syntax_ast::AstAttributeKind::Managed
                  ? ResolvedFunctionAttributeKind::Managed
                  : ResolvedFunctionAttributeKind::Unified,
      .range = attribute.range,
  };
  for (const auto& value : attribute.values)
    resolved.values.emplace_back(value.text);
  return resolved;
}

std::optional<uint64_t> array_size(
    const syntax_ast::AstVariableDeclarator& declarator) {
  if (declarator.array_dimensions.empty())
    return std::nullopt;
  uint64_t size = 1;
  for (const auto& dimension : declarator.array_dimensions) {
    if (!dimension.size)
      return std::nullopt;
    const auto extent =
        declaration_semantics::constantArrayExtent(*dimension.size);
    if (!extent || (*extent != 0 &&
                    size > std::numeric_limits<uint64_t>::max() / *extent)) {
      return std::nullopt;
    }
    size *= *extent;
  }
  return size;
}

CallArgumentProperties call_argument_properties(
    const syntax_ast::AstVariableDeclaration& declaration,
    const syntax_ast::AstVariableDeclarator& declarator,
    const binding::Symbol& symbol) {
  return {
      .state_space = call_state_space(declaration.state_space),
      .scalar_type = call_scalar_type(declaration.type.text),
      .vector_shape = call_vector_shape(declaration.vector_type),
      .type_spelling = declaration.type.text,
      .array_alignment = symbol.address_alignment.value_or(1),
      .is_array = !declarator.array_dimensions.empty(),
      .array_size = array_size(declarator),
  };
}

std::optional<binding::SymbolId> declared_symbol(
    const binding::SymbolTable& symbols, binding::ScopeId scope,
    const syntax_ast::AstVariableDeclarator& declarator) {
  return symbols.exactDeclaration(scope, declarator.name.syntax.text,
                                  declarator.parameterized_count.has_value());
}

binding::ScopeId block_scope(const binding::SymbolTable& symbols,
                             binding::ScopeId parent,
                             const syntax_ast::AstBlock& block) {
  const auto scope = symbols.blockScope(parent, block.range);
  if (!scope)
    throw ResolveException("Bound syntax block has no lexical scope.");
  return *scope;
}

void index_body_call_arguments(
    const std::vector<syntax_ast::AstFunctionBodyItem>& body,
    const binding::SymbolTable& symbols, binding::ScopeId scope,
    CallArgumentPropertyIndex& properties) {
  for (const auto& item : body) {
    if (const auto* declaration =
            std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
      for (const auto& declarator : declaration->declarators) {
        const auto symbol_id = declared_symbol(symbols, scope, declarator);
        if (symbol_id) {
          properties.emplace(symbol_id->value, call_argument_properties(
                                                   *declaration, declarator,
                                                   symbols.symbol(*symbol_id)));
        }
      }
    } else if (const auto* block =
                   std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
               block != nullptr && *block) {
      index_body_call_arguments((*block)->body, symbols,
                                block_scope(symbols, scope, **block),
                                properties);
    }
  }
}

void index_function_call_arguments(const syntax_ast::AstFunction& function,
                                   const binding::SymbolTable& symbols,
                                   binding::ScopeId scope,
                                   CallArgumentPropertyIndex& properties) {
  const auto signature = declaration_semantics::functionSignature(function);
  const auto index_parameters = [&](const auto& parameters,
                                    const auto& contracts) {
    for (size_t index = 0; index < parameters.size(); ++index) {
      const auto lookup =
          symbols.lookup(scope, parameters[index].name.syntax.text);
      if (lookup) {
        properties.emplace(
            lookup->symbol.value,
            declaration_semantics::call_argument_properties(contracts[index]));
      }
    }
  };
  index_parameters(function.return_parameters, signature.return_parameters);
  index_parameters(function.parameters, signature.parameters);
  index_body_call_arguments(function.body, symbols, scope, properties);
}

void index_body_metadata_signatures(
    const std::vector<syntax_ast::AstFunctionBodyItem>& body,
    const binding::SymbolTable& symbols, binding::ScopeId function_scope,
    FunctionSignatureIndex& signatures) {
  for (const auto& item : body) {
    if (const auto* prototype =
            std::get_if<syntax_ast::AstCallPrototype>(&item)) {
      const auto lookup =
          symbols.lookup(function_scope, prototype->label.syntax.text);
      if (!lookup)
        throw ResolveException("Bound .callprototype has no local symbol.");
      signatures.try_emplace(
          lookup->symbol.value,
          declaration_semantics::functionSignature(*prototype));
      continue;
    }
    if (const auto* targets = std::get_if<syntax_ast::AstCallTargets>(&item)) {
      if (targets->targets.empty())
        throw ResolveException("Validated .calltargets has no member.");
      const auto metadata =
          symbols.lookup(function_scope, targets->label.syntax.text);
      const auto target = symbols.lookup(symbols.moduleScope(),
                                         targets->targets.front().syntax.text);
      if (!metadata || !target)
        throw ResolveException("Validated .calltargets has no bound symbol.");
      const binding::Symbol& target_symbol = symbols.symbol(target->symbol);
      const binding::SymbolId canonical =
          target_symbol.canonical_function.value_or(target_symbol.id);
      const auto signature = signatures.find(canonical.value);
      if (signature == signatures.end())
        throw ResolveException(
            "Validated .calltargets member has no signature.");
      signatures.try_emplace(metadata->symbol.value, signature->second);
      continue;
    }
    if (const auto* block =
            std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
        block != nullptr && *block) {
      index_body_metadata_signatures((*block)->body, symbols, function_scope,
                                     signatures);
    }
  }
}

void index_function_metadata_signatures(const syntax_ast::AstFunction& function,
                                        const binding::SymbolTable& symbols,
                                        binding::ScopeId scope,
                                        FunctionSignatureIndex& signatures) {
  index_body_metadata_signatures(function.body, symbols, scope, signatures);
}

/**
 * Preserve function-local indirect-control declarations in their source order.
 *
 * Binding deliberately gives metadata labels function scope even when their
 * declaration occurs in a nested lexical block, so consumers can resolve the
 * same identity that branch and call operands carry.
 */
void resolve_control_contracts(
    const std::vector<syntax_ast::AstFunctionBodyItem>& body,
    const binding::SymbolTable& symbols, binding::ScopeId function_scope,
    const FunctionSignatureIndex& signatures, ResolvedFunction& function) {
  const auto metadata_symbol = [&](std::string_view name,
                                   binding::SymbolKind expected) {
    const auto lookup = symbols.lookup(function_scope, name);
    if (!lookup || symbols.symbol(lookup->symbol).kind != expected)
      throw ResolveException(
          "Validated control metadata has no bound declaration.");
    return lookup->symbol;
  };
  for (const auto& item : body) {
    if (const auto* branches =
            std::get_if<syntax_ast::AstBranchTargets>(&item)) {
      ResolvedBranchTargetSetContract contract{
          .symbol_id = metadata_symbol(branches->label.syntax.text,
                                       binding::SymbolKind::BranchTargetSet),
          .scope_id = function_scope,
          .name = branches->label.syntax.text,
          .range = branches->range,
      };
      for (const auto& target : branches->targets) {
        std::vector<std::string> logical_names;
        if (target.count) {
          const auto count = normalized_u32(target.count->text);
          if (!count || *count == 0 || *count > symbols.symbols().size())
            throw ResolveException("Validated branch target count is invalid.");
          logical_names.reserve(*count);
          for (uint32_t index = 0; index < *count; ++index)
            logical_names.push_back(
                fmt::format("{}{}", target.name.syntax.text, index));
        } else {
          logical_names.emplace_back(target.name.syntax.text);
        }
        for (const auto& name : logical_names) {
          const auto lookup = symbols.lookup(function_scope, name);
          if (!lookup || symbols.symbol(lookup->symbol).kind !=
                             binding::SymbolKind::Label) {
            throw ResolveException(
                "Validated branch target has no bound label.");
          }
          contract.targets.push_back({
              .symbol_id = lookup->symbol,
              .name = name,
              .range = target.range,
          });
        }
      }
      function.branch_target_sets.push_back(std::move(contract));
    } else if (const auto* targets =
                   std::get_if<syntax_ast::AstCallTargets>(&item)) {
      ResolvedCallTargetSetContract contract{
          .symbol_id = metadata_symbol(targets->label.syntax.text,
                                       binding::SymbolKind::CallTargetSet),
          .scope_id = function_scope,
          .name = targets->label.syntax.text,
          .range = targets->range,
      };
      for (const auto& target : targets->targets) {
        const auto lookup =
            symbols.lookup(symbols.moduleScope(), target.syntax.text);
        if (!lookup || symbols.symbol(lookup->symbol).kind !=
                           binding::SymbolKind::Function) {
          throw ResolveException(
              "Validated call target has no bound function.");
        }
        const auto& symbol = symbols.symbol(lookup->symbol);
        contract.targets.push_back({
            .symbol_id = symbol.id,
            .canonical_function = symbol.canonical_function.value_or(symbol.id),
            .name = target.syntax.text,
            .range = target.syntax.range,
        });
      }
      if (contract.targets.empty())
        throw ResolveException("Validated call target set has no target.");
      const auto signature =
          signatures.find(contract.targets.front().canonical_function.value);
      if (signature == signatures.end()) {
        throw ResolveException("Validated call target set has no signature.");
      }
      contract.signature = signature->second;
      function.call_target_sets.push_back(std::move(contract));
    } else if (const auto* prototype =
                   std::get_if<syntax_ast::AstCallPrototype>(&item)) {
      ResolvedCallPrototypeContract contract{
          .symbol_id = metadata_symbol(prototype->label.syntax.text,
                                       binding::SymbolKind::CallPrototype),
          .scope_id = function_scope,
          .name = prototype->label.syntax.text,
          .signature = declaration_semantics::functionSignature(*prototype),
          .is_noreturn = prototype->noreturn_directive.has_value(),
          .abi_preserve = resolve_abi_contract(prototype->abi_preserve, false),
          .abi_preserve_control =
              resolve_abi_contract(prototype->abi_preserve_control, true),
          .range = prototype->range,
      };
      function.call_prototypes.push_back(std::move(contract));
    } else if (const auto* block =
                   std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
               block != nullptr && *block) {
      resolve_control_contracts((*block)->body, symbols, function_scope,
                                signatures, function);
    }
  }
}

std::string_view compatibility_message(
    CallArgumentCompatibility compatibility) {
  switch (compatibility) {
    case CallArgumentCompatibility::Compatible:
      return "compatible";
    case CallArgumentCompatibility::FormalStateSpaceMismatch:
      return "callee formal has an invalid call state space";
    case CallArgumentCompatibility::ActualStateSpaceMismatch:
      return "call argument state-space mismatch";
    case CallArgumentCompatibility::TypeMismatch:
      return "type or vector shape mismatch";
    case CallArgumentCompatibility::ArrayMismatch:
      return "array shape mismatch";
    case CallArgumentCompatibility::ArraySizeMismatch:
      return "array size mismatch";
    case CallArgumentCompatibility::AlignmentMismatch:
      return "array alignment mismatch";
    case CallArgumentCompatibility::PointerMismatch:
      return "pointer qualification mismatch";
    case CallArgumentCompatibility::PointedStateSpaceMismatch:
      return "pointed state-space mismatch";
    case CallArgumentCompatibility::PointedAlignmentMismatch:
      return "pointed alignment mismatch";
  }
  return "incompatible";
}

/** Return the input argument group owned by a resolved call, when it has one. */
ResolvedCallArguments* resolved_call_arguments(
    ResolvedInstruction& instruction) {
  ResolvedCallArguments* arguments = nullptr;
  std::visit(
      [&](auto& candidate) {
        if (candidate.get_resolved_descriptor().opcode_name != "call")
          return;
        std::visit(
            [&](auto& selected) {
              if constexpr (requires { selected.operands; }) {
                std::visit(
                    [&](auto& operands) {
                      if constexpr (requires {
                                      operands.arguments.value.values;
                                    }) {
                        arguments = &operands.arguments.value;
                      }
                    },
                    selected.operands);
              }
            },
            candidate.variant);
      },
      instruction);
  return arguments;
}

/**
 * Validate a call ABI and retain formal-driven literal values in owned IR.
 *
 * The AST is used only to identify the formal and preserve existing
 * diagnostics; the resolved call receives the semantic value before the AST
 * can be destroyed.
 */
void check_call_abi(const syntax_ast::AstInstruction& call,
                    ResolvedInstruction& resolved_call,
                    const binding::SymbolTable& symbols, binding::ScopeId scope,
                    const FunctionSignatureIndex& signatures,
                    const CallArgumentPropertyIndex& properties,
                    ModuleResolveDiagnostics& diagnostics) {
  if (call.opcode.syntax.text != "call")
    return;

  const syntax_ast::AstCallTarget* target = nullptr;
  const syntax_ast::AstCallTargetSet* metadata = nullptr;
  const syntax_ast::AstCallParameterList* returns = nullptr;
  const syntax_ast::AstCallParameterList* inputs = nullptr;
  for (const auto& operand : call.operands) {
    if (const auto* value = std::get_if<syntax_ast::AstCallTarget>(&operand))
      target = value;
    else if (const auto* value =
                 std::get_if<syntax_ast::AstCallTargetSet>(&operand))
      metadata = value;
    else if (const auto* value =
                 std::get_if<syntax_ast::AstCallParameterList>(&operand)) {
      if (value->kind == syntax_ast::AstCallParameterListKind::Return)
        returns = value;
      else
        inputs = value;
    }
  }
  const declaration_semantics::FunctionSignature* signature = nullptr;
  std::string callee_name;
  std::string call_subject;
  SourceRange target_range;
  if (metadata != nullptr) {
    const auto lookup = symbols.lookup(scope, metadata->name.syntax.text);
    if (!lookup)
      throw ResolveException(
          "Validated indirect call metadata has no local symbol.");
    const binding::Symbol& target_symbol = symbols.symbol(lookup->symbol);
    const binding::SymbolId canonical =
        target_symbol.canonical_function.value_or(target_symbol.id);
    const auto found = signatures.find(canonical.value);
    if (found == signatures.end())
      throw ResolveException(
          "Validated indirect call metadata has no canonical signature.");
    signature = &found->second;
    callee_name = metadata->name.syntax.text;
    call_subject = fmt::format("Indirect call via metadata '{}'", callee_name);
    target_range = metadata->range;
  } else {
    if (target == nullptr)
      return;
    const auto lookup = symbols.lookup(scope, target->name.syntax.text);
    if (!lookup ||
        symbols.symbol(lookup->symbol).kind != binding::SymbolKind::Function)
      return;
    const binding::Symbol& target_symbol = symbols.symbol(lookup->symbol);
    const binding::SymbolId canonical =
        target_symbol.canonical_function.value_or(target_symbol.id);
    const auto found = signatures.find(canonical.value);
    if (found == signatures.end())
      return;
    signature = &found->second;
    callee_name = target->name.syntax.text;
    call_subject = fmt::format("Direct call to '{}'", callee_name);
    target_range = target->range;
  }

  if (signature == nullptr)
    return;

  ResolvedCallArguments* resolved_inputs =
      resolved_call_arguments(resolved_call);

  const auto check_group = [&](std::string_view kind, const auto* actuals,
                               const auto& formals) {
    const size_t actual_count =
        actuals == nullptr ? 0 : actuals->parameters.size();
    // A validated final unsized byte input can be omitted for an empty payload.
    const bool omitted_unsized_input =
        kind == "input" && !formals.empty() &&
        actual_count == formals.size() - 1 && formals.back().is_array &&
        !formals.back().array_extent &&
        formals.back().scalar_type == base::ScalarType::B8 &&
        formals.back().state_space == CallArgumentStateSpace::Parameter;
    if (actual_count != formals.size() && !omitted_unsized_input) {
      diagnostics.push_back(ResolveDiagnostic{
          .range = actuals == nullptr ? target_range : actuals->range,
          .message = fmt::format(
              "{} has {} {} argument{} but callee requires {}.", call_subject,
              actual_count, kind, actual_count == 1 ? "" : "s", formals.size()),
      });
    }
    if (actuals == nullptr)
      return;
    const size_t count = std::min(actuals->parameters.size(), formals.size());
    for (size_t index = 0; index < count; ++index) {
      const auto& actual = actuals->parameters[index];
      const SourceRange range = std::visit(
          [](const auto& value) { return value.syntax.range; }, actual);
      const auto formal_properties =
          declaration_semantics::call_argument_properties(formals[index]);
      CallArgumentProperties actual_properties;
      if (const auto* immediate =
              std::get_if<syntax_ast::AstImmediate>(&actual)) {
        const auto literal = resolve_call_literal(
            ResolvedCallLiteral{.spelling = immediate->syntax.text,
                                .kind = immediate->kind},
            range, formals[index]);
        if (!literal) {
          diagnostics.push_back(std::move(literal.error()));
          continue;
        }
        actual_properties = {
            .state_space = CallArgumentStateSpace::Register,
            .scalar_type = formals[index].scalar_type,
            .vector_shape = CallArgumentVectorShape::Scalar,
            .type_spelling = formals[index].type_spelling,
        };
        if (kind == "input" && resolved_inputs != nullptr &&
            index < resolved_inputs->values.size()) {
          auto* retained = std::get_if<ResolvedCallLiteral>(
              &resolved_inputs->values[index].value);
          if (retained == nullptr) {
            throw ResolveException(
                "Resolved call argument no longer matches its syntax literal.");
          }
          retained->value = literal->value;
        }
      } else {
        const auto& identifier = std::get<syntax_ast::AstIdentifierRef>(actual);
        const auto actual_lookup =
            symbols.lookup(scope, identifier.syntax.text);
        if (!actual_lookup) {
          throw ResolveException(
              fmt::format("Bound call argument '{}' has no symbol.",
                          identifier.syntax.text));
        }
        const auto properties_it = properties.find(actual_lookup->symbol.value);
        if (properties_it == properties.end()) {
          throw ResolveException(
              fmt::format("Bound call argument '{}' has no ABI properties.",
                          identifier.syntax.text));
        }
        actual_properties = properties_it->second;
      }
      const auto compatibility =
          call_argument_compatibility::checkCallArgumentCompatibility(
              formal_properties, actual_properties);
      if (compatibility != CallArgumentCompatibility::Compatible) {
        diagnostics.push_back(ResolveDiagnostic{
            .range = range,
            .message =
                metadata == nullptr
                    ? fmt::format("Direct call {} argument {} for '{}' has {}.",
                                  kind, index + 1, callee_name,
                                  compatibility_message(compatibility))
                    : fmt::format(
                          "Indirect call via metadata '{}' {} argument {} "
                          "has {}.",
                          callee_name, kind, index + 1,
                          compatibility_message(compatibility)),
        });
      }
    }
  };

  check_group("return", returns, signature->return_parameters);
  check_group("input", inputs, signature->parameters);
}

struct CallParameterIdentity {
  binding::SymbolId symbol;
  std::optional<uint32_t> parameterized_index;
  bool operator==(const CallParameterIdentity&) const = default;
};

std::optional<CallParameterIdentity> call_parameter_identity(
    const syntax_ast::AstIdentifierRef& identifier,
    const binding::SymbolTable& symbols, binding::ScopeId scope) {
  const auto lookup = symbols.lookup(scope, identifier.syntax.text);
  if (!lookup || symbols.symbol(lookup->symbol).kind !=
                     binding::SymbolKind::CallParameter) {
    return std::nullopt;
  }
  return CallParameterIdentity{
      .symbol = lookup->symbol,
      .parameterized_index = lookup->parameterized_index};
}

bool has_parameter_modifier(const syntax_ast::AstInstruction& instruction) {
  for (const auto& modifier : instruction.modifiers) {
    if (modifier.syntax.text == ".param" ||
        modifier.syntax.text == ".param::entry" ||
        modifier.syntax.text == ".param::func") {
      return true;
    }
  }
  return false;
}

std::optional<CallParameterIdentity> staging_parameter(
    const syntax_ast::AstInstruction& instruction,
    const binding::SymbolTable& symbols, binding::ScopeId scope) {
  const bool is_load = instruction.opcode.syntax.text == "ld";
  const bool is_store = instruction.opcode.syntax.text == "st";
  if ((!is_load && !is_store) || !has_parameter_modifier(instruction))
    return std::nullopt;

  const size_t address_index = is_load ? 1 : 0;
  if (instruction.operands.size() <= address_index)
    return std::nullopt;
  const auto* address =
      std::get_if<syntax_ast::AstAddress>(&instruction.operands[address_index]);
  if (address == nullptr || !address->bracketed)
    return std::nullopt;
  const auto* identifier =
      std::get_if<syntax_ast::AstIdentifierRef>(&address->base);
  if (identifier == nullptr)
    return std::nullopt;
  return call_parameter_identity(*identifier, symbols, scope);
}

bool is_staging_store(const syntax_ast::AstFunctionBodyItem& item,
                      const binding::SymbolTable& symbols,
                      binding::ScopeId scope) {
  const auto* instruction = std::get_if<syntax_ast::AstInstruction>(&item);
  return instruction != nullptr && instruction->opcode.syntax.text == "st" &&
         staging_parameter(*instruction, symbols, scope).has_value();
}

bool is_staging_load(const syntax_ast::AstFunctionBodyItem& item,
                     const binding::SymbolTable& symbols,
                     binding::ScopeId scope) {
  const auto* instruction = std::get_if<syntax_ast::AstInstruction>(&item);
  return instruction != nullptr && instruction->opcode.syntax.text == "ld" &&
         staging_parameter(*instruction, symbols, scope).has_value();
}

/** Skip non-executing declarations/debug items without crossing control or scope boundaries. */
bool is_staging_transparent(const syntax_ast::AstFunctionBodyItem& item) {
  return std::holds_alternative<syntax_ast::AstVariableDeclaration>(item) ||
         std::holds_alternative<syntax_ast::AstLocDirective>(item) ||
         std::holds_alternative<syntax_ast::AstPragma>(item);
}

bool call_uses_parameter(const syntax_ast::AstInstruction& call,
                         syntax_ast::AstCallParameterListKind group_kind,
                         const CallParameterIdentity& parameter,
                         const binding::SymbolTable& symbols,
                         binding::ScopeId scope) {
  if (call.opcode.syntax.text != "call")
    return false;
  for (const auto& operand : call.operands) {
    const auto* group = std::get_if<syntax_ast::AstCallParameterList>(&operand);
    if (group == nullptr || group->kind != group_kind)
      continue;
    for (const auto& value : group->parameters) {
      const auto* identifier =
          std::get_if<syntax_ast::AstIdentifierRef>(&value);
      if (identifier &&
          call_parameter_identity(*identifier, symbols, scope) == parameter) {
        return true;
      }
    }
  }
  return false;
}

void check_parameter_qualifier(const syntax_ast::AstFunction& function,
                               const syntax_ast::AstInstruction& instruction,
                               const binding::SymbolTable& symbols,
                               binding::ScopeId scope,
                               ModuleResolveDiagnostics& diagnostics) {
  std::string_view qualifier;
  for (const auto& modifier : instruction.modifiers) {
    if (modifier.syntax.text == ".param::entry" ||
        modifier.syntax.text == ".param::func") {
      qualifier = modifier.syntax.text;
      break;
    }
  }
  if (qualifier.empty())
    return;

  const bool is_load = instruction.opcode.syntax.text == "ld";
  const bool is_store = instruction.opcode.syntax.text == "st";
  const size_t address_index = is_load ? 1 : 0;
  if ((!is_load && !is_store) || instruction.operands.size() <= address_index)
    return;
  const auto* address =
      std::get_if<syntax_ast::AstAddress>(&instruction.operands[address_index]);
  if (address == nullptr || !address->bracketed)
    return;
  const auto* identifier =
      std::get_if<syntax_ast::AstIdentifierRef>(&address->base);
  if (identifier == nullptr)
    return;
  const auto lookup = symbols.lookup(scope, identifier->syntax.text);
  if (!lookup)
    return;
  const binding::SymbolKind kind = symbols.symbol(lookup->symbol).kind;

  const bool valid =
      qualifier == ".param::entry"
          ? function.is_entry && kind == binding::SymbolKind::InputParameter
          : kind == binding::SymbolKind::CallParameter || !function.is_entry;
  if (!valid) {
    diagnostics.push_back(ResolveDiagnostic{
        .range = instruction.range,
        .message = qualifier == ".param::entry"
                       ? ".param::entry may access only a kernel entry input "
                         "parameter."
                       : ".param::func may access only a device-function "
                         "parameter or function-local call parameter.",
    });
  }
}

void check_call_staging_body(
    const syntax_ast::AstFunction& function,
    const std::vector<syntax_ast::AstFunctionBodyItem>& body,
    const binding::SymbolTable& symbols, binding::ScopeId scope,
    ModuleResolveDiagnostics& diagnostics) {
  for (size_t index = 0; index < body.size(); ++index) {
    const auto* instruction =
        std::get_if<syntax_ast::AstInstruction>(&body[index]);
    if (instruction == nullptr) {
      if (const auto* block =
              std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&body[index]);
          block != nullptr && *block) {
        check_call_staging_body(function, (*block)->body, symbols,
                                block_scope(symbols, scope, **block),
                                diagnostics);
      }
      continue;
    }

    check_parameter_qualifier(function, *instruction, symbols, scope,
                              diagnostics);
    const auto parameter = staging_parameter(*instruction, symbols, scope);
    if (!parameter)
      continue;
    const bool is_store = instruction->opcode.syntax.text == "st";
    if (instruction->predicate) {
      diagnostics.push_back(ResolveDiagnostic{
          .range = instruction->predicate->range,
          .message =
              is_store
                  ? "A function-local .param argument store cannot be "
                    "predicated."
                  : "A function-local .param return load cannot be predicated.",
      });
      continue;
    }

    if (is_store) {
      size_t call_index = index + 1;
      while (call_index < body.size() &&
             (is_staging_store(body[call_index], symbols, scope) ||
              is_staging_transparent(body[call_index]))) {
        ++call_index;
      }
      const auto* call =
          call_index < body.size()
              ? std::get_if<syntax_ast::AstInstruction>(&body[call_index])
              : nullptr;
      if (call == nullptr ||
          !call_uses_parameter(*call,
                               syntax_ast::AstCallParameterListKind::Input,
                               *parameter, symbols, scope)) {
        diagnostics.push_back(ResolveDiagnostic{
            .range = instruction->range,
            .message =
                "A function-local .param argument store must be in the "
                "contiguous block immediately before a call that uses it.",
        });
      }
      continue;
    }

    size_t call_index = index;
    while (call_index > 0 &&
           (is_staging_load(body[call_index - 1], symbols, scope) ||
            is_staging_transparent(body[call_index - 1]))) {
      --call_index;
    }
    const auto* call =
        call_index > 0
            ? std::get_if<syntax_ast::AstInstruction>(&body[call_index - 1])
            : nullptr;
    if (call == nullptr ||
        !call_uses_parameter(*call,
                             syntax_ast::AstCallParameterListKind::Return,
                             *parameter, symbols, scope)) {
      diagnostics.push_back(ResolveDiagnostic{
          .range = instruction->range,
          .message =
              "A function-local .param return load must be in the contiguous "
              "block immediately after a call that returns it.",
      });
    }
  }
}

/** Compute declared bytes after semantic validation, without allocating ABI slots. */
std::optional<uint64_t> parameter_byte_extent(
    ScalarType type, uint32_t vector_width,
    const std::vector<std::optional<uint64_t>>& extents) {
  uint64_t bytes = base::scalar_size_of(type) * vector_width;
  for (const auto extent : extents) {
    if (!extent)
      return std::nullopt;
    if (*extent != 0 && bytes > std::numeric_limits<uint64_t>::max() / *extent)
      throw ResolveException("Validated parameter byte extent overflows.");
    bytes *= *extent;
  }
  return bytes;
}

/** Project a validated header .param while preserving its declaration role. */
ResolvedParameterDeclaration resolve_parameter_declaration(
    const syntax_ast::AstFunctionParameter& parameter,
    ParameterDeclarationRole role, const binding::Symbol& symbol,
    const CallArgumentProperties& properties) {
  const auto type =
      declaration_semantics::parameterScalarType(parameter.type.text);
  if (!type || !symbol.address_alignment)
    throw ResolveException(
        "Validated parameter has no scalar type or alignment.");
  std::vector<std::optional<uint64_t>> extents;
  if (parameter.is_array)
    extents.push_back(
        parameter.array_size
            ? declaration_semantics::constantArrayExtent(*parameter.array_size)
            : std::nullopt);
  const auto bytes = parameter_byte_extent(*type, 1, extents);
  return {
      .symbol_id = symbol.id,
      .scope_id = symbol.scope,
      .role = role,
      .scalar_type = *type,
      .alignment = *symbol.address_alignment,
      .explicit_alignment = parameter.alignment.has_value(),
      .array_extents = std::move(extents),
      .byte_extent = bytes,
      .pointer = properties.pointer,
  };
}

/** Project one body-local .param declarator with owned multidimensional shape. */
ResolvedParameterDeclaration resolve_parameter_declaration(
    const syntax_ast::AstVariableDeclaration& declaration,
    const syntax_ast::AstVariableDeclarator& declarator,
    const binding::Symbol& symbol) {
  const auto type =
      declaration_semantics::parameterScalarType(declaration.type.text);
  if (!type || !symbol.address_alignment)
    throw ResolveException(
        "Validated parameter has no scalar type or alignment.");
  const uint32_t lanes = declaration.vector_type
                             ? (declaration.vector_type->text == ".v2" ? 2 : 4)
                             : 1;
  std::vector<std::optional<uint64_t>> extents;
  extents.reserve(declarator.array_dimensions.size());
  for (const auto& dimension : declarator.array_dimensions)
    extents.push_back(
        dimension.size
            ? declaration_semantics::constantArrayExtent(*dimension.size)
            : std::nullopt);
  const auto bytes = parameter_byte_extent(*type, lanes, extents);
  return {
      .symbol_id = symbol.id,
      .scope_id = symbol.scope,
      .role = ParameterDeclarationRole::BodyLocal,
      .scalar_type = *type,
      .alignment = *symbol.address_alignment,
      .explicit_alignment = declaration.alignment.has_value(),
      .vector_width = lanes,
      .array_extents = std::move(extents),
      .byte_extent = bytes,
  };
}

void resolve_body(const std::vector<syntax_ast::AstFunctionBodyItem>& body,
                  const ResolveContext& context,
                  const binding::SymbolTable& symbols,
                  const FunctionSignatureIndex& signatures,
                  const CallArgumentPropertyIndex& call_argument_properties,
                  ResolvedFunction& resolved_function,
                  ModuleResolveDiagnostics& diagnostics) {
  for (const auto& body_item : body) {
    if (const auto* declaration =
            std::get_if<syntax_ast::AstVariableDeclaration>(&body_item);
        declaration &&
        declaration->state_space == syntax_ast::AstStateSpace::Parameter) {
      for (const auto& declarator : declaration->declarators) {
        const auto symbol = declared_symbol(symbols, context.scope, declarator);
        if (!symbol)
          throw ResolveException("Bound body parameter has no local symbol.");
        resolved_function.parameter_declarations.push_back(
            resolve_parameter_declaration(*declaration, declarator,
                                          symbols.symbol(*symbol)));
      }
    } else if (const auto* instruction =
                   std::get_if<syntax_ast::AstInstruction>(&body_item)) {
      auto resolved = resolveInstruction(*instruction, context);
      if (!resolved) {
        diagnostics.push_back(std::move(resolved.error()));
        continue;
      }
      check_call_abi(*instruction, *resolved, symbols, context.scope,
                     signatures, call_argument_properties, diagnostics);
      resolved_function.body.push_back(std::move(*resolved));
      resolved_function.instruction_ranges.push_back(instruction->range);
      resolved_function.instruction_opcodes.emplace_back(
          instruction->opcode.syntax.text);
    } else if (const auto* label =
                   std::get_if<syntax_ast::AstLabel>(&body_item)) {
      const binding::ScopeId function_scope =
          context.function_scope.value_or(context.scope);
      const auto lookup =
          symbols.lookup(function_scope, label->name.syntax.text);
      if (!lookup) {
        throw ResolveException(
            "Bound module has no symbol for a syntax label.");
      }
      const binding::Symbol& symbol = symbols.symbol(lookup->symbol);
      if (symbol.kind != binding::SymbolKind::Label ||
          symbol.scope != function_scope) {
        throw ResolveException("Bound label symbol is not function-local.");
      }
      resolved_function.label_positions.push_back(
          {.symbol_id = symbol.id,
           .instruction_offset = resolved_function.body.size()});
    } else if (const auto* block =
                   std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(
                       &body_item);
               block != nullptr && *block) {
      ResolveContext nested = context;
      nested.scope = block_scope(symbols, context.scope, **block);
      resolve_body((*block)->body, nested, symbols, signatures,
                   call_argument_properties, resolved_function, diagnostics);
    }
  }
}

}  // namespace

std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveModuleOnly(
    const syntax_ast::AstModule& ast) {
  binding::SymbolBinding binding_result = binding::bindSymbols(ast);

  ModuleResolveDiagnostics diagnostics;
  diagnostics.reserve(binding_result.diagnostics.size());
  for (const binding::BindDiagnostic& diagnostic : binding_result.diagnostics) {
    diagnostics.push_back(ResolveDiagnostic{
        .range = diagnostic.range,
        .message = diagnostic.message,
        .previous_range = diagnostic.previous_range,
        .binding_kind = diagnostic.kind,
    });
  }
  for (const auto& diagnostic :
       declaration_semantics::checkDeclarations(ast, binding_result.table)) {
    diagnostics.push_back(ResolveDiagnostic{
        .range = diagnostic.range,
        .message = diagnostic.message,
        .declaration_kind = diagnostic.kind,
        .previous_range = diagnostic.previous_range,
    });
  }
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));

  auto storage = resolve_storage_declarations(ast, binding_result.table);
  if (!storage) {
    for (const auto& diagnostic : storage.error()) {
      diagnostics.push_back(ResolveDiagnostic{
          .range = diagnostic.range,
          .message = diagnostic.message,
          .declaration_kind = diagnostic.kind,
          .previous_range = diagnostic.previous_range,
      });
    }
    return std::unexpected(std::move(diagnostics));
  }
  ResolvedModuleHeader header = resolve_module_header(ast);

  FunctionSignatureIndex signatures;
  CallArgumentPropertyIndex call_argument_properties;
  /** Declaration ranges distinguish prototypes from their shared definition. */
  const auto declaration_scope = [&](const syntax_ast::AstFunction& function) {
    const auto scope = binding_result.table.functionScope(function.range);
    if (!scope)
      throw ResolveException(
          "Bound module has no unique scope for syntax function declaration.");
    return *scope;
  };
  for (const syntax_ast::AstModuleItem& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (function == nullptr)
      continue;
    const binding::ScopeId scope = declaration_scope(*function);
    const auto lookup = binding_result.table.lookup(
        binding_result.table.moduleScope(), function->name.syntax.text);
    if (!lookup)
      continue;
    signatures.try_emplace(lookup->symbol.value,
                           declaration_semantics::functionSignature(*function));
    index_function_call_arguments(*function, binding_result.table, scope,
                                  call_argument_properties);
  }
  for (const syntax_ast::AstModuleItem& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (function == nullptr)
      continue;
    index_function_metadata_signatures(*function, binding_result.table,
                                       declaration_scope(*function),
                                       signatures);
  }

  std::vector<ResolvedFunction> functions;
  /** Region zero is the targetless prefix before the first .target directive. */
  std::size_t active_region = 0;
  for (const syntax_ast::AstModuleItem& item : ast.items) {
    if (const auto* target =
            std::get_if<syntax_ast::AstTargetDirective>(&item)) {
      (void)target;
      ++active_region;
      if (active_region >= header.regions.size())
        throw ResolveException(
            "Source target has no owned configuration region.");
      continue;
    }
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (function == nullptr)
      continue;
    const binding::ScopeId scope = declaration_scope(*function);

    const auto lookup = binding_result.table.lookup(
        binding_result.table.moduleScope(), function->name.syntax.text);
    if (!lookup) {
      throw ResolveException(
          "Bound module has no symbol for a syntax function.");
    }
    const binding::Symbol& symbol = binding_result.table.symbol(lookup->symbol);
    if (symbol.kind != binding::SymbolKind::Function) {
      throw ResolveException(
          "Bound function symbol has no associated function scope.");
    }

    ResolveContext context{
        .symbols = binding_result.table,
        .scope = scope,
        .function_scope = scope,
        .function_is_entry = function->is_entry,
    };
    ResolvedFunction resolved_function{
        .symbol_id = symbol.id,
        .name = symbol.name,
        .is_entry = function->is_entry,
        .is_prototype = function->is_prototype,
        .contract =
            {
                .signature =
                    declaration_semantics::functionSignature(*function),
                .linkage = symbol.linkage,
                .canonical_function =
                    symbol.canonical_function.value_or(symbol.id),
                .is_noreturn = function->is_noreturn,
                .abi_preserve =
                    resolve_abi_contract(function->abi_preserve, false),
                .abi_preserve_control =
                    resolve_abi_contract(function->abi_preserve_control, true),
                .blocks_are_clusters =
                    function->blocks_are_clusters.has_value(),
                .language_values =
                    function->language
                        ? std::optional<std::vector<std::string>>{std::in_place}
                        : std::nullopt,
            },
        .range = function->range,
        .declaration_scope = scope,
        .source_target =
            header.regions[active_region].target_options.empty()
                ? std::nullopt
                : std::optional<std::string>{header.regions[active_region]
                                                 .target_options.front()},
        .source_version = header.regions[active_region].version,
        .source_region = active_region,
        .source_identity = detail::function_source_identity(*function),
    };
    if (function->language) {
      auto& language_values = *resolved_function.contract.language_values;
      language_values.reserve(function->language->values.size());
      for (const auto& value : function->language->values)
        language_values.emplace_back(value.text);
    }
    for (const auto& attribute : function->attributes)
      resolved_function.contract.attributes.push_back(
          resolve_function_attribute(attribute));
    for (const auto& resource : function->resources)
      resolved_function.contract.resources.push_back(
          resolve_resource_contract(resource));
    /** Append header declarations in return-list then input-list order. */
    const auto append_parameters =
        [&](const std::vector<syntax_ast::AstFunctionParameter>& parameters,
            ParameterDeclarationRole role) {
          for (const auto& parameter : parameters) {
            if (parameter.state_space != syntax_ast::AstStateSpace::Parameter)
              continue;
            const auto found =
                binding_result.table.lookup(scope, parameter.name.syntax.text);
            if (!found)
              throw ResolveException("Bound parameter has no local symbol.");
            const auto& parameter_symbol =
                binding_result.table.symbol(found->symbol);
            resolved_function.parameter_declarations.push_back(
                resolve_parameter_declaration(
                    parameter, role, parameter_symbol,
                    call_argument_properties.at(parameter_symbol.id.value)));
          }
        };
    append_parameters(function->return_parameters,
                      ParameterDeclarationRole::DeviceReturn);
    append_parameters(function->parameters,
                      function->is_entry
                          ? ParameterDeclarationRole::EntryInput
                          : ParameterDeclarationRole::DeviceInput);
    resolve_control_contracts(function->body, binding_result.table, scope,
                              signatures, resolved_function);
    resolve_body(function->body, context, binding_result.table, signatures,
                 call_argument_properties, resolved_function, diagnostics);
    check_call_staging_body(*function, function->body, binding_result.table,
                            scope, diagnostics);
    functions.push_back(std::move(resolved_function));
  }

  std::vector<ResolvedFunctionAlias> function_aliases;
  std::size_t alias_region = 0;
  for (const auto& item : ast.items) {
    if (std::holds_alternative<syntax_ast::AstTargetDirective>(item)) {
      ++alias_region;
      continue;
    }
    const auto* alias = std::get_if<syntax_ast::AstAliasDirective>(&item);
    if (alias == nullptr)
      continue;
    const auto lookup = binding_result.table.lookup(
        binding_result.table.moduleScope(), alias->alias.syntax.text);
    if (!lookup)
      throw ResolveException("Validated function alias has no bound symbol.");
    const auto& symbol = binding_result.table.symbol(lookup->symbol);
    if (symbol.kind != binding::SymbolKind::Function ||
        !symbol.canonical_function) {
      throw ResolveException(
          "Validated function alias has no canonical function.");
    }
    function_aliases.push_back({
        .symbol_id = symbol.id,
        .canonical_function = *symbol.canonical_function,
        .name = alias->alias.syntax.text,
        .aliasee_name = alias->aliasee.syntax.text,
        .range = alias->range,
        .source_region = alias_region,
    });
  }

  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  ResolvedModule module{
      .symbols = std::move(binding_result.table),
      .functions = std::move(functions),
      .function_aliases = std::move(function_aliases),
      .header = std::move(header),
      .range = ast.range,
      .storage_declarations = std::move(*storage),
      .source_identity = detail::module_source_identity(ast),
  };
  return module;
}

namespace {
/** Resolve once, then apply the caller's explicit validation-context policy. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolve_and_check(
    const syntax_ast::AstModule& ast, ModuleValidationPolicy policy) {
  auto module = resolveModuleOnly(ast);
  if (!module)
    return module;
  const auto availability = validateModule(ast, *module, policy);
  if (!availability) {
    ModuleResolveDiagnostics availability_diagnostics;
    availability_diagnostics.reserve(availability.error().size());
    for (const checker::CheckDiagnostic& diagnostic : availability.error()) {
      availability_diagnostics.push_back({.range = diagnostic.range,
                                          .message = diagnostic.message,
                                          .checker_kind = diagnostic.kind});
    }
    return std::unexpected(std::move(availability_diagnostics));
  }
  const auto owned_validation = validateModule(*module, policy);
  if (!owned_validation) {
    ModuleResolveDiagnostics owned_diagnostics;
    owned_diagnostics.reserve(owned_validation.error().size());
    for (const checker::CheckDiagnostic& diagnostic :
         owned_validation.error()) {
      owned_diagnostics.push_back({.range = diagnostic.range,
                                   .message = diagnostic.message,
                                   .checker_kind = diagnostic.kind});
    }
    return std::unexpected(std::move(owned_diagnostics));
  }
  return module;
}
}  // namespace

std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveModule(
    const syntax_ast::AstModule& ast) {
  return resolve_and_check(ast, ModuleValidationPolicy::AvailableContext);
}

std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveAndValidateModule(const syntax_ast::AstModule& ast) {
  return resolve_and_check(ast, ModuleValidationPolicy::RequireCompleteContext);
}

}  // namespace ptx_frontend::resolved_ir
