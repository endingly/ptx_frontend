#include "ptx_ir/bind/ptx_symbol_table.hpp"

#include <algorithm>
#include <array>
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

bool isIndexedSpecialRegister(std::string_view spelling,
                              std::string_view prefix, uint32_t count,
                              std::string_view suffix = {}) {
  if (!spelling.starts_with(prefix) || !spelling.ends_with(suffix) ||
      spelling.size() <= prefix.size() + suffix.size()) {
    return false;
  }
  const std::string_view index_text = spelling.substr(
      prefix.size(), spelling.size() - prefix.size() - suffix.size());
  if (index_text.size() > 1 && index_text.front() == '0')
    return false;
  uint32_t index = 0;
  const auto [end, error] = std::from_chars(
      index_text.data(), index_text.data() + index_text.size(), index);
  return error == std::errc{} && end == index_text.data() + index_text.size() &&
         index < count;
}

SymbolLinkage linkageFromSpelling(std::string_view spelling) {
  if (spelling == ".extern")
    return SymbolLinkage::External;
  if (spelling == ".visible")
    return SymbolLinkage::Visible;
  if (spelling == ".weak")
    return SymbolLinkage::Weak;
  return SymbolLinkage::None;
}

std::string_view referenceDescription(ReferenceKind kind) {
  switch (kind) {
    case ReferenceKind::InstructionOperand:
      return "instruction operand";
    case ReferenceKind::Predicate:
      return "predicate";
    case ReferenceKind::Initializer:
      return "initializer";
    case ReferenceKind::ArrayDimension:
      return "array dimension";
    case ReferenceKind::CallTarget:
      return "call target";
    case ReferenceKind::CallReturnParameter:
      return "call return parameter";
    case ReferenceKind::CallArgument:
      return "call argument";
    case ReferenceKind::CallTargetSet:
      return "call target set or prototype";
    case ReferenceKind::BranchTarget:
      return "branch target";
  }
  return "symbol";
}

}  // namespace

