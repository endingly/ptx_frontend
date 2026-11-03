#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>
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
  .reg .v2 .u16 Selector;
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
    {%c0,%c1,%c2,%c3}, %sa, {Selector.x,Selector.y}, %sb, {2,3};
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

/** Build one complete scalar brace pack with distinct register spellings. */
std::string denseF64Pack(char role, unsigned count) {
  std::string text = "{";
  for (unsigned lane = 0; lane < count; ++lane)
    text += (lane ? ",%" : "%") + std::string(1, role) + std::to_string(lane);
  return text + "}";
}

/** Spell the exact dense F64 form, permitting bounded adjacent shape controls. */
std::string denseF64Instruction(std::string_view shape = "m8n8k4",
                                std::string_view rounding = "") {
  const bool small = shape == "m8n8k4" || !shape.starts_with("m16n8k");
  const unsigned a_count = small                ? 1
                           : shape == "m16n8k4" ? 2
                           : shape == "m16n8k8" ? 4
                                                : 8;
  return "mma.sync.aligned." + std::string(shape) + ".row.col.f64.f64.f64.f64" +
         std::string(rounding) + " " + denseF64Pack('d', small ? 2 : 4) + ", " +
         denseF64Pack('a', a_count) + ", " +
         denseF64Pack('b', small ? 1 : a_count / 2) + ", " +
         denseF64Pack('c', small ? 2 : 4) + ";";
}

/** Initialize real same-function scalar donors without depending on producer history. */
std::string denseF64Module(std::string_view instruction,
                           std::string_view version = "9.3",
                           std::string_view target = "sm_80") {
  std::string text = ".version " + std::string(version) + "\n.target " +
                     std::string(target) +
                     "\n.address_size 64\n.entry kernel(){\n"
                     ".reg .f64 %d<4>, %a<8>, %b<4>, %c<4>, %x;\n"
                     ".reg .f32 %narrow;\nmov.b32 %narrow,0;\nmov.b64 %x,0;\n";
  for (const char role : std::string_view("dabc"))
    for (unsigned lane = 0; lane < (role == 'a' ? 8u : 4u); ++lane)
      text +=
          "mov.b64 %" + std::string(1, role) + std::to_string(lane) + ",0;\n";
  return text + std::string(instruction) + "\n}";
}

