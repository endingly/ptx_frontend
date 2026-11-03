#include "ptx_legacy_dwarf.hpp"

#include <limits>
#include <ptx_frontend/base/ptx_identifier.hpp>
#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>
#include <tuple>

namespace ptx_frontend::resolved_ir::detail {
namespace {
/** One borrowed definition occurrence, not a canonical symbol's preferred scope. */
struct Definition {
  /** Actual declaration occurrence in the borrowed table. */
  binding::ScopeId scope;
  /** Canonical function identity owning that occurrence. */
  binding::SymbolId symbol;
  /** Current physical definition extent, not a cached producer version. */
  SourceRange range;
};
/** Keep missing scope joins invalid rather than assigning metadata to the module. */
binding::ScopeId missing_scope() {
  return {std::numeric_limits<uint32_t>::max()};
}
/** Copy depth-first lexical values without introducing executable nodes. */
void collect_body(std::vector<ResolvedLegacyDwarfRecord>& result,
                  const std::vector<syntax_ast::AstFunctionBodyItem>& body,
                  const binding::SymbolTable& symbols, binding::ScopeId scope) {
  for (const auto& item : body) {
    if (const auto* record =
            std::get_if<syntax_ast::AstLegacyDwarfRecord>(&item))
      result.push_back({*record, scope});
    else if (const auto* block =
                 std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
             block && *block)
      collect_body(
          result, (*block)->body, symbols,
          symbols.blockScope(scope, (*block)->range).value_or(missing_scope()));
  }
}
/** Compare physical coordinates only within known owning extents. */
bool before(SourcePos a, SourcePos b) {
  return std::tie(a.line, a.column) <= std::tie(b.line, b.column);
}
/** Wholly absent provenance is unknown; partial, empty and reversed facts are invalid. */
bool valid_range(SourceRange r) {
  return r == SourceRange{} ||
         (r.start.line > 0 && r.start.column > 0 && r.end.line > 0 &&
          r.end.column > 0 && r.start != r.end && before(r.start, r.end));
}
/** A known owner requires a known contained child; no ordering is inferred between records. */
bool contains(SourceRange owner, SourceRange use) {
  return valid_range(owner) && valid_range(use) &&
         (owner == SourceRange{} ||
          (use != SourceRange{} && before(owner.start, use.start) &&
           before(use.end, owner.end)));
}
/** Permit only an ordinary identifier or one leading dot followed by an identifier. */
bool symbolic_name(std::string_view text) {
  return base::isIdentifier(text) ||
         (text.starts_with('.') && base::isIdentifier(text.substr(1)));
}
/** Validate both source projections with one typed grammar, scope and format contract. */
checker::CheckDiagnostics check(
    std::span<const ResolvedLegacyDwarfRecord> records,
    const binding::SymbolTable& symbols,
    std::span<const Definition> definitions,
    std::optional<checker::PtxVersion> version, SourceRange module_range) {
  checker::CheckDiagnostics errors;
  const auto report = [&](SourceRange r, std::string message,
                          checker::CheckDiagnosticKind kind =
                              checker::CheckDiagnosticKind::RuleViolation) {
    errors.push_back({.kind = kind, .range = r, .message = std::move(message)});
  };
  /** Borrowed definition pointers expire when this synchronous validation finishes. */
  std::vector<const Definition*> owners(symbols.scopes().size());
  for (const auto& def : definitions)
    if (def.scope.value < owners.size())
      owners[def.scope.value] = &def;
  for (const auto& wrapped : records) {
    const auto& record = wrapped.value;
    if (version && (*version < checker::PtxVersion{1, 2} ||
                    *version >= checker::PtxVersion{2, 0}))
      report(record.range, "@@DWARF requires PTX ISA >= 1.2 and < 2.0.",
             checker::CheckDiagnosticKind::UnsupportedAvailability);
    bool scope_valid = false;
    auto scope = std::optional{wrapped.scope_id};
    for (size_t depth = 0; scope && depth < symbols.scopes().size(); ++depth) {
      if (scope->value >= symbols.scopes().size())
        break;
      const auto& current = symbols.scope(*scope);
      if (current.id != *scope)
        break;
      if (current.kind == binding::ScopeKind::Module) {
        scope_valid = current.id == symbols.moduleScope() && !current.parent &&
                      !current.owner;
        break;
      }
      if (!current.range || !contains(*current.range, record.range))
        break;
      if (current.kind == binding::ScopeKind::Function) {
        const auto* def = owners[current.id.value];
        scope_valid =
            def && current.parent == symbols.moduleScope() &&
            current.owner == def->symbol && current.range == def->range &&
            symbols.functionScope(def->range) == current.id &&
            def->symbol.value < symbols.symbols().size() &&
            symbols.symbol(def->symbol).kind == binding::SymbolKind::Function;
        break;
      }
      if (current.kind != binding::ScopeKind::Block || !current.parent ||
          symbols.blockScope(*current.parent, *current.range) != current.id)
        break;
      scope = current.parent;
    }
    if (!scope_valid)
      report(record.range,
             "Legacy debug record has no actual owning definition scope.",
             checker::CheckDiagnosticKind::ModuleSourceMismatch);
    if (!contains(module_range, record.range))
      report(record.range,
             "Legacy debug record lies outside its owning module boundaries.");
    if (!valid_range(record.range))
      report(record.range,
             "Legacy debug record has invalid source boundaries.");
    if (record.marker.text != "@@DWARF")
      report(record.marker.range, "Legacy debug record has an invalid marker.");
    if (!contains(record.range, record.marker.range))
      report(record.marker.range,
             "Legacy debug marker lies outside its record.");
    const auto payload_range =
        std::visit([](const auto& p) { return p.range; }, record.payload);
    if (!contains(record.range, payload_range) ||
        (record.marker.range != SourceRange{} &&
         payload_range != SourceRange{} &&
         !before(record.marker.range.end, payload_range.start)))
      report(payload_range,
             "Legacy debug payload is outside or precedes its marker.");
    std::optional<SourceRange> previous;
    const auto leaf = [&](const syntax_ast::AstSyntax& value) {
      if (!contains(payload_range, value.range) ||
          (previous && *previous != SourceRange{} &&
           value.range != SourceRange{} &&
           !before(previous->end, value.range.start)))
        report(value.range,
               "Legacy debug leaf is outside its payload or out of order.");
      previous = value.range;
    };
    if (const auto* data =
            std::get_if<syntax_ast::AstLegacyDwarfData>(&record.payload)) {
      uint64_t limit = 0;
      std::string_view spelling;
      switch (data->kind) {
        case syntax_ast::LegacyDwarfDataKind::Byte:
          limit = UINT8_MAX;
          spelling = ".byte";
          break;
        case syntax_ast::LegacyDwarfDataKind::FourByte:
          limit = UINT32_MAX;
          spelling = ".4byte";
          break;
        case syntax_ast::LegacyDwarfDataKind::Quad:
          limit = UINT64_MAX;
          spelling = ".quad";
          break;
        default:
          report(data->range, "Legacy debug data has an invalid width kind.");
          break;
      }
      leaf(data->directive);
      if (data->directive.text != spelling)
        report(data->directive.range,
               "Legacy debug width and directive disagree.");
      if (const auto* words =
              std::get_if<std::vector<syntax_ast::AstSyntax>>(&data->value)) {
        if (words->empty())
          report(data->range,
                 "Legacy debug hexadecimal list must be nonempty.");
        for (const auto& word : *words) {
          leaf(word);
          const auto value = base::parseIntegerMagnitude(word.text);
          if (!(word.text.starts_with("0x") || word.text.starts_with("0X")) ||
              !value || *value > limit)
            report(word.range,
                   "Legacy debug word must be an original unsigned hexadecimal "
                   "value within its width.");
        }
      } else {
        const auto& symbol = std::get<syntax_ast::AstSyntax>(data->value);
        leaf(symbol);
        if (data->kind == syntax_ast::LegacyDwarfDataKind::Byte ||
            !symbolic_name(symbol.text))
          report(symbol.range,
                 "Legacy debug symbol requires a single label and a wide data "
                 "kind.");
      }
    } else {
      const auto& header =
          std::get<syntax_ast::AstLegacyDwarfSectionHeader>(record.payload);
      for (const auto* value : {&header.directive, &header.name, &header.flags,
                                &header.type_prefix, &header.type_name})
        leaf(*value);
      if (header.directive.text != ".section" ||
          !symbolic_name(header.name.text) || header.flags.text != "\"\"" ||
          header.type_prefix.text != "@" || header.type_name.text != "progbits")
        report(header.range,
               "Legacy debug section header requires a name, empty flags and "
               "@progbits.");
    }
  }
  return errors;
}
}  // namespace

std::vector<ResolvedLegacyDwarfRecord> legacy_dwarf_records(
    const syntax_ast::AstModule& ast, const binding::SymbolTable& symbols) {
  std::vector<ResolvedLegacyDwarfRecord> result;
  for (const auto& item : ast.items) {
    if (const auto* record =
            std::get_if<syntax_ast::AstLegacyDwarfRecord>(&item))
      result.push_back({*record, symbols.moduleScope()});
    else if (const auto* function = std::get_if<syntax_ast::AstFunction>(&item))
      collect_body(
          result, function->body, symbols,
          symbols.functionScope(function->range).value_or(missing_scope()));
  }
  return result;
}
checker::CheckDiagnostics check_legacy_dwarf(
    const ResolvedModule& module, std::optional<checker::PtxVersion> version) {
  std::vector<Definition> definitions;
  for (const auto& f : module.functions)
    if (!f.is_prototype)
      definitions.push_back({f.declaration_scope, f.symbol_id, f.range});
  return check(module.legacy_dwarf_records, module.symbols, definitions,
               version, module.range);
}
checker::CheckDiagnostics check_legacy_dwarf(
    const syntax_ast::AstModule& ast, const binding::SymbolTable& symbols,
    std::span<const ResolvedLegacyDwarfRecord> records,
    std::optional<checker::PtxVersion> version) {
  std::vector<Definition> definitions;
  for (const auto& item : ast.items)
    if (const auto* f = std::get_if<syntax_ast::AstFunction>(&item);
        f && !f->is_prototype) {
      const auto scope = symbols.functionScope(f->range);
      if (scope && symbols.scope(*scope).owner)
        definitions.push_back({*scope, *symbols.scope(*scope).owner, f->range});
    }
  return check(records, symbols, definitions, version, ast.range);
}
}  // namespace ptx_frontend::resolved_ir::detail
