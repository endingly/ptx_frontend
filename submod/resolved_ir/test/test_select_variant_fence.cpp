#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fence.gen.hpp>
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

TEST(SelectVariantFence, SelectsModernProxyFormsAndRejectsNeighbors) {
  const auto expect_variant = [](std::string_view source,
                                 std::string_view expected) {
    const auto selected = select_variant_name(parse_instruction(source), fence_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  for (const std::string_view source : {
           "fence.proxy.async;",
           "fence.proxy.async.global;",
           "fence.proxy.async.shared::cta;",
       }) {
    expect_variant(source, "ProxyAsync");
  }
  expect_variant("fence.proxy.async.shared::cluster;",
                 "ProxyAsyncSharedCluster");

  for (const std::string_view scope : {"cta", "gpu", "sys"}) {
    expect_variant(std::string("fence.proxy.tensormap::generic.release.") +
                       std::string(scope) + ";",
                   "ProxyTensormapGenericRelease");
    expect_variant(std::string("fence.proxy.tensormap::generic.acquire.") +
                       std::string(scope) + " [%rd0], 128;",
                   "ProxyTensormapGenericAcquire");
  }
  expect_variant("fence.proxy.tensormap::generic.release.cluster;",
                 "ProxyTensormapGenericReleaseCluster");
  expect_variant("fence.proxy.tensormap::generic.acquire.cluster [%rd0], 128;",
                 "ProxyTensormapGenericAcquireCluster");
  expect_variant(
      "fence.proxy.async::generic.acquire.sync_restrict::shared::cluster."
      "cluster;",
      "ProxyAsyncGenericAcquireSyncRestrictSharedCluster");
  expect_variant(
      "fence.proxy.async::generic.release.sync_restrict::shared::cta.cluster;",
      "ProxyAsyncGenericReleaseSyncRestrictSharedCta");

  for (const std::string_view source : {
           "fence.proxy.generic::tensormap.release.gpu;",
           "fence.proxy.async::generic.acquire.cluster.sync_restrict::shared::"
           "cluster;",
           "fence.proxy.async::generic.release.sync_restrict::shared::cluster."
           "cluster;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(select_variant_name(parse_instruction(source), fence_syntax_descriptor()).has_value());
  }
}

/** Keep ordinary fence order aliases on disjoint semantic/scope variants. */
TEST(SelectVariantFence, SelectsOrdinaryFenceSemanticsAndScopes) {
  const auto expect_variant = [](std::string_view source,
                                 std::string_view expected) {
    const auto selected = select_variant_name(parse_instruction(source), fence_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << source;
    EXPECT_EQ(*selected, expected);
  };
  for (const std::string_view scope : {"cta", "gpu", "sys", "cluster"}) {
    const auto variant = scope == "cta" ? "OrdinaryCta"
                         : scope == "cluster"
                             ? "OrdinaryCluster"
                             : "OrdinaryGpuSys";
    expect_variant(std::string("fence.") + std::string(scope) + ";", variant);
    for (const std::string_view semantics :
         {"sc", "acq_rel", "acquire", "release"}) {
      const auto expected = scope == "cta" && semantics == "acq_rel"
                                ? "AcqRelCta"
                                : variant;
      expect_variant(std::string("fence.") + std::string(semantics) + "." +
                         std::string(scope) + ";",
                     expected);
      expect_variant(std::string("fence.") + std::string(scope) + "." +
                         std::string(semantics) + ";",
                     expected);
    }
  }
  for (const std::string_view source : {
           "fence;",
           "fence.gl;",
           "fence.relaxed.cta;",
           "fence.weak.gpu;",
           "fence.cta.acquire.release;",
       }) {
    EXPECT_FALSE(select_variant_name(parse_instruction(source), fence_syntax_descriptor()).has_value())
        << source;
  }
}

/** Keep the restricted mbarrier-init fence separate from ordinary fences. */
TEST(SelectVariantFence, SelectsMbarrierInitReleaseCluster) {
  const auto selected = select_variant_name(
      parse_instruction("fence.mbarrier_init.release.cluster;"), fence_syntax_descriptor());
  ASSERT_TRUE(selected.has_value()) << selected.error().message;
  EXPECT_EQ(*selected, "MbarrierInitReleaseCluster");
  const auto ordinary =
      select_variant_name(parse_instruction("fence.release.cluster;"), fence_syntax_descriptor());
  ASSERT_TRUE(ordinary.has_value()) << ordinary.error().message;
  EXPECT_EQ(*ordinary, "OrdinaryCluster");
  for (const std::string_view source : {
           "fence.mbarrier_init.cluster;",
           "fence.mbarrier_init.acquire.cluster;",
           "fence.mbarrier_init.release.cta;",
           "fence.release.mbarrier_init.cluster;",
       }) {
    EXPECT_FALSE(select_variant_name(parse_instruction(source), fence_syntax_descriptor()).has_value())
        << source;
  }
}

/** Keep restricted shared-memory fences distinct from proxy fences. */
TEST(SelectVariantFence, SelectsSharedSyncRestrictedForms) {
  for (const auto& [source, expected] :
       {std::pair{"fence.acquire.sync_restrict::shared::cluster.cluster;",
                  "AcquireSyncRestrictSharedCluster"},
        std::pair{"fence.release.sync_restrict::shared::cta.cluster;",
                  "ReleaseSyncRestrictSharedCta"}}) {
    const auto selected = select_variant_name(parse_instruction(source), fence_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  }
  for (const std::string_view source : {
           "fence.acquire.sync_restrict::shared::cta.cluster;",
           "fence.release.sync_restrict::shared::cluster.cluster;",
           "fence.acquire.sync_restrict::shared::cluster.cta;",
           "fence.release.sync_restrict::shared::cta.cta;",
           "fence.sync_restrict::shared::cluster.acquire.cluster;",
           "fence.acquire.cluster.sync_restrict::shared::cluster;",
       }) {
    EXPECT_FALSE(select_variant_name(parse_instruction(source), fence_syntax_descriptor()).has_value())
        << source;
  }
}

/** Select the fixed alias-proxy fence without accepting incomplete suffixes. */
TEST(SelectVariantFence, SelectsFixedProxyAlias) {
  const auto selected =
      select_variant_name(parse_instruction("fence.proxy.alias;"), fence_syntax_descriptor());
  ASSERT_TRUE(selected.has_value()) << selected.error().message;
  EXPECT_EQ(*selected, "ProxyAlias");
  for (const std::string_view source : {
           "fence.proxy;",
           "fence.alias;",
           "fence.proxy.alias.cta;",
           "fence.alias.proxy;",
       }) {
    EXPECT_FALSE(select_variant_name(parse_instruction(source), fence_syntax_descriptor()).has_value())
        << source;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
