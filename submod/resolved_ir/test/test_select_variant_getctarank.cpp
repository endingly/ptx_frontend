#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/getctarank.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for a generated-opcode test. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

TEST(SelectVariantGetctarank, SelectsSharedClusterAndGenericForms) {
  const auto expect_variant = [](std::string_view source,
                                 std::string_view expected) {
    const auto selected = select_variant_name(parse_instruction(source), getctarank_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("getctarank.shared::cluster.u32 %r0, %r1;",
                 "SharedCluster");
  expect_variant("getctarank.shared::cluster.u64 %r0, shared_value+4;",
                 "SharedCluster");
  expect_variant("getctarank.u32 %r0, %r1;", "Generic");
  expect_variant("getctarank.u64 %r0, %rd1;", "Generic");

  for (const std::string_view source : {
           "getctarank.shared::cluster %r0, %r1;",
           "getctarank.u32.shared::cluster %r0, %r1;",
           "getctarank.shared::cluster.u32.u64 %r0, %r1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        select_variant_name(parse_instruction(source), getctarank_syntax_descriptor()).has_value());
  }
  EXPECT_FALSE(resolveGetctarank(
                   parse_instruction("getctarank.shared::cluster.u32 %r0;"))
                   .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
