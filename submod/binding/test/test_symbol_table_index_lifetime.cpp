#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::binding {
namespace {

/** Assert that a table still resolves the expected compact and exact names. */
void expectIndexedLookups(const SymbolTable& table, ScopeId scope) {
  const auto global = table.lookup(table.moduleScope(), "global_511");
  ASSERT_TRUE(global.has_value());
  EXPECT_EQ(global->symbol.value, 511u);
  EXPECT_EQ(table.symbol(global->symbol).name, "global_511");

  const auto local = table.lookup(scope, "%local_511");
  ASSERT_TRUE(local.has_value());
  EXPECT_EQ(local->symbol.value, 1025u);
  EXPECT_EQ(table.symbol(local->symbol).name, "%local_511");

  const auto final_member = table.lookup(scope, "%huge4294967294");
  ASSERT_TRUE(final_member.has_value());
  EXPECT_EQ(final_member->symbol.value, 513u);
  EXPECT_EQ(table.symbol(final_member->symbol).name, "%huge");
  EXPECT_EQ(final_member->parameterized_index, 4294967294u);
  EXPECT_FALSE(table.lookup(scope, "%huge4294967295").has_value());
}

/** Vector growth, copy, and move preserve owned lookup-index keys and IDs. */
TEST(SymbolTableIndexLifetime, SurvivesGrowthCopyAndMove) {
  std::string source;
  for (uint32_t index = 0; index < 512; ++index)
    source += ".global .u32 global_" + std::to_string(index) + ";\n";
  source += ".entry indexed() {\n.reg .u32 %huge<4294967295>;\n";
  for (uint32_t index = 0; index < 512; ++index)
    source += ".reg .u32 %local_" + std::to_string(index) + ";\n";
  source += "ret;\n}\n";

  PtxSyntaxParser parser(source);
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed.diagnostics.empty());
  SymbolTable moved;
  SymbolTable move_assigned;
  ScopeId scope{};
  {
    const auto bound = bindSymbols(*parsed);
    ASSERT_TRUE(bound.diagnostics.empty());
    ASSERT_EQ(bound.table.symbols().size(), 1026u);

    const auto function =
        bound.table.lookup(bound.table.moduleScope(), "indexed");
    ASSERT_TRUE(function.has_value());
    const auto function_scope =
        bound.table.symbol(function->symbol).owned_scope;
    ASSERT_TRUE(function_scope.has_value());
    scope = *function_scope;
    expectIndexedLookups(bound.table, scope);

    SymbolTable copied(bound.table);
    SymbolTable copy_assigned;
    copy_assigned = bound.table;
    moved = std::move(copied);
    move_assigned = std::move(copy_assigned);
  }

  expectIndexedLookups(moved, scope);
  expectIndexedLookups(move_assigned, scope);
}

/** Group bases are not members, so digit-ending group bases can be disjoint. */
TEST(SymbolTableIndexLifetime, DoesNotTreatParameterizedBasesAsMembers) {
  for (const std::string_view declarations :
       {".reg .u32 %r<1>; .reg .u32 %r0<1>;",
        ".reg .u32 %r0<1>; .reg .u32 %r<1>;"}) {
    PtxSyntaxParser parser(std::string{".entry indexed() { "} +
                           std::string{declarations} + " ret; }");
    const auto parsed = parser.parseModule();
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed.diagnostics.empty());
    const auto bound = bindSymbols(*parsed);
    EXPECT_TRUE(bound.diagnostics.empty());
  }
}

/** Invalid zero-count groups have no members and therefore no overlap diagnostics. */
TEST(SymbolTableIndexLifetime, ZeroCountGroupsDoNotCreateOverlapCandidates) {
  for (const std::string_view declarations :
       {".reg .u32 %r<1>; .reg .u32 %r0<0>;",
        ".reg .u32 %r0<0>; .reg .u32 %r<1>;"}) {
    PtxSyntaxParser parser(std::string{".entry indexed() { "} +
                           std::string{declarations} + " ret; }");
    const auto parsed = parser.parseModule();
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed.diagnostics.empty());
    const auto bound = bindSymbols(*parsed);

    size_t invalid_count = 0;
    size_t duplicate_count = 0;
    for (const BindDiagnostic& diagnostic : bound.diagnostics) {
      invalid_count +=
          diagnostic.kind == BindDiagnosticKind::InvalidParameterizedCount;
      duplicate_count += diagnostic.kind == BindDiagnosticKind::DuplicateSymbol;
    }
    EXPECT_EQ(invalid_count, 1u);
    EXPECT_EQ(duplicate_count, 0u);
  }
}

/** Keep the sparse overlap index correct at the highest valid member number. */
TEST(SymbolTableIndexLifetime, DetectsHighestRepresentableParameterizedMember) {
  constexpr std::string_view source = R"ptx(
.entry indexed() {
  .reg .u32 %r<4294967295>;
  .reg .u32 %r4294967294;
  .reg .u32 %r4294967295;
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed.diagnostics.empty());
  const auto bound = bindSymbols(*parsed);
  ASSERT_EQ(bound.diagnostics.size(), 1u);
  EXPECT_EQ(bound.diagnostics.front().kind, BindDiagnosticKind::DuplicateSymbol);
  EXPECT_TRUE(bound.diagnostics.front().previous_range.has_value());
}

}  // namespace
}  // namespace ptx_frontend::binding