/** Return checked owned state only after the actual parser/source/AST owners die. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> ownedDenseF64(
    std::string text) {
  const auto parsed = test_helpers::parseModule(text);
  EXPECT_TRUE(parsed && parsed.diagnostics.empty());
  if (!parsed || !parsed.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModule(*parsed);
}

/** Keep the exact spelling, static topology and optional rounding provenance. */
TEST(WarpMatrixMmaCoverage, F64CanonicalShapeAndRounding) {
  using Exact = MmaSyncAlignedM8n8k4RowColF64F64F64F64;
  static_assert(std::is_final_v<Exact>);
  static_assert(
      Exact::a_type == ScalarType::F64 && Exact::b_type == ScalarType::F64 &&
      Exact::c_type == ScalarType::F64 && Exact::d_type == ScalarType::F64);
  unsigned ordinal = 0;
  for (const auto rounding : {"", ".rn", ".rz", ".rm", ".rp"}) {
    const auto text = denseF64Module(denseF64Instruction("m8n8k4", rounding));
    auto owned = ownedDenseF64(text);
    ASSERT_TRUE(owned) << owned.error().front().message;
    auto* exact =
        dynamic_cast<Exact*>(owned->functions.front().body.back().get());
    ASSERT_NE(exact, nullptr);
    EXPECT_EQ(exact->instruction_kind(), Exact::kind);
    EXPECT_EQ(exact->matrix_descriptor(), &Exact::matrix_topology);
    EXPECT_EQ(exact->matrix_descriptor()->shape, (MatrixShape{8, 8, 4}));
    EXPECT_EQ(exact->matrix_descriptor()->a_layout, MatrixLayout::ROW);
    EXPECT_EQ(exact->matrix_descriptor()->b_layout, MatrixLayout::COL);
    EXPECT_EQ(exact->dst.value.elements.size(), 2u);
    EXPECT_EQ(exact->a.value.elements.size(), 1u);
    EXPECT_EQ(exact->b.value.elements.size(), 1u);
    EXPECT_EQ(exact->c.value.elements.size(), 2u);
    const RoundingMode expected[] = {RoundingMode::Rn, RoundingMode::Rn,
                                     RoundingMode::Rz, RoundingMode::Rm,
                                     RoundingMode::Rp};
    EXPECT_EQ(exact->rounding.value, expected[ordinal]);
    EXPECT_EQ(exact->rounding.locs.empty(), ordinal == 0);
    ++ordinal;
    EXPECT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
    ResolvedModule copied = *owned;
    ResolvedModule assigned;
    assigned = copied;
    ResolvedModule moved = std::move(assigned);
    EXPECT_TRUE(
        validateModule(moved, ModuleValidationPolicy::RequireCompleteContext));
    const auto& copied_form =
        dynamic_cast<const Exact&>(*moved.functions.front().body.back());
    EXPECT_EQ(copied_form.matrix_descriptor(), &Exact::matrix_topology);
    EXPECT_EQ(copied_form.rounding, exact->rounding);
    EXPECT_EQ(copied_form.dst, exact->dst);
    auto clone = copied_form.clone();
    EXPECT_EQ(dynamic_cast<Exact&>(*clone).c, exact->c);
    ResolvedFunction function_copy = owned->functions.front();
    ResolvedFunction function_assigned;
    function_assigned = function_copy;
    EXPECT_EQ(dynamic_cast<Exact&>(*function_assigned.body.back()).a, exact->a);
    const checker::Context supported{
        .target = {.ptx_version = {9, 3}, .sm_version = 80}};
    EXPECT_TRUE(clone->check(supported));
    EXPECT_TRUE(function_assigned.body.back()->check(supported));
  }
}

