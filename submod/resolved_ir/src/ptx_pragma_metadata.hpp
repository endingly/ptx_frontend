#pragma once

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

#include <algorithm>
#include <limits>
#include <span>
#include <tuple>
#include <vector>

namespace ptx_frontend::resolved_ir::detail {
namespace pragma_detail {
/** A declaration occurrence, rather than a canonical function's first owned scope. */
struct Occurrence {
  /** Actual bound declaration scope in the borrowed owning table. */
  binding::ScopeId scope;
  /** Function identity owning this declaration occurrence. */
  binding::SymbolId symbol;
  /** Whether the occurrence admits entry-header placement. */
  bool entry;
  /** Whether statement placement can reach a real definition. */
  bool definition;
  /** Full physical declaration extent. */
  SourceRange range;
};

/** Missing bound occurrences stay invalid instead of becoming module pragmas. */
inline binding::ScopeId missing_scope() {
  return binding::ScopeId{std::numeric_limits<uint32_t>::max()};
}

/** Copy grouped raw leaves without borrowing the producing AST. */
inline void append(std::vector<ResolvedPragma>& output,
                   const syntax_ast::AstPragma& pragma,
                   ResolvedPragmaPlacement placement, binding::ScopeId scope) {
  output.push_back({placement, scope, pragma.strings, pragma.range});
}

/** Preserve depth-first statement order and actual nested block scopes. */
inline void collect_body(
    std::vector<ResolvedPragma>& output,
    const std::vector<syntax_ast::AstFunctionBodyItem>& body,
    binding::ScopeId scope, const binding::SymbolTable& symbols) {
  for (const auto& item : body) {
    if (const auto* pragma = std::get_if<syntax_ast::AstPragma>(&item))
      append(output, *pragma, ResolvedPragmaPlacement::Statement, scope);
    else if (const auto* block =
                 std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
             block && *block) {
      const auto child = symbols.blockScope(scope, (*block)->range);
      collect_body(output, (*block)->body, child.value_or(missing_scope()),
                   symbols);
    }
  }
}

/** Physical pragma ranges are positive nonempty spans, not historical identities. */
inline bool valid_range(SourceRange range) {
  return range.start.line > 0 && range.start.column > 0 && range.end.line > 0 &&
         range.end.column > 0 &&
         std::tie(range.start.line, range.start.column) <
             std::tie(range.end.line, range.end.column);
}

/** Known lexical owners contain their whole directive or string leaf. */
inline bool contains(SourceRange owner, SourceRange use) {
  return valid_range(owner) && valid_range(use) &&
         std::tie(owner.start.line, owner.start.column) <=
             std::tie(use.start.line, use.start.column) &&
         std::tie(use.end.line, use.end.column) <=
             std::tie(owner.end.line, owner.end.column);
}

/** Recheck placement, owning occurrence and grouped leaves synchronously. */
inline checker::CheckDiagnostics check(
    std::span<const ResolvedPragma> records,
    const binding::SymbolTable& symbols,
    std::span<const Occurrence> occurrences,
    std::optional<checker::PtxVersion> version, SourceRange module_range) {
  checker::CheckDiagnostics errors;
  const auto report = [&](SourceRange range, std::string message,
                          checker::CheckDiagnosticKind kind =
                              checker::CheckDiagnosticKind::RuleViolation) {
    errors.push_back(
        {.kind = kind, .range = range, .message = std::move(message)});
  };
  /** Borrowed occurrence pointers expire with this validation call. */
  std::vector<const Occurrence*> owners(symbols.scopes().size());
  for (const auto& occurrence : occurrences)
    if (occurrence.scope.value < owners.size())
      owners[occurrence.scope.value] = &occurrence;
  for (const auto& record : records) {
    if (version && *version < checker::PtxVersion{2, 0})
      report(record.range, ".pragma requires PTX ISA >= 2.0.",
             checker::CheckDiagnosticKind::UnsupportedAvailability);
    bool associated = false;
    if (record.placement == ResolvedPragmaPlacement::Module) {
      associated =
          record.scope_id == symbols.moduleScope() &&
          record.scope_id.value < symbols.scopes().size() &&
          symbols.scope(record.scope_id).id == record.scope_id &&
          symbols.scope(record.scope_id).kind == binding::ScopeKind::Module;
    } else if (record.placement == ResolvedPragmaPlacement::EntryHeader ||
               record.placement == ResolvedPragmaPlacement::Statement) {
      auto scope = std::optional{record.scope_id};
      for (size_t count = 0; scope && count < symbols.scopes().size();
           ++count) {
        if (scope->value >= symbols.scopes().size())
          break;
        const auto& current = symbols.scope(*scope);
        if (current.id != *scope || !current.range ||
            !contains(*current.range, record.range))
          break;
        if (current.kind == binding::ScopeKind::Function) {
          const auto* occurrence = owners[current.id.value];
          associated = occurrence && current.owner == occurrence->symbol &&
                       current.range == occurrence->range &&
                       occurrence->symbol.value < symbols.symbols().size() &&
                       symbols.symbol(occurrence->symbol).kind ==
                           binding::SymbolKind::Function &&
                       (record.placement == ResolvedPragmaPlacement::EntryHeader
                            ? record.scope_id == current.id && occurrence->entry
                            : occurrence->definition);
          break;
        }
        if (record.placement != ResolvedPragmaPlacement::Statement ||
            current.kind != binding::ScopeKind::Block)
          break;
        scope = current.parent;
      }
    }
    if (!associated)
      report(
          record.range,
          "Pragma metadata has no valid placement and declaration occurrence.");
    if (module_range != SourceRange{} && !contains(module_range, record.range))
      report(record.range,
             "Pragma metadata lies outside its owning module boundaries.");
    if (!valid_range(record.range))
      report(record.range, "Pragma metadata has invalid source boundaries.");
    if (record.strings.empty())
      report(record.range, "Pragma metadata requires a nonempty string list.");
    std::optional<SourceRange> previous;
    for (const auto& leaf : record.strings) {
      if (leaf.text.size() < 2 || leaf.text.front() != '"' ||
          leaf.text.back() != '"')
        report(leaf.range,
               "Pragma metadata requires quoted raw string leaves.");
      if (!contains(record.range, leaf.range) ||
          (previous &&
           std::tie(previous->end.line, previous->end.column) >
               std::tie(leaf.range.start.line, leaf.range.start.column)))
        report(leaf.range,
               "Pragma metadata has invalid string-list boundaries.");
      previous = leaf.range;
    }
  }
  return errors;
}
}  // namespace pragma_detail

/** Project only structured pragma producers, retaining written traversal order. */
inline std::vector<ResolvedPragma> pragma_records(
    const syntax_ast::AstModule& ast, const binding::SymbolTable& symbols) {
  std::vector<ResolvedPragma> result;
  for (const auto& item : ast.items) {
    if (const auto* pragma = std::get_if<syntax_ast::AstPragma>(&item))
      pragma_detail::append(result, *pragma, ResolvedPragmaPlacement::Module,
                            symbols.moduleScope());
    else if (const auto* function =
                 std::get_if<syntax_ast::AstFunction>(&item)) {
      const auto scope = symbols.functionScope(function->range)
                             .value_or(pragma_detail::missing_scope());
      for (const auto& pragma : function->pragmas)
        pragma_detail::append(result, pragma,
                              ResolvedPragmaPlacement::EntryHeader, scope);
      pragma_detail::collect_body(result, function->body, scope, symbols);
    }
  }
  return result;
}

/** Validate current module records using actual retained declaration occurrences. */
inline checker::CheckDiagnostics check_pragmas(
    const ResolvedModule& module, std::optional<checker::PtxVersion> version) {
  std::vector<pragma_detail::Occurrence> occurrences;
  for (const auto& function : module.functions)
    occurrences.push_back({function.declaration_scope, function.symbol_id,
                           function.is_entry, !function.is_prototype,
                           function.range});
  return pragma_detail::check(module.pragmas, module.symbols, occurrences,
                              version, module.range);
}

/** Independently project and validate supplied advisory data without retaining AST borrows. */
inline checker::CheckDiagnostics check_pragmas(
    const syntax_ast::AstModule& ast, const binding::SymbolTable& symbols,
    std::optional<checker::PtxVersion> version) {
  std::vector<pragma_detail::Occurrence> occurrences;
  for (const auto& item : ast.items) {
    const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
    if (!function)
      continue;
    const auto scope = symbols.functionScope(function->range);
    if (scope && symbols.scope(*scope).owner)
      occurrences.push_back({*scope, *symbols.scope(*scope).owner,
                             function->is_entry, !function->is_prototype,
                             function->range});
  }
  const auto records = pragma_records(ast, symbols);
  return pragma_detail::check(records, symbols, occurrences, version,
                              ast.range);
}
}  // namespace ptx_frontend::resolved_ir::detail
