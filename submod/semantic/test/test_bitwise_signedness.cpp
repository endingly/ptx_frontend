#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::declaration_semantics {
namespace {

/** Expected typed result of a source-level integer constant expression. */
struct BitwiseCase {
  /** Source expression, parsed through an ordinary global initializer. */
  std::string_view expression;
  /** Full two's-complement 64-bit result, before storage conversion. */
  uint64_t bits;
  /** Whether usual arithmetic conversions produce an unsigned result. */
  bool is_unsigned;
};

/** Binary bitwise operators preserve the usual-conversion type, unlike `~`. */
TEST(BitwiseSignedness, UsesUsualConversionsForBinaryOperators) {
  constexpr auto all_bits = UINT64_MAX;
  constexpr std::array cases{
      BitwiseCase{"-1 & -1", all_bits, false},
      BitwiseCase{"-1 | 0", all_bits, false},
      BitwiseCase{"-1 ^ 0", all_bits, false},
      BitwiseCase{"-1 & -1U", all_bits, true},
      BitwiseCase{"0U | -1", all_bits, true},
      BitwiseCase{"-1 ^ 0U", all_bits, true},
      BitwiseCase{"(-1 & -1) < 0", 1, false},
      BitwiseCase{"(-1 | 0) < 0", 1, false},
      BitwiseCase{"(-1 ^ 0) < 0", 1, false},
      BitwiseCase{"(-4 | 0) >> 1", all_bits - 1, false},
      BitwiseCase{"((.s64)(-4 | 0)) >> 1", all_bits - 1, false},
      BitwiseCase{"(-1 & -1U) < 0", 0, false},
      BitwiseCase{"~0", all_bits, true},
      BitwiseCase{"(~0) < 0", 0, false},
      BitwiseCase{"(-1 + 0) < 0", 1, false},
  };
  for (const auto& test : cases) {
    SCOPED_TRACE(test.expression);
    PtxSyntaxParser parser(".global .u64 value = (" +
                           std::string(test.expression) + ");");
    const auto module = parser.parseModule();
    ASSERT_TRUE(module.has_value());
    ASSERT_TRUE(module.diagnostics.empty());
    ASSERT_EQ(module->items.size(), 1u);
    const auto* declaration =
        std::get_if<syntax_ast::AstVariableDeclaration>(&module->items.front());
    ASSERT_NE(declaration, nullptr);
    ASSERT_EQ(declaration->declarators.size(), 1u);
    const auto& initializer = declaration->declarators.front().initializer;
    ASSERT_TRUE(initializer.has_value());
    const auto* expression =
        std::get_if<syntax_ast::AstConstantExpression>(&initializer->value);
    ASSERT_NE(expression, nullptr);
    const auto value = constantIntegerValue(*expression);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->bits, test.bits);
    EXPECT_EQ(value->is_unsigned, test.is_unsigned);
  }
}

}  // namespace
}  // namespace ptx_frontend::declaration_semantics
