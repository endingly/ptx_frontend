#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <tuple>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend {
namespace {
using namespace syntax_cst;

/** Expect-style version failures retain their literal as the recovery error. */
TEST(PtxCstParser, RetainsRejectedVersionLiteral) {
  const auto parsed = PtxCstParser(".version 93 .target sm_80").parseModule();
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed.diagnostics.size(), 1u);
  EXPECT_EQ(parsed.diagnostics.front().message, "expected PTX version");
  EXPECT_EQ(parsed.diagnostics.front().expected_kind, TokenKind::F64);
  const auto& items = parsed->module()->items;
  ASSERT_EQ(items.size(), 3u);
  const auto& skipped = std::get<CstRecoveryNode>(items[0]);
  const auto& error = std::get<CstRecoveryNode>(items[1]);
  EXPECT_EQ(skipped.kind, CstRecoveryKind::Skipped);
  ASSERT_TRUE(skipped.token_range);
  EXPECT_EQ(skipped.token_range->first, 0u);
  EXPECT_EQ(skipped.token_range->last, 1u);
  EXPECT_EQ(error.kind, CstRecoveryKind::Error);
  ASSERT_TRUE(error.token_range);
  EXPECT_EQ(error.token_range->first, 1u);
  EXPECT_EQ(error.token_range->last, 2u);
  EXPECT_EQ(error.range, parsed->token(1).range);
  EXPECT_TRUE(std::holds_alternative<CstModuleDirective>(items[2]));
}

/** Parameter and section name validation consumes exactly one rejected token. */
TEST(PtxCstParser, ConsumesRejectedParameterAndSectionPrefixes) {
  // Each tuple records the original recovery cursor, not the diagnostic anchor.
  constexpr std::array cases{
      std::tuple{
          std::string_view{".entry k({ .reg .u32 r; ret;} .entry good(){ret;}"},
          std::string_view{"expected '.reg' or '.param' parameter"}, 4u},
      std::tuple{std::string_view{
                     ".section ( .debug_info { .b8 0; }; .entry good(){ret;}"},
                 std::string_view{"expected section name"}, 2u}};
  for (const auto& [source, message, cursor] : cases) {
    SCOPED_TRACE(source);
    const auto parsed = PtxCstParser(source).parseModule();
    ASSERT_TRUE(parsed.has_value());
    ASSERT_FALSE(parsed.diagnostics.empty());
    EXPECT_EQ(parsed.diagnostics.front().message, message);
    EXPECT_FALSE(parsed.diagnostics.front().expected_kind);
    const auto& items = parsed->module()->items;
    ASSERT_FALSE(items.empty());
    const auto& skipped = std::get<CstRecoveryNode>(items.front());
    ASSERT_TRUE(skipped.token_range);
    EXPECT_EQ(skipped.token_range->first, 0u);
    EXPECT_EQ(skipped.token_range->last, cursor);
    EXPECT_EQ(parsed->sourceRange(*skipped.token_range), skipped.range);
    EXPECT_TRUE(std::holds_alternative<CstFunction>(items.back()));
    EXPECT_EQ(parsed->sourceText(), source);
  }
}

/** Address, named-index and function-name errors keep distinct public contexts. */
TEST(PtxCstParser, DistinguishesMalformedAddressAndHeaderContexts) {
  constexpr std::array cases{
      std::tuple{std::string_view{"mov.u32 r, [% rd];"},
                 std::string_view{"expected address base"}, TokenKind::Ident},
      std::tuple{std::string_view{"mov.u32 r, A[% rd];"},
                 std::string_view{"expected constant expression"},
                 TokenKind::Eof},
      std::tuple{std::string_view{"mov.u32 r, A[i dx];"},
                 std::string_view{"expected ']' after named-array index"},
                 TokenKind::RBracket}};
  for (const auto& [source, message, expected] : cases) {
    SCOPED_TRACE(source);
    const auto parsed = PtxCstParser(source).parseInstruction();
    ASSERT_FALSE(parsed.has_value());
    ASSERT_EQ(parsed.diagnostics.size(), 1u);
    EXPECT_EQ(parsed.diagnostics.front().message, message);
    if (expected == TokenKind::Eof)
      EXPECT_FALSE(parsed.diagnostics.front().expected_kind);
    else
      EXPECT_EQ(parsed.diagnostics.front().expected_kind, expected);
  }
  for (const auto source :
       {".func+ caller(){}", ".func] caller(){}", ".func, caller(){}"}) {
    const auto parsed = PtxCstParser(source).parseModule();
    ASSERT_TRUE(parsed.has_value());
    ASSERT_FALSE(parsed.diagnostics.empty());
    EXPECT_EQ(parsed.diagnostics.front().message, "expected function name");
    EXPECT_EQ(parsed.diagnostics.front().expected_kind, TokenKind::Ident);
  }
}

