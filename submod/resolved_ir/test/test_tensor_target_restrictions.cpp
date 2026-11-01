#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tensor_cache_controls.hpp>
#include <ptx_frontend/resolved_ir/ptx_tensor_map_known_facts.hpp>
#include <string>
#include <string_view>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** Canonical base features share the exact target restriction, not their shapes. */
constexpr std::array<std::string_view, 2> restricted_tensor_instructions{
    "cp.async.bulk.tensor.2d.shared::cluster.global.tile::gather4.mbarrier::"
    "complete_tx::bytes [dst], [tensor_map, {%r0,%r1,%r2,%r3,%r4}], [mbar];",
    "cp.async.bulk.tensor.3d.shared::cluster.global.im2col::w.mbarrier::"
    "complete_tx::bytes [dst], [tensor_map, {%r0,%r1,%r2}], [mbar];"};
/** Wrap a real aligned MAP object, shared storage/barrier, and typed coordinates. */
std::string tensor_target_source(std::string_view instruction,
                                 std::string_view target = "sm_100a",
                                 std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) +
         "\n.address_size 64\n.global .align 64 .b8 tensor_map[128];\n"
         ".shared .align 16 .b8 dst[1024];\n.shared .align 16 .b8 src[1024];\n"
         ".shared .align 8 .b64 mbar;\n.entry kernel(){\n.reg .s32 %r<5>;"
         ".reg .u16 %h<3>; .reg .b16 %mask; .reg .b64 %cache;\n"
         "mov.s32 %r0,0;mov.s32 %r1,1;mov.s32 %r2,2;mov.s32 %r3,3;mov.s32 "
         "%r4,4;"
         "mov.u16 %h0,1;mov.u16 %h1,1;mov.u16 %h2,1;mov.b16 %mask,1;mov.b64 "
         "%cache,0;\n" +
         std::string(instruction) + "\n}";
}
/** Use the catalog's exact flavor, family features and capabilities together. */
checker::Context tensor_target_context(std::string_view target,
                                       SourceRange range = {},
                                       checker::PtxVersion version = {9, 3}) {
  const auto profile = base::find_target_profile(target);
  EXPECT_TRUE(profile);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = version,
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
      .instruction_range = range};
}
/** Return a draft after source text, parser and syntax tree have actually died. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_tensor_target(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}
/** Require an actual availability failure, with no source/type/header masking. */
void expect_tensor_unavailable(const checker::CheckResult& result) {
  ASSERT_FALSE(result);
  ASSERT_FALSE(result.error().empty());
  for (const auto& diagnostic : result.error()) {
    EXPECT_EQ(diagnostic.kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability)
        << diagnostic.message;
    EXPECT_GT(diagnostic.range.start.line, 0);
    EXPECT_GT(diagnostic.range.start.column, 0);
  }
}
/** Keep owned header/function target caches coherent without changing bound operands. */
void retarget_tensor_module(ResolvedModule& module, std::string_view target) {
  size_t retargeted = 0;
  for (auto& region : module.header.regions) {
    // The initial pre-target region legitimately has no active target.
    if (region.target_options.empty())
      continue;
    region.target_options.front() = target;
    ++retargeted;
  }
  ASSERT_GT(retargeted, 0u);
  for (auto& function : module.functions) {
    ASSERT_TRUE(function.source_region);
    ASSERT_LT(*function.source_region, module.header.regions.size());
    ASSERT_FALSE(
        module.header.regions[*function.source_region].target_options.empty());
    EXPECT_EQ(
        module.header.regions[*function.source_region].target_options.front(),
        target);
    function.source_target = target;
  }
}
/** Validate each independent owner and instruction clone at its current exact target. */
void expect_tensor_lifetime(const ResolvedModule& module,
                            bool available = true) {
  const auto& function = module.functions.front();
  ASSERT_TRUE(function.source_target);
  const auto context = tensor_target_context(
      *function.source_target, function.instruction_ranges.back());
  auto direct = function.body.back()->check(context);
  auto owned = validateModule(module);
  if (available) {
    EXPECT_TRUE(direct);
    EXPECT_TRUE(owned);
  } else {
    expect_tensor_unavailable(direct);
    expect_tensor_unavailable(owned);
  }
  ResolvedModule copied(module), assigned;
  assigned = copied;
  if (available) {
    EXPECT_TRUE(validateModule(copied));
    EXPECT_TRUE(validateModule(assigned));
  } else {
    expect_tensor_unavailable(validateModule(copied));
    expect_tensor_unavailable(validateModule(assigned));
  }
  ResolvedFunction copy(function), copy_assigned;
  copy_assigned = copy;
  EXPECT_NE(copy.body.back().get(), function.body.back().get());
  auto function_result = copy_assigned.body.back()->check(context);
  if (available)
    EXPECT_TRUE(function_result);
  else
    expect_tensor_unavailable(function_result);
  assigned.functions.front() = copy_assigned;
  assigned.functions.front().body.back() = copy.body.back()->clone();
  if (available)
    EXPECT_TRUE(validateModule(assigned));
  else
    expect_tensor_unavailable(validateModule(assigned));
}
/** One fixed canonical family, retaining its written layout order and group domain. */
struct TensorTargetCase {
  /** Canonical name used for exact model/runtime joins. */
  std::string_view name;
  /** Existing generated class identity, with no new alternative. */
  InstructionKind kind;
  /** Tensor rank; gather4 retains five coordinates independently. */
  unsigned rank;
  /** Distinguishes gather4 from plain W, never W128. */
  bool gather;
  /** Whether the existing L2 hint variant is selected. */
  bool cache;
  /** Whether the existing multicast mask operand is present. */
  bool multicast;
  /** Whether written CTA group values one and two are required. */
  bool grouped;
  /** Existing layout names in order; empty trailing slots are unused. */
  std::array<std::string_view, 4> layouts;
};
constexpr std::array tensor_target_cases{
    TensorTargetCase{"cp_async_bulk_tensor_2d_shared_cluster_tile_gather4",
                     CpAsyncBulkTensor2dSharedClusterTileGather4::kind,
                     2,
                     true,
                     false,
                     false,
                     false,
                     {"default"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_cache_hint",
        CpAsyncBulkTensor2dSharedClusterTileGather4CacheHint::kind,
        2,
        true,
        true,
        false,
        false,
        {"without_policy", "with_policy"}},
    TensorTargetCase{"cp_async_bulk_tensor_3d_shared_cluster_im2col_w",
                     CpAsyncBulkTensor3dSharedClusterIm2colW::kind,
                     3,
                     false,
                     false,
                     false,
                     false,
                     {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_3d_shared_cluster_im2col_w_cache_hint",
        CpAsyncBulkTensor3dSharedClusterIm2colWCacheHint::kind,
        3,
        false,
        true,
        false,
        false,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{"cp_async_bulk_tensor_4d_shared_cluster_im2col_w",
                     CpAsyncBulkTensor4dSharedClusterIm2colW::kind,
                     4,
                     false,
                     false,
                     false,
                     false,
                     {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_4d_shared_cluster_im2col_w_cache_hint",
        CpAsyncBulkTensor4dSharedClusterIm2colWCacheHint::kind,
        4,
        false,
        true,
        false,
        false,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{"cp_async_bulk_tensor_5d_shared_cluster_im2col_w",
                     CpAsyncBulkTensor5dSharedClusterIm2colW::kind,
                     5,
                     false,
                     false,
                     false,
                     false,
                     {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_5d_shared_cluster_im2col_w_cache_hint",
        CpAsyncBulkTensor5dSharedClusterIm2colWCacheHint::kind,
        5,
        false,
        true,
        false,
        false,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_multicast",
        CpAsyncBulkTensor2dSharedClusterTileGather4Multicast::kind,
        2,
        true,
        false,
        true,
        false,
        {"default"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_multicast_cache_"
        "hint",
        CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCacheHint::kind,
        2,
        true,
        true,
        true,
        false,
        {"without_policy", "with_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_3d_shared_cluster_im2col_w_multicast",
        CpAsyncBulkTensor3dSharedClusterIm2colWMulticast::kind,
        3,
        false,
        false,
        true,
        false,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_3d_shared_cluster_im2col_w_multicast_cache_hint",
        CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCacheHint::kind,
        3,
        false,
        true,
        true,
        false,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_4d_shared_cluster_im2col_w_multicast",
        CpAsyncBulkTensor4dSharedClusterIm2colWMulticast::kind,
        4,
        false,
        false,
        true,
        false,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_4d_shared_cluster_im2col_w_multicast_cache_hint",
        CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCacheHint::kind,
        4,
        false,
        true,
        true,
        false,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_5d_shared_cluster_im2col_w_multicast",
        CpAsyncBulkTensor5dSharedClusterIm2colWMulticast::kind,
        5,
        false,
        false,
        true,
        false,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_5d_shared_cluster_im2col_w_multicast_cache_hint",
        CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCacheHint::kind,
        5,
        false,
        true,
        true,
        false,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_cta_group",
        CpAsyncBulkTensor2dSharedClusterTileGather4CtaGroup::kind,
        2,
        true,
        false,
        false,
        true,
        {"default"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_cta_group_cache_"
        "hint",
        CpAsyncBulkTensor2dSharedClusterTileGather4CtaGroupCacheHint::kind,
        2,
        true,
        true,
        false,
        true,
        {"without_policy", "with_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_3d_shared_cluster_im2col_w_cta_group",
        CpAsyncBulkTensor3dSharedClusterIm2colWCtaGroup::kind,
        3,
        false,
        false,
        false,
        true,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_3d_shared_cluster_im2col_w_cta_group_cache_hint",
        CpAsyncBulkTensor3dSharedClusterIm2colWCtaGroupCacheHint::kind,
        3,
        false,
        true,
        false,
        true,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_4d_shared_cluster_im2col_w_cta_group",
        CpAsyncBulkTensor4dSharedClusterIm2colWCtaGroup::kind,
        4,
        false,
        false,
        false,
        true,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_4d_shared_cluster_im2col_w_cta_group_cache_hint",
        CpAsyncBulkTensor4dSharedClusterIm2colWCtaGroupCacheHint::kind,
        4,
        false,
        true,
        false,
        true,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_5d_shared_cluster_im2col_w_cta_group",
        CpAsyncBulkTensor5dSharedClusterIm2colWCtaGroup::kind,
        5,
        false,
        false,
        false,
        true,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_5d_shared_cluster_im2col_w_cta_group_cache_hint",
        CpAsyncBulkTensor5dSharedClusterIm2colWCtaGroupCacheHint::kind,
        5,
        false,
        true,
        false,
        true,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_multicast_cta_"
        "group",
        CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCtaGroup::kind,
        2,
        true,
        false,
        true,
        true,
        {"default"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_multicast_cta_"
        "group_cache_hint",
        CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCtaGroupCacheHint::
            kind,
        2,
        true,
        true,
        true,
        true,
        {"without_policy", "with_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_3d_shared_cluster_im2col_w_multicast_cta_group",
        CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCtaGroup::kind,
        3,
        false,
        false,
        true,
        true,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_3d_shared_cluster_im2col_w_multicast_cta_group_"
        "cache_hint",
        CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCtaGroupCacheHint::kind,
        3,
        false,
        true,
        true,
        true,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_4d_shared_cluster_im2col_w_multicast_cta_group",
        CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCtaGroup::kind,
        4,
        false,
        false,
        true,
        true,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_4d_shared_cluster_im2col_w_multicast_cta_group_"
        "cache_hint",
        CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCtaGroupCacheHint::kind,
        4,
        false,
        true,
        true,
        true,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_5d_shared_cluster_im2col_w_multicast_cta_group",
        CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCtaGroup::kind,
        5,
        false,
        false,
        true,
        true,
        {"without_info", "with_info"}},
    TensorTargetCase{
        "cp_async_bulk_tensor_5d_shared_cluster_im2col_w_multicast_cta_group_"
        "cache_hint",
        CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCtaGroupCacheHint::kind,
        5,
        false,
        true,
        true,
        true,
        {"without_info", "without_info_policy", "with_info",
         "with_info_policy"}},
};
/** Read each actual selected layout tag from the bounded existing class set. */
uint32_t tensor_target_layout(const Instruction& instruction) {
  switch (instruction.instruction_kind()) {
    case CpAsyncBulkTensor2dSharedClusterTileGather4::kind:
      return dynamic_cast<const CpAsyncBulkTensor2dSharedClusterTileGather4&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor2dSharedClusterTileGather4CacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor2dSharedClusterTileGather4CacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor3dSharedClusterIm2colW::kind:
      return dynamic_cast<const CpAsyncBulkTensor3dSharedClusterIm2colW&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor3dSharedClusterIm2colWCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor3dSharedClusterIm2colWCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor4dSharedClusterIm2colW::kind:
      return dynamic_cast<const CpAsyncBulkTensor4dSharedClusterIm2colW&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor4dSharedClusterIm2colWCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor4dSharedClusterIm2colWCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor5dSharedClusterIm2colW::kind:
      return dynamic_cast<const CpAsyncBulkTensor5dSharedClusterIm2colW&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor5dSharedClusterIm2colWCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor5dSharedClusterIm2colWCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor2dSharedClusterTileGather4Multicast::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor2dSharedClusterTileGather4Multicast&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor3dSharedClusterIm2colWMulticast::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor3dSharedClusterIm2colWMulticast&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor4dSharedClusterIm2colWMulticast::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor4dSharedClusterIm2colWMulticast&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor5dSharedClusterIm2colWMulticast::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor5dSharedClusterIm2colWMulticast&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor2dSharedClusterTileGather4CtaGroup::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor2dSharedClusterTileGather4CtaGroup&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor2dSharedClusterTileGather4CtaGroupCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor2dSharedClusterTileGather4CtaGroupCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor3dSharedClusterIm2colWCtaGroup::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor3dSharedClusterIm2colWCtaGroup&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor3dSharedClusterIm2colWCtaGroupCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor3dSharedClusterIm2colWCtaGroupCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor4dSharedClusterIm2colWCtaGroup::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor4dSharedClusterIm2colWCtaGroup&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor4dSharedClusterIm2colWCtaGroupCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor4dSharedClusterIm2colWCtaGroupCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor5dSharedClusterIm2colWCtaGroup::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor5dSharedClusterIm2colWCtaGroup&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor5dSharedClusterIm2colWCtaGroupCacheHint::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor5dSharedClusterIm2colWCtaGroupCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCtaGroup::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCtaGroup&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCtaGroupCacheHint::
        kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCtaGroupCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCtaGroup::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCtaGroup&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCtaGroupCacheHint::
        kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCtaGroupCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCtaGroup::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCtaGroup&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCtaGroupCacheHint::
        kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCtaGroupCacheHint&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCtaGroup::kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCtaGroup&>(
                 instruction)
          .operand_layout.value;
    case CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCtaGroupCacheHint::
        kind:
      return dynamic_cast<
                 const CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCtaGroupCacheHint&>(
                 instruction)
          .operand_layout.value;
    default:
      ADD_FAILURE() << "Unexpected restricted tensor form";
      return 999;
  }
}
/** Spell one legal existing topology/layout without changing operand contracts. */
std::string tensor_topology_source(const TensorTargetCase& item,
                                   std::string_view layout, unsigned group) {
  std::string coordinates = "%r0";
  for (unsigned index = 1; index < (item.gather ? 5 : item.rank); ++index)
    coordinates += ",%r" + std::to_string(index);
  std::string text = "cp.async.bulk.tensor." + std::to_string(item.rank) +
                     "d.shared::cluster.global." +
                     (item.gather ? "tile::gather4" : "im2col::w") +
                     ".mbarrier::complete_tx::bytes";
  if (item.multicast)
    text += ".multicast::cluster";
  if (group)
    text += ".cta_group::" + std::to_string(group);
  if (item.cache)
    text += ".L2::cache_hint";
  text += " [dst],[tensor_map,{" + coordinates + "}],[mbar]";
  if (layout == "with_info" || layout == "with_info_policy")
    text += ",{%h0,%h1}";
  if (item.multicast)
    text += ",%mask";
  if (layout == "with_policy" || layout == "without_info_policy" ||
      layout == "with_info_policy")
    text += ",%cache";
  return text + ";";
}
TEST(TensorTargetRestrictions, RetainsMatchedExactAndFamilyControls) {
  for (const auto instruction : restricted_tensor_instructions)
    for (const auto target : {"sm_100a", "sm_100f", "sm_103a", "sm_103f"}) {
      SCOPED_TRACE(std::string(target) + " " + std::string(instruction));
      const auto ast =
          test_helpers::parseModule(tensor_target_source(instruction, target));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto strict = resolveAndValidateModule(*ast);
      ASSERT_TRUE(strict) << strict.error().front().message;
      EXPECT_TRUE(
          strict->functions.front().body.back()->check(tensor_target_context(
              target, strict->functions.front().instruction_ranges.back())));
    }
}
TEST(TensorTargetRestrictions,
     RejectsCrossFamilyProfilesWithUnchangedBoundOperands) {
  for (const auto instruction : restricted_tensor_instructions)
    for (const auto target : {"sm_110a", "sm_110f"}) {
      SCOPED_TRACE(std::string(target) + " " + std::string(instruction));
      const auto ast =
          test_helpers::parseModule(tensor_target_source(instruction, target));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto draft = resolveModuleOnly(*ast);
      ASSERT_TRUE(draft) << draft.error().front().message;
      const auto strict = resolveAndValidateModule(*ast);
      ASSERT_FALSE(strict);
      EXPECT_TRUE(
          std::ranges::all_of(strict.error(), [](const auto& diagnostic) {
            return diagnostic.checker_kind ==
                   checker::CheckDiagnosticKind::UnsupportedAvailability;
          }));
      const auto& function = draft->functions.front();
      const auto context =
          tensor_target_context(target, function.instruction_ranges.back());
      expect_tensor_unavailable(function.body.back()->check(context));
      expect_tensor_unavailable(validateModule(*ast, *draft));
      auto dead =
          owned_tensor_target(tensor_target_source(instruction, target));
      ASSERT_TRUE(dead);
      expect_tensor_unavailable(validateModule(*dead));
      ResolvedModule copied(*dead), assigned;
      assigned = copied;
      expect_tensor_unavailable(validateModule(copied));
      expect_tensor_unavailable(validateModule(assigned));
      auto projection =
          project_tensor_known_access_context(*function.body.back(), context);
      ASSERT_TRUE(projection.diagnostics.empty());
      ASSERT_TRUE(projection.access);
      EXPECT_FALSE(
          projection.access->selected_variant_available.value_or(true));
      const auto report = validate_tensor_access_facts(*projection.access,
                                                       TensorMapKnownFacts{});
      EXPECT_TRUE(std::ranges::any_of(report.outcomes, [](const auto& result) {
        return result.status == TensorKnownFactStatus::Unresolved;
      }));
      std::cout << "CELL " << target << " kind="
                << static_cast<unsigned>(
                       function.body.back()->instruction_kind())
                << " draft=1 strict=" << bool(strict) << " selected_available="
                << projection.access->selected_variant_available.value_or(false)
                << " optional_facts_unresolved=1\n";
    }
}
TEST(TensorTargetRestrictions, ClosesAllCurrentTargetAndVersionBoundaryCells) {
  constexpr std::array<std::string_view, 26> targets{
      "sm_50",   "sm_52",   "sm_53",  "sm_60",   "sm_70",   "sm_75",
      "sm_80",   "sm_86",   "sm_89",  "sm_90",   "sm_90a",  "sm_100",
      "sm_100a", "sm_100f", "sm_103", "sm_103a", "sm_103f", "sm_110",
      "sm_110a", "sm_110f", "sm_120", "sm_120a", "sm_120f", "sm_121",
      "sm_121a", "sm_121f"};
  size_t cells = 0;
  for (const auto text : restricted_tensor_instructions) {
    auto module = owned_tensor_target(tensor_target_source(text));
    ASSERT_TRUE(module);
    ASSERT_TRUE(validateModule(*module));
    const auto& function = module->functions.front();
    for (const auto target : targets) {
      SCOPED_TRACE(std::string(target));
      const bool allowed = target == "sm_100a" || target == "sm_100f" ||
                           target == "sm_103a" || target == "sm_103f";
      const auto context =
          tensor_target_context(target, function.instruction_ranges.back());
      const auto result = function.body.back()->check(context);
      if (allowed)
        EXPECT_TRUE(result);
      else
        expect_tensor_unavailable(result);
      auto projected =
          project_tensor_known_access_context(*function.body.back(), context);
      ASSERT_TRUE(projected.diagnostics.empty());
      ASSERT_TRUE(projected.access);
      EXPECT_EQ(projected.access->selected_variant_available, allowed);
      EXPECT_EQ(projected.access->target_identity->source_spelling, target);
      std::cout << "TARGET kind="
                << static_cast<unsigned>(
                       function.body.back()->instruction_kind())
                << " profile=" << target << " allowed=" << allowed << '\n';
      ++cells;
    }
  }
  EXPECT_EQ(cells, 52u);
  /** Instruction-unit contexts may precede a profile's own source-header floor. */
  struct Boundary {
    /** Exact catalog identity, never a numeric-SM substitute. */
    std::string_view target;
    /** Instruction-unit PTX threshold; a complete header may still be invalid. */
    checker::PtxVersion version;
    /** Expected instruction availability independent of header admissibility. */
    bool allowed;
  };
  constexpr std::array boundaries{
      Boundary{"sm_100a", {8, 5}, false}, Boundary{"sm_100a", {8, 6}, true},
      Boundary{"sm_100f", {8, 7}, false}, Boundary{"sm_100f", {8, 8}, true},
      Boundary{"sm_103a", {8, 6}, false}, Boundary{"sm_103a", {8, 8}, true},
      Boundary{"sm_103f", {8, 7}, false}, Boundary{"sm_103f", {8, 8}, true},
      Boundary{"sm_110a", {9, 0}, false}, Boundary{"sm_110f", {9, 0}, false},
      Boundary{"sm_110a", {9, 3}, false}, Boundary{"sm_110f", {9, 3}, false},
  };
  for (const auto text : restricted_tensor_instructions) {
    auto module = owned_tensor_target(tensor_target_source(text));
    ASSERT_TRUE(module);
    for (const auto& cell : boundaries) {
      const auto result =
          module->functions.front().body.back()->check(tensor_target_context(
              cell.target, module->functions.front().instruction_ranges.back(),
              cell.version));
      if (cell.allowed)
        EXPECT_TRUE(result);
      else
        expect_tensor_unavailable(result);
      const auto profile = base::find_target_profile(cell.target);
      ASSERT_TRUE(profile);
      const checker::PtxVersion minimum{profile->minimum_ptx_version.major,
                                        profile->minimum_ptx_version.minor};
      if (cell.version < minimum) {
        std::cout << "BOUNDARY profile=" << cell.target
                  << " version=" << cell.version.major << '.'
                  << cell.version.minor
                  << " source_header=N/A; synthetic_instruction_context\n";
        continue;
      }
      const auto version = std::to_string(cell.version.major) + "." +
                           std::to_string(cell.version.minor);
      const auto ast = test_helpers::parseModule(
          tensor_target_source(text, cell.target, version));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto strict = resolveAndValidateModule(*ast);
      EXPECT_EQ(bool(strict), cell.allowed);
      if (!cell.allowed && !strict)
        EXPECT_TRUE(
            std::ranges::all_of(strict.error(), [](const auto& diagnostic) {
              return diagnostic.checker_kind ==
                     checker::CheckDiagnosticKind::UnsupportedAvailability;
            }));
    }
  }
}
TEST(TensorTargetRestrictions,
     PropagatesEveryLayoutAndGroupThroughExistingTopologies) {
  size_t keys = 0;
  for (const auto& item : tensor_target_cases)
    for (size_t index = 0; index < item.layouts.size(); ++index) {
      const auto layout = item.layouts[index];
      if (layout.empty())
        continue;
      for (const unsigned group : {0u, 1u, 2u}) {
        if (item.grouped ? group == 0 : group != 0)
          continue;
        const auto text = tensor_topology_source(item, layout, group);
        SCOPED_TRACE(text);
        const auto ast = test_helpers::parseModule(tensor_target_source(text));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        ASSERT_TRUE(resolveAndValidateModule(*ast));
        auto owned = owned_tensor_target(tensor_target_source(text));
        ASSERT_TRUE(owned);
        auto& function = owned->functions.front();
        auto& instruction = *function.body.back();
        EXPECT_EQ(instruction.instruction_kind(), item.kind);
        EXPECT_EQ(tensor_target_layout(instruction), index);
        expect_tensor_lifetime(*owned);
        const auto context = tensor_target_context(
            "sm_100a", function.instruction_ranges.back());
        auto projected =
            project_tensor_known_access_context(instruction, context);
        ASSERT_TRUE(projected.diagnostics.empty());
        ASSERT_TRUE(projected.access);
        EXPECT_EQ(static_cast<unsigned>(projected.access->rank), item.rank);
        EXPECT_EQ(projected.access->coordinate_arity,
                  item.gather ? 5u : item.rank);
        EXPECT_EQ(projected.access->mode, item.gather
                                              ? TensorAccessMode::TileGather4
                                              : TensorAccessMode::Im2colW);
        const bool info = layout == "with_info" || layout == "with_info_policy";
        EXPECT_EQ(projected.access->info_arity, info ? 2u : 0u);
        if (group)
          EXPECT_EQ(projected.access->group,
                    group == 1 ? TensorCtaGroup::One : TensorCtaGroup::Two);
        const auto cache = query_tensor_cache_controls(instruction);
        EXPECT_TRUE(cache.diagnostics.empty());
        EXPECT_EQ(cache.hint.has_value(), item.cache);
        const bool policy = layout == "with_policy" ||
                            layout == "without_info_policy" ||
                            layout == "with_info_policy";
        EXPECT_EQ(cache.policy.has_value(), policy);
        for (const auto target : {"sm_110a", "sm_110f"}) {
          retarget_tensor_module(*owned, target);
          expect_tensor_lifetime(*owned, false);
          auto negative = project_tensor_known_access_context(
              instruction, tensor_target_context(
                               target, function.instruction_ranges.back()));
          ASSERT_TRUE(negative.diagnostics.empty());
          ASSERT_TRUE(negative.access);
          EXPECT_EQ(negative.access->selected_variant_available, false);
          EXPECT_EQ(negative.access->rank, projected.access->rank);
          EXPECT_EQ(negative.access->coordinate_arity,
                    projected.access->coordinate_arity);
        }
        retarget_tensor_module(*owned, "sm_100a");
        expect_tensor_lifetime(*owned);
        std::cout << "LAYOUT " << item.name << " layout=" << layout
                  << " group=" << group << '\n';
        ++keys;
      }
    }
  EXPECT_EQ(keys, 126u);
}
TEST(TensorTargetRestrictions,
     KeepsAdjacentGrantsAndSeparatesTargetFromSourceIdentity) {
  constexpr std::array<std::string_view, 4> adjacent{
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col::w::128.mbarrier::"
      "complete_tx::bytes [dst],[tensor_map,{%r0,%r1,%r2}],[mbar];",
      "cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4.bulk_group "
      "[tensor_map,{%r0,%r1,%r2,%r3,%r4}],[src];",
      "cp.async.bulk.tensor.1d.shared::cluster.global.tile.mbarrier::complete_"
      "tx::bytes.cta_group::2 [dst],[tensor_map,{%r0}],[mbar];",
      "cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4.mbarrier::"
      "complete_tx::bytes [dst],[tensor_map,{%r0,%r1,%r2,%r3,%r4}],[mbar];"};
  for (const auto text : adjacent)
    for (const auto target : {"sm_110a", "sm_110f"}) {
      const auto ast =
          test_helpers::parseModule(tensor_target_source(text, target));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      ASSERT_TRUE(resolveAndValidateModule(*ast));
    }
  for (const auto target : {"sm_100", "sm_110"})
    for (const auto text :
         {"cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4.mbarrier::"
          "complete_tx::bytes [dst],[tensor_map,{%r0,%r1,%r2,%r3,%r4}],[mbar];",
          "cp.async.bulk.tensor.3d.shared::cta.global.im2col::w.mbarrier::"
          "complete_tx::bytes [dst],[tensor_map,{%r0,%r1,%r2}],[mbar];"}) {
      const auto ast =
          test_helpers::parseModule(tensor_target_source(text, target));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      ASSERT_TRUE(resolveAndValidateModule(*ast));
    }
  for (const auto text : restricted_tensor_instructions) {
    auto module = owned_tensor_target(tensor_target_source(text));
    ASSERT_TRUE(module);
    const auto changed_target =
        test_helpers::parseModule(tensor_target_source(text, "sm_110f"));
    ASSERT_MODULE_PARSE_SUCCEEDS(changed_target);
    expect_tensor_unavailable(validateModule(*changed_target, *module));
    auto changed = std::string(text);
    changed.replace(changed.find("%r1"), 3, "%r0");
    const auto changed_ast =
        test_helpers::parseModule(tensor_target_source(changed));
    ASSERT_MODULE_PARSE_SUCCEEDS(changed_ast);
    ASSERT_TRUE(resolveAndValidateModule(*changed_ast));
    const auto mismatch = validateModule(*changed_ast, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
