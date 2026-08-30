#include <ptx_frontend/binding/ptx_symbol_table.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <ptx_frontend/base/ptx_special_register.hpp>

namespace ptx_frontend::binding {
namespace {

std::optional<uint32_t> parseParameterizedIndex(std::string_view base,
                                                std::string_view name) {
  if (!name.starts_with(base) || name.size() == base.size())
    return std::nullopt;
  const std::string_view suffix = name.substr(base.size());
  if (suffix.size() > 1 && suffix.front() == '0')
    return std::nullopt;
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

bool parameterizedNameContains(std::string_view base, uint32_t count,
                               std::string_view name) {
  const auto index = parseParameterizedIndex(base, name);
  return index && *index < count;
}

bool symbolNameSetsOverlap(const Symbol& existing, std::string_view name,
                           std::optional<uint32_t> parameterized_count) {
  if (existing.parameterized_count &&
      parameterizedNameContains(existing.name, *existing.parameterized_count,
                                name)) {
    return true;
  }
  if (parameterized_count &&
      parameterizedNameContains(name, *parameterized_count, existing.name)) {
    return true;
  }
  if (!existing.parameterized_count || !parameterized_count)
    return false;

  // For distinct bases, an overlap can only begin at the zero element of one
  // of the two parameterized declarations.
  const std::string existing_first = existing.name + "0";
  const std::string candidate_first = std::string{name} + "0";
  return parameterizedNameContains(existing.name, *existing.parameterized_count,
                                   candidate_first) ||
         parameterizedNameContains(name, *parameterized_count, existing_first);
}

bool isMetadataSymbol(SymbolKind kind) {
  return kind == SymbolKind::DebugFile ||
         kind == SymbolKind::DebugStringLabel;
}

std::optional<uint64_t> parseDebugFileId(std::string_view text) {
  if (!text.empty() && (text.back() == 'u' || text.back() == 'U'))
    text.remove_suffix(1);
  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) {
    text.remove_prefix(2);
    base = 16;
  }
  uint64_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size())
    return std::nullopt;
  return value;
}

bool isInitializerOperator(std::string_view spelling) {
  return spelling == "generic";
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

std::optional<uint64_t> scalarAlignment(std::string_view type) {
  if (type == ".u8" || type == ".s8" || type == ".b8" ||
      type == ".pred")
    return 1;
  if (type == ".u16" || type == ".s16" || type == ".b16" ||
      type == ".f16" || type == ".bf16")
    return 2;
  if (type == ".u32" || type == ".s32" || type == ".b32" ||
      type == ".f32" || type == ".f16x2" || type == ".tf32")
    return 4;
  if (type == ".u64" || type == ".s64" || type == ".b64" ||
      type == ".f64")
    return 8;
  if (type == ".b128")
    return 16;
  return std::nullopt;
}

std::optional<uint64_t> parseAlignment(std::string_view text) {
  if (!text.empty() && (text.back() == 'u' || text.back() == 'U'))
    text.remove_suffix(1);
  uint64_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size() ||
      value == 0 || (value & (value - 1)) != 0)
    return std::nullopt;
  return value;
}

std::optional<uint64_t> declarationAlignment(
    const std::optional<syntax_ast::AstSyntax>& alignment,
    const std::optional<syntax_ast::AstSyntax>& vector_type,
    std::string_view type) {
  if (alignment)
    return parseAlignment(alignment->text);
  const auto scalar = scalarAlignment(type);
  if (!scalar)
    return std::nullopt;
  if (!vector_type)
    return scalar;
  return *scalar * (vector_type->text == ".v2" ? 2 : 4);
}

std::optional<uint8_t> vectorWidth(
    const std::optional<syntax_ast::AstSyntax>& vector_type) {
  if (!vector_type)
    return std::nullopt;
  return vector_type->text == ".v2" ? 2 : 4;
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
    case ReferenceKind::BranchTargetSet:
      return "branch target set";
    case ReferenceKind::DebugFile:
      return "debug file";
    case ReferenceKind::DebugFunctionName:
      return "debug function name";
  }
  return "symbol";
}

}  // namespace

bool isSpecialRegister(std::string_view spelling) noexcept {
  return base::lookup(spelling).has_value();
}

const Scope& SymbolTable::scope(ScopeId id) const {
  return scopes_.at(id.value);
}

