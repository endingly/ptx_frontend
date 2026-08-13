#include "ptx_ir/bind/ptx_symbol_table.hpp"

#include <cctype>
#include <charconv>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

namespace ptx_frontend::binding {
namespace {

std::optional<uint32_t> parseParameterizedIndex(std::string_view base,
                                                std::string_view name) {
  if (!name.starts_with(base) || name.size() == base.size())
    return std::nullopt;
  const std::string_view suffix = name.substr(base.size());
  for (const char character : suffix) {
    if (!std::isdigit(static_cast<unsigned char>(character)))
      return std::nullopt;
  }
  uint32_t index = 0;
  const auto [end, error] =
      std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
  if (error != std::errc{} || end != suffix.data() + suffix.size())
    return std::nullopt;
  return index;
}

bool isInitializerOperator(std::string_view spelling) {
  return spelling == "generic";
}

}  // namespace

const Scope& SymbolTable::scope(ScopeId id) const {
  return scopes_.at(id.value);
}

const Symbol& SymbolTable::symbol(SymbolId id) const {
  return symbols_.at(id.value);
}

std::optional<SymbolLookup> SymbolTable::lookup(ScopeId scope_id,
                                                std::string_view name) const {
  for (;;) {
    for (const Symbol& candidate : symbols_) {
      if (candidate.scope != scope_id)
        continue;
      if (!candidate.parameterized_count && candidate.name == name)
        return SymbolLookup{candidate.id, std::nullopt};
    }
    for (const Symbol& candidate : symbols_) {
      if (candidate.scope != scope_id || !candidate.parameterized_count)
        continue;
      const auto index = parseParameterizedIndex(candidate.name, name);
      if (index && *index < *candidate.parameterized_count)
        return SymbolLookup{candidate.id, *index};
    }
    const Scope& current = scope(scope_id);
    if (!current.parent)
      return std::nullopt;
    scope_id = *current.parent;
  }
}

struct SymbolTableBuilder {
  struct FunctionContext {
    const syntax_ast::AstFunction* function{};
    ScopeId scope;
  };

  SymbolBinding result;
  std::vector<FunctionContext> functions;

  SymbolTableBuilder() {
    result.table.scopes_.push_back(Scope{
        .id = ScopeId{0},
        .kind = ScopeKind::Module,
        .parent = std::nullopt,
        .owner = std::nullopt,
    });
  }

  ScopeId addFunctionScope(std::optional<SymbolId> owner) {
    const ScopeId id{static_cast<uint32_t>(result.table.scopes_.size())};
    result.table.scopes_.push_back(Scope{
        .id = id,
        .kind = ScopeKind::Function,
        .parent = result.table.moduleScope(),
        .owner = owner,
    });
    if (owner)
      result.table.symbols_[owner->value].owned_scope = id;
    return id;
  }

  std::optional<SymbolId> exactSymbol(ScopeId scope,
                                      std::string_view name) const {
    for (const Symbol& symbol : result.table.symbols_) {
      if (symbol.scope == scope && symbol.name == name)
        return symbol.id;
    }
    return std::nullopt;
  }

  SymbolId addSymbol(
      ScopeId scope, SymbolKind kind, std::string_view name,
      SourceRange declaration_range,
      std::optional<syntax_ast::AstStateSpace> state_space = std::nullopt,
      std::optional<std::string_view> type = std::nullopt,
      std::optional<uint32_t> parameterized_count = std::nullopt) {
    if (const auto previous = exactSymbol(scope, name)) {
      const Symbol& existing = result.table.symbol(*previous);
      result.diagnostics.push_back(BindDiagnostic{
          .kind = BindDiagnosticKind::DuplicateSymbol,
          .range = declaration_range,
          .previous_range = existing.declaration_range,
          .message =
              fmt::format("Duplicate symbol '{}' in the same scope.", name),
      });
      return *previous;
    }

    const SymbolId id{static_cast<uint32_t>(result.table.symbols_.size())};
    result.table.symbols_.push_back(Symbol{
        .id = id,
        .scope = scope,
        .kind = kind,
        .name = std::string{name},
        .declaration_range = declaration_range,
        .state_space = state_space,
        .type = type ? std::optional<std::string>{std::string{*type}}
                     : std::nullopt,
        .parameterized_count = parameterized_count,
        .owned_scope = std::nullopt,
    });
    return id;
  }

