#include <gtest/gtest.h>

#include <optional>
#include <string>
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
  .reg .u16 %byte_id, %thread_id;
  mma.sync.aligned.m16n8k16.row.col.f64.f64.f64.f64.rn
    {%fd0,%fd1,%fd2,%fd3}, {%fa0,%fa1,%fa2,%fa3,%fa4,%fa5,%fa6,%fa7},
    {%fb0,%fb1,%fb2,%fb3}, {%fc0,%fc1,%fc2,%fc3};
  mma.sp::ordered_metadata.sync.aligned.m16n8k64.row.col.f32.e4m3.e4m3.f32
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1,%b2,%b3},
    {%c0,%c1,%c2,%c3}, %e, 0;
  mma.sync.aligned.m16n8k32.row.col.kind::mxf8f6f4.block_scale.scale_vec::1X.f32.e4m3.e4m3.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {0,0}, %sb, {2,3};
  mma.sync.aligned.m16n8k32.row.col.kind::mxf8f6f4.block_scale.scale_vec::1X.f32.e4m3.e4m3.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {%byte_id,%thread_id}, %sb, {2,3};
  mma.sp::ordered_metadata.sync.aligned.m16n8k64.row.col.kind::mxf8f6f4.block_scale.scale_vec::1X.f32.e4m3.e4m3.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1,%b2,%b3},
    {%c0,%c1,%c2,%c3}, %e, 0, %sa, {0,0}, %sb, {2,3};
  mma.sp::ordered_metadata.sync.aligned.m16n8k128.row.col.kind::mxf4.block_scale.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1,%b2,%b3},
    {%c0,%c1,%c2,%c3}, %e, 0, %sa, {2,1}, %sb, {2,3};
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_EQ(owned->functions.front().body.size(), 6u);
  auto& body = owned->functions.front().body;
  const auto& dense = std::get<Mma>(body[0]);
  const auto dense_descriptor =
      std::visit([](const auto& selected) { return selected.matrix.value; },
                 dense.variant);
  EXPECT_EQ(dense_descriptor.shape, (MatrixShape{16, 8, 16}));
  EXPECT_EQ(dense_descriptor.fragments[1].register_count, 8);
  EXPECT_EQ(dense_descriptor.fragments[1].register_type, base::ScalarType::F64);
  auto& sparse = std::get<Mma>(body[1]);
  EXPECT_EQ(std::visit(
                [](const auto& selected) {
                  return selected.matrix.value.sparse_order;
                },
                sparse.variant),
            MatrixSparseOrder::ORDERED);
  auto& scaled = std::get<Mma>(body[2]);
  const auto scaled_descriptor =
      std::visit([](const auto& selected) { return selected.matrix.value; },
                 scaled.variant);
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
    EXPECT_TRUE(
        checker::check(std::get<Mma>(instruction), supported).has_value());
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  /** Keep sparse exact targets distinct from the modern family targets. */
  const auto check_target = [&](std::string_view name, size_t index) {
    const auto target = base::find_target_profile(name);
    EXPECT_TRUE(target.has_value()) << name;
    if (!target)
      return false;
    const checker::Context context{
        .target = {.ptx_version = {9, 3},
                   .sm_version = target->identity.architecture.number,
                   .enabled_family_features = target->enabled_family_features,
                   .identity = target->identity,
                   .capabilities = target->capabilities},
    };
    return checker::check(std::get<Mma>(body[index]), context).has_value();
  };
  EXPECT_TRUE(check_target("sm_121a", 2));
  EXPECT_TRUE(check_target("sm_121a", 4));
  EXPECT_TRUE(check_target("sm_121a", 5));
  EXPECT_TRUE(check_target("sm_120f", 4));
  EXPECT_FALSE(check_target("sm_120f", 5));
  auto& sparse_selector =
      std::get<Mma::SpOrderedMetadataSyncAlignedM16n8k64RowColF32E4m3E4m3F32>(
          sparse.variant)
          .selector.value;
  sparse_selector.bits = 4;
  EXPECT_FALSE(checker::check(sparse, supported).has_value());
  sparse_selector.bits = 0;
  sparse_selector.integer_source_bits = 4;
  EXPECT_FALSE(checker::check(sparse, supported).has_value());
  sparse_selector.integer_source_bits = 0;
  using Scaled = Mma::
      SyncAlignedM16n8k32RowColKindMxf8f6f4BlockScaleScaleVec1F32E4m3E4m3F32Ue8m0;
  auto& selected = std::get<Scaled>(scaled.variant);
  ASSERT_TRUE(scaled.matrix_logical_index().has_value());
  ASSERT_NE(scaled.matrix_descriptor(), nullptr);
  EXPECT_EQ(selected.matrix.value, *scaled.matrix_descriptor());
  const auto original_form = scaled.semantic_form.value;
  const auto original_matrix = selected.matrix.value;
  scaled.semantic_form.value = Mma::VariantType::
      SyncAlignedM16n8k32RowColKindMxf8f6f4BlockScaleScaleVec1F32E4m3E5m2F32Ue8m0;
  ASSERT_TRUE(scaled.matrix_logical_index().has_value());
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
  selected.matrix.value = *scaled.matrix_descriptor();
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
  selected.matrix.value = original_matrix;
  scaled.semantic_form.value =
      Mma::VariantType::SyncAlignedM8n8k4RowColF64F64F64F64;
  EXPECT_FALSE(scaled.matrix_logical_index().has_value());
  EXPECT_EQ(scaled.matrix_descriptor(), nullptr);
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
  scaled.semantic_form.value = static_cast<Mma::VariantType>(9999);
  EXPECT_FALSE(scaled.matrix_logical_index().has_value());
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
  scaled.semantic_form.value = original_form;
  selected.a_type.value = base::ScalarType::E5m2;
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
  selected.a_type.value = base::ScalarType::E4m3;
  selected.block_scale.value = false;
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
  selected.block_scale.value = true;
  EXPECT_TRUE(checker::check(scaled, supported).has_value());
  auto& a_identity = selected.a.value.elements.front()->symbol_id;
  const auto saved_identity = a_identity;
  a_identity = binding::SymbolId{.value = 999999u};
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  a_identity = saved_identity;
  auto& byte_id =
      std::get<ResolvedImmediate>(selected.scale_a_selector.value.byte_id);
  byte_id.bits = 9;
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
  byte_id.bits = 0;
  byte_id.integer_source_bits = 9;
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
  byte_id.integer_source_bits = 0;
  std::visit(
      [](auto& selected) {
        selected.matrix.value.scale_selectors[0].byte_mask = 1;
      },
      scaled.variant);
  EXPECT_FALSE(checker::check(scaled, supported).has_value());
}