const Symbol& SymbolTable::symbol(SymbolId id) const {
  return symbols_.at(id.value);
}

std::optional<ScopeId> SymbolTable::blockScope(ScopeId parent,
                                               SourceRange range) const {
  const auto found = std::ranges::find_if(
      scopes_, [parent, range](const Scope& scope) {
        return scope.kind == ScopeKind::Block && scope.parent == parent &&
               scope.range == range;
      });
  return found == scopes_.end() ? std::nullopt : std::optional{found->id};
}

std::optional<SymbolLookup> SymbolTable::lookup(ScopeId scope_id,
                                                std::string_view name) const {
  for (;;) {
    for (const Symbol& candidate : symbols_) {
      if (candidate.scope != scope_id || isMetadataSymbol(candidate.kind))
        continue;
      if (!candidate.parameterized_count && candidate.name == name)
        return SymbolLookup{candidate.id, std::nullopt};
    }
    for (const Symbol& candidate : symbols_) {
      if (candidate.scope != scope_id || isMetadataSymbol(candidate.kind) ||
          !candidate.parameterized_count)
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
        .range = std::nullopt,
    });
  }

  ScopeId addFunctionScope(SymbolId owner, bool prefer_as_owned_scope) {
    const ScopeId id{static_cast<uint32_t>(result.table.scopes_.size())};
    result.table.scopes_.push_back(Scope{
        .id = id,
        .kind = ScopeKind::Function,
        .parent = result.table.moduleScope(),
        .owner = owner,
        .range = std::nullopt,
    });
    Symbol& symbol = result.table.symbols_[owner.value];
    if (!symbol.owned_scope || prefer_as_owned_scope)
      symbol.owned_scope = id;
    return id;
  }

  ScopeId addBlockScope(ScopeId parent, SourceRange range) {
    const ScopeId id{static_cast<uint32_t>(result.table.scopes_.size())};
    result.table.scopes_.push_back(Scope{
        .id = id,
        .kind = ScopeKind::Block,
        .parent = parent,
        .owner = std::nullopt,
        .range = range,
    });
    return id;
  }

  std::optional<SymbolId> exactSymbol(
      ScopeId scope, std::string_view name,
      std::optional<uint32_t> parameterized_count) const {
    for (const Symbol& symbol : result.table.symbols_) {
      if (!isMetadataSymbol(symbol.kind) && symbol.scope == scope &&
          symbol.name == name &&
          symbol.parameterized_count.has_value() ==
              parameterized_count.has_value()) {
        return symbol.id;
      }
    }
    return std::nullopt;
  }

  SymbolId addSymbol(
      ScopeId scope, SymbolKind kind, std::string_view name,
      SourceRange declaration_range,
      SymbolLinkage linkage = SymbolLinkage::None,
      std::optional<syntax_ast::AstStateSpace> state_space = std::nullopt,
      std::optional<std::string_view> type = std::nullopt,
      std::optional<uint64_t> address_alignment = std::nullopt,
      std::optional<uint32_t> parameterized_count = std::nullopt,
      bool allow_redeclaration = false, bool function_is_entry = false,
      std::optional<uint8_t> vector_width = std::nullopt) {
    if (const auto previous = exactSymbol(scope, name, parameterized_count)) {
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

    for (const Symbol& existing : result.table.symbols_) {
      if (isMetadataSymbol(existing.kind) || existing.scope != scope ||
          !symbolNameSetsOverlap(existing, name, parameterized_count)) {
        continue;
      }
      result.diagnostics.push_back(BindDiagnostic{
          .kind = BindDiagnosticKind::DuplicateSymbol,
          .range = declaration_range,
          .previous_range = existing.declaration_range,
          .message = fmt::format(
              "Declarations '{}' and '{}' produce overlapping symbol names "
              "in the same scope.",
              name, existing.name),
      });
      break;
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
        .vector_width = vector_width,
        .address_alignment = address_alignment,
        .parameterized_count = parameterized_count,
        .owned_scope = std::nullopt,
        .function_is_entry = function_is_entry,
        .canonical_function = std::nullopt,
    });
    return id;
  }

  std::optional<SymbolId> findMetadataSymbol(SymbolKind kind,
                                              std::string_view name) const {
    for (const Symbol& symbol : result.table.symbols_) {
      if (symbol.scope == result.table.moduleScope() && symbol.kind == kind &&
          symbol.name == name) {
        return symbol.id;
      }
    }
    return std::nullopt;
  }

  SymbolId addMetadataSymbol(SymbolKind kind, std::string_view name,
                             SourceRange declaration_range,
                             bool idempotent) {
    if (const auto previous = findMetadataSymbol(kind, name)) {
      if (!idempotent) {
        result.diagnostics.push_back(BindDiagnostic{
            .kind = BindDiagnosticKind::DuplicateSymbol,
            .range = declaration_range,
            .previous_range =
                result.table.symbol(*previous).declaration_range,
            .message = fmt::format("Duplicate symbol '{}' in debug metadata.",
                                   name),
        });
      }
      return *previous;
    }
    const SymbolId id{static_cast<uint32_t>(result.table.symbols_.size())};
    result.table.symbols_.push_back(Symbol{
        .id = id,
        .scope = result.table.moduleScope(),
        .kind = kind,
        .name = std::string{name},
        .declaration_range = declaration_range,
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
    const SymbolKind kind =
        scope != result.table.moduleScope() &&
                declaration.state_space == syntax_ast::AstStateSpace::Parameter
            ? SymbolKind::CallParameter
            : SymbolKind::Variable;
    for (const auto& declarator : declaration.declarators) {
      addSymbol(
          scope, kind, declarator.name.syntax.text,
          declarator.name.syntax.range, declaration_linkage,
          declaration.state_space, declaration.type.text,
          declarationAlignment(declaration.alignment, declaration.vector_type,
                               declaration.type.text),
          parameterizedCount(declarator), scope == result.table.moduleScope(),
          false, vectorWidth(declaration.vector_type));
    }
  }

  void collectDebugFile(const syntax_ast::AstFileDirective& directive) {
    const auto id = parseDebugFileId(directive.file_index.text);
    if (!id) {
      result.diagnostics.push_back(BindDiagnostic{
          .kind = BindDiagnosticKind::InvalidDebugFileId,
          .range = directive.file_index.range,
          .previous_range = std::nullopt,
          .message = "Debug file index must be an unsigned 64-bit integer.",
      });
      return;
    }
    addMetadataSymbol(SymbolKind::DebugFile, std::to_string(*id),
                      directive.file_index.range, true);
  }

  void collectDebugStringSection(
      const syntax_ast::AstSectionDirective& directive) {
    if (directive.name.text != ".debug_str")
      return;
    addMetadataSymbol(SymbolKind::DebugStringLabel, directive.name.text,
                      directive.name.range, true);
    for (size_t index = 0; index + 1 < directive.payload.size(); ++index) {
      if (directive.payload[index + 1].text == ":") {
        addMetadataSymbol(SymbolKind::DebugStringLabel,
                          directive.payload[index].text,
                          directive.payload[index].range, false);
      }
    }
  }

  void collectFunction(const syntax_ast::AstFunction& function) {
    const SymbolId function_symbol =
        addSymbol(result.table.moduleScope(), SymbolKind::Function,
                  function.name.syntax.text, function.name.syntax.range,
                  linkage(function.qualifiers, function.range), std::nullopt,
                  std::nullopt, std::nullopt, std::nullopt, true,
                  function.is_entry);
    const ScopeId function_scope =
        addFunctionScope(function_symbol, !function.is_prototype);
    functions.push_back(FunctionContext{&function, function_scope});

    for (const auto& parameter : function.return_parameters) {
      addSymbol(function_scope, SymbolKind::ReturnParameter,
                parameter.name.syntax.text, parameter.name.syntax.range,
                SymbolLinkage::None, parameter.state_space,
                parameter.type.text,
                declarationAlignment(parameter.alignment, std::nullopt,
                                     parameter.type.text));
    }
    for (const auto& parameter : function.parameters) {
      addSymbol(function_scope, SymbolKind::InputParameter,
                parameter.name.syntax.text, parameter.name.syntax.range,
                SymbolLinkage::None, parameter.state_space,
                parameter.type.text,
                declarationAlignment(parameter.alignment, std::nullopt,
                                     parameter.type.text));
    }
    collectBody(function.body, function_scope, function_scope);
  }

  void collectAlias(const syntax_ast::AstAliasDirective& alias) {
    const auto target = exactSymbol(result.table.moduleScope(),
                                    alias.aliasee.syntax.text, std::nullopt);
    if (!target || result.table.symbol(*target).kind != SymbolKind::Function)
      return;
    const SymbolId alias_symbol =
        addSymbol(result.table.moduleScope(), SymbolKind::Function,
                  alias.alias.syntax.text, alias.alias.syntax.range,
                  SymbolLinkage::None, std::nullopt, std::nullopt,
                  std::nullopt, std::nullopt, true, false);
    Symbol& symbol = result.table.symbols_[alias_symbol.value];
    if (symbol.kind == SymbolKind::Function)
      symbol.canonical_function = *target;
  }

  void collectBody(const std::vector<syntax_ast::AstFunctionBodyItem>& body,
                   ScopeId function_scope, ScopeId lexical_scope) {
    for (const auto& item : body) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        collectVariableDeclaration(lexical_scope, *declaration);
      } else if (const auto* label = std::get_if<syntax_ast::AstLabel>(&item)) {
        addSymbol(function_scope, SymbolKind::Label, label->name.syntax.text,
                  label->name.syntax.range);
      } else if (const auto* prototype =
                     std::get_if<syntax_ast::AstCallPrototype>(&item)) {
        addSymbol(function_scope, SymbolKind::CallPrototype,
                  prototype->label.syntax.text,
                  prototype->label.syntax.range);
      } else if (const auto* targets =
                     std::get_if<syntax_ast::AstCallTargets>(&item)) {
        addSymbol(function_scope, SymbolKind::CallTargetSet,
                  targets->label.syntax.text, targets->label.syntax.range);
      } else if (const auto* targets =
                     std::get_if<syntax_ast::AstBranchTargets>(&item)) {
        addSymbol(function_scope, SymbolKind::BranchTargetSet,
                  targets->label.syntax.text, targets->label.syntax.range);
      } else if (const auto* block =
                     std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(
                         &item);
                 block != nullptr && *block) {
        collectBody((*block)->body, function_scope,
                    addBlockScope(lexical_scope, (*block)->range));
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

  const SymbolReference& addMetadataReference(
      ScopeId scope, ReferenceKind kind, const syntax_ast::AstSyntax& syntax) {
    std::optional<SymbolId> target;
    if (kind == ReferenceKind::DebugFile) {
      if (const auto id = parseDebugFileId(syntax.text)) {
        target = findMetadataSymbol(SymbolKind::DebugFile,
                                    std::to_string(*id));
      }
    } else {
      target = findMetadataSymbol(SymbolKind::DebugStringLabel, syntax.text);
    }
    result.table.references_.push_back(SymbolReference{
        .scope = scope,
        .kind = kind,
        .spelling = syntax.text,
        .range = syntax.range,
        .classification = target ? ReferenceClassification::DeclaredSymbol
                                 : ReferenceClassification::Unresolved,
        .target = target ? std::optional{SymbolLookup{*target, std::nullopt}}
                         : std::nullopt,
    });
    if (!target) {
      result.diagnostics.push_back(BindDiagnostic{
          .kind = BindDiagnosticKind::UnresolvedReference,
          .range = syntax.range,
          .previous_range = std::nullopt,
          .message = fmt::format("Unresolved {} '{}'.",
                                 referenceDescription(kind), syntax.text),
      });
    }
    return result.table.references_.back();
  }

  void bindLoc(ScopeId scope, const syntax_ast::AstLocDirective& directive) {
    addMetadataReference(scope, ReferenceKind::DebugFile,
                         directive.file_index);
    if (!directive.inline_context)
      return;
    addMetadataReference(scope, ReferenceKind::DebugFunctionName,
                         directive.inline_context->function_name_label.syntax);
    addMetadataReference(scope, ReferenceKind::DebugFile,
                         directive.inline_context->file_index);
  }

  bool isRegisterOrParameter(const Symbol& symbol) const {
    const bool supported_kind = symbol.kind == SymbolKind::Variable ||
                                symbol.kind == SymbolKind::InputParameter ||
                                symbol.kind == SymbolKind::ReturnParameter ||
                                symbol.kind == SymbolKind::CallParameter;
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

  void bindOperand(ScopeId scope, ScopeId function_scope,
                   const syntax_ast::AstOperand& operand) {
    std::visit(
        [this, scope, function_scope](const auto& value) {
          using Value = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::same_as<Value, syntax_ast::AstIdentifierRef>) {
            // '_' is a write-only sink in selected operands, not a user-declared
            // symbol. Instruction resolution validates whether its position permits it.
            if (value.syntax.text != "_")
              addReference(scope, ReferenceKind::InstructionOperand, value);
          } else if constexpr (std::same_as<
                                   Value,
                                   syntax_ast::AstRegisterPredicatePair>) {
            if (value.dst.syntax.text != "_")
              addReference(scope, ReferenceKind::InstructionOperand, value.dst);
            addReference(scope, ReferenceKind::Predicate, value.predicate);
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
                // ``_`` is a write-only sink in selected vector operands, not
                // a user-declared symbol. Instruction resolution validates
                // whether the selected operand position permits it.
                if (identifier->syntax.text == "_")
                  continue;
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
            const SymbolReference& reference =
                addReference(scope, ReferenceKind::CallTargetSet, value.name);
            if (reference.target) {
              const SymbolKind kind =
                  result.table.symbol(reference.target->symbol).kind;
              diagnoseInvalidTarget(
                  reference, kind == SymbolKind::CallPrototype ||
                                 kind == SymbolKind::CallTargetSet,
                  fmt::format("Call target set '{}' must name a "
                              ".callprototype or .calltargets declaration.",
                                value.name.syntax.text));
            }
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstBranchTargetSet>) {
            const SymbolReference& reference =
                addReference(scope, ReferenceKind::BranchTargetSet, value.name);
            if (reference.target) {
              const Symbol& symbol =
                  result.table.symbol(reference.target->symbol);
              diagnoseInvalidTarget(
                  reference, symbol.kind == SymbolKind::BranchTargetSet &&
                                 symbol.scope == function_scope,
                  fmt::format("Branch target set '{}' must name a "
                              ".branchtargets declaration in the current "
                              "function.",
                              value.name.syntax.text));
            }
          } else {
            const SymbolReference& reference =
                addReference(scope, ReferenceKind::BranchTarget, value.name);
            if (reference.target) {
              const Symbol& symbol =
                  result.table.symbol(reference.target->symbol);
              diagnoseInvalidTarget(
                  reference, symbol.kind == SymbolKind::Label &&
                                 symbol.scope == function_scope,
                  fmt::format("Branch target '{}' must name a label in the "
                              "current function.",
                              value.name.syntax.text));
            }
          }
        },
        operand);
  }

  void bindInstruction(ScopeId scope, ScopeId function_scope,
                       const syntax_ast::AstInstruction& instruction) {
    if (instruction.predicate)
      addReference(scope, ReferenceKind::Predicate,
                   instruction.predicate->name);
    for (const auto& operand : instruction.operands)
      bindOperand(scope, function_scope, operand);
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
    bindBody(function.body, context.scope, context.scope);
  }

  void bindBody(const std::vector<syntax_ast::AstFunctionBodyItem>& body,
                ScopeId function_scope, ScopeId lexical_scope) {
    for (const auto& item : body) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        bindVariableDeclaration(lexical_scope, *declaration);
      } else if (const auto* instruction =
                     std::get_if<syntax_ast::AstInstruction>(&item)) {
        bindInstruction(lexical_scope, function_scope, *instruction);
      } else if (const auto* loc =
                     std::get_if<syntax_ast::AstLocDirective>(&item)) {
        bindLoc(lexical_scope, *loc);
      } else if (const auto* block =
                     std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(
                         &item);
                 block != nullptr && *block) {
        const auto block_scope =
            result.table.blockScope(lexical_scope, (*block)->range);
        if (!block_scope)
          throw std::logic_error("Collected syntax block has no scope.");
        bindBody((*block)->body, function_scope, *block_scope);
      }
    }
  }

  SymbolBinding build(const syntax_ast::AstModule& module) {
    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        collectVariableDeclaration(result.table.moduleScope(), *declaration);
      } else if (const auto* file =
                     std::get_if<syntax_ast::AstFileDirective>(&item)) {
        collectDebugFile(*file);
      } else if (const auto* section =
                     std::get_if<syntax_ast::AstSectionDirective>(&item)) {
        collectDebugStringSection(*section);
      } else if (const auto* function =
                     std::get_if<syntax_ast::AstFunction>(&item)) {
        collectFunction(*function);
      }
    }
    for (const auto& item : module.items) {
      if (const auto* alias = std::get_if<syntax_ast::AstAliasDirective>(&item))
        collectAlias(*alias);
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
