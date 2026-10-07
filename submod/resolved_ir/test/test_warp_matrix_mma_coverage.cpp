#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

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
  auto* dense =
      dynamic_cast<MmaSyncAlignedM16n8k16RowColF64F64F64F64*>(body[0].get());
  ASSERT_NE(dense, nullptr);
  const auto& dense_descriptor = *dense->matrix_descriptor();
  EXPECT_EQ(dense_descriptor.shape, (MatrixShape{16, 8, 16}));
  EXPECT_EQ(dense_descriptor.fragments[1].register_count, 8);
  EXPECT_EQ(dense_descriptor.fragments[1].register_type, base::ScalarType::F64);
  auto* sparse = dynamic_cast<
      MmaSpOrderedMetadataSyncAlignedM16n8k64RowColF32E4m3E4m3F32*>(
      body[1].get());
  ASSERT_NE(sparse, nullptr);
  EXPECT_EQ(sparse->matrix_descriptor()->sparse_order,
            MatrixSparseOrder::ORDERED);
  using Scaled =
      MmaSyncAlignedM16n8k32RowColKindMxf8f6f4BlockScaleScaleVec1F32E4m3E4m3F32Ue8m0;
  auto* scaled = dynamic_cast<Scaled*>(body[2].get());
  ASSERT_NE(scaled, nullptr);
  const auto& scaled_descriptor = *scaled->matrix_descriptor();
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
    EXPECT_TRUE(instruction->check(supported).has_value());
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
    return body[index]->check(context).has_value();
  };
  EXPECT_TRUE(check_target("sm_121a", 2));
  EXPECT_TRUE(check_target("sm_121a", 4));
  EXPECT_TRUE(check_target("sm_121a", 5));
  EXPECT_TRUE(check_target("sm_120f", 4));
  EXPECT_FALSE(check_target("sm_120f", 5));
  auto& sparse_selector = sparse->selector.value;
  sparse_selector.bits = 4;
  EXPECT_FALSE(sparse->check(supported).has_value());
  sparse_selector.bits = 0;
  sparse_selector.integer_source_bits = 4;
  EXPECT_FALSE(sparse->check(supported).has_value());
  sparse_selector.integer_source_bits = 0;
  ASSERT_NE(scaled->matrix_descriptor(), nullptr);
  EXPECT_EQ(scaled->matrix_descriptor(), &Scaled::matrix_topology);
  EXPECT_EQ(scaled->instruction_kind(), Scaled::kind);
  EXPECT_EQ(Scaled::a_type, base::ScalarType::E4m3);
  EXPECT_TRUE(Scaled::block_scale);
  const auto saved_layout = scaled->operand_layout;
  scaled->operand_layout.value = 9999;
  EXPECT_FALSE(scaled->check(supported).has_value());
  scaled->operand_layout = saved_layout;
  EXPECT_TRUE(scaled->check(supported).has_value());
  auto& a_identity = scaled->a.value.elements.front()->symbol_id;
  const auto saved_identity = a_identity;
  a_identity = binding::SymbolId{.value = 999999u};
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  a_identity = saved_identity;
  auto& byte_id =
      std::get<ResolvedImmediate>(scaled->scale_a_selector.value.byte_id);
  byte_id.bits = 9;
  EXPECT_FALSE(scaled->check(supported).has_value());
  byte_id.bits = 0;
  byte_id.integer_source_bits = 9;
  EXPECT_FALSE(scaled->check(supported).has_value());
  byte_id.integer_source_bits = 0;
  byte_id.bits = 4;
  byte_id.integer_source_bits = 4;
  EXPECT_FALSE(scaled->check(supported).has_value());
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
    EXPECT_TRUE(instruction->check(context).has_value());

  using Scaled =
      MmaSyncAlignedM16n8k64RowColKindMxf4BlockScaleScaleVec2F32E2m1E2m1F32Ue8m0;
  auto& invalid_byte = std::get<ResolvedImmediate>(
      dynamic_cast<Scaled&>(*body[1]).scale_a_selector.value.byte_id);
  invalid_byte.bits = 1;
  invalid_byte.integer_source_bits = 1;
  EXPECT_FALSE(body[1]->check(context).has_value());
  auto& invalid_thread = std::get<ResolvedImmediate>(
      dynamic_cast<Scaled&>(*body[2]).scale_a_selector.value.thread_id);
  invalid_thread.bits = 2;
  invalid_thread.integer_source_bits = 2;
  EXPECT_FALSE(body[2]->check(context).has_value());
  auto& sparse_selector =
      dynamic_cast<MmaSpSpSyncAlignedM16n8k32RowColF32E4m3E4m3F32&>(*body[3])
          .selector.value;
  sparse_selector.bits = 2;
  sparse_selector.integer_source_bits = 2;
  EXPECT_FALSE(body[3]->check(context).has_value());
  auto& selector_register = std::get<ResolvedRegisterRef>(
      dynamic_cast<Scaled&>(*body[4]).scale_a_selector.value.byte_id);
  selector_register.declared_type = base::ScalarType::U32;
  EXPECT_FALSE(body[4]->check(context).has_value());

  std::string invalid_source = source;
  const auto selector_position = invalid_source.find("{2,1}");
  ASSERT_NE(selector_position, std::string::npos);
  invalid_source.replace(selector_position, 5, "{1,1}");
  const auto invalid_parsed = test_helpers::parseModule(invalid_source);
  ASSERT_MODULE_PARSE_SUCCEEDS(invalid_parsed);
  EXPECT_FALSE(resolveModule(*invalid_parsed).has_value());
}