/** Range-check each scale-selector slot and the sparse immediate selector. */
TEST(WarpMatrixMmaCoverage, RejectsScaleSelectorAndSparseSelectorLimits) {
  const std::string source = R"ptx(
.version 9.3
.target sm_120a
.address_size 64
.entry kernel() {
  .reg .f32 %d<4>, %c<4>;
  .reg .b32 %a<4>, %b<4>, %e, %sa, %sb;
  .reg .u16 %selector;
  mma.sync.aligned.m16n8k64.row.col.kind::mxf4.block_scale.scale_vec::2X.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {2,1}, %sb, {2,3};
  mma.sync.aligned.m16n8k64.row.col.kind::mxf4.block_scale.scale_vec::2X.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {2,1}, %sb, {2,3};
  mma.sync.aligned.m16n8k64.row.col.kind::mxf4.block_scale.scale_vec::2X.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {2,1}, %sb, {2,3};
  mma.sp.sync.aligned.m16n8k32.row.col.f32.e4m3.e4m3.f32
    {%d0,%d1,%d2,%d3}, {%a0,%a1}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %e, 0;
  mma.sync.aligned.m16n8k64.row.col.kind::mxf4.block_scale.scale_vec::2X.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {%selector,1}, %sb, {2,3};
}
)ptx";
  const auto parsed = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const auto profile = base::find_target_profile("sm_120a");
  ASSERT_TRUE(profile.has_value());
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(
        checker::check(std::get<Mma>(instruction), context).has_value());

  using Scaled = Mma::
      SyncAlignedM16n8k64RowColKindMxf4BlockScaleScaleVec2F32E2m1E2m1F32Ue8m0;
  auto& invalid_byte = std::get<ResolvedImmediate>(
      std::get<Scaled>(std::get<Mma>(body[1]).variant)
          .scale_a_selector.value.byte_id);
  invalid_byte.bits = 1;
  invalid_byte.integer_source_bits = 1;
  EXPECT_FALSE(checker::check(std::get<Mma>(body[1]), context).has_value());
  auto& invalid_thread = std::get<ResolvedImmediate>(
      std::get<Scaled>(std::get<Mma>(body[2]).variant)
          .scale_a_selector.value.thread_id);
  invalid_thread.bits = 2;
  invalid_thread.integer_source_bits = 2;
  EXPECT_FALSE(checker::check(std::get<Mma>(body[2]), context).has_value());
  auto& sparse_selector =
      std::get<Mma::SpSpSyncAlignedM16n8k32RowColF32E4m3E4m3F32>(
          std::get<Mma>(body[3]).variant)
          .selector.value;
  sparse_selector.bits = 2;
  sparse_selector.integer_source_bits = 2;
  EXPECT_FALSE(checker::check(std::get<Mma>(body[3]), context).has_value());
  auto& selector_register = std::get<ResolvedRegisterRef>(
      std::get<Scaled>(std::get<Mma>(body[4]).variant)
          .scale_a_selector.value.byte_id);
  selector_register.declared_type = base::ScalarType::U32;
  EXPECT_FALSE(checker::check(std::get<Mma>(body[4]), context).has_value());

  std::string invalid_source = source;
  const auto selector_position = invalid_source.find("{2,1}");
  ASSERT_NE(selector_position, std::string::npos);
  invalid_source.replace(selector_position, 5, "{1,1}");
  const auto invalid_parsed = test_helpers::parseModule(invalid_source);
  ASSERT_MODULE_PARSE_SUCCEEDS(invalid_parsed);
  EXPECT_FALSE(resolveModule(*invalid_parsed).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