bool isSpecialRegister(std::string_view spelling) noexcept {
  constexpr std::array exact_names{
      std::string_view{"%laneid"},
      std::string_view{"%warpid"},
      std::string_view{"%nwarpid"},
      std::string_view{"%smid"},
      std::string_view{"%nsmid"},
      std::string_view{"%gridid"},
      std::string_view{"%is_explicit_cluster"},
      std::string_view{"%cluster_ctarank"},
      std::string_view{"%cluster_nctarank"},
      std::string_view{"%lanemask_eq"},
      std::string_view{"%lanemask_le"},
      std::string_view{"%lanemask_lt"},
      std::string_view{"%lanemask_ge"},
      std::string_view{"%lanemask_gt"},
      std::string_view{"%clock"},
      std::string_view{"%clock_hi"},
      std::string_view{"%clock64"},
      std::string_view{"%globaltimer"},
      std::string_view{"%globaltimer_lo"},
      std::string_view{"%globaltimer_hi"},
      std::string_view{"%reserved_smem_offset_begin"},
      std::string_view{"%reserved_smem_offset_end"},
      std::string_view{"%reserved_smem_offset_cap"},
      std::string_view{"%total_smem_size"},
      std::string_view{"%aggr_smem_size"},
      std::string_view{"%dynamic_smem_size"},
      std::string_view{"%current_graph_exec"},
  };
  if (std::ranges::contains(exact_names, spelling))
    return true;

  constexpr std::array vector_names{
      std::string_view{"%tid"},           std::string_view{"%ntid"},
      std::string_view{"%ctaid"},         std::string_view{"%nctaid"},
      std::string_view{"%clusterid"},     std::string_view{"%nclusterid"},
      std::string_view{"%cluster_ctaid"}, std::string_view{"%cluster_nctaid"},
  };
  for (const std::string_view name : vector_names) {
    if (spelling == name)
      return true;
    if (spelling.starts_with(name) && spelling.size() == name.size() + 2 &&
        spelling[name.size()] == '.' &&
        (spelling.back() == 'x' || spelling.back() == 'y' ||
         spelling.back() == 'z')) {
      return true;
    }
  }

  return isIndexedSpecialRegister(spelling, "%pm", 8) ||
         isIndexedSpecialRegister(spelling, "%pm", 8, "_64") ||
         isIndexedSpecialRegister(spelling, "%envreg", 32) ||
         isIndexedSpecialRegister(spelling, "%reserved_smem_offset_", 2);
}

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

  ScopeId addFunctionScope(SymbolId owner, bool prefer_as_owned_scope) {
    const ScopeId id{static_cast<uint32_t>(result.table.scopes_.size())};
    result.table.scopes_.push_back(Scope{
        .id = id,
        .kind = ScopeKind::Function,
        .parent = result.table.moduleScope(),
        .owner = owner,
    });
    Symbol& symbol = result.table.symbols_[owner.value];
    if (!symbol.owned_scope || prefer_as_owned_scope)
      symbol.owned_scope = id;
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
      SymbolLinkage linkage = SymbolLinkage::None,
      std::optional<syntax_ast::AstStateSpace> state_space = std::nullopt,
      std::optional<std::string_view> type = std::nullopt,
      std::optional<uint32_t> parameterized_count = std::nullopt,
      bool allow_redeclaration = false) {
    if (const auto previous = exactSymbol(scope, name)) {
      const Symbol& existing = result.table.symbol(*previous);
      if (!allow_redeclaration) {
        result.diagnostics.push_back(BindDiagnostic{
            .kind = BindDiagnosticKind::DuplicateSymbol,
            .range = declaration_range,
            .previous_range = existing.declaration_range,
            .message =
                fmt::format("Duplicate symbol '{}' in the same scope.", name),
        });
      }
      return *previous;
    }

    const SymbolId id{static_cast<uint32_t>(result.table.symbols_.size())};
    result.table.symbols_.push_back(Symbol{
        .id = id,
        .scope = scope,
        .kind = kind,
        .name = std::string{name},
        .declaration_range = declaration_range,
        .linkage = linkage,
        .state_space = state_space,
        .type = type ? std::optional<std::string>{std::string{*type}}
                     : std::nullopt,
        .parameterized_count = parameterized_count,
        .owned_scope = std::nullopt,
    });
    return id;
  }

  SymbolLinkage linkage(const std::vector<syntax_ast::AstSyntax>& qualifiers,
                        SourceRange declaration_range) {
    SymbolLinkage result_linkage = SymbolLinkage::None;
    std::optional<SourceRange> first_linkage_range;
    for (const syntax_ast::AstSyntax& qualifier : qualifiers) {
      const SymbolLinkage candidate = linkageFromSpelling(qualifier.text);
      if (candidate == SymbolLinkage::None)
        continue;
      if (result_linkage != SymbolLinkage::None) {
        result.diagnostics.push_back(BindDiagnostic{
            .kind = BindDiagnosticKind::ConflictingLinkageQualifiers,
            .range = qualifier.range,
            .previous_range = first_linkage_range.value_or(declaration_range),
            .message = "A declaration may have only one linkage qualifier.",
        });
        continue;
      }
      result_linkage = candidate;
      first_linkage_range = qualifier.range;
    }
    return result_linkage;
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
    const SymbolLinkage declaration_linkage =
        linkage(declaration.qualifiers, declaration.range);
    for (const auto& declarator : declaration.declarators) {
      addSymbol(scope, SymbolKind::Variable, declarator.name.syntax.text,
                declarator.name.syntax.range, declaration_linkage,
                declaration.state_space, declaration.type.text,
                parameterizedCount(declarator),
                scope == result.table.moduleScope());
    }
  }

  void collectFunction(const syntax_ast::AstFunction& function) {
    const SymbolId function_symbol =
        addSymbol(result.table.moduleScope(), SymbolKind::Function,
                  function.name.syntax.text, function.name.syntax.range,
                  linkage(function.qualifiers, function.range), std::nullopt,
                  std::nullopt, std::nullopt, true);
    const ScopeId function_scope =
        addFunctionScope(function_symbol, !function.is_prototype);
    functions.push_back(FunctionContext{&function, function_scope});

    for (const auto& parameter : function.return_parameters) {
      addSymbol(function_scope, SymbolKind::ReturnParameter,
                parameter.name.syntax.text, parameter.name.syntax.range,
                SymbolLinkage::None, parameter.state_space,
                parameter.type.text);
    }
    for (const auto& parameter : function.parameters) {
      addSymbol(function_scope, SymbolKind::InputParameter,
                parameter.name.syntax.text, parameter.name.syntax.range,
                SymbolLinkage::None, parameter.state_space,
                parameter.type.text);
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

  const SymbolReference& addReference(
      ScopeId scope, ReferenceKind kind,
      const syntax_ast::AstIdentifierRef& identifier) {
    std::optional<SymbolLookup> target;
    ReferenceClassification classification =
        ReferenceClassification::Unresolved;
    if (isSpecialRegister(identifier.syntax.text)) {
      classification = ReferenceClassification::SpecialRegister;
    } else if ((target = result.table.lookup(scope, identifier.syntax.text))) {
      classification =
          result.table.symbol(target->symbol).linkage == SymbolLinkage::External
              ? ReferenceClassification::ExternalSymbol
              : ReferenceClassification::DeclaredSymbol;
    }
    result.table.references_.push_back(SymbolReference{
        .scope = scope,
        .kind = kind,
        .spelling = identifier.syntax.text,
        .range = identifier.syntax.range,
        .classification = classification,
        .target = target,
    });
    if (classification == ReferenceClassification::Unresolved) {
      result.diagnostics.push_back(BindDiagnostic{
          .kind = BindDiagnosticKind::UnresolvedReference,
          .range = identifier.syntax.range,
          .previous_range = std::nullopt,
          .message =
              fmt::format("Unresolved {} '{}'.", referenceDescription(kind),
                          identifier.syntax.text),
      });
    }
    return result.table.references_.back();
  }

  bool isRegisterOrParameter(const Symbol& symbol) const {
    const bool supported_kind = symbol.kind == SymbolKind::Variable ||
                                symbol.kind == SymbolKind::InputParameter ||
                                symbol.kind == SymbolKind::ReturnParameter;
    return supported_kind && symbol.state_space &&
           (*symbol.state_space == syntax_ast::AstStateSpace::Register ||
            *symbol.state_space == syntax_ast::AstStateSpace::Parameter);
  }

  void diagnoseInvalidTarget(const SymbolReference& reference,
                             bool valid_target, std::string message) {
    if (reference.target && !valid_target) {
      result.diagnostics.push_back(BindDiagnostic{
          .kind = BindDiagnosticKind::InvalidReferenceTarget,
          .range = reference.range,
          .previous_range =
              result.table.symbol(reference.target->symbol).declaration_range,
          .message = std::move(message),
      });
    }
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
          } else if constexpr (std::same_as<Value, syntax_ast::AstVectorPack>) {
            for (const auto& element : value.elements) {
              if (const auto* identifier =
                      std::get_if<syntax_ast::AstIdentifierRef>(&element)) {
                addReference(scope, ReferenceKind::InstructionOperand,
                             *identifier);
              }
            }
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstCallParameterList>) {
            const ReferenceKind kind =
                value.kind == syntax_ast::AstCallParameterListKind::Return
                    ? ReferenceKind::CallReturnParameter
                    : ReferenceKind::CallArgument;
            for (const auto& parameter : value.parameters) {
              const auto* identifier =
                  std::get_if<syntax_ast::AstIdentifierRef>(&parameter);
              if (identifier == nullptr)
                continue;
              const SymbolReference& reference =
                  addReference(scope, kind, *identifier);
              if (reference.target) {
                const Symbol& symbol =
                    result.table.symbol(reference.target->symbol);
                diagnoseInvalidTarget(
                    reference, isRegisterOrParameter(symbol),
                    fmt::format("Call parameter '{}' must name a .reg or "
                                ".param variable.",
                                identifier->syntax.text));
              }
            }
          } else if constexpr (std::same_as<Value, syntax_ast::AstCallTarget>) {
            const SymbolReference& reference =
                addReference(scope, ReferenceKind::CallTarget, value.name);
            if (reference.target) {
              const Symbol& symbol =
                  result.table.symbol(reference.target->symbol);
              diagnoseInvalidTarget(
                  reference,
                  symbol.kind == SymbolKind::Function ||
                      (isRegisterOrParameter(symbol) &&
                       symbol.state_space ==
                           syntax_ast::AstStateSpace::Register),
                  fmt::format("Call target '{}' must name a function or a "
                              ".reg function pointer.",
                              value.name.syntax.text));
            }
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstCallTargetSet>) {
            addReference(scope, ReferenceKind::CallTargetSet, value.name);
          } else {
            const SymbolReference& reference =
                addReference(scope, ReferenceKind::BranchTarget, value.name);
            if (reference.target) {
              const Symbol& symbol =
                  result.table.symbol(reference.target->symbol);
              diagnoseInvalidTarget(
                  reference, symbol.kind == SymbolKind::Label,
                  fmt::format("Branch target '{}' must name a label in the "
                              "current function.",
                              value.name.syntax.text));
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
