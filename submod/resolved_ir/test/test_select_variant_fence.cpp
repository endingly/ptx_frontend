#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fence/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fence/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fence/resolution.gen.hpp>
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

TEST(SelectVariantFence, SelectsModernProxyFormsAndRejectsNeighbors) {
  const auto expect_variant = [](std::string_view source,
                                 Fence::VariantType expected) {
    const auto selected = selectVariant<Fence>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  for (const std::string_view source : {
           "fence.proxy.async;",
           "fence.proxy.async.global;",
           "fence.proxy.async.shared::cta;",
       }) {
    expect_variant(source, Fence::VariantType::ProxyAsync);
  }
  expect_variant("fence.proxy.async.shared::cluster;",
                 Fence::VariantType::ProxyAsyncSharedCluster);

  for (const std::string_view scope : {"cta", "gpu", "sys"}) {
    expect_variant(std::string("fence.proxy.tensormap::generic.release.") +
                       std::string(scope) + ";",
                   Fence::VariantType::ProxyTensormapGenericRelease);
    expect_variant(std::string("fence.proxy.tensormap::generic.acquire.") +
                       std::string(scope) + " [%rd0], 128;",
                   Fence::VariantType::ProxyTensormapGenericAcquire);
  }
  expect_variant("fence.proxy.tensormap::generic.release.cluster;",
                 Fence::VariantType::ProxyTensormapGenericReleaseCluster);
  expect_variant("fence.proxy.tensormap::generic.acquire.cluster [%rd0], 128;",
                 Fence::VariantType::ProxyTensormapGenericAcquireCluster);
  expect_variant(
      "fence.proxy.async::generic.acquire.sync_restrict::shared::cluster."
      "cluster;",
      Fence::VariantType::ProxyAsyncGenericAcquireSyncRestrictSharedCluster);
  expect_variant(
      "fence.proxy.async::generic.release.sync_restrict::shared::cta.cluster;",
      Fence::VariantType::ProxyAsyncGenericReleaseSyncRestrictSharedCta);

  for (const std::string_view source : {
           "fence.proxy.alias;",
           "fence.proxy.generic::tensormap.release.gpu;",
           "fence.proxy.async::generic.acquire.cluster.sync_restrict::shared::"
           "cluster;",
           "fence.proxy.async::generic.release.sync_restrict::shared::cluster."
           "cluster;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Fence>(parse_instruction(source)).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
