#include "ptx_debug_locations.hpp"

#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace ptx_frontend::resolved_ir::detail {
namespace {
/** Definition occurrence identity; canonical function-owned scope is not authoritative. */
struct Definition {
  /** Actual definition occurrence scope, independent of canonical owned_scope. */
  binding::ScopeId scope;
  /** Stable Function identity owning this occurrence. */
  binding::SymbolId symbol;
  /** Whole physical definition extent. */
  SourceRange range;
};
/** Physical ordering is used only within an event and for ownership containment. */
bool before(SourcePos a, SourcePos b) {
  return std::tie(a.line, a.column) <= std::tie(b.line, b.column);
}
/** All-zero ranges defer physical facts; partial, empty and reversed facts reject. */
bool valid_range(SourceRange r) {
  return r == SourceRange{} ||
         (r.start.line > 0 && r.start.column > 0 && r.end.line > 0 &&
          r.end.column > 0 && r.start != r.end && before(r.start, r.end));
}
/** Known owners contain known uses without inventing absent source coordinates. */
bool contains(SourceRange owner, SourceRange use) {
  return valid_range(owner) && valid_range(use) &&
         (owner == SourceRange{} ||
          (use != SourceRange{} && before(owner.start, use.start) &&
           before(use.end, owner.end)));
}
/** Unknown boundaries do not create an ordering assertion. */
bool follows(SourceRange previous, SourceRange next) {
  return previous == SourceRange{} || next == SourceRange{} ||
         before(previous.end, next.start);
}
/** Recheck one module's records and reference multiplicity in their stored order. */
checker::CheckDiagnostics check_locations(
    const binding::SymbolTable& table, std::span<const Definition> definitions,
    std::optional<checker::PtxVersion> version) {
  checker::CheckDiagnostics errors;
  const auto report = [&](SourceRange range, std::string message,
                          checker::CheckDiagnosticKind kind =
                              checker::CheckDiagnosticKind::RuleViolation) {
    errors.push_back(
        {.kind = kind, .range = range, .message = std::move(message)});
  };
  using Key = std::array<uint64_t, 3>;
  /** Only successfully decoded base triples enter this temporary numeric set. */
  std::set<Key> seen;
  /** Borrowed keys and pointers are limited to this immutable validation call. */
  using ReferenceKey = std::tuple<binding::ReferenceKind, uint32_t, int32_t,
                                  int32_t, int32_t, int32_t, std::string_view>;
  /** Consume references one-to-one, including coincident physical keys. */
  struct ReferenceGroup {
    /** Original reference indices; the table owns their spellings. */
    std::vector<size_t> indices;
    /** Number consumed by ordered location leaves. */
    size_t used{};
  };
  const auto key = [](binding::ReferenceKind kind, binding::ScopeId scope,
                      SourceRange range, std::string_view text) {
    return ReferenceKey{kind,
                        scope.value,
                        range.start.line,
                        range.start.column,
                        range.end.line,
                        range.end.column,
                        text};
  };
  std::map<ReferenceKey, ReferenceGroup> references;
  for (size_t index = 0; index < table.references().size(); ++index) {
    const auto& ref = table.references()[index];
    if (ref.kind == binding::ReferenceKind::DebugFile ||
        ref.kind == binding::ReferenceKind::DebugFunctionName)
      references[key(ref.kind, ref.scope, ref.range, ref.spelling)]
          .indices.push_back(index);
  }
  std::map<uint64_t, const binding::DebugFileDeclaration*> files;
  for (const auto& file : table.debugFiles())
    files.try_emplace(file.index, &file);
  std::map<std::string_view, const binding::Symbol*> labels;
  for (const auto& symbol : table.symbols())
    if (symbol.kind == binding::SymbolKind::DebugStringLabel &&
        symbol.scope == table.moduleScope())
      labels.try_emplace(symbol.name, &symbol);
  std::vector<const Definition*> definition_scopes(table.scopes().size());
  for (const auto& definition : definitions)
    if (definition.scope.value < definition_scopes.size())
      definition_scopes[definition.scope.value] = &definition;
  const auto reference = [&](binding::ScopeId scope,
                             binding::ReferenceKind kind,
                             const binding::DebugFileSourceLeaf& leaf) {
    const binding::Symbol* expected = nullptr;
    if (kind == binding::ReferenceKind::DebugFile) {
      const auto index = base::parseIntegerMagnitude(leaf.text);
      const auto found = index ? files.find(*index) : files.end();
      if (found != files.end()) {
        const auto& file = *found->second;
        if (file.symbol_id.value < table.symbols().size()) {
          const auto& symbol = table.symbol(file.symbol_id);
          if (symbol.kind == binding::SymbolKind::DebugFile &&
              symbol.scope == table.moduleScope() &&
              symbol.id == file.symbol_id &&
              symbol.name == std::to_string(*index) &&
              symbol.declaration_range == file.source_index.range)
            expected = &symbol;
        }
      }
    } else if (const auto found = labels.find(leaf.text);
               found != labels.end()) {
      expected = found->second;
    }
    const auto found = references.find(key(kind, scope, leaf.range, leaf.text));
    if (found == references.end() ||
        found->second.used == found->second.indices.size()) {
      report(leaf.range,
             "Debug location leaf requires one unused matching metadata "
             "reference.");
      return;
    }
    auto& group = found->second;
    const auto& ref = table.references()[group.indices[group.used++]];
    if (!expected || !ref.target ||
        ref.classification !=
            binding::ReferenceClassification::DeclaredSymbol ||
        ref.target->parameterized_index || ref.target->symbol != expected->id)
      report(leaf.range,
             "Debug location leaf disagrees with its bound metadata identity.");
  };
  for (const auto& location : table.debugLocations()) {
    if (version && *version < checker::PtxVersion{1, 0})
      report(location.range, ".loc requires PTX ISA >= 1.0.",
             checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    bool associated = false;
    auto scope = std::optional{location.scope};
    for (size_t count = 0; scope && count < table.scopes().size(); ++count) {
      if (scope->value >= table.scopes().size())
        break;
      const auto& current = table.scope(*scope);
      if (current.id != *scope ||
          (current.range && !contains(*current.range, location.range)))
        break;
      if (current.kind == binding::ScopeKind::Function) {
        const auto* definition = definition_scopes[current.id.value];
        associated = definition && current.owner == definition->symbol &&
                     current.range == definition->range &&
                     definition->symbol.value < table.symbols().size() &&
                     table.symbol(definition->symbol).kind ==
                         binding::SymbolKind::Function;
        break;
      }
      if (current.kind != binding::ScopeKind::Block)
        break;
      scope = current.parent;
    }
    if (!associated || !valid_range(location.range))
      report(
          location.range,
          "Debug location has no containing function-definition occurrence.");
    const auto leaf = [&](const binding::DebugFileSourceLeaf& value,
                          SourceRange owner) {
      if (value.text.empty() || !contains(owner, value.range))
        report(value.range,
               "Debug location leaf has invalid source boundaries.");
      const auto number = base::parseIntegerMagnitude(value.text);
      if (!number)
        report(value.range,
               "Debug location integer must fit an unsigned 64-bit magnitude.");
      return number;
    };
    const auto triple = [&](const binding::DebugLocationSourceTriple& value,
                            SourceRange owner) -> std::optional<Key> {
      const auto file = leaf(value.file_index, owner);
      const auto line = leaf(value.line_number, owner);
      const auto column = leaf(value.column_position, owner);
      if (!follows(value.file_index.range, value.line_number.range) ||
          !follows(value.line_number.range, value.column_position.range))
        report(owner, "Debug location triple fields are out of written order.");
      reference(location.scope, binding::ReferenceKind::DebugFile,
                value.file_index);
      if (!file || !line || !column)
        return std::nullopt;
      return Key{*file, *line, *column};
    };
    const auto base = triple(location.base, location.range);
    if (location.inline_context) {
      const auto& context = *location.inline_context;
      if (version && *version < checker::PtxVersion{7, 2})
        report(context.range, ".loc inline attributes require PTX ISA >= 7.2.",
               checker::CheckDiagnosticKind::UnsupportedPtxVersion);
      if (!contains(location.range, context.range) ||
          !follows(location.base.column_position.range, context.range) ||
          !contains(context.range, context.function_name_label.range) ||
          context.function_name_label.text.empty())
        report(context.range,
               "Debug location inline context has invalid source boundaries.");
      reference(location.scope, binding::ReferenceKind::DebugFunctionName,
                context.function_name_label);
      auto last = context.function_name_label.range;
      if (context.function_name_offset) {
        leaf(*context.function_name_offset, context.range);
        if (!follows(last, context.function_name_offset->range))
          report(context.function_name_offset->range,
                 "Debug function-name offset is out of written order.");
        last = context.function_name_offset->range;
      }
      if (!follows(last, context.target.file_index.range))
        report(context.target.file_index.range,
               "Debug inline target is out of written order.");
      const auto target = triple(context.target, context.range);
      if (target && !seen.contains(*target))
        report({context.target.file_index.range.start,
                context.target.column_position.range.end},
               "inlined_at requires a preceding .loc base (" +
                   std::to_string((*target)[0]) + ", " +
                   std::to_string((*target)[1]) + ", " +
                   std::to_string((*target)[2]) + ").");
    }
    if (base)
      seen.insert(*base);
  }
  for (const auto& [unused_key, group] : references)
    for (size_t index = group.used; index < group.indices.size(); ++index)
      report(table.references()[group.indices[index]].range,
             "Debug metadata reference has no owning location leaf.");
  return errors;
}
}  // namespace

checker::CheckDiagnostics check_debug_locations(
    const binding::SymbolTable& symbols, const ResolvedModule& module,
    std::optional<checker::PtxVersion> version) {
  std::vector<Definition> definitions;
  for (const auto& function : module.functions)
    if (!function.is_prototype)
      definitions.push_back(
          {function.declaration_scope, function.symbol_id, function.range});
  return check_locations(symbols, definitions, version);
}

checker::CheckDiagnostics check_debug_locations(
    const binding::SymbolTable& symbols, const syntax_ast::AstModule& ast,
    std::optional<checker::PtxVersion> version) {
  std::vector<Definition> definitions;
  for (const auto& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (!function || function->is_prototype)
      continue;
    const auto scope = symbols.functionScope(function->range);
    if (scope && symbols.scope(*scope).owner)
      definitions.push_back(
          {*scope, *symbols.scope(*scope).owner, function->range});
  }
  return check_locations(symbols, definitions, version);
}
}  // namespace ptx_frontend::resolved_ir::detail