/** Check sparse mxf4 target introduction and the later 4X/ue8m0 pair. */
TEST(WarpMatrixMmaCoverage, SparseMxf4VersionAndTargetBoundaries) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_121a
.address_size 64
.entry kernel() {
  .reg .f32 %d<4>, %c<4>;
  .reg .b32 %a<4>, %b<4>, %e, %sa, %sb;
  mma.sp::ordered_metadata.sync.aligned.m16n8k128.row.col.kind::mxf4.block_scale.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1,%b2,%b3},
    {%c0,%c1,%c2,%c3}, %e, 0, %sa, {2,1}, %sb, {2,3};
  mma.sp::ordered_metadata.sync.aligned.m16n8k128.row.col.kind::mxf4nvf4.block_scale.scale_vec::4X.f32.e2m1.e2m1.f32.ue4m3
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1,%b2,%b3},
    {%c0,%c1,%c2,%c3}, %e, 0, %sa, {0,0}, %sb, {0,0};
  mma.sp::ordered_metadata.sync.aligned.m16n8k128.row.col.kind::mxf4nvf4.block_scale.scale_vec::4X.f32.e2m1.e2m1.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1,%b2,%b3},
    {%c0,%c1,%c2,%c3}, %e, 0, %sa, {0,0}, %sb, {0,0};
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);

  const auto check_at = [&](size_t index, std::string_view target_name,
                            int major, int minor) {
    const auto profile = base::find_target_profile(target_name);
    EXPECT_TRUE(profile.has_value()) << target_name;
    if (!profile)
      return false;
    const checker::Context context{
        .target = {.ptx_version = {static_cast<uint16_t>(major),
                                   static_cast<uint16_t>(minor)},
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities},
    };
    return body[index]->check(context).has_value();
  };
  EXPECT_FALSE(check_at(0, "sm_121a", 8, 7));
  EXPECT_TRUE(check_at(0, "sm_121a", 8, 8));
  EXPECT_TRUE(check_at(0, "sm_120a", 8, 7));
  EXPECT_FALSE(check_at(1, "sm_121a", 8, 7));
  EXPECT_TRUE(check_at(1, "sm_121a", 8, 8));
  EXPECT_TRUE(check_at(1, "sm_120a", 8, 7));
  EXPECT_FALSE(check_at(2, "sm_120a", 9, 0));
  EXPECT_FALSE(check_at(2, "sm_121a", 9, 0));
  EXPECT_TRUE(check_at(2, "sm_120a", 9, 1));
  EXPECT_TRUE(check_at(2, "sm_121a", 9, 1));
}

/** Exact forms reject corrupt layouts without a mutable logical-form tag. */
TEST(WarpMatrixMmaCoverage, InvalidOwnedLayoutReturnsModuleDiagnostics) {
  constexpr std::string_view body = R"ptx(
.entry kernel() {
  .reg .f32 %d<4>, %c<4>;
  .reg .f16x2 %a<2>, %b<1>;
  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32
    {%d0,%d1,%d2,%d3}, {%a0,%a1}, {%b0}, {%c0,%c1,%c2,%c3};
}
)ptx";
  std::optional<ResolvedModule> full;
  {
    const auto parsed = test_helpers::parseModule(
        std::string{".version 9.3\n.target sm_120a\n.address_size 64\n"} +
        std::string{body});
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    full = std::move(*resolved);
  }
  std::optional<ResolvedModule> without_target;
  {
    const auto parsed = test_helpers::parseModule(
        std::string{".version 9.3\n.address_size 64\n"} + std::string{body});
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    without_target = std::move(*resolved);
  }
  ASSERT_TRUE(
      validateModule(*full, ModuleValidationPolicy::RequireCompleteContext));
  ASSERT_TRUE(validateModule(*without_target,
                             ModuleValidationPolicy::AvailableContext));

  using Exact = MmaSyncAlignedM16n8k8RowColF32F16F16F32;
  static_assert(std::is_final_v<Exact>);
  static_assert(Exact::kind ==
                InstructionKind::MmaSyncAlignedM16n8k8RowColF32F16F16F32);
  const auto expect_diagnostic = [](ResolvedModule& module,
                                    ModuleValidationPolicy policy,
                                    bool require_context) {
    auto& owner = module.functions.front().body.front();
    auto* instruction = dynamic_cast<Exact*>(owner.get());
    ASSERT_NE(instruction, nullptr);
    const auto saved_layout = instruction->operand_layout;
    instruction->operand_layout = ResolvedOperandLayoutTag{9999};
    detail::IReferenceObserver observer;
    EXPECT_NO_THROW(owner->visit_references(observer));
    if (require_context) {
      const auto profile = base::find_target_profile("sm_120a");
      ASSERT_TRUE(profile.has_value());
      const checker::Context context{
          .target = {
              .ptx_version = {9, 3},
              .sm_version = profile->identity.architecture.number,
              .enabled_family_features = profile->enabled_family_features,
              .identity = profile->identity,
              .capabilities = profile->capabilities}};
      const auto direct = instruction->check(context);
      ASSERT_FALSE(direct.has_value());
      EXPECT_EQ(direct.error().back().kind,
                checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
      const auto result = validateModule(module, policy);
      ASSERT_FALSE(result.has_value());
      EXPECT_TRUE(
          std::ranges::any_of(result.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::InvalidOperandLayoutTag;
          }));
    } else {
      // AvailableContext deliberately defers final instruction checks without a target.
      EXPECT_NO_THROW((void)validateModule(module, policy));
    }
    instruction->operand_layout = saved_layout;
    EXPECT_TRUE(validateModule(module, policy).has_value());
  };
  expect_diagnostic(*full, ModuleValidationPolicy::RequireCompleteContext,
                    true);
  expect_diagnostic(*without_target, ModuleValidationPolicy::AvailableContext,
                    false);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
