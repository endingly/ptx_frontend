#include <gtest/gtest.h>

#include <string>
#include <variant>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend {
namespace {

/** A dotted-identifier module form remains a neighbor of an unknown directive. */
TEST(PtxCstParser, RecoveryPreservesAliasAfterUnknownDirective) {
  const std::string source =
      ".bogus\n.alias a,b;\n.visible .entry k(){ret;}\n// eof trivia\n";
  PtxCstParser parser(source);
  const auto result = parser.parseModule();
  ASSERT_TRUE(result.value.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1u);
  EXPECT_EQ(result->sourceText(), source);
  const auto& items = result->module()->items;
  ASSERT_EQ(items.size(), 3u);
  const auto& error = std::get<syntax_cst::CstRecoveryNode>(items[0]);
  EXPECT_EQ(error.kind, syntax_cst::CstRecoveryKind::Error);
  ASSERT_TRUE(error.token_range);
  EXPECT_EQ(error.token_range->last, error.token_range->first + 1);
  EXPECT_EQ(result->token(error.token_range->first).text, ".bogus");
  const auto& alias = std::get<syntax_cst::CstAliasDirective>(items[1]);
  EXPECT_EQ(result->token(alias.directive).text, ".alias");
  EXPECT_EQ(result->token(alias.alias).text, "a");
  EXPECT_EQ(result->token(alias.aliasee).text, "b");
  EXPECT_EQ(error.token_range->last, alias.token_range.first);
  const auto& function = std::get<syntax_cst::CstFunction>(items[2]);
  EXPECT_EQ(result->token(function.name).text, "k");
  ASSERT_EQ(function.body.size(), 1u);
  EXPECT_TRUE(
      std::holds_alternative<syntax_cst::CstInstruction>(function.body[0]));
  EXPECT_EQ(alias.token_range.last, function.token_range.first);
}

/** A module source start need not terminate an opaque section payload. */
TEST(PtxCstParser, AliasSpellingRemainsOpaqueInsideSection) {
  const std::string source =
      ".section .debug_info { .alias a,b; }\n.entry k(){ret;}";
  PtxCstParser parser(source);
  const auto result = parser.parseModule();
  ASSERT_TRUE(result.value.has_value());
  EXPECT_TRUE(result.diagnostics.empty());
  ASSERT_EQ(result->module()->items.size(), 2u);
  const auto& section = std::get<syntax_cst::CstSectionDirective>(
      result->module()->items.front());
  ASSERT_FALSE(section.payload.empty());
  EXPECT_EQ(result->token(section.payload.front()).text, ".alias");
  EXPECT_EQ(result->sourceText(), source);
}

/** Missing body terminators retain each context's message and zero-width node. */
TEST(PtxCstParser, SharedBodyRecoveryRetainsNestedAndFunctionTermination) {
  const std::string source = ".entry a(){ { ret;\n.visible .entry b(){ret;}";
  PtxCstParser parser(source);
  const auto result = parser.parseModule();
  ASSERT_TRUE(result.value.has_value());
  ASSERT_EQ(result.diagnostics.size(), 2u);
  EXPECT_EQ(result.diagnostics[0].message,
            "expected '}' at end of nested block");
  EXPECT_EQ(result.diagnostics[1].message,
            "expected '}' at end of function body");
  const auto& items = result->module()->items;
  ASSERT_EQ(items.size(), 2u);
  const auto& first = std::get<syntax_cst::CstFunction>(items[0]);
  const auto& second = std::get<syntax_cst::CstFunction>(items[1]);
  ASSERT_FALSE(first.right_brace);
  ASSERT_EQ(first.body.size(), 2u);
  const auto& block =
      *std::get<std::unique_ptr<syntax_cst::CstBlock>>(first.body[0]);
  ASSERT_FALSE(block.right_brace);
  ASSERT_EQ(block.body.size(), 2u);
  for (const auto* node :
       {&std::get<syntax_cst::CstRecoveryNode>(block.body.back()),
        &std::get<syntax_cst::CstRecoveryNode>(first.body.back())}) {
    EXPECT_EQ(node->kind, syntax_cst::CstRecoveryKind::Inserted);
    EXPECT_EQ(node->expected_kind, TokenKind::RBrace);
    EXPECT_FALSE(node->token_range);
    EXPECT_EQ(node->range.start, node->range.end);
    EXPECT_EQ(node->range.start,
              result->token(second.token_range.first).range.start);
  }
  EXPECT_EQ(first.token_range.last, second.token_range.first);
  EXPECT_EQ(block.token_range.last, second.token_range.first);
  EXPECT_EQ(result->sourceText(), source);
}

}  // namespace
}  // namespace ptx_frontend
