#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>

#include <utility>

namespace ptx_frontend::resolved_ir {
namespace {

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

void check_call_staging(const syntax_ast::AstFunction& function,
                        const binding::SymbolTable& symbols,
                        binding::ScopeId scope,
                        ModuleResolveDiagnostics& diagnostics) {
  for (size_t index = 0; index < function.body.size(); ++index) {
    const auto* instruction =
        std::get_if<syntax_ast::AstInstruction>(&function.body[index]);
    if (instruction == nullptr)
      continue;

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
      while (call_index < function.body.size() &&
             is_staging_store(function.body[call_index], symbols, scope)) {
        ++call_index;
      }
      const auto* call = call_index < function.body.size()
                             ? std::get_if<syntax_ast::AstInstruction>(
                                   &function.body[call_index])
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
           is_staging_load(function.body[call_index - 1], symbols, scope)) {
      --call_index;
    }
    const auto* call = call_index > 0 ? std::get_if<syntax_ast::AstInstruction>(
                                            &function.body[call_index - 1])
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
    check_call_staging(*function, binding_result.table, *symbol.owned_scope,
                       diagnostics);
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