/** Generic CST modifiers do not invent a semantic alias for malformed shapes. */
TEST(WarpMatrixMmaCoverage, F64RejectsMalformedShapeSpellings) {
  auto canonical = ownedDenseF64(denseF64Module(denseF64Instruction()));
  ASSERT_TRUE(canonical);
  for (const auto shape : {"m8n84", "m8n8", "m8n8k", "m8n8k5"}) {
    SCOPED_TRACE(shape);
    const auto text = denseF64Module(denseF64Instruction(shape));
    PtxCstParser parser(text);
    const auto cst = parser.parseModule();
    ASSERT_TRUE(cst && cst.diagnostics.empty());
    const auto parsed = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModule(*parsed);
    ASSERT_FALSE(resolved);
    ASSERT_EQ(resolved.error().size(), 1u);
    const auto& diagnostic = resolved.error().front();
    EXPECT_EQ(diagnostic.stage(), ResolveDiagnosticStage::Resolution);
    EXPECT_EQ(diagnostic.message,
              "Unknown modifier '." + std::string(shape) + "'.");
    const auto instruction = text.rfind("mma.sync.aligned.");
    const auto line = static_cast<int32_t>(
        std::ranges::count(text.substr(0, instruction), '\n') + 1);
    EXPECT_EQ(diagnostic.range,
              (SourceRange{{line, 17},
                           {line, 18 + static_cast<int32_t>(
                                           std::string_view(shape).size())}}));
    const auto association = validateModule(
        *parsed, *canonical, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(association);
    EXPECT_TRUE(std::ranges::any_of(association.error(), [](const auto& item) {
      return item.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
  }
  const auto valid =
      test_helpers::parseModule(denseF64Module(denseF64Instruction()));
  ASSERT_MODULE_PARSE_SUCCEEDS(valid);
  EXPECT_TRUE(validateModule(*valid, *canonical,
                             ModuleValidationPolicy::RequireCompleteContext));
  auto mixed = denseF64Module(denseF64Instruction());
  const auto suffix = mixed.rfind(".f64.f64.f64.f64");
  ASSERT_NE(suffix, std::string::npos);
  mixed.replace(suffix, 16, ".f64.f32.f64.f64");
  const auto mixed_ast = test_helpers::parseModule(mixed);
  ASSERT_MODULE_PARSE_SUCCEEDS(mixed_ast);
  EXPECT_FALSE(resolveModule(*mixed_ast));
}

/** Reject genuine current payload defects independently of an original valid AST. */
TEST(WarpMatrixMmaCoverage, F64OwnedFragmentsLayoutAndWidth) {
  using Exact = MmaSyncAlignedM8n8k4RowColF64F64F64F64;
  const auto text = denseF64Module(denseF64Instruction());
  auto owned = ownedDenseF64(text);
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto& body = owned->functions.front().body;
  auto& exact = dynamic_cast<Exact&>(*body.back());
  const auto spare = dynamic_cast<MovScalar&>(*body[1]).dst_register->value;
  const auto narrow = dynamic_cast<MovScalar&>(*body[0]).dst_register->value;
  ASSERT_EQ(narrow.declared_type, ScalarType::F32);
  ASSERT_TRUE(narrow.symbol_id);
  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  const auto supplied = test_helpers::parseModule(text);
  ASSERT_MODULE_PARSE_SUCCEEDS(supplied);
  for (auto* field : {&exact.dst, &exact.a, &exact.b, &exact.c}) {
    const auto saved = *field;
    const unsigned legal = saved.value.elements.size();
    for (const unsigned count : {1u, 2u, 3u}) {
      if (count == legal || (legal == 1 && count == 3))
        continue;
      if (count < legal) {
        field->value.elements.resize(count);
        field->locs.resize(count);
      } else {
        field->value.elements.push_back(spare);
        field->locs.push_back(saved.locs.front());
      }
      ASSERT_TRUE(
          valid_register_vector_source(field->value, field->locs, true));
      const auto result = exact.check(supported);
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::InvalidVectorOperand &&
               d.range == saved.locs.front() &&
               d.message.find("element count") != std::string::npos;
      }));
      const auto module = validateModule(
          *owned, ModuleValidationPolicy::RequireCompleteContext);
      ASSERT_FALSE(module);
      EXPECT_TRUE(std::ranges::any_of(module.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::InvalidVectorOperand;
      }));
      const auto joined = validateModule(
          *supplied, *owned, ModuleValidationPolicy::RequireCompleteContext);
      ASSERT_FALSE(joined);
      EXPECT_TRUE(std::ranges::any_of(joined.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::InvalidVectorOperand;
      }));
      *field = saved;
      EXPECT_TRUE(validateModule(
          *owned, ModuleValidationPolicy::RequireCompleteContext));
    }
    field->value.elements.front() = narrow;
    ASSERT_TRUE(valid_register_vector_source(field->value, field->locs, true));
    const auto width = exact.check(supported);
    ASSERT_FALSE(width);
    EXPECT_TRUE(std::ranges::any_of(width.error(), [&](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::OperandTypeMismatch &&
             d.range == saved.locs.front();
    }));
    const auto narrow_join = validateModule(
        *supplied, *owned, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(narrow_join);
    EXPECT_TRUE(std::ranges::any_of(narrow_join.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::OperandTypeMismatch;
    }));
    *field = saved;
    field->value.elements.front() = spare;
    EXPECT_TRUE(exact.check(supported));
    EXPECT_TRUE(validateModule(*supplied, *owned,
                               ModuleValidationPolicy::RequireCompleteContext));
    *field = saved;
  }
  const auto layout = exact.operand_layout;
  exact.operand_layout.value = 9999;
  const auto invalid = exact.check(supported);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().back().kind,
            checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
  EXPECT_FALSE(validateModule(*supplied, *owned,
                              ModuleValidationPolicy::RequireCompleteContext));
  exact.operand_layout = layout;
  EXPECT_TRUE(validateModule(*supplied, *owned,
                             ModuleValidationPolicy::RequireCompleteContext));
}

