#include "ptx_debug_sections.hpp"

#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

#include <ptx_frontend/base/ptx_identifier.hpp>
#include <ptx_frontend/base/ptx_integer.hpp>

#include <limits>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>

namespace ptx_frontend::resolved_ir::detail {
namespace {
/** Compare source coordinates only for containment, never linked byte addresses. */
bool before(SourcePos a, SourcePos b) {
  return std::tie(a.line, a.column) <= std::tie(b.line, b.column);
}
/** Unknown ranges carry no positive source fact; partial or reversed facts are invalid. */
bool valid_range(SourceRange range) {
  return range == SourceRange{} ||
         (range.start.line > 0 && range.start.column > 0 &&
          range.end.line > 0 && range.end.column > 0 &&
          range.start != range.end && before(range.start, range.end));
}
/** Preserve containment whenever the owning record carries a known extent. */
bool contains(SourceRange owner, SourceRange use) {
  return valid_range(owner) && valid_range(use) &&
         (owner == SourceRange{} ||
          (use != SourceRange{} && before(owner.start, use.start) &&
           before(use.end, owner.end)));
}
/** A section base may be dotted; ordinary labels use the existing identifier grammar. */
bool symbolic_name(std::string_view name) {
  return base::isIdentifier(name) ||
         (name.starts_with('.') && base::isIdentifier(name.substr(1)));
}
/** Keep magnitude and sign separate so UINT64_MAX and negative 2^63 never overflow. */
struct IntegerValue {
  /** Unsigned original magnitude, never a narrowed storage word. */
  uint64_t magnitude;
  /** Mathematical negativity; signed zero is false. */
  bool negative;
};
/** Decode only an already-tokenized integer leaf, without lexing an expression. */
std::optional<IntegerValue> integer_value(
    const syntax_ast::AstSectionInteger& leaf) {
  if (leaf.sign && leaf.sign->text != "+" && leaf.sign->text != "-")
    return std::nullopt;
  const auto magnitude = base::parseIntegerMagnitude(leaf.magnitude.text);
  if (!magnitude)
    return std::nullopt;
  return IntegerValue{*magnitude,
                      leaf.sign && leaf.sign->text == "-" && *magnitude != 0};
}
}  // namespace

std::vector<ResolvedDebugSection> debug_sections(
    const syntax_ast::AstModule& ast) {
  std::vector<ResolvedDebugSection> sections;
  for (const auto& item : ast.items)
    if (const auto* section =
            std::get_if<syntax_ast::AstSectionDirective>(&item))
      sections.push_back(*section);
  return sections;
}

checker::CheckDiagnostics check_debug_sections(
    std::span<const ResolvedDebugSection> sections,
    std::optional<checker::PtxVersion> version, SourceRange module_range) {
  checker::CheckDiagnostics diagnostics;
  const auto report = [&](SourceRange range, std::string message,
                          checker::CheckDiagnosticKind kind =
                              checker::CheckDiagnosticKind::RuleViolation) {
    diagnostics.push_back(
        {.kind = kind, .range = range, .message = std::move(message)});
  };
  const auto gate = [&](checker::PtxVersion minimum, SourceRange range,
                        std::string_view feature) {
    if (version && *version < minimum)
      report(range,
             "Debug section " + std::string(feature) +
                 " is unavailable in this PTX version.",
             checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  };
  /** Same-name fragments share a logical section; actual labels remain unique there. */
  std::unordered_map<std::string, std::vector<std::string>> definitions;
  std::set<std::string> section_names;
  std::set<std::pair<std::string, std::string>> labels;
  for (const auto& section : sections) {
    if (section_names.insert(section.name.text).second)
      definitions[section.name.text].push_back(section.name.text);
    for (const auto& entry : section.payload)
      if (const auto* label =
              std::get_if<syntax_ast::AstSectionLabelDefinition>(&entry)) {
        definitions[label->name.text].push_back(section.name.text);
        if (!labels.emplace(section.name.text, label->name.text).second)
          report(label->range, "Duplicate label in one logical debug section.");
      }
  }
  SourceRange previous_section;
  for (const auto& section : sections) {
    gate({2, 0}, section.range, "directive");
    if (!valid_range(section.range) || (section.range != SourceRange{} &&
                                        !contains(module_range, section.range)))
      report(module_range != SourceRange{} ? module_range : section.range,
             "Debug section fragment range lies outside its owning module.");
    if (!symbolic_name(section.name.text) ||
        !contains(section.range, section.name.range) ||
        (previous_section != SourceRange{} && section.range != SourceRange{} &&
         !before(previous_section.end, section.range.start)))
      report(section.range,
             "Debug section name, range or fragment order is invalid.");
    if (section.range != SourceRange{})
      previous_section = section.range;
    SourceRange previous = section.name.range;
    for (const auto& entry : section.payload) {
      const auto range =
          std::visit([](const auto& value) { return value.range; }, entry);
      if (!contains(section.range, range) ||
          (previous != SourceRange{} && range != SourceRange{} &&
           !before(previous.end, range.start)))
        report(range, "Debug section entry range or order is inconsistent.");
      if (range != SourceRange{})
        previous = range;
      if (const auto* label =
              std::get_if<syntax_ast::AstSectionLabelDefinition>(&entry)) {
        gate({7, 2}, label->range, "label definition");
        if (!base::isIdentifier(label->name.text) ||
            !contains(label->range, label->name.range))
          report(label->range, "Debug section label definition is malformed.");
        continue;
      }
      const auto& data = std::get<syntax_ast::AstSectionData>(entry);
      unsigned bits = 0;
      switch (data.width) {
        case syntax_ast::AstSectionWidth::B8:
          bits = 8;
          break;
        case syntax_ast::AstSectionWidth::B16:
          bits = 16;
          break;
        case syntax_ast::AstSectionWidth::B32:
          bits = 32;
          break;
        case syntax_ast::AstSectionWidth::B64:
          bits = 64;
          break;
      }
      if (!bits || data.type.text != ".b" + std::to_string(bits) ||
          !contains(data.range, data.type.range)) {
        report(data.range,
               "Debug section data width or type spelling is invalid.");
        continue;
      }
      if (bits == 16)
        gate({6, 0}, data.type.range, "B16 data");
      /** Source-backed leaves must retain sign/magnitude order inside their parent. */
      const auto leaf_valid = [&](const syntax_ast::AstSectionInteger& leaf) {
        return contains(data.range, leaf.range) &&
               contains(leaf.range, leaf.magnitude.range) &&
               (leaf.range == SourceRange{} ||
                before(data.type.range.end, leaf.range.start)) &&
               (!leaf.sign ||
                (contains(leaf.range, leaf.sign->range) &&
                 (leaf.sign->range == SourceRange{} ||
                  before(leaf.sign->range.end, leaf.magnitude.range.start))));
      };
      if (const auto* values =
              std::get_if<std::vector<syntax_ast::AstSectionInteger>>(
                  &data.value)) {
        if (values->empty())
          report(data.range, "Debug section integer list must not be empty.");
        SourceRange last = data.type.range;
        for (const auto& leaf : *values) {
          const auto value = integer_value(leaf);
          if (!value || !leaf_valid(leaf) ||
              (last != SourceRange{} && leaf.range != SourceRange{} &&
               !before(last.end, leaf.range.start))) {
            report(leaf.range, "Debug section integer leaf is malformed.");
            continue;
          }
          if (leaf.range != SourceRange{})
            last = leaf.range;
          const uint64_t maximum = value->negative ? uint64_t{1} << (bits - 1)
                                   : bits == 64
                                       ? std::numeric_limits<uint64_t>::max()
                                       : (uint64_t{1} << bits) - 1;
          if (value->magnitude > maximum)
            report(leaf.range,
                   "Debug section integer is outside its original mathematical "
                   "range.",
                   checker::CheckDiagnosticKind::ImmediateValueMismatch);
          if (value->negative)
            gate({7, 5}, leaf.range, "negative value");
        }
        continue;
      }
      if (bits != 32 && bits != 64) {
        report(data.range, "Debug section symbolic data requires B32 or B64.");
        continue;
      }
      const auto symbol_valid = [&](const syntax_ast::AstSyntax& symbol) {
        return symbolic_name(symbol.text) &&
               contains(data.range, symbol.range) &&
               (symbol.range == SourceRange{} ||
                before(data.type.range.end, symbol.range.start));
      };
      if (const auto* symbol =
              std::get_if<syntax_ast::AstSectionSymbol>(&data.value)) {
        if (!symbol_valid(symbol->name))
          report(data.range, "Debug section symbolic address is malformed.");
      } else if (const auto* addend =
                     std::get_if<syntax_ast::AstSectionAddend>(&data.value)) {
        gate({3, 2}, data.range, "label addend");
        const auto value = integer_value(addend->value);
        if (!value || !symbol_valid(addend->name) ||
            !leaf_valid(addend->value) ||
            (addend->operation.text != "+" && addend->operation.text != "-") ||
            !contains(data.range, addend->operation.range) ||
            (addend->operation.range != SourceRange{} &&
             (!before(addend->name.range.end, addend->operation.range.start) ||
              !before(addend->operation.range.end,
                      addend->value.range.start)))) {
          report(data.range, "Debug section label addend is malformed.");
          continue;
        }
        const bool negative =
            value->magnitude != 0 &&
            (value->negative != (addend->operation.text == "-"));
        const uint64_t limit = (uint64_t{1} << (bits - 1)) - (negative ? 0 : 1);
        if (value->magnitude > limit)
          report(addend->value.range,
                 "Debug section label addend exceeds the signed byte-offset "
                 "range.",
                 checker::CheckDiagnosticKind::ImmediateValueMismatch);
        if (negative)
          gate({7, 5}, addend->value.range, "negative value");
      } else {
        gate({7, 5}, data.range, "label difference");
        const auto& difference =
            std::get<syntax_ast::AstSectionDifference>(data.value);
        if (!symbol_valid(difference.left) || !symbol_valid(difference.right) ||
            difference.operation.text != "-" ||
            !contains(data.range, difference.operation.range) ||
            (difference.operation.range != SourceRange{} &&
             (!before(difference.left.range.end,
                      difference.operation.range.start) ||
              !before(difference.operation.range.end,
                      difference.right.range.start))))
          report(data.range, "Debug section label difference is malformed.");
        const auto left = definitions.find(difference.left.text),
                   right = definitions.find(difference.right.text);
        if (left == definitions.end() || right == definitions.end() ||
            left->second.size() != 1 || right->second.size() != 1 ||
            left->second.front() != right->second.front())
          report(data.range,
                 "Debug section label difference requires unique known labels "
                 "in one logical section.");
      }
    }
  }
  return diagnostics;
}

checker::CheckDiagnostics check_debug_section_references(
    std::span<const ResolvedDebugSection> sections,
    const binding::SymbolTable& symbols) {
  std::set<std::string> names;
  for (const auto& section : sections)
    if (section.name.text == ".debug_str") {
      names.insert(section.name.text);
      for (const auto& entry : section.payload)
        if (const auto* label =
                std::get_if<syntax_ast::AstSectionLabelDefinition>(&entry))
          names.insert(label->name.text);
    }
  checker::CheckDiagnostics diagnostics;
  for (const auto& ref : symbols.references()) {
    if (ref.kind != binding::ReferenceKind::DebugFunctionName)
      continue;
    const binding::Symbol* symbol = nullptr;
    if (ref.target && ref.target->symbol.value < symbols.symbols().size())
      symbol = &symbols.symbol(ref.target->symbol);
    if (!symbol || ref.target->parameterized_index ||
        ref.classification !=
            binding::ReferenceClassification::DeclaredSymbol ||
        symbol->kind != binding::SymbolKind::DebugStringLabel ||
        symbol->name != ref.spelling || !names.contains(symbol->name))
      diagnostics.push_back(
          {.kind = checker::CheckDiagnosticKind::RuleViolation,
           .range = ref.range,
           .message = "Debug function-name reference has no current typed "
                      ".debug_str label."});
  }
  return diagnostics;
}
}  // namespace ptx_frontend::resolved_ir::detail
