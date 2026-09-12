#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>

#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/base/ptx_special_register.hpp>

namespace ptx_frontend::binding {
namespace {

std::optional<uint32_t> parseParameterizedIndex(std::string_view base,
                                                std::string_view name) {
  if (base.size() >= name.size() || name.size() - base.size() > 10 ||
      !name.starts_with(base))
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
  if (!existing.parameterized_count) {
    if (!parameterized_count)
      return existing.name == name;
    return parameterizedNameContains(name, *parameterized_count, existing.name);
  }
  if (!parameterized_count)
    return parameterizedNameContains(existing.name,
                                     *existing.parameterized_count, name);

  if (*existing.parameterized_count == 0 || *parameterized_count == 0)
    return false;

  // Neither group base is a member.  If two nonempty groups overlap, the
  // first member of one group is contained by the other group.
  const std::string existing_first = existing.name + "0";
  const std::string candidate_first = std::string{name} + "0";
  return parameterizedNameContains(existing.name, *existing.parameterized_count,
                                   candidate_first) ||
         parameterizedNameContains(name, *parameterized_count, existing_first);
}

}  // namespace

void SymbolTable::keepEarliest(std::optional<SymbolId>& destination,
                               SymbolId candidate) {
  if (!destination || candidate.value < destination->value)
    destination = candidate;
}

size_t SymbolTable::SourceRangeHash::operator()(
    const SourceRange& range) const noexcept {
  size_t result = std::hash<int32_t>{}(range.start.line);
  const auto combine = [&result](int32_t value) {
    result ^= std::hash<int32_t>{}(value) + 0x9e3779b9u + (result << 6u) +
              (result >> 2u);
  };
  combine(range.start.column);
  combine(range.end.line);
  combine(range.end.column);
  return result;
}

void SymbolTable::indexMemberNumber(NumericMemberIndex& index, uint32_t member,
                                    SymbolId symbol) {
  uint32_t node = 0;
  keepEarliest(index.nodes[node].earliest_symbol, symbol);
  for (int bit = 31; bit >= 0; --bit) {
    const auto branch = static_cast<size_t>((member >> bit) & 1u);
    const std::optional<uint32_t> child = index.nodes[node].children[branch];
    if (!child) {
      const uint32_t next = static_cast<uint32_t>(index.nodes.size());
      index.nodes[node].children[branch] = next;
      index.nodes.push_back({});
      node = next;
    } else {
      node = *child;
    }
    keepEarliest(index.nodes[node].earliest_symbol, symbol);
  }
}

std::optional<SymbolId> SymbolTable::earliestMemberBelow(
    const NumericMemberIndex& index, uint32_t limit) {
  if (limit == 0 || index.nodes.empty())
    return std::nullopt;

  std::optional<SymbolId> result;
  uint32_t node = 0;
  for (int bit = 31; bit >= 0; --bit) {
    const auto branch = static_cast<size_t>((limit >> bit) & 1u);
    if (branch != 0 && index.nodes[node].children[0]) {
      keepEarliest(
          result, *index.nodes[*index.nodes[node].children[0]].earliest_symbol);
    }
    const std::optional<uint32_t>& next = index.nodes[node].children[branch];
    if (!next)
      return result;
    node = *next;
  }
  return result;
}

void SymbolTable::indexParameterizedBase(ParameterizedPrefixIndex& index,
                                         std::string_view base,
                                         SymbolId symbol) {
  uint32_t node = 0;
  for (const char character : base) {
    uint32_t next;
    if (const auto found = index.nodes[node].children.find(character);
        found != index.nodes[node].children.end()) {
      next = found->second;
    } else {
      next = static_cast<uint32_t>(index.nodes.size());
      index.nodes[node].children.emplace(character, next);
      index.nodes.push_back({});
    }
    node = next;
  }
  keepEarliest(index.nodes[node].symbol, symbol);
}

std::optional<SymbolId> SymbolTable::parameterizedContaining(
    const ParameterizedPrefixIndex& index, const std::vector<Symbol>& symbols,
    std::string_view spelling) {
  if (index.nodes.empty())
    return std::nullopt;

  std::optional<SymbolId> result;
  uint32_t node = 0;
  for (const char character : spelling) {
    const auto found = index.nodes[node].children.find(character);
    if (found == index.nodes[node].children.end())
      break;
    node = found->second;
    if (!index.nodes[node].symbol)
      continue;
    const Symbol& candidate = symbols[index.nodes[node].symbol->value];
    const auto member = parseParameterizedIndex(candidate.name, spelling);
    if (member && candidate.parameterized_count &&
        *member < *candidate.parameterized_count) {
      keepEarliest(result, candidate.id);
    }
  }
  return result;
}

void SymbolTable::indexMemberSpelling(ScopeNameIndex& index,
                                      std::string_view spelling,
                                      SymbolId symbol) {
  const size_t first_base_length =
      std::max<size_t>(1, spelling.size() > 10 ? spelling.size() - 10 : 1);
  for (size_t base_length = first_base_length; base_length < spelling.size();
       ++base_length) {
    const std::string_view base = spelling.substr(0, base_length);
    const auto member = parseParameterizedIndex(base, spelling);
    if (!member)
      continue;
    const auto insertion = index.member_prefixes.try_emplace(std::string{base});
    indexMemberNumber(insertion.first->second, *member, symbol);
  }
}

namespace {

std::optional<uint64_t> parseDebugFileId(std::string_view text) {
  return base::parseIntegerMagnitude(text);
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
  if (type == ".u8" || type == ".s8" || type == ".b8" || type == ".pred")
    return 1;
  if (type == ".u16" || type == ".s16" || type == ".b16" || type == ".f16" ||
      type == ".bf16")
    return 2;
  if (type == ".u32" || type == ".s32" || type == ".b32" || type == ".f32" ||
      type == ".f16x2" || type == ".tf32")
    return 4;
  if (type == ".u64" || type == ".s64" || type == ".b64" || type == ".f64")
    return 8;
  if (type == ".b128")
    return 16;
  return std::nullopt;
}

std::optional<uint64_t> parseAlignment(std::string_view text) {
  const auto value = base::parseIntegerMagnitude(text);
  if (!value || *value == 0 || (*value & (*value - 1)) != 0)
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

/** Find the unique declaration scope, including distinct prototype scopes. */
std::optional<ScopeId> SymbolTable::functionScope(SourceRange range) const {
  std::optional<ScopeId> result;
  for (const Scope& candidate : scopes_) {
    if (candidate.kind != ScopeKind::Function || candidate.range != range)
      continue;
    if (result)
      return std::nullopt;
    result = candidate.id;
  }
  return result;
}

std::optional<ScopeId> SymbolTable::blockScope(ScopeId parent,
                                               SourceRange range) const {
  const auto found =
      std::ranges::find_if(scopes_, [parent, range](const Scope& scope) {
        return scope.kind == ScopeKind::Block && scope.parent == parent &&
               scope.range == range;
      });
  return found == scopes_.end() ? std::nullopt : std::optional{found->id};
}

std::optional<SymbolLookup> SymbolTable::lookup(ScopeId scope_id,
                                                std::string_view name) const {
  for (;;) {
    const ScopeNameIndex& index = scope_name_indexes_.at(scope_id.value);
    if (const auto ordinary = index.ordinary_exact.find(name);
        ordinary != index.ordinary_exact.end()) {
      return SymbolLookup{ordinary->second, std::nullopt};
    }
    // A single compact group needs no prefix traversal or candidate ordering.
    if (index.parameterized_exact.size() == 1) {
      const Symbol& symbol =
          symbols_[index.parameterized_exact.begin()->second.value];
      const auto member = parseParameterizedIndex(symbol.name, name);
      if (member && symbol.parameterized_count &&
          *member < *symbol.parameterized_count)
        return SymbolLookup{symbol.id, *member};
    } else if (const auto parameterized = parameterizedContaining(
                   index.parameterized_prefixes, symbols_, name)) {
      const Symbol& symbol = symbols_[parameterized->value];
      return SymbolLookup{*parameterized,
                          parseParameterizedIndex(symbol.name, name)};
    }
    const Scope& current = scope(scope_id);
    if (!current.parent)
      return std::nullopt;
    scope_id = *current.parent;
  }
}

std::optional<SymbolId> SymbolTable::exactDeclaration(
    ScopeId scope, std::string_view name, bool parameterized) const {
  const ScopeNameIndex& index = scope_name_indexes_.at(scope.value);
  const auto& declarations =
      parameterized ? index.parameterized_exact : index.ordinary_exact;
  const auto found = declarations.find(name);
  return found == declarations.end() ? std::nullopt
                                     : std::optional<SymbolId>{found->second};
}

const SymbolReference* SymbolTable::initializerReference(
    SourceRange range) const noexcept {
  const auto found = initializer_reference_indexes_.find(range);
  if (found == initializer_reference_indexes_.end())
    return nullptr;
  return &references_[found->second];
}

const SymbolReference* SymbolTable::branchTargetSetReference(
    SourceRange range) const noexcept {
  const auto found = branch_target_set_reference_indexes_.find(range);
  if (found == branch_target_set_reference_indexes_.end())
    return nullptr;
  return &references_[found->second];
}

std::optional<bool> SymbolTable::hasPriorDeclaration(
    SymbolId symbol, SourceRange use) const noexcept {
  const auto use_order = source_orders_.find(use);
  if (use_order == source_orders_.end() ||
      symbol.value >= declaration_occurrences_.size()) {
    return std::nullopt;
  }
  const auto& occurrences = declaration_occurrences_[symbol.value];
  bool unknown_occurrence_order = false;
  for (const SymbolDeclarationOccurrence& occurrence : occurrences) {
    if (occurrence.lexical_order &&
        *occurrence.lexical_order < use_order->second)
      return true;
    unknown_occurrence_order |= !occurrence.lexical_order.has_value();
  }
  return unknown_occurrence_order ? std::nullopt : std::optional{false};
}

struct SymbolTableBuilder {
  struct FunctionContext {
    const syntax_ast::AstFunction* function{};
    ScopeId scope;
  };

  SymbolBinding result;
  std::vector<FunctionContext> functions;
  std::unordered_set<SourceRange, SymbolTable::SourceRangeHash>
      ambiguous_source_orders;
  uint32_t next_source_order{};

  SymbolTableBuilder() {
    result.table.scopes_.push_back(Scope{
        .id = ScopeId{0},
        .kind = ScopeKind::Module,
        .parent = std::nullopt,
        .owner = std::nullopt,
        .range = std::nullopt,
    });
    result.table.scope_name_indexes_.emplace_back();
  }

  /** Record one source event unless a duplicate range makes its order ambiguous. */
  void indexSourceRange(SourceRange range) {
    if (ambiguous_source_orders.contains(range))
      return;
    const auto [iterator, inserted] =
        result.table.source_orders_.try_emplace(range, next_source_order++);
    if (!inserted) {
      result.table.source_orders_.erase(iterator);
      ambiguous_source_orders.insert(range);
    }
  }

  /** Index symbol references in a constant expression in lexical traversal order. */
  void indexConstantExpression(
      const syntax_ast::AstConstantExpression& expression) {
    std::visit(
        [this](const auto& value) {
          using Value = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::same_as<Value, syntax_ast::AstConstantSymbol>) {
            indexSourceRange(value.name.syntax.range);
          } else if constexpr (std::same_as<
                                   Value,
                                   syntax_ast::AstConstantParenthesized>) {
            indexConstantExpression(*value.expression);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantCall>) {
            indexConstantExpression(*value.callee);
            indexConstantExpression(*value.argument);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantCast> ||
                               std::same_as<Value,
                                            syntax_ast::AstConstantUnary>) {
            indexConstantExpression(*value.operand);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantBinary>) {
            indexConstantExpression(*value.left);
            indexConstantExpression(*value.right);
          } else if constexpr (std::same_as<
                                   Value, syntax_ast::AstConstantConditional>) {
            indexConstantExpression(*value.condition);
            indexConstantExpression(*value.true_expression);
            indexConstantExpression(*value.false_expression);
          }
        },
        expression.node);
  }

  /** Index initializer symbol use sites in one declaration before later items. */
  void indexVariableInitializers(
      const syntax_ast::AstVariableDeclaration& declaration) {
    for (const auto& declarator : declaration.declarators) {
      if (!declarator.initializer)
        continue;
      const auto index_initializer =
          [this](const auto& self,
                 const syntax_ast::AstInitializer& initializer) -> void {
        if (const auto* expression =
                std::get_if<syntax_ast::AstConstantExpression>(
                    &initializer.value)) {
          indexConstantExpression(*expression);
        } else {
          const auto& elements =
              std::get<syntax_ast::AstInitializerList>(initializer.value)
                  .elements;
          for (const auto& element : elements)
            self(self, element);
        }
      };
      index_initializer(index_initializer, *declarator.initializer);
    }
  }

  /** Index declaration/use events in a function body, including nested blocks. */
  void indexBodySourceOrder(
      const std::vector<syntax_ast::AstFunctionBodyItem>& body) {
    for (const auto& item : body) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        indexVariableInitializers(*declaration);
      } else if (const auto* targets =
                     std::get_if<syntax_ast::AstBranchTargets>(&item)) {
        indexSourceRange(targets->label.syntax.range);
      } else if (const auto* instruction =
                     std::get_if<syntax_ast::AstInstruction>(&item)) {
        for (const auto& operand : instruction->operands) {
          if (const auto* target_set =
                  std::get_if<syntax_ast::AstBranchTargetSet>(&operand))
            indexSourceRange(target_set->name.syntax.range);
        }
      } else if (const auto* block =
                     std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
                 block != nullptr && *block) {
        indexBodySourceOrder((*block)->body);
      }
    }
  }