/** Preserve the distinct base and later dense F64 target/version gates. */
TEST(WarpMatrixMmaCoverage, F64BaseAndAdjacentAvailability) {
  auto owned = ownedDenseF64(denseF64Module(denseF64Instruction()));
  ASSERT_TRUE(owned);
  const auto& exact = *owned->functions.front().body.back();
  for (const auto version :
       {checker::PtxVersion{6, 5}, checker::PtxVersion{7, 0}}) {
    const checker::Context context{
        .target = {.ptx_version = version, .sm_version = 80}};
    EXPECT_EQ(bool(exact.check(context)),
              version == (checker::PtxVersion{7, 0}));
  }
  const checker::Context sm75{
      .target = {.ptx_version = {9, 3}, .sm_version = 75}};
  EXPECT_FALSE(exact.check(sm75));
  for (const auto shape : {"m16n8k4", "m16n8k8", "m16n8k16"}) {
    auto adjacent = ownedDenseF64(
        denseF64Module(denseF64Instruction(shape), "7.8", "sm_90"));
    ASSERT_TRUE(adjacent) << adjacent.error().front().message;
    const auto& form = *adjacent->functions.front().body.back();
    for (const auto version :
         {checker::PtxVersion{7, 7}, checker::PtxVersion{7, 8}})
      for (const auto sm : {80u, 90u}) {
        const checker::Context context{
            .target = {.ptx_version = version, .sm_version = sm}};
        EXPECT_EQ(bool(form.check(context)),
                  version == (checker::PtxVersion{7, 8}) && sm == 90);
      }
  }
}

/** One representative source tuple, distinct from the complete static catalogue. */
struct SparseKindCase {
  /** Scalar input element spellings; containers remain ordinary B32 registers. */
  std::string_view a, b;
  /** Whether C/D use two F16x2 rather than four F32 registers. */
  bool half;
  /** Exact generated semantic identity selected by this source tuple. */
  InstructionKind kind;
};
constexpr SparseKindCase sparse_kind_cases[] = {
    {"e4m3", "e4m3", false,
     MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F32E4m3E4m3F32::
         kind},
    {"e3m2", "e2m3", false,
     MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F32E3m2E2m3F32::
         kind},
    {"e2m1", "e5m2", false,
     MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F32E2m1E5m2F32::
         kind},
    {"e5m2", "e2m1", true,
     MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F16E5m2E2m1F16::
         kind},
    {"e2m3", "e3m2", true,
     MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F16E2m3E3m2F16::
         kind}};

/** Borrow only actual mutable fields of one live exact generated owner. */
struct SparseKindFields {
  /** Static-lifetime generated descriptor; reacquired from each new owner. */
  const MatrixInstructionDescriptor& topology;
  /** Owned fragment payloads and per-lane source locations. */
  WithLocs<ResolvedRegisterVector>& dst;
  /** Borrowed A fragment and its source ranges. */
  WithLocs<ResolvedRegisterVector>& a;
  /** Borrowed B fragment and its source ranges. */
  WithLocs<ResolvedRegisterVector>& b;
  /** Borrowed C fragment and its source ranges. */
  WithLocs<ResolvedRegisterVector>& c;
  /** Bound 32-bit metadata register; its runtime contents remain opaque. */
  WithLocs<ResolvedRegisterRef>& metadata;
  /** Actual immediate selector and source/provenance cache. */
  WithLocs<ResolvedImmediate>& selector;
  /** Real mutable layout tag, independent of fixed kind and sparse order. */
  ResolvedOperandLayoutTag& layout;
};

