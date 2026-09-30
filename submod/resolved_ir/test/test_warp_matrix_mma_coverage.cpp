#include <gtest/gtest.h>

#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Keep dense, sparse, and scaled MMA contracts alive after syntax destruction. */
TEST(WarpMatrixMmaCoverage, ResolvesOwnedTopologyAndRejectsMutation) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_120a
.address_size 64
.entry kernel() {
  .reg .f64 %fd<4>, %fa<8>, %fb<4>, %fc<4>;
  .reg .f32 %d<4>, %c<4>;
  .reg .b32 %a<4>, %b<4>, %e, %sa, %sb;
  mma.sync.aligned.m16n8k16.row.col.f64.f64.f64.f64.rn
    {%fd0,%fd1,%fd2,%fd3}, {%fa0,%fa1,%fa2,%fa3,%fa4,%fa5,%fa6,%fa7},
    {%fb0,%fb1,%fb2,%fb3}, {%fc0,%fc1,%fc2,%fc3};
  mma.sp::ordered_metadata.sync.aligned.m16n8k64.row.col.f32.e4m3.e4m3.f32
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1,%b2,%b3},
    {%c0,%c1,%c2,%c3}, %e, 0;
  mma.sync.aligned.m16n8k32.row.col.kind::mxf8f6f4.block_scale.scale_vec::1X.f32.e4m3.e4m3.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {0,0}, %sb, {2,3};
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_EQ(owned->functions.front().body.size(), 3u);
  const auto& body = owned->functions.front().body;
  const auto& dense = std::get<Mma>(body[0]);
  const auto dense_descriptor = std::visit([](const auto& selected) {
    return selected.matrix.value;
  }, dense.variant);
  EXPECT_EQ(dense_descriptor.shape, (MatrixShape{16, 8, 16}));
  EXPECT_EQ(dense_descriptor.fragments[1].register_count, 8);
  EXPECT_EQ(dense_descriptor.fragments[1].register_type, base::ScalarType::F64);
  const auto& sparse = std::get<Mma>(body[1]);
  EXPECT_EQ(std::visit([](const auto& selected) {
    return selected.matrix.value.sparse_order;
  }, sparse.variant), MatrixSparseOrder::ORDERED);
  auto& scaled = std::get<Mma>(body[2]);
  const auto scaled_descriptor = std::visit([](const auto& selected) {
    return selected.matrix.value;
  }, scaled.variant);
  EXPECT_EQ(scaled_descriptor.kind, MatrixKind::MXF8F6F4);
  EXPECT_EQ(scaled_descriptor.scale_selector_count, 2);
  EXPECT_EQ(scaled_descriptor.scale_selectors[0].byte_mask, 0b1111);
  const auto profile = base::find_target_profile("sm_120a");
  ASSERT_TRUE(profile.has_value());
  const checker::Context supported{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Mma>(instruction), supported).has_value());
  std::visit([](auto& selected) {
    selected.matrix.value.scale_selectors[0].byte_mask = 1;
  }, scaled.variant);
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
}

/** Range-check each scale-selector slot and the sparse immediate selector. */
TEST(WarpMatrixMmaCoverage, RejectsScaleSelectorAndSparseSelectorLimits) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_120a
.address_size 64
.entry kernel() {
  .reg .f32 %d<4>, %c<4>;
  .reg .b32 %a<4>, %b<4>, %e, %sa, %sb;
  mma.sync.aligned.m16n8k64.row.col.kind::mxf4.block_scale.scale_vec::2X.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {2,1}, %sb, {2,3};
  mma.sync.aligned.m16n8k64.row.col.kind::mxf4.block_scale.scale_vec::2X.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {1,1}, %sb, {2,3};
  mma.sync.aligned.m16n8k64.row.col.kind::mxf4.block_scale.scale_vec::2X.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {2,2}, %sb, {2,3};
  mma.sp.sync.aligned.m16n8k32.row.col.f32.e4m3.e4m3.f32
    {%d0,%d1,%d2,%d3}, {%a0,%a1}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %e, 2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const auto profile = base::find_target_profile("sm_120a");
  ASSERT_TRUE(profile.has_value());
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
  };
  EXPECT_TRUE(checker::check(std::get<Mma>(body[0]), context).has_value());
  for (size_t index = 1; index < body.size(); ++index)
    EXPECT_FALSE(checker::check(std::get<Mma>(body[index]), context).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