  std::optional<uint32_t> parameterizedCount(
      const syntax_ast::AstVariableDeclarator& declarator) {
    if (!declarator.parameterized_count)
      return std::nullopt;
    std::string_view text = declarator.parameterized_count->text;
    if (!text.empty() && (text.back() == 'u' || text.back() == 'U'))
      text.remove_suffix(1);
    uint32_t count = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), count);
    if (error != std::errc{} || end != text.data() + text.size() ||
        count == 0) {
      result.diagnostics.push_back(BindDiagnostic{
          .kind = BindDiagnosticKind::InvalidParameterizedCount,
          .range = declarator.parameterized_count->range,
          .previous_range = std::nullopt,
          .message = "Parameterized variable count must be a positive "
                     "32-bit integer.",
      });
      return 0;
    }
    return count;
  }

  void collectVariableDeclaration(
      ScopeId scope, const syntax_ast::AstVariableDeclaration& declaration) {
    for (const auto& declarator : declaration.declarators) {
      addSymbol(scope, SymbolKind::Variable, declarator.name.syntax.text,
                declarator.name.syntax.range, declaration.state_space,
                declaration.type.text, parameterizedCount(declarator));
    }
  }

  void collectFunction(const syntax_ast::AstFunction& function) {
    const auto previous =
        exactSymbol(result.table.moduleScope(), function.name.syntax.text);
    const SymbolId function_symbol =
        addSymbol(result.table.moduleScope(), SymbolKind::Function,
                  function.name.syntax.text, function.name.syntax.range);
    const ScopeId function_scope = addFunctionScope(
        previous ? std::nullopt : std::optional{function_symbol});
    functions.push_back(FunctionContext{&function, function_scope});

    for (const auto& parameter : function.return_parameters) {
      addSymbol(function_scope, SymbolKind::ReturnParameter,
                parameter.name.syntax.text, parameter.name.syntax.range,
                parameter.state_space, parameter.type.text);
    }
    for (const auto& parameter : function.parameters) {
      addSymbol(function_scope, SymbolKind::InputParameter,
                parameter.name.syntax.text, parameter.name.syntax.range,
                parameter.state_space, parameter.type.text);
    }
    for (const auto& item : function.body) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        collectVariableDeclaration(function_scope, *declaration);
      } else if (const auto* label = std::get_if<syntax_ast::AstLabel>(&item)) {
        addSymbol(function_scope, SymbolKind::Label, label->name.syntax.text,
                  label->name.syntax.range);
      }
    }
  }

  void addReference(ScopeId scope, ReferenceKind kind,
                    const syntax_ast::AstIdentifierRef& identifier) {
    result.table.references_.push_back(SymbolReference{
        .scope = scope,
        .kind = kind,
        .spelling = identifier.syntax.text,
        .range = identifier.syntax.range,
        .target = result.table.lookup(scope, identifier.syntax.text),
    });
  }

  void bindConstantExpression(
      ScopeId scope, ReferenceKind kind,
      const syntax_ast::AstConstantExpression& expression) {
    std::visit(
        [this, scope, kind](const auto& value) {
          using Value = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::same_as<Value, syntax_ast::AstConstantLiteral>) {
            return;
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantSymbol>) {
            addReference(scope, kind, value.name);
          } else if constexpr (std::same_as<
                                   Value,
                                   syntax_ast::AstConstantParenthesized>) {
            bindConstantExpression(scope, kind, *value.expression);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantCall>) {
            const auto* callee =
                std::get_if<syntax_ast::AstConstantSymbol>(&value.callee->node);
            if (callee == nullptr ||
                !isInitializerOperator(callee->name.syntax.text)) {
              bindConstantExpression(scope, kind, *value.callee);
            }
            bindConstantExpression(scope, kind, *value.argument);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantCast> ||
                               std::same_as<Value,
                                            syntax_ast::AstConstantUnary>) {
            bindConstantExpression(scope, kind, *value.operand);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantBinary>) {
            bindConstantExpression(scope, kind, *value.left);
            bindConstantExpression(scope, kind, *value.right);
          } else {
            bindConstantExpression(scope, kind, *value.condition);
            bindConstantExpression(scope, kind, *value.true_expression);
            bindConstantExpression(scope, kind, *value.false_expression);
          }
        },
        expression.node);
  }

  void bindInitializer(ScopeId scope,
                       const syntax_ast::AstInitializer& initializer) {
    if (const auto* expression = std::get_if<syntax_ast::AstConstantExpression>(
            &initializer.value)) {
      bindConstantExpression(scope, ReferenceKind::Initializer, *expression);
      return;
    }
    for (const auto& element :
         std::get<syntax_ast::AstInitializerList>(initializer.value).elements) {
      bindInitializer(scope, element);
    }
  }

  void bindVariableDeclaration(
      ScopeId scope, const syntax_ast::AstVariableDeclaration& declaration) {
    for (const auto& declarator : declaration.declarators) {
      for (const auto& dimension : declarator.array_dimensions) {
        if (dimension.size) {
          bindConstantExpression(scope, ReferenceKind::ArrayDimension,
                                 *dimension.size);
        }
      }
      if (declarator.initializer)
        bindInitializer(scope, *declarator.initializer);
    }
  }

  void bindOperand(ScopeId scope, const syntax_ast::AstOperand& operand) {
    std::visit(
        [this, scope](const auto& value) {
          using Value = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::same_as<Value, syntax_ast::AstIdentifierRef>) {
            addReference(scope, ReferenceKind::InstructionOperand, value);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstPredicateOperand>) {
            addReference(scope, ReferenceKind::Predicate, value.name);
          } else if constexpr (std::same_as<Value, syntax_ast::AstImmediate>) {
            return;
          } else if constexpr (std::same_as<Value, syntax_ast::AstAddress>) {
            if (const auto* identifier =
                    std::get_if<syntax_ast::AstIdentifierRef>(&value.base)) {
              addReference(scope, ReferenceKind::InstructionOperand,
                           *identifier);
            }
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstVectorMember>) {
            addReference(scope, ReferenceKind::InstructionOperand, value.base);
          } else {
            for (const auto& element : value.elements) {
              if (const auto* identifier =
                      std::get_if<syntax_ast::AstIdentifierRef>(&element)) {
                addReference(scope, ReferenceKind::InstructionOperand,
                             *identifier);
              }
            }
          }
        },
        operand);
  }

  void bindInstruction(ScopeId scope,
                       const syntax_ast::AstInstruction& instruction) {
    if (instruction.predicate)
      addReference(scope, ReferenceKind::Predicate,
                   instruction.predicate->name);
    for (const auto& operand : instruction.operands)
      bindOperand(scope, operand);
  }

  void bindFunction(const FunctionContext& context) {
    const auto& function = *context.function;
    for (const auto& parameter : function.return_parameters) {
      if (parameter.array_size) {
        bindConstantExpression(context.scope, ReferenceKind::ArrayDimension,
                               *parameter.array_size);
      }
    }
    for (const auto& parameter : function.parameters) {
      if (parameter.array_size) {
        bindConstantExpression(context.scope, ReferenceKind::ArrayDimension,
                               *parameter.array_size);
      }
    }
    for (const auto& item : function.body) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        bindVariableDeclaration(context.scope, *declaration);
      } else if (const auto* instruction =
                     std::get_if<syntax_ast::AstInstruction>(&item)) {
        bindInstruction(context.scope, *instruction);
      }
    }
  }

  SymbolBinding build(const syntax_ast::AstModule& module) {
    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        collectVariableDeclaration(result.table.moduleScope(), *declaration);
      } else if (const auto* function =
                     std::get_if<syntax_ast::AstFunction>(&item)) {
        collectFunction(*function);
      }
    }

    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        bindVariableDeclaration(result.table.moduleScope(), *declaration);
      }
    }
    for (const auto& function : functions)
      bindFunction(function);
    return std::move(result);
  }
};

SymbolBinding bindSymbols(const syntax_ast::AstModule& module) {
  return SymbolTableBuilder{}.build(module);
}

}  // namespace ptx_frontend::binding