/** Extract common storage only after matching one of the five concrete classes. */
SparseKindFields sparseKindFields(Instruction& instruction) {
  if (auto* form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F32E4m3E4m3F32*>(
          &instruction))
    return {*form->matrix_descriptor(),
            form->dst,
            form->a,
            form->b,
            form->c,
            form->metadata,
            form->selector,
            form->operand_layout};
  if (auto* form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F32E3m2E2m3F32*>(
          &instruction))
    return {*form->matrix_descriptor(),
            form->dst,
            form->a,
            form->b,
            form->c,
            form->metadata,
            form->selector,
            form->operand_layout};
  if (auto* form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F32E2m1E5m2F32*>(
          &instruction))
    return {*form->matrix_descriptor(),
            form->dst,
            form->a,
            form->b,
            form->c,
            form->metadata,
            form->selector,
            form->operand_layout};
  if (auto* form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F16E5m2E2m1F16*>(
          &instruction))
    return {*form->matrix_descriptor(),
            form->dst,
            form->a,
            form->b,
            form->c,
            form->metadata,
            form->selector,
            form->operand_layout};
  if (auto* form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindF8f6f4F16E2m3E3m2F16*>(
          &instruction))
    return {*form->matrix_descriptor(),
            form->dst,
            form->a,
            form->b,
            form->c,
            form->metadata,
            form->selector,
            form->operand_layout};
  throw std::logic_error("Unexpected sparse kind test owner.");
}

/** Spell the existing ordered sparse kind without source aliases or rewrites. */
std::string sparseKindInstruction(const SparseKindCase& item,
                                  unsigned selector = 0) {
  const std::string dc = item.half ? "f16" : "f32";
  return "mma.sp::ordered_metadata.sync.aligned.m16n8k64.row.col.kind::"
         "f8f6f4." +
         dc + "." + std::string(item.a) + "." + std::string(item.b) + "." + dc +
         " " + denseF64Pack('d', item.half ? 2 : 4) + "," +
         denseF64Pack('a', 4) + "," + denseF64Pack('b', 4) + "," +
         denseF64Pack('c', item.half ? 2 : 4) + ",%meta," +
         std::to_string(selector) + ";";
}

/** Initialize legal same-function carriers; no container contents are authenticated. */
std::string sparseKindModule(const SparseKindCase& item,
                             std::string_view instruction,
                             std::string_view target = "sm_120a") {
  std::string text = ".version 9.3\n.target " + std::string(target) +
                     "\n.address_size 64\n.entry kernel(){\n.reg .b32 "
                     "%a<4>,%b<4>,%meta,%alt;\n.reg ." +
                     std::string(item.half ? "f16x2" : "f32") +
                     " %d<4>,%c<4>;\n.reg .u16 %wrong;\nmov.b16 "
                     "%wrong,0;\nmov.b32 %alt,0;\nmov.b32 %meta,0;\n";
  for (const char role : std::string_view("abcd"))
    for (unsigned lane = 0; lane < 4; ++lane)
      text +=
          "mov.b32 %" + std::string(1, role) + std::to_string(lane) + ",0;\n";
  return text + std::string(instruction) + "\n}";
}

