#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/getctarank/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/getctarank/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/getctarank/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
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
                                 Getctarank::VariantType expected) {
    const auto selected = selectVariant<Getctarank>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("getctarank.shared::cluster.u32 %r0, %r1;",
                 Getctarank::VariantType::SharedCluster);
  expect_variant("getctarank.shared::cluster.u64 %r0, shared_value+4;",
                 Getctarank::VariantType::SharedCluster);
  expect_variant("getctarank.u32 %r0, %r1;", Getctarank::VariantType::Generic);
  expect_variant("getctarank.u64 %r0, %rd1;", Getctarank::VariantType::Generic);

  for (const std::string_view source : {
           "getctarank.shared::cluster %r0, %r1;",
           "getctarank.u32.shared::cluster %r0, %r1;",
           "getctarank.shared::cluster.u32.u64 %r0, %r1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Getctarank>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Getctarank>(
                   parse_instruction("getctarank.shared::cluster.u32 %r0;"))
                   .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
