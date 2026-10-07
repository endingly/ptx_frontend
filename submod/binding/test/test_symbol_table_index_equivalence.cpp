#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::binding {
namespace {

/** Independently retain the original vector-search contract as a test oracle. */
std::optional<SymbolLookup> linearLookup(const SymbolTable& table,
                                         ScopeId scope, std::string_view name) {
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
      const auto [end, error] =
          std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
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

/** Independently retain exact local declaration matching as an oracle. */
std::optional<SymbolId> linearExactDeclaration(const SymbolTable& table,
                                               ScopeId scope,
                                               std::string_view name,
                                               bool parameterized) {
  for (const Symbol& symbol : table.symbols()) {
    if (symbol.scope == scope && symbol.kind != SymbolKind::DebugFile &&
        symbol.kind != SymbolKind::DebugStringLabel && symbol.name == name &&
        symbol.parameterized_count.has_value() == parameterized) {
      return symbol.id;
    }
  }
  return std::nullopt;
}

/** Expand a deliberately small declaration into its actual finite name set. */
std::unordered_set<std::string> expandedNames(
    std::string_view name, std::optional<uint32_t> parameterized_count) {
  std::unordered_set<std::string> names;
  if (!parameterized_count) {
    names.emplace(name);
    return names;
  }
  for (uint32_t member = 0; member < *parameterized_count; ++member)
    names.emplace(std::string{name} + std::to_string(member));
  return names;
}

/** Compare two finite explicit name sets without production parsing helpers. */
bool expandedNamesOverlap(std::string_view left,
                          std::optional<uint32_t> left_count,
                          std::string_view right,
                          std::optional<uint32_t> right_count) {
  const auto left_names = expandedNames(left, left_count);
  const auto right_names = expandedNames(right, right_count);
  return std::any_of(
      left_names.begin(), left_names.end(),
      [&right_names](const auto& name) { return right_names.contains(name); });
}

/** Format a scalar or compact declaration for the parser-driven oracle. */
std::string declaration(std::string_view name,
                        std::optional<uint32_t> parameterized_count) {
  std::string result = ".reg .u32 ";
  result += name;
  if (parameterized_count)
    result += "<" + std::to_string(*parameterized_count) + ">";
  result += ";\n";
  return result;
}

/** Count duplicate-symbol diagnostics while ignoring intentional count errors. */
size_t duplicateDiagnosticCount(const SymbolBinding& binding) {
  return std::count_if(binding.diagnostics.begin(), binding.diagnostics.end(),
                       [](const auto& diagnostic) {
                         return diagnostic.kind ==
                                BindDiagnosticKind::DuplicateSymbol;
                       });
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
    source += ".reg .u32 zero<0>;\n";
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
  std::vector<const BindDiagnostic*> duplicate_diagnostics;
  size_t invalid_parameterized_count = 0;
  for (const auto& diagnostic : bound.diagnostics) {
    if (diagnostic.kind == BindDiagnosticKind::InvalidParameterizedCount) {
      ++invalid_parameterized_count;
      continue;
    }
    EXPECT_EQ(diagnostic.kind, BindDiagnosticKind::DuplicateSymbol);
    EXPECT_TRUE(diagnostic.previous_range.has_value());
    duplicate_diagnostics.push_back(&diagnostic);
  }
  EXPECT_EQ(invalid_parameterized_count, 4u);
  size_t overlap_count = 0;
  for (const auto& current : bound.table.symbols()) {
    for (const auto& previous : bound.table.symbols()) {
      if (previous.id.value >= current.id.value)
        break;
      if (previous.scope != current.scope ||
          previous.kind == SymbolKind::DebugFile ||
          previous.kind == SymbolKind::DebugStringLabel ||
          !expandedNamesOverlap(previous.name, previous.parameterized_count,
                                current.name, current.parameterized_count))
        continue;
      ASSERT_LT(overlap_count, duplicate_diagnostics.size());
      const BindDiagnostic& diagnostic =
          *duplicate_diagnostics[overlap_count++];
      EXPECT_EQ(diagnostic.range, current.declaration_range);
      EXPECT_EQ(diagnostic.previous_range, previous.declaration_range);
      break;
    }
  }
  EXPECT_EQ(overlap_count, duplicate_diagnostics.size());

  std::vector<std::string> queries{"missing", "metadata_only", "1",
                                   "child3",  "sibling",       "outer10"};
  for (const auto& symbol : bound.table.symbols()) {
    queries.push_back(symbol.name);
    if (symbol.parameterized_count) {
      for (std::string_view suffix :
           std::array{"0", "1", "2", "9", "10", "11", "12", "19", "20", "00",
                      "01", "4294967295", "4294967296"})
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
    for (const auto& query : queries) {
      for (const bool parameterized : {false, true}) {
        SCOPED_TRACE(query);
        SCOPED_TRACE(scope.id.value);
        SCOPED_TRACE(parameterized);
        const auto expected =
            linearExactDeclaration(bound.table, scope.id, query, parameterized);
        EXPECT_EQ(bound.table.exactDeclaration(scope.id, query, parameterized),
                  expected);
      }
    }
  }
}

/** Compare compact declaration diagnostics with many independently expanded sets. */
TEST(SymbolTableIndexEquivalence,
     MatchesExplicitFiniteOverlapSetsAcrossFormsAndOrders) {
  constexpr std::array<std::string_view, 8> bases{"%r", "%r1", "%r2", "%r9",
                                                  "%x", "%x0", "%x9", "%q"};
  constexpr std::array<uint32_t, 7> counts{0, 1, 2, 3, 9, 10, 12};

  for (size_t left_base = 0; left_base < bases.size(); ++left_base) {
    for (size_t right_base = 0; right_base < bases.size(); ++right_base) {
      if (left_base == right_base)
        continue;
      for (const uint32_t left_count : counts) {
        for (const uint32_t right_count : counts) {
          for (const bool reverse : {false, true}) {
            const std::string_view first =
                reverse ? bases[right_base] : bases[left_base];
            const uint32_t first_count = reverse ? right_count : left_count;
            const std::string_view second =
                reverse ? bases[left_base] : bases[right_base];
            const uint32_t second_count = reverse ? left_count : right_count;
            const std::string source =
                ".entry overlap() {\n" + declaration(first, first_count) +
                declaration(second, second_count) + "ret;\n}\n";
            PtxSyntaxParser parser(source);
            const auto parsed = parser.parseModule();
            ASSERT_TRUE(parsed.has_value()) << source;
            ASSERT_TRUE(parsed.diagnostics.empty()) << source;
            const auto bound = bindSymbols(*parsed);
            SCOPED_TRACE(source);
            EXPECT_EQ(
                duplicateDiagnosticCount(bound),
                expandedNamesOverlap(first, first_count, second, second_count));
          }
        }
      }
    }
  }

  for (const std::string_view base : bases) {
    for (const uint32_t count : counts) {
      for (const std::string suffix : {"", "0", "1", "9", "10", "19"}) {
        const std::string ordinary = std::string{base} + suffix;
        for (const bool reverse : {false, true}) {
          const std::string first = reverse ? ordinary : std::string{base};
          const std::optional<uint32_t> first_count =
              reverse ? std::nullopt : std::optional<uint32_t>{count};
          const std::string second = reverse ? std::string{base} : ordinary;
          const std::optional<uint32_t> second_count =
              reverse ? std::optional<uint32_t>{count} : std::nullopt;
          const std::string source =
              ".entry ordinary() {\n" + declaration(first, first_count) +
              declaration(second, second_count) + "ret;\n}\n";
          PtxSyntaxParser parser(source);
          const auto parsed = parser.parseModule();
          ASSERT_TRUE(parsed.has_value()) << source;
          ASSERT_TRUE(parsed.diagnostics.empty()) << source;
          const auto bound = bindSymbols(*parsed);
          SCOPED_TRACE(source);
          EXPECT_EQ(
              duplicateDiagnosticCount(bound),
              expandedNamesOverlap(first, first_count, second, second_count));
        }
      }
    }
  }
}

/** Ensure a disjoint early prefix group cannot hide a later true overlap. */
TEST(SymbolTableIndexEquivalence, IndexesOnlySemanticOverlapCandidates) {
  constexpr std::string_view source = R"ptx(
.entry candidates() {
  .reg .u32 %sink;
  .reg .u32 %r<10>;
  .reg .u32 %r19;
  .reg .u32 %r1<10>;
  mov.u32 %sink, %r18;
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed.diagnostics.empty());
  const auto bound = bindSymbols(*parsed);
  ASSERT_EQ(duplicateDiagnosticCount(bound), 1u);
  const BindDiagnostic& diagnostic = bound.diagnostics.front();
  ASSERT_TRUE(diagnostic.previous_range.has_value());

  const auto function =
      bound.table.lookup(bound.table.moduleScope(), "candidates");
  ASSERT_TRUE(function.has_value());
  const auto scope = bound.table.symbol(function->symbol).owned_scope;
  ASSERT_TRUE(scope.has_value());
  const auto ordinary = bound.table.exactDeclaration(*scope, "%r19", false);
  ASSERT_TRUE(ordinary.has_value());
  EXPECT_EQ(diagnostic.previous_range,
            bound.table.symbol(*ordinary).declaration_range);

  const auto group = bound.table.exactDeclaration(*scope, "%r1", true);
  ASSERT_TRUE(group.has_value());
  const auto member = bound.table.lookup(*scope, "%r18");
  ASSERT_TRUE(member.has_value());
  EXPECT_EQ(member->symbol, *group);
  EXPECT_EQ(member->parameterized_index, 8u);
  const auto reference = std::find_if(
      bound.table.references().begin(), bound.table.references().end(),
      [](const SymbolReference& item) { return item.spelling == "%r18"; });
  ASSERT_NE(reference, bound.table.references().end());
  ASSERT_TRUE(reference->target.has_value());
  EXPECT_EQ(reference->target->symbol, *group);
  EXPECT_EQ(reference->target->parameterized_index, 8u);
}

}  // namespace
}  // namespace ptx_frontend::binding
