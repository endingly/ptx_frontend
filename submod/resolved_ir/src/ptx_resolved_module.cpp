#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include <ptx_frontend/semantic/ptx_call_argument_compatibility.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>

#include <algorithm>
#include <charconv>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>

namespace ptx_frontend::resolved_ir {
namespace {

using call_argument_compatibility::CallArgumentCompatibility;
using call_argument_compatibility::CallArgumentProperties;
using call_argument_compatibility::CallArgumentStateSpace;
using call_argument_compatibility::PointedStateSpace;

using CallArgumentPropertyIndex =
    std::unordered_map<uint32_t, CallArgumentProperties>;
using FunctionSignatureIndex =
    std::unordered_map<uint32_t, declaration_semantics::FunctionSignature>;

std::optional<CallArgumentStateSpace> call_state_space(
    syntax_ast::AstStateSpace state_space) {
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
  return std::nullopt;
}

std::optional<PointedStateSpace> pointed_state_space(
    const std::optional<std::string>& spelling) {
  if (!spelling)
    return std::nullopt;
  if (*spelling == ".local")
    return PointedStateSpace::Local;
  if (*spelling == ".shared")
    return PointedStateSpace::Shared;
  if (*spelling == ".global")
    return PointedStateSpace::Global;
  if (*spelling == ".const")
    return PointedStateSpace::Constant;
  return std::nullopt;
}

std::optional<uint64_t> unsigned_value(std::string_view spelling) {
  if (!spelling.empty() && (spelling.back() == 'u' || spelling.back() == 'U'))
    spelling.remove_suffix(1);
  uint64_t value = 0;
  const auto [end, error] = std::from_chars(
      spelling.data(), spelling.data() + spelling.size(), value);
  if (spelling.empty() || error != std::errc{} ||
      end != spelling.data() + spelling.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<uint64_t> contract_array_size(
    const declaration_semantics::FunctionParameterContract& contract) {
  if (!contract.array_extent || contract.array_extent->empty() ||
      contract.array_extent->front() != '#') {
    return std::nullopt;
  }
  return unsigned_value(std::string_view{*contract.array_extent}.substr(1));
}

CallArgumentProperties call_argument_properties(
    const declaration_semantics::FunctionParameterContract& contract) {
  CallArgumentProperties properties{
      .state_space = *call_state_space(contract.state_space),
      .type_spelling = contract.type,
      .array_alignment = contract.alignment
                             ? unsigned_value(*contract.alignment).value_or(1)
                             : 1,
      .is_array = contract.is_array,
      .array_size = contract_array_size(contract),
  };
  if (contract.is_pointer) {
    const uint64_t pointer_alignment =
        contract.pointer_alignment
            ? unsigned_value(*contract.pointer_alignment).value_or(4)
            : 4;
    properties.pointer = {
        .pointed_state_space = pointed_state_space(contract.pointer_space),
        .pointed_alignment = pointer_alignment,
    };
  }
  return properties;
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
      .state_space = *call_state_space(declaration.state_space),
      .type_spelling = declaration.vector_type ? declaration.vector_type->text +
                                                     " " + declaration.type.text
                                               : declaration.type.text,
      .array_alignment = symbol.address_alignment.value_or(1),
      .is_array = !declarator.array_dimensions.empty(),
      .array_size = array_size(declarator),
  };
}

std::optional<binding::SymbolId> declared_symbol(
    const binding::SymbolTable& symbols, binding::ScopeId scope,
    const syntax_ast::AstVariableDeclarator& declarator) {
  const bool parameterized = declarator.parameterized_count.has_value();
  const auto found = std::ranges::find_if(symbols.symbols(),
                                          [&](const binding::Symbol& symbol) {
    return symbol.scope == scope &&
           symbol.name == declarator.name.syntax.text &&
           symbol.parameterized_count.has_value() == parameterized;
  });
  if (found == symbols.symbols().end())
    return std::nullopt;
  return found->id;
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
          properties.emplace(
              symbol_id->value,
              call_argument_properties(*declaration, declarator,
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
        properties.emplace(lookup->symbol.value,
                           call_argument_properties(contracts[index]));
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
      signatures.try_emplace(lookup->symbol.value,
                             declaration_semantics::functionSignature(
                                 *prototype));
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

void index_function_metadata_signatures(
    const syntax_ast::AstFunction& function, const binding::SymbolTable& symbols,
    binding::ScopeId scope, FunctionSignatureIndex& signatures) {
  index_body_metadata_signatures(function.body, symbols, scope, signatures);
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

void check_call_abi(const syntax_ast::AstInstruction& call,
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
    if (!lookup || symbols.symbol(lookup->symbol).kind !=
                       binding::SymbolKind::Function)
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

  const auto check_group = [&](std::string_view kind, const auto* actuals,
                               const auto& formals) {
    const size_t actual_count =
        actuals == nullptr ? 0 : actuals->parameters.size();
    if (actual_count != formals.size()) {
      diagnostics.push_back(ResolveDiagnostic{
          .range = actuals == nullptr ? target_range : actuals->range,
          .message = fmt::format("{} has {} {} argument{} but callee requires {}.",
                                 call_subject, actual_count, kind,
                                 actual_count == 1 ? "" : "s", formals.size()),
      });
    }
    if (actuals == nullptr)
      return;
    const size_t count = std::min(actuals->parameters.size(), formals.size());
    for (size_t index = 0; index < count; ++index) {
      const auto& actual = actuals->parameters[index];
      const SourceRange range = std::visit(
          [](const auto& value) { return value.syntax.range; }, actual);
      const auto formal_properties = call_argument_properties(formals[index]);
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
            .type_spelling = formals[index].type,
        };
      } else {
        const auto& identifier =
            std::get<syntax_ast::AstIdentifierRef>(actual);
        const auto actual_lookup =
            symbols.lookup(scope, identifier.syntax.text);
        if (!actual_lookup) {
          throw ResolveException(fmt::format(
              "Bound call argument '{}' has no symbol.", identifier.syntax.text));
        }
        const auto properties_it = properties.find(actual_lookup->symbol.value);
        if (properties_it == properties.end()) {
          throw ResolveException(fmt::format(
              "Bound call argument '{}' has no ABI properties.",
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
            .message = metadata == nullptr
                ? fmt::format("Direct call {} argument {} for '{}' has {}.",
                              kind, index + 1, callee_name,
                              compatibility_message(compatibility))
                : fmt::format("Indirect call via metadata '{}' {} argument {} "
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

bool is_staging_transparent(const syntax_ast::AstFunctionBodyItem& item) {
  return std::holds_alternative<syntax_ast::AstLocDirective>(item) ||
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
      const auto* call = call_index < body.size()
                             ? std::get_if<syntax_ast::AstInstruction>(
                                   &body[call_index])
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

void resolve_body(
    const std::vector<syntax_ast::AstFunctionBodyItem>& body,
    const ResolveContext& context, const binding::SymbolTable& symbols,
    const FunctionSignatureIndex& signatures,
    const CallArgumentPropertyIndex& call_argument_properties,
    ResolvedFunction& resolved_function, ModuleResolveDiagnostics& diagnostics) {
  for (const auto& body_item : body) {
    if (const auto* instruction =
            std::get_if<syntax_ast::AstInstruction>(&body_item)) {
      auto resolved = resolveInstruction(*instruction, context);
      if (!resolved) {
        diagnostics.push_back(std::move(resolved.error()));
        continue;
      }
      check_call_abi(*instruction, symbols, context.scope, signatures,
                     call_argument_properties, diagnostics);
      resolved_function.body.push_back(std::move(*resolved));
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

  FunctionSignatureIndex signatures;
  CallArgumentPropertyIndex call_argument_properties;
  std::vector<binding::ScopeId> function_scopes;
  for (const binding::Scope& scope : binding_result.table.scopes()) {
    if (scope.kind == binding::ScopeKind::Function)
      function_scopes.push_back(scope.id);
  }
  size_t function_index = 0;
  for (const syntax_ast::AstModuleItem& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (function == nullptr)
      continue;
    if (function_index >= function_scopes.size())
      throw ResolveException(
          "Bound module has no function scope for syntax function.");
    const binding::ScopeId scope = function_scopes[function_index++];
    const auto lookup = binding_result.table.lookup(
        binding_result.table.moduleScope(), function->name.syntax.text);
    if (!lookup)
      continue;
    signatures.try_emplace(lookup->symbol.value,
                           declaration_semantics::functionSignature(*function));
    index_function_call_arguments(*function, binding_result.table, scope,
                                  call_argument_properties);
  }
  function_index = 0;
  for (const syntax_ast::AstModuleItem& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (function == nullptr)
      continue;
    if (function_index >= function_scopes.size())
      throw ResolveException(
          "Bound module has no function scope for syntax function.");
    index_function_metadata_signatures(*function, binding_result.table,
                                       function_scopes[function_index++],
                                       signatures);
  }

  std::vector<ResolvedFunction> functions;
  function_index = 0;
  for (const syntax_ast::AstModuleItem& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (function == nullptr)
      continue;
    if (function_index >= function_scopes.size())
      throw ResolveException(
          "Bound module has no function scope for syntax function.");
    const binding::ScopeId scope = function_scopes[function_index++];

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
        .range = function->range,
    };
    resolve_body(function->body, context, binding_result.table, signatures,
                 call_argument_properties, resolved_function, diagnostics);
    check_call_staging_body(*function, function->body, binding_result.table,
                            scope, diagnostics);
    functions.push_back(std::move(resolved_function));
  }

  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  ResolvedModule module{
      .symbols = std::move(binding_result.table),
      .functions = std::move(functions),
      .range = ast.range,
  };
  const auto availability = checkModuleAvailability(ast, module);
  if (!availability) {
    ModuleResolveDiagnostics availability_diagnostics;
    availability_diagnostics.reserve(availability.error().size());
    for (const checker::CheckDiagnostic& diagnostic : availability.error()) {
      availability_diagnostics.push_back(
          {.range = diagnostic.range, .message = diagnostic.message});
    }
    return std::unexpected(std::move(availability_diagnostics));
  }
  return module;
}

}  // namespace ptx_frontend::resolved_ir