/** Completed numeric and bracket prefixes validate before unrelated bad suffixes. */
TEST(PtxCstParser, ValidatesCompletedOperandsBeforeTrailingSyntax) {
  constexpr std::array cases{
      std::tuple{std::string_view{"all.uni (%result)b;"},
                 std::string_view{
                     "registers cannot participate in constant expressions"},
                 10u},
      std::tuple{std::string_view{"add.u32 r, +%r"},
                 std::string_view{
                     "registers cannot participate in constant expressions"},
                 13u},
      std::tuple{
          std::string_view{"mov.u32 r, [base+4,];"},
          std::string_view{"fabric handle endpoint cannot have an offset"},
          19u}};
  for (const auto& [source, message, column] : cases) {
    SCOPED_TRACE(source);
    const auto parsed = PtxCstParser(source).parseInstruction();
    ASSERT_FALSE(parsed.has_value());
    ASSERT_EQ(parsed.diagnostics.size(), 1u);
    EXPECT_EQ(parsed.diagnostics.front().message, message);
    EXPECT_EQ(parsed.diagnostics.front().range.start.column, column);
    EXPECT_FALSE(parsed.diagnostics.front().expected_kind);
  }
  // An incomplete expression must still report syntax, not numeric validation.
  for (const auto source :
       {"mov.u32 r, +%r+;", "mov.u32 r, (+%r;", "mov.u32 r, +%r?1:;"}) {
    const auto parsed = PtxCstParser(source).parseInstruction();
    ASSERT_FALSE(parsed.has_value());
    ASSERT_FALSE(parsed.diagnostics.empty());
    EXPECT_NE(parsed.diagnostics.front().message,
              "registers cannot participate in constant expressions");
  }
}

/** Directive field labels and delimiters outrank generic expression contexts. */
TEST(PtxCstParser, PreservesDirectiveFieldDiagnosticPriority) {
  constexpr std::array cases{
      std::tuple{std::string_view{".file nope \"f\""},
                 std::string_view{"expected file index"}, TokenKind::Eof},
      std::tuple{std::string_view{".file 0 nope"},
                 std::string_view{"expected file name"}, TokenKind::String},
      std::tuple{std::string_view{".file 0 \"f\", nope"},
                 std::string_view{"expected file timestamp"},
                 TokenKind::Decimal},
      std::tuple{std::string_view{".file 0 \"f\", 12 nope"},
                 std::string_view{"expected comma before file size"},
                 TokenKind::Comma},
      std::tuple{std::string_view{".file 0 \"f\", 12, nope"},
                 std::string_view{"expected file size"}, TokenKind::Decimal},
      std::tuple{std::string_view{".file 1.2 \"f\""},
                 std::string_view{"expected file index"}, TokenKind::Eof},
      std::tuple{std::string_view{".file 0 \"f\", 1.2, 3"},
                 std::string_view{"expected file timestamp"},
                 TokenKind::Decimal},
      std::tuple{std::string_view{".file 0 \"f\", 1, 1.2"},
                 std::string_view{"expected file size"}, TokenKind::Decimal},
      std::tuple{std::string_view{".address_size 1.2"},
                 std::string_view{"expected address size"}, TokenKind::Decimal},
      std::tuple{std::string_view{".entry k() .maxnreg ,32 {}"},
                 std::string_view{"expected kernel resource value"},
                 TokenKind::Decimal},
      std::tuple{std::string_view{".entry k(.param .u32 a[1 n]){}"},
                 std::string_view{"expected ']'"}, TokenKind::RBracket}};
  for (const auto& [source, message, expected] : cases) {
    SCOPED_TRACE(source);
    const auto parsed = PtxCstParser(source).parseModule();
    ASSERT_TRUE(parsed.has_value());
    ASSERT_FALSE(parsed.diagnostics.empty());
    EXPECT_EQ(parsed.diagnostics.front().message, message);
    if (expected == TokenKind::Eof)
      EXPECT_FALSE(parsed.diagnostics.front().expected_kind);
    else
      EXPECT_EQ(parsed.diagnostics.front().expected_kind, expected);
  }
}

}  // namespace
}  // namespace ptx_frontend
