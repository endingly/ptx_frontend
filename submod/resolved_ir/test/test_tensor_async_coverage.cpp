#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Every tiled prefetch rank retains descriptor, coordinates, and tile spelling. */
TEST(TensorAsync, PrefetchRanksAndTileProvenance) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 tensor_map[128];
.entry kernel() {
  .reg .s32 %r<5>;
  cp.async.bulk.prefetch.tensor.1d.L2.global [tensor_map, {%r0}];
  cp.async.bulk.prefetch.tensor.2d.L2.global.tile [tensor_map, {%r0, -2}];
  cp.async.bulk.prefetch.tensor.3d.L2.global [tensor_map, {%r0, %r1, %r2}];
  cp.async.bulk.prefetch.tensor.4d.L2.global.tile [tensor_map, {%r0, %r1, %r2, %r3}];
  cp.async.bulk.prefetch.tensor.5d.L2.global [tensor_map, {%r0, %r1, %r2, %r3, %r4}];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkPrefetchTensor1d>(
      std::get<Cp>(body[0]).variant));
  const auto& first = std::get<Cp::AsyncBulkPrefetchTensor1d>(
      std::get<Cp>(body[0]).variant);
  const auto& second = std::get<Cp::AsyncBulkPrefetchTensor2d>(
      std::get<Cp>(body[1]).variant);
  EXPECT_FALSE(first.tile.value);
  EXPECT_TRUE(first.tile.locs.empty());
  EXPECT_TRUE(second.tile.value);
  EXPECT_FALSE(second.tile.locs.empty());
  EXPECT_EQ(first.tensor.value.rank, TensorRank::One);
  EXPECT_EQ(second.tensor.value.rank, TensorRank::Two);
  EXPECT_EQ(second.tensor.value.coordinates.elements.size(), 2u);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(item), context).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
