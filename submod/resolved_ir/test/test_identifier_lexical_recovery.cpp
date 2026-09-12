#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Recovered internal-percent source never enters the resolved module as a symbol. */
TEST(IdentifierLexicalRecovery,
     ExcludesInvalidNamesAndRejectsRecoveredReference) {
  constexpr std::string_view source = R"ptx(.version 9.3
.target sm_80
.address_size 64
.global .u32 bad%name = 1;
.entry k() {
  .reg .u32 %r<2>;
  mov.u32 %r0, %r%tmp;
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto parsed = parser.parseModule();

  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed.diagnostics.size(), 2u);
  EXPECT_EQ(parsed.diagnostics[0].range, (SourceRange{{4, 17}, {4, 22}}));
  EXPECT_EQ(parsed.diagnostics[1].range, (SourceRange{{7, 18}, {7, 22}}));
  ASSERT_EQ(parsed->items.size(), 4u);
  const auto bound = binding::bindSymbols(*parsed);
  EXPECT_TRUE(std::ranges::none_of(
      bound.table.symbols(), [](const binding::Symbol& symbol) {
        return symbol.name == "bad" || symbol.name == "%name" ||
               symbol.name == "%tmp";
      }));
  const auto resolved = resolveModule(*parsed);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_FALSE(resolved.error().empty());
  EXPECT_EQ(resolved.error().front().range, (SourceRange{{7, 18}, {7, 22}}));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
