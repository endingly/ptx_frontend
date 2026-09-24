#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/mapa/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mapa/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mapa/checker.gen.hpp>
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

TEST(SelectVariantMapa, SelectsSharedClusterAndGenericForms) {
  const auto expect_variant = [](std::string_view source,
                                 Mapa::VariantType expected) {
    const auto selected = selectVariant<Mapa>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mapa.shared::cluster.u32 %r0, %r1, 0;",
                 Mapa::VariantType::SharedCluster);
  expect_variant("mapa.shared::cluster.u64 %rd0, shared_value+4, %r0;",
                 Mapa::VariantType::SharedCluster);
  expect_variant("mapa.u32 %r0, %r1, 0;", Mapa::VariantType::Generic);
  expect_variant("mapa.u64 %rd0, %rd1, %r0;", Mapa::VariantType::Generic);

  for (const std::string_view source : {
           "mapa.shared::cluster %r0, %r1, 0;",
           "mapa.u32.shared::cluster %r0, %r1, 0;",
           "mapa.shared::cluster.u32.u64 %r0, %r1, 0;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mapa>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Mapa>(parse_instruction("mapa.shared::cluster.u32 %r0, %r1;"))
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
