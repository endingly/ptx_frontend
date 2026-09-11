#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse and resolve one digit-prefix compact-group ordering. */
void expectDisjointDigitPrefixGroupsResolve(bool reverse) {
  const std::string declarations =
      reverse ? ".reg .u32 %r1<10>;\n.reg .u32 %r<10>;\n"
              : ".reg .u32 %r<10>;\n.reg .u32 %r1<10>;\n";
  PtxSyntaxParser parser(".entry digit_prefix() {\n" + declarations +
                         ".reg .u32 %out;\n"
                         "mov.u32 %out, %r0;\n"
                         "mov.u32 %out, %r19;\n"
                         "}\n");
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed.diagnostics.empty());
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);

  const auto function_scope =
      resolved->symbols.symbol(resolved->functions.front().symbol_id).owned_scope;
  ASSERT_TRUE(function_scope.has_value());
  constexpr std::array<std::pair<std::string_view, std::string_view>, 2>
      expected{{{"%r0", "%r"}, {"%r19", "%r1"}}};
  for (const auto [spelling, base] : expected) {
    const auto reference = std::find_if(
        resolved->symbols.references().begin(), resolved->symbols.references().end(),
        [spelling](const binding::SymbolReference& item) {
          return item.spelling == spelling;
        });
    ASSERT_NE(reference, resolved->symbols.references().end());
    ASSERT_TRUE(reference->target.has_value());
    const auto declaration =
        resolved->symbols.exactDeclaration(*function_scope, base, true);
    ASSERT_TRUE(declaration.has_value());
    EXPECT_EQ(reference->target->symbol, *declaration);
    EXPECT_EQ(reference->target->parameterized_index,
              spelling == "%r0" ? 0u : 9u);
  }
}

/** Bind and resolve both orders of two disjoint digit-prefix compact groups. */
TEST(ResolvedModule, ResolvesDisjointDigitPrefixParameterizedGroups) {
  expectDisjointDigitPrefixGroupsResolve(false);
  expectDisjointDigitPrefixGroupsResolve(true);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
