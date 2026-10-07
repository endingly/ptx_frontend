#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Public cp catalogs are contiguous, stable, and ordered across all shards. */
TEST(DescriptorStorage, CpShardBoundariesAndBorrowedLifetime) {
  const auto& syntax = cp_syntax_descriptor();
  const auto& resolved = cp_resolved_descriptor();
  const auto& legality = cp_checker_descriptor();
  constexpr size_t kExpectedForms = 295;
  ASSERT_EQ(syntax.variants.size(), kExpectedForms);
  ASSERT_EQ(resolved.variants.size(), kExpectedForms);
  ASSERT_EQ(legality.variants.size(), kExpectedForms);
  EXPECT_EQ(syntax.Opcode_name, "cp");
  EXPECT_EQ(resolved.opcode_name, "cp");
  EXPECT_EQ(legality.opcode_name, "cp");

  const auto syntax_view = syntax.variants;
  const auto resolved_view = resolved.variants;
  const auto legality_view = legality.variants;
  ASSERT_NE(syntax_view.data(), nullptr);
  ASSERT_NE(resolved_view.data(), nullptr);
  ASSERT_NE(legality_view.data(), nullptr);

  for (size_t repeat = 0; repeat < 16; ++repeat) {
    EXPECT_EQ(&cp_syntax_descriptor(), &syntax);
    EXPECT_EQ(&cp_resolved_descriptor(), &resolved);
    EXPECT_EQ(&cp_checker_descriptor(), &legality);
    EXPECT_EQ(cp_syntax_descriptor().variants.data(), syntax_view.data());
    EXPECT_EQ(cp_resolved_descriptor().variants.data(), resolved_view.data());
    EXPECT_EQ(cp_checker_descriptor().variants.data(), legality_view.data());
  }

  const std::array<std::pair<size_t, std::string_view>, 10> boundaries{{
      {0, "AsyncCaSharedGlobal"},
      {63, "ReduceAsyncBulkSharedAdd"},
      {64, "ReduceAsyncBulkSharedMin"},
      {127, "AsyncBulkTensor4dSharedCta"},
      {128, "AsyncBulkTensor5dSharedCta"},
      {191, "AsyncBulkTensor5dSharedCtaIm2colW128CtaGroup"},
      {192, "AsyncBulkTensor1dSharedClusterMulticastCtaGroup"},
      {255, "ReduceAsyncBulkTensor5dXor"},
      {256, "ReduceAsyncBulkTensor3dAddIm2colNoOffs"},
      {294, "AsyncBulkPrefetchTensor5dIm2colW128"},
  }};
  for (const auto& [index, name] : boundaries) {
    SCOPED_TRACE(index);
    EXPECT_EQ(syntax_view[index].variant_name, name);
    EXPECT_EQ(resolved_view[index].variant_name, name);
    EXPECT_EQ(legality_view[index].variant_name, name);
  }

  std::unordered_set<std::string_view> identities;
  for (size_t index = 0; index < kExpectedForms; ++index) {
    const auto name = syntax_view[index].variant_name;
    SCOPED_TRACE(index);
    EXPECT_FALSE(name.empty());
    EXPECT_TRUE(identities.insert(name).second);
    EXPECT_EQ(resolved_view[index].variant_name, name);
    EXPECT_EQ(legality_view[index].variant_name, name);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
