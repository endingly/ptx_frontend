#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/membar.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for memory-barrier selection tests. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

/** Distinguish the three source-level memory-barrier scope spellings. */
TEST(SelectVariantMembar, SelectsEachMemoryBarrierLevel) {
  for (const auto [source, expected] :
       std::array<std::pair<std::string_view, std::string_view>, 3>{{
           {"membar.cta;", "Cta"},
           {"membar.gl;", "Gl"},
           {"membar.sys;", "Sys"},
       }}) {
    const auto selected = select_variant_name(parse_instruction(source), membar_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << source;
    EXPECT_EQ(*selected, expected);
  }
  for (const std::string_view source : {
           "membar;",
           "membar.gpu;",
           "membar.cluster;",
           "membar.cta.sys;",
       }) {
    EXPECT_FALSE(select_variant_name(parse_instruction(source), membar_syntax_descriptor()).has_value())
        << source;
  }
}

/** Select alias-proxy ordering without accepting incomplete suffixes. */
TEST(SelectVariantMembar, SelectsFixedProxyAlias) {
  const auto selected =
      select_variant_name(parse_instruction("membar.proxy.alias;"), membar_syntax_descriptor());
  ASSERT_TRUE(selected.has_value()) << selected.error().message;
  EXPECT_EQ(*selected, "ProxyAlias");
  for (const std::string_view source : {
           "membar.proxy;",
           "membar.alias;",
           "membar.proxy.alias.cta;",
       }) {
    EXPECT_FALSE(select_variant_name(parse_instruction(source), membar_syntax_descriptor()).has_value())
        << source;
  }
}

/** Preserve the written async proxy's state-space selection. */
TEST(SelectVariantMembar, SelectsAsyncProxySpaces) {
  for (const std::string_view source : {
           "membar.proxy.async;",
           "membar.proxy.async.global;",
           "membar.proxy.async.shared::cta;",
       }) {
    const auto selected = select_variant_name(parse_instruction(source), membar_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << source;
    EXPECT_EQ(*selected, "ProxyAsync");
  }
  const auto cluster = select_variant_name(
      parse_instruction("membar.proxy.async.shared::cluster;"), membar_syntax_descriptor());
  ASSERT_TRUE(cluster.has_value()) << cluster.error().message;
  EXPECT_EQ(*cluster, "ProxyAsyncSharedCluster");
  for (const std::string_view source : {
           "membar.proxy.async.shared;",
           "membar.proxy.async.shared::cluster.global;",
           "membar.proxy.global;",
       }) {
    EXPECT_FALSE(select_variant_name(parse_instruction(source), membar_syntax_descriptor()).has_value())
        << source;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
