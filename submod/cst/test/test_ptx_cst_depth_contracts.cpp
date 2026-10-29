#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <variant>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend {
namespace {

/** Height budgets include roots and wrappers, but each sibling gets a full copy. */
TEST(PtxCstParser, RetainsExactConstantTreeHeightBoundary) {
  for (const std::size_t height : {128u, 129u}) {
    const std::string unary = std::string(height - 1, '+') + "1";
    const std::string named = "member=" + std::string(height - 2, '+') + "1";
    const std::string siblings = "{" + std::string(height - 2, '+') + "1," +
                                 std::string(height - 2, '+') + "2}";
    const std::string nested_named =
        "{member=" + std::string(height - 3, '+') + "1}";
    for (const auto& initializer : {unary, named, siblings, nested_named}) {
      SCOPED_TRACE(initializer);
      const std::string source = ".global .u32 x = " + initializer +
                                 ";\n.visible .entry neighbor(){ret;}";
      PtxCstParser parser(source);
      const auto result = parser.parseModule();
      ASSERT_TRUE(result.value.has_value());
      EXPECT_EQ(result.diagnostics.empty(), height == 128u);
      const auto& items = result->module()->items;
      ASSERT_FALSE(items.empty());
      ASSERT_TRUE(
          std::holds_alternative<syntax_cst::CstFunction>(items.back()));
      EXPECT_EQ(
          result->token(std::get<syntax_cst::CstFunction>(items.back()).name)
              .text,
          "neighbor");
      if (height == 128u) {
        ASSERT_EQ(items.size(), 2u);
        EXPECT_TRUE(std::holds_alternative<syntax_cst::CstVariableDeclaration>(
            items.front()));
      } else {
        ASSERT_FALSE(result.diagnostics.empty());
        EXPECT_NE(result.diagnostics.front().message.find("tree depth limit"),
                  std::string::npos);
      }
    }
  }
}

/** Lexical block depth is independent of tree height and excludes the function. */
TEST(PtxCstParser, RetainsExactNestedBlockDepthBoundary) {
  for (const std::size_t depth : {256u, 257u}) {
    const std::string source = ".entry k(){" + std::string(depth, '{') +
                               "ret;" + std::string(depth, '}') + "}";
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.diagnostics.empty(), depth == 256u);
    if (depth == 256u) {
      const auto& function =
          std::get<syntax_cst::CstFunction>(result->module()->items.front());
      const auto* body = &function.body;
      for (std::size_t level = 0; level < depth; ++level) {
        ASSERT_EQ(body->size(), 1u);
        const auto& block =
            *std::get<std::unique_ptr<syntax_cst::CstBlock>>(body->front());
        EXPECT_TRUE(block.right_brace.has_value());
        body = &block.body;
      }
      ASSERT_EQ(body->size(), 1u);
      EXPECT_TRUE(
          std::holds_alternative<syntax_cst::CstInstruction>(body->front()));
    } else {
      ASSERT_FALSE(result.diagnostics.empty());
      EXPECT_EQ(result.diagnostics.front().message,
                "nested block depth exceeds parser limit");
    }
  }
}

}  // namespace
}  // namespace ptx_frontend
