#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::binding {
namespace {

/** Independently retain the original vector-search contract as a test oracle. */
std::optional<SymbolLookup> linearLookup(const SymbolTable& table, ScopeId scope,
                                        std::string_view name) {
  for (;;) {
    for (const auto& symbol : table.symbols()) {
      if (symbol.scope == scope && symbol.kind != SymbolKind::DebugFile &&
          symbol.kind != SymbolKind::DebugStringLabel &&
          !symbol.parameterized_count && symbol.name == name)
        return SymbolLookup{symbol.id, std::nullopt};
    }
    for (const auto& symbol : table.symbols()) {
      if (symbol.scope != scope || !symbol.parameterized_count ||
          !name.starts_with(symbol.name) || name.size() == symbol.name.size())
        continue;
      const auto suffix = name.substr(symbol.name.size());
      if (suffix.size() > 1 && suffix.front() == '0')
        continue;
      bool decimal = true;
      for (char character : suffix)
        decimal = decimal && character >= '0' && character <= '9';
      uint32_t index = 0;
      const auto [end, error] = std::from_chars(
          suffix.data(), suffix.data() + suffix.size(), index);
      if (decimal && error == std::errc{} &&
          end == suffix.data() + suffix.size() &&
          index < *symbol.parameterized_count)
        return SymbolLookup{symbol.id, index};
    }
    if (!table.scope(scope).parent)
      return std::nullopt;
    scope = *table.scope(scope).parent;
  }
}

/** Test represented membership without using the production overlap helpers. */
bool memberOf(std::string_view base, uint32_t count, std::string_view name) {
  if (!name.starts_with(base) || name.size() == base.size())
    return false;
  const auto suffix = name.substr(base.size());
  if (suffix.size() > 1 && suffix.front() == '0')
    return false;
  for (char character : suffix)
    if (character < '0' || character > '9')
      return false;
  uint32_t index = 0;
  const auto [end, error] = std::from_chars(
      suffix.data(), suffix.data() + suffix.size(), index);
  return error == std::errc{} && end == suffix.data() + suffix.size() &&
         index < count;
}

/** Retain the existing overlap contract, including group-base comparisons. */
bool overlaps(const Symbol& previous, const Symbol& current) {
  if (previous.parameterized_count &&
      memberOf(previous.name, *previous.parameterized_count, current.name))
    return true;
  if (current.parameterized_count &&
      memberOf(current.name, *current.parameterized_count, previous.name))
    return true;
  return previous.parameterized_count && current.parameterized_count &&
         (memberOf(previous.name, *previous.parameterized_count,
                   current.name + "0") ||
          memberOf(current.name, *current.parameterized_count,
                   previous.name + "0"));
}

/** Check indexes against lexical vector search, including diagnosed overlaps. */
TEST(SymbolTableIndexEquivalence, MatchesLinearLookupAcrossMixedScopes) {
  std::string source = R"ptx(
.file 1 "oracle.ptx"
.section .debug_str { metadata_only: .b8 0; };
.global .u32 outer<12>;
.global .u32 outer10;
.global .u32 child3;
.global .u32 metadata_only;
)ptx";
  for (int function = 0; function < 4; ++function) {
    source += ".entry k" + std::to_string(function) + "() {\n";
    source += ".reg .u32 child<10>;\n.reg .u32 outer;\n";
    for (int group = 0; group < 16; ++group) {
      const std::string base = "%r" + std::to_string(group) + "_";
      source += ".reg .u32 " + base + "<12>;\n";
      source += ".reg .u32 " + base + "1<3>;\n";
      source += ".reg .u32 " + base + "10;\n";
      source += ".reg .u32 " + base + ";\n";
    }
    source += "{ .reg .u32 child3; .reg .u32 sibling; }\n";
    source += "{ .reg .u32 child<2>; .reg .u32 outer10; }\nret;\n}\n";
  }
  PtxSyntaxParser parser(source);
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed.diagnostics.empty());
  const auto bound = bindSymbols(*parsed);
  // Overlapping declarations are intentionally retained to test first-ID wins.
  ASSERT_FALSE(bound.diagnostics.empty());
  for (const auto& diagnostic : bound.diagnostics) {
    EXPECT_EQ(diagnostic.kind, BindDiagnosticKind::DuplicateSymbol);
    EXPECT_TRUE(diagnostic.previous_range.has_value());
  }
  size_t overlap_count = 0;
  for (const auto& current : bound.table.symbols()) {
    for (const auto& previous : bound.table.symbols()) {
      if (previous.id.value >= current.id.value)
        break;
      if (previous.scope != current.scope ||
          previous.kind == SymbolKind::DebugFile ||
          previous.kind == SymbolKind::DebugStringLabel ||
          !overlaps(previous, current))
        continue;
      ASSERT_LT(overlap_count, bound.diagnostics.size());
      const auto& diagnostic = bound.diagnostics[overlap_count++];
      EXPECT_EQ(diagnostic.range, current.declaration_range);
      EXPECT_EQ(diagnostic.previous_range, previous.declaration_range);
      break;
    }
  }
  EXPECT_EQ(overlap_count, bound.diagnostics.size());

  std::vector<std::string> queries{"missing", "metadata_only", "1", "child3",
                                   "sibling", "outer10"};
  for (const auto& symbol : bound.table.symbols()) {
    queries.push_back(symbol.name);
    if (symbol.parameterized_count) {
      for (std::string_view suffix :
           std::array{"0", "1", "2", "9", "10", "11", "12", "19", "20",
                      "00", "01", "4294967295", "4294967296"})
        queries.push_back(symbol.name + std::string{suffix});
    }
  }
  for (const auto& scope : bound.table.scopes()) {
    for (const auto& query : queries) {
      SCOPED_TRACE(query);
      SCOPED_TRACE(scope.id.value);
      const auto expected = linearLookup(bound.table, scope.id, query);
      const auto actual = bound.table.lookup(scope.id, query);
      ASSERT_EQ(actual.has_value(), expected.has_value());
      if (expected) {
        EXPECT_EQ(actual->symbol, expected->symbol);
        EXPECT_EQ(actual->parameterized_index, expected->parameterized_index);
      }
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::binding