  /** Build a unique lexical-order index without deriving order from coordinates. */
  void indexSourceOrder(const syntax_ast::AstModule& module) {
    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        indexVariableInitializers(*declaration);
      } else if (const auto* function =
                     std::get_if<syntax_ast::AstFunction>(&item)) {
        indexSourceRange(function->name.syntax.range);
        indexBodySourceOrder(function->body);
      }
    }
  }

  /** Retain a declaration occurrence independently of its canonical SymbolId. */
  void recordDeclarationOccurrence(SymbolId symbol, SourceRange range) {
    if (symbol.value >= result.table.declaration_occurrences_.size())
      throw std::logic_error("Symbol occurrence has no stable identity.");
    const auto order = result.table.source_orders_.find(range);
    result.table.declaration_occurrences_[symbol.value].push_back(
        SymbolDeclarationOccurrence{
            .symbol = symbol,
            .range = range,
            .lexical_order = order == result.table.source_orders_.end()
                                 ? std::nullopt
                                 : std::optional{order->second}});
  }

  /** Associate a declaration's range with its scope, not its canonical symbol. */
  ScopeId addFunctionScope(SymbolId owner, SourceRange range,
                           bool prefer_as_owned_scope) {
    const ScopeId id{static_cast<uint32_t>(result.table.scopes_.size())};
    result.table.scopes_.push_back(Scope{
        .id = id,
        .kind = ScopeKind::Function,
        .parent = result.table.moduleScope(),
        .owner = owner,
        .range = range,
    });
    result.table.scope_name_indexes_.emplace_back();
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
    result.table.scope_name_indexes_.emplace_back();
    return id;
  }

  std::optional<SymbolId> exactSymbol(
      ScopeId scope, std::string_view name,
      std::optional<uint32_t> parameterized_count) const {
    const SymbolTable::ScopeNameIndex& index =
        result.table.scope_name_indexes_.at(scope.value);
    const auto& exact =
        parameterized_count ? index.parameterized_exact : index.ordinary_exact;
    const auto found = exact.find(name);
    return found == exact.end() ? std::nullopt
                                : std::optional<SymbolId>{found->second};
  }

  /** Find the earliest same-scope declaration matching the overlap predicate. */
  std::optional<SymbolId> overlappingSymbol(
      ScopeId scope, std::string_view name,
      std::optional<uint32_t> parameterized_count) const {
    const SymbolTable::ScopeNameIndex& index =
        result.table.scope_name_indexes_.at(scope.value);
    std::optional<SymbolId> result_symbol;
    if (!parameterized_count)
      return SymbolTable::parameterizedContaining(index.parameterized_prefixes,
                                                  result.table.symbols_, name);

    if (*parameterized_count == 0)
      return std::nullopt;

    const std::string first_member = std::string{name} + "0";
    if (const auto containing = SymbolTable::parameterizedContaining(
            index.parameterized_prefixes, result.table.symbols_, first_member))
      SymbolTable::keepEarliest(result_symbol, *containing);
    if (const auto members = index.member_prefixes.find(name);
        members != index.member_prefixes.end()) {
      if (const auto member = SymbolTable::earliestMemberBelow(
              members->second, *parameterized_count))
        SymbolTable::keepEarliest(result_symbol, *member);
    }
    return result_symbol;
  }

  /** Add one non-metadata declaration to its scope-local owned indexes. */
  void indexSymbol(const Symbol& symbol) {
    SymbolTable::ScopeNameIndex& index =
        result.table.scope_name_indexes_.at(symbol.scope.value);
    if (!symbol.parameterized_count) {
      index.ordinary_exact.emplace(symbol.name, symbol.id);
      SymbolTable::indexMemberSpelling(index, symbol.name, symbol.id);
      return;
    }
    index.parameterized_exact.emplace(symbol.name, symbol.id);
    SymbolTable::indexParameterizedBase(index.parameterized_prefixes,
                                        symbol.name, symbol.id);
    if (*symbol.parameterized_count != 0)
      SymbolTable::indexMemberSpelling(index, symbol.name + "0", symbol.id);
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

    if (const auto overlap =
            overlappingSymbol(scope, name, parameterized_count)) {
      const Symbol& existing = result.table.symbol(*overlap);
      if (symbolNameSetsOverlap(existing, name, parameterized_count)) {
        result.diagnostics.push_back(BindDiagnostic{
            .kind = BindDiagnosticKind::DuplicateSymbol,
            .range = declaration_range,
            .previous_range = existing.declaration_range,
            .message = fmt::format(
                "Declarations '{}' and '{}' produce overlapping symbol names "
                "in the same scope.",
                name, existing.name),
        });
      }
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
    result.table.declaration_occurrences_.emplace_back();
    indexSymbol(result.table.symbols_.back());
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
                             SourceRange declaration_range, bool idempotent) {
    if (const auto previous = findMetadataSymbol(kind, name)) {
      if (!idempotent) {
        result.diagnostics.push_back(BindDiagnostic{
            .kind = BindDiagnosticKind::DuplicateSymbol,
            .range = declaration_range,
            .previous_range = result.table.symbol(*previous).declaration_range,
            .message =
                fmt::format("Duplicate symbol '{}' in debug metadata.", name),
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
    result.table.declaration_occurrences_.emplace_back();
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
    const auto count =
        base::parseIntegerMagnitude(declarator.parameterized_count->text);
    if (!count || *count == 0 ||
        *count > std::numeric_limits<uint32_t>::max()) {
      result.diagnostics.push_back(BindDiagnostic{
          .kind = BindDiagnosticKind::InvalidParameterizedCount,
          .range = declarator.parameterized_count->range,
          .previous_range = std::nullopt,
          .message = "Parameterized variable count must be a positive "
                     "32-bit integer.",
      });
      return 0;
    }
    return static_cast<uint32_t>(*count);
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
    const SymbolId function_symbol = addSymbol(
        result.table.moduleScope(), SymbolKind::Function,
        function.name.syntax.text, function.name.syntax.range,
        linkage(function.qualifiers, function.range), std::nullopt,
        std::nullopt, std::nullopt, std::nullopt, true, function.is_entry);
    const ScopeId function_scope = addFunctionScope(
        function_symbol, function.range, !function.is_prototype);
    recordDeclarationOccurrence(function_symbol, function.name.syntax.range);
    functions.push_back(FunctionContext{&function, function_scope});

    for (const auto& parameter : function.return_parameters) {
      addSymbol(function_scope, SymbolKind::ReturnParameter,
                parameter.name.syntax.text, parameter.name.syntax.range,
                SymbolLinkage::None, parameter.state_space, parameter.type.text,
                declarationAlignment(parameter.alignment, std::nullopt,
                                     parameter.type.text));
    }
    for (const auto& parameter : function.parameters) {
      addSymbol(function_scope, SymbolKind::InputParameter,
                parameter.name.syntax.text, parameter.name.syntax.range,
                SymbolLinkage::None, parameter.state_space, parameter.type.text,
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
    const SymbolId alias_symbol = addSymbol(
        result.table.moduleScope(), SymbolKind::Function,
        alias.alias.syntax.text, alias.alias.syntax.range, SymbolLinkage::None,
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, true, false);
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
                  prototype->label.syntax.text, prototype->label.syntax.range);
      } else if (const auto* targets =
                     std::get_if<syntax_ast::AstCallTargets>(&item)) {
        addSymbol(function_scope, SymbolKind::CallTargetSet,
                  targets->label.syntax.text, targets->label.syntax.range);
      } else if (const auto* targets =
                     std::get_if<syntax_ast::AstBranchTargets>(&item)) {
        const SymbolId target_set =
            addSymbol(function_scope, SymbolKind::BranchTargetSet,
                      targets->label.syntax.text, targets->label.syntax.range);
        recordDeclarationOccurrence(target_set, targets->label.syntax.range);
      } else if (const auto* block =
                     std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
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
    if (kind == ReferenceKind::Initializer) {
      result.table.initializer_reference_indexes_.try_emplace(
          identifier.syntax.range, result.table.references_.size() - 1);
    } else if (kind == ReferenceKind::BranchTargetSet) {
      result.table.branch_target_set_reference_indexes_.try_emplace(
          identifier.syntax.range, result.table.references_.size() - 1);
    }
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
        target = findMetadataSymbol(SymbolKind::DebugFile, std::to_string(*id));
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
    addMetadataReference(scope, ReferenceKind::DebugFile, directive.file_index);
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
            if (value.predicate.syntax.text != "_")
              addReference(scope, ReferenceKind::Predicate, value.predicate);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstPredicateOperand>) {
            addReference(scope, ReferenceKind::Predicate, value.name);
          } else if constexpr (std::same_as<Value, syntax_ast::AstImmediate> ||
                               std::same_as<Value,
                                            syntax_ast::AstNegatedImmediate>) {
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
                  reference,
                  kind == SymbolKind::CallPrototype ||
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
                  reference,
                  symbol.kind == SymbolKind::BranchTargetSet &&
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
                  reference,
                  symbol.kind == SymbolKind::Label &&
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
                     std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
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
    indexSourceOrder(module);
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