/** Use the actual qualified profile rather than fabricating target feature bits. */
checker::Context sparseKindContext(std::string_view target = "sm_120a") {
  const auto profile = base::find_target_profile(target);
  EXPECT_TRUE(profile);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Five source representatives preserve exact immutable kind, order and containers. */
TEST(WarpMatrixMmaCoverage, SparseKindCanonicalOwnedControls) {
  for (const auto& item : sparse_kind_cases) {
    SCOPED_TRACE(std::string(item.a) + "/" + std::string(item.b));
    const auto text = sparseKindModule(item, sparseKindInstruction(item));
    auto owned = ownedDenseF64(text);
    ASSERT_TRUE(owned) << owned.error().front().message;
    auto& body = owned->functions.front().body;
    auto& instruction = *body.back();
    auto fields = sparseKindFields(instruction);
    EXPECT_EQ(instruction.instruction_kind(), item.kind);
    EXPECT_EQ(fields.topology.kind, MatrixKind::F8F6F4);
    EXPECT_EQ(fields.topology.sparse_order, MatrixSparseOrder::ORDERED);
    EXPECT_EQ(fields.topology.shape, (MatrixShape{16, 8, 64}));
    EXPECT_EQ(fields.topology.a_layout, MatrixLayout::ROW);
    EXPECT_EQ(fields.topology.b_layout, MatrixLayout::COL);
    EXPECT_EQ(fields.topology.fragment_count, 4);
    const MatrixElementType expected_a[] = {
        MatrixElementType::E4M3, MatrixElementType::E3M2,
        MatrixElementType::E2M1, MatrixElementType::E5M2,
        MatrixElementType::E2M3};
    const MatrixElementType expected_b[] = {
        MatrixElementType::E4M3, MatrixElementType::E2M3,
        MatrixElementType::E5M2, MatrixElementType::E2M1,
        MatrixElementType::E3M2};
    const auto case_index = &item - sparse_kind_cases;
    EXPECT_EQ(fields.topology.fragments[1].element_type,
              expected_a[case_index]);
    EXPECT_EQ(fields.topology.fragments[2].element_type,
              expected_b[case_index]);
    EXPECT_EQ(fields.topology.fragments[0].element_type,
              item.half ? MatrixElementType::F16 : MatrixElementType::F32);
    EXPECT_EQ(fields.topology.fragments[3].element_type,
              fields.topology.fragments[0].element_type);
    EXPECT_EQ(fields.topology.fragments[0].register_count, item.half ? 2 : 4);
    EXPECT_EQ(fields.topology.fragments[1].register_count, 4);
    EXPECT_EQ(fields.topology.fragments[2].register_count, 4);
    EXPECT_EQ(fields.topology.fragments[3].register_count, item.half ? 2 : 4);
    EXPECT_EQ(fields.topology.fragments[0].register_type,
              item.half ? ScalarType::F16x2 : ScalarType::F32);
    EXPECT_EQ(fields.topology.fragments[1].register_type, ScalarType::B32);
    EXPECT_EQ(fields.topology.fragments[2].register_type, ScalarType::B32);
    EXPECT_EQ(fields.selector.value.bits, 0u);
    EXPECT_EQ(fields.selector.value.integer_source_bits, 0u);
    EXPECT_EQ(fields.metadata.value.declared_type, ScalarType::B32);
    ASSERT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto context = sparseKindContext();
    const auto narrow = dynamic_cast<MovScalar&>(*body[0]).dst_register->value;
    ASSERT_EQ(narrow.declared_type, ScalarType::U16);
    for (auto* fragment : {&fields.dst, &fields.a, &fields.b, &fields.c}) {
      const auto saved = *fragment;
      fragment->value.elements.pop_back();
      fragment->locs.pop_back();
      ASSERT_TRUE(
          valid_register_vector_source(fragment->value, fragment->locs, true));
      const auto count = instruction.check(context);
      ASSERT_FALSE(count);
      EXPECT_TRUE(std::ranges::any_of(count.error(), [&](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::InvalidVectorOperand &&
               d.range == saved.locs.front();
      }));
      EXPECT_FALSE(validateModule(
          *ast, *owned, ModuleValidationPolicy::RequireCompleteContext));
      *fragment = saved;
      fragment->value.elements.front() = narrow;
      const auto width = instruction.check(context);
      ASSERT_FALSE(width);
      EXPECT_TRUE(std::ranges::any_of(width.error(), [&](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::OperandTypeMismatch &&
               d.range == saved.locs.front();
      }));
      EXPECT_FALSE(validateModule(
          *ast, *owned, ModuleValidationPolicy::RequireCompleteContext));
      *fragment = saved;
      EXPECT_TRUE(validateModule(
          *owned, ModuleValidationPolicy::RequireCompleteContext));
    }
    const auto metadata = fields.metadata;
    fields.metadata.value.symbol_id = binding::SymbolId{.value = 999999u};
    const auto identity =
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(identity);
    EXPECT_TRUE(std::ranges::any_of(identity.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch &&
             d.message ==
                 "Resolved module operand has an out-of-range binding "
                 "identity.";
    }));
    fields.metadata = metadata;
    fields.metadata.value =
        dynamic_cast<MovScalar&>(*body[1]).dst_register->value;
    EXPECT_TRUE(instruction.check(context));
    EXPECT_TRUE(validateModule(*ast, *owned,
                               ModuleValidationPolicy::RequireCompleteContext));
    fields.metadata = metadata;
    const auto layout = fields.layout;
    fields.layout.value = 9999;
    const auto bad_layout = instruction.check(context);
    ASSERT_FALSE(bad_layout);
    EXPECT_EQ(bad_layout.error().back().kind,
              checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
    fields.layout = layout;
    EXPECT_TRUE(validateModule(*ast, *owned,
                               ModuleValidationPolicy::RequireCompleteContext));
    ResolvedModule copied = *owned;
    ResolvedModule assigned;
    assigned = copied;
    ResolvedModule moved = std::move(assigned);
    EXPECT_TRUE(
        validateModule(moved, ModuleValidationPolicy::RequireCompleteContext));
    auto copied_fields = sparseKindFields(*moved.functions.front().body.back());
    EXPECT_EQ(&copied_fields.topology, &fields.topology);
    EXPECT_EQ(copied_fields.metadata, fields.metadata);
    EXPECT_EQ(copied_fields.selector, fields.selector);
    auto clone = moved.functions.front().body.back()->clone();
    EXPECT_EQ(sparseKindFields(*clone).a, fields.a);
    EXPECT_TRUE(clone->check(context));
    ResolvedFunction function_copy = owned->functions.front();
    ResolvedFunction function_assigned;
    function_assigned = function_copy;
    EXPECT_EQ(sparseKindFields(*function_assigned.body.back()).dst, fields.dst);
    EXPECT_TRUE(function_assigned.body.back()->check(context));
    auto family = ownedDenseF64(
        sparseKindModule(item, sparseKindInstruction(item), "sm_120f"));
    ASSERT_TRUE(family) << family.error().front().message;
    const auto generic = test_helpers::parseModule(
        sparseKindModule(item, sparseKindInstruction(item), "sm_120"));
    ASSERT_MODULE_PARSE_SUCCEEDS(generic);
    EXPECT_FALSE(resolveModule(*generic));
  }
}

/** Capture the actual source stage for separator, kind and family negatives. */
TEST(WarpMatrixMmaCoverage, SparseKindRejectsMalformedAndUnrelatedSources) {
  const auto& item = sparse_kind_cases[1];
  const auto canonical = sparseKindInstruction(item);
  for (unsigned mutation = 0; mutation < 6; ++mutation) {
    auto instruction = canonical;
    switch (mutation) {
      case 0:
        instruction.replace(instruction.find(".col.kind"), 9, ".colkind");
        break;
      case 1:
        instruction.replace(instruction.find(".col.kind"), 9, ".col..kind");
        break;
      case 2:
        instruction.replace(instruction.find("kind::f8f6f4"), 12,
                            "kind::unknown");
        break;
      case 3:
        instruction.replace(instruction.find(".sp::ordered_metadata"), 21,
                            ".sp");
        break;
      case 4:
        instruction.replace(instruction.rfind(".f32"), 4, ".f16");
        break;
      case 5:
        instruction.replace(instruction.find("kind::f8f6f4"), 12,
                            "kind::mxf8f6f4");
        break;
    }
    const auto text = sparseKindModule(item, instruction);
    PtxCstParser parser(text);
    const auto cst = parser.parseModule();
    const auto ast = test_helpers::parseModule(text);
    if (mutation == 1) {
      ASSERT_TRUE(cst);
      ASSERT_TRUE(ast);
      ASSERT_FALSE(cst.diagnostics.empty());
      ASSERT_FALSE(ast.diagnostics.empty());
      EXPECT_EQ(ast.diagnostics.front().message, "expected operand");
      continue;
    }
    ASSERT_TRUE(cst && cst.diagnostics.empty());
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto resolved = resolveModule(*ast);
    ASSERT_FALSE(resolved);
    ASSERT_EQ(resolved.error().size(), 1u);
    const auto& diagnostic = resolved.error().front();
    EXPECT_EQ(diagnostic.stage(), ResolveDiagnosticStage::Resolution);
    if (mutation == 0 || mutation == 2) {
      const std::string token =
          mutation == 0 ? ".colkind::f8f6f4" : ".kind::unknown";
      EXPECT_EQ(diagnostic.message, "Unknown modifier '" + token + "'.");
      const auto line = static_cast<int32_t>(
          std::ranges::count(text.substr(0, text.rfind(instruction)), '\n') +
          1);
      const auto column = static_cast<int32_t>(instruction.find(token) + 1);
      EXPECT_EQ(
          diagnostic.range,
          (SourceRange{{line, column},
                       {line, column + static_cast<int32_t>(token.size())}}));
    } else {
      EXPECT_EQ(
          diagnostic.message,
          "No variant of instruction 'mma' accepts this modifier combination.");
    }
    auto canonical_owner = ownedDenseF64(sparseKindModule(item, canonical));
    ASSERT_TRUE(canonical_owner);
    const auto association = validateModule(
        *ast, *canonical_owner, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(association);
    EXPECT_TRUE(std::ranges::any_of(association.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
  }
}

/** Coherent selector bits reject by range independently of cached provenance. */
TEST(WarpMatrixMmaCoverage, SparseKindSelectorSourceAndOwned) {
  const auto& item = sparse_kind_cases[1];
  const auto text = sparseKindModule(item, sparseKindInstruction(item));
  auto owned = ownedDenseF64(text);
  ASSERT_TRUE(owned);
  auto& instruction = *owned->functions.front().body.back();
  auto fields = sparseKindFields(instruction);
  const auto saved = fields.selector;
  const auto context = sparseKindContext();
  const auto good_ast = test_helpers::parseModule(text);
  ASSERT_MODULE_PARSE_SUCCEEDS(good_ast);
  for (unsigned selector : {1u, 2u, 3u}) {
    const auto bad_ast = test_helpers::parseModule(
        sparseKindModule(item, sparseKindInstruction(item, selector)));
    ASSERT_MODULE_PARSE_SUCCEEDS(bad_ast);
    const auto source = resolveModule(*bad_ast);
    ASSERT_FALSE(source);
    EXPECT_EQ(source.error().front().stage(), ResolveDiagnosticStage::Checking);
    EXPECT_EQ(source.error().front().checker_kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(source.error().front().message,
              "Immediate operand 'selector' has value " +
                  std::to_string(selector) + " outside the supported range.");
    fields.selector.value.bits = selector;
    fields.selector.value.integer_source_bits = selector;
    const auto current = instruction.check(context);
    ASSERT_FALSE(current);
    EXPECT_EQ(current.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(current.error().front().range, saved.locs.front());
    EXPECT_EQ(current.error().front().message, source.error().front().message);
    const auto module =
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(module);
    EXPECT_TRUE(std::ranges::any_of(module.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ImmediateValueMismatch;
    }));
    const auto joined = validateModule(
        *good_ast, *owned, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(joined);
    EXPECT_TRUE(std::ranges::any_of(joined.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ImmediateValueMismatch;
    }));
    fields.selector = saved;
    EXPECT_TRUE(validateModule(*good_ast, *owned,
                               ModuleValidationPolicy::RequireCompleteContext));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
