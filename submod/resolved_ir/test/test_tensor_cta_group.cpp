#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse and resolve before returning, so callers only own the released IR. */
std::optional<ResolvedModule> owned_grouped(std::string_view source,
                                            std::string_view target = "sm_110a",
                                            std::string_view version = "9.3") {
  const std::string text =
      ".version " + std::string(version) + "\n.target " + std::string(target) +
      "\n.address_size 64\n"
      ".global .align 64 .b8 tensor_map[128];\n"
      ".shared .align 16 .b8 dst[1024];\n"
      ".shared .align 8 .b64 mbar;\n"
      ".entry kernel() {\n"
      ".reg .s32 %r<5>;\n.reg .u16 %h<3>;\n.reg .b16 %mask;\n" +
      std::string(source) + "\n}\n";
  const auto parsed = test_helpers::parseModule(text);
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Resolve catalog features, including exact and family targets. */
checker::Context group_context(std::string_view target = "sm_110a",
                               checker::PtxVersion version = {9, 3}) {
  const auto profile = base::find_target_profile(target);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = version,
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Construct one of the five approved load modes without new operand roles. */
std::string group_copy(int rank, std::string_view mode,
                       std::string_view destination, bool multicast, bool info,
                       int group, bool reversed = false) {
  std::string coords = "%r0";
  for (int index = 1; index < (mode == "tile::gather4" ? 5 : rank); ++index)
    coords += ", %r" + std::to_string(index);
  std::string result = "cp.async.bulk.tensor." + std::to_string(rank) +
                       "d.shared::" + std::string(destination) + ".global." +
                       std::string(mode) + ".mbarrier::complete_tx::bytes";
  const std::string group_suffix = ".cta_group::" + std::to_string(group);
  if (multicast && reversed)
    result += group_suffix + ".multicast::cluster";
  else {
    if (multicast)
      result += ".multicast::cluster";
    result += group_suffix;
  }
  result += " [dst], [tensor_map, {" + coords + "}], [mbar]";
  if (info) {
    const int count = mode == "im2col" ? rank - 2 : 2;
    result += ", {%h0";
    for (int index = 1; index < count; ++index)
      result += ", %h" + std::to_string(index);
    result += "}";
  }
  if (multicast)
    result += ", %mask";
  return result + ";";
}

/** Every source group value shares one typed variant for each eligible base. */
TEST(TensorCtaGroup, AllModesLayoutsValuesAndAliasAfterAstRelease) {
  std::set<InstructionKind> alternatives;
  int layout_groups = 0;
  for (const auto destination : {"cta", "cluster"}) {
    for (const bool multicast : {false, true}) {
      if (multicast && destination == std::string_view("cta"))
        continue;
      for (int rank = 1; rank <= 5; ++rank) {
        for (const auto mode : {"tile", "tile::gather4", "im2col", "im2col::w",
                                "im2col::w::128"}) {
          if ((mode == std::string_view("tile::gather4") && rank != 2) ||
              (mode != std::string_view("tile") &&
               mode != std::string_view("tile::gather4") && rank < 3))
            continue;
          const bool im2col = mode != std::string_view("tile") &&
                              mode != std::string_view("tile::gather4");
          for (const bool info : {false, true}) {
            if (!im2col && info)
              continue;
            for (const int group : {1, 2}) {
              for (const bool alias : {false, true}) {
                if (alias && !multicast)
                  continue;
                const auto text = group_copy(rank, mode, destination, multicast,
                                             info, group, alias);
                auto owned = owned_grouped(text);
                ASSERT_TRUE(owned) << text;
                auto& copy = owned->functions.front().body.front();
                alternatives.insert(copy->instruction_kind());
                EXPECT_TRUE(copy->check(group_context())) << text;
                EXPECT_TRUE(validateModule(
                    *owned, ModuleValidationPolicy::RequireCompleteContext))
                    << text;
                bool inspected = false;
                auto inspect = [&](const auto& selected) {
                  inspected = true;
                  const auto role = selected.tensor_cta_group_role();
                  ASSERT_TRUE(role);
                  ASSERT_TRUE(role->spelled);
                  EXPECT_EQ(role->spelled->locs.size(), 1u);
                  EXPECT_EQ(role->effective, group == 1 ? TensorCtaGroup::One
                                                        : TensorCtaGroup::Two);
                  EXPECT_EQ(selected.cta_group.value, role->effective);
                  EXPECT_EQ(
                      role->routing,
                      multicast
                          ? (group == 1
                                 ? TensorCtaSignalRouting::MulticastDestinations
                                 : TensorCtaSignalRouting::MulticastParityPeers)
                          : (group == 1
                                 ? TensorCtaSignalRouting::Destination
                                 : TensorCtaSignalRouting::DestinationOrPeer));
                };
#define PTX_TRY_GROUP(CLASS)                                         \
  if (const auto* selected = dynamic_cast<const CLASS*>(copy.get())) \
  inspect(*selected)
                PTX_TRY_GROUP(CpAsyncBulkTensor1dSharedClusterCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor2dSharedClusterCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor2dSharedClusterTileGather4CtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor3dSharedClusterCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor4dSharedClusterCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor5dSharedClusterCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor1dSharedCtaCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor2dSharedCtaCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor2dSharedCtaTileGather4CtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor3dSharedCtaCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor4dSharedCtaCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor5dSharedCtaCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor3dSharedClusterIm2colCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor3dSharedClusterIm2colWCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor3dSharedClusterIm2colW128CtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor4dSharedClusterIm2colCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor4dSharedClusterIm2colWCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor4dSharedClusterIm2colW128CtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor5dSharedClusterIm2colCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor5dSharedClusterIm2colWCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor5dSharedClusterIm2colW128CtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor3dSharedCtaIm2colCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor3dSharedCtaIm2colWCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor3dSharedCtaIm2colW128CtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor4dSharedCtaIm2colCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor4dSharedCtaIm2colWCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor4dSharedCtaIm2colW128CtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor5dSharedCtaIm2colCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor5dSharedCtaIm2colWCtaGroup);
                PTX_TRY_GROUP(CpAsyncBulkTensor5dSharedCtaIm2colW128CtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor1dSharedClusterMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor2dSharedClusterMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor2dSharedClusterTileGather4MulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor3dSharedClusterMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor4dSharedClusterMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor5dSharedClusterMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor3dSharedClusterIm2colMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor3dSharedClusterIm2colWMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor3dSharedClusterIm2colW128MulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor4dSharedClusterIm2colMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor4dSharedClusterIm2colWMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor4dSharedClusterIm2colW128MulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor5dSharedClusterIm2colMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor5dSharedClusterIm2colWMulticastCtaGroup);
                PTX_TRY_GROUP(
                    CpAsyncBulkTensor5dSharedClusterIm2colW128MulticastCtaGroup);
#undef PTX_TRY_GROUP
                EXPECT_TRUE(inspected) << text;
                if (!alias)
                  ++layout_groups;
              }
            }
          }
        }
      }
    }
  }
  EXPECT_EQ(alternatives.size(), 45u);
  EXPECT_EQ(layout_groups, 144);
}

/** Omitted and explicit one agree semantically but retain different provenance. */
TEST(TensorCtaGroup, OmissionAndMixedGroupsRemainFunctionLocal) {
  const auto one = group_copy(1, "tile", "cta", false, false, 1);
  const auto two = group_copy(1, "tile", "cta", false, false, 2);
  const std::string omitted =
      "cp.async.bulk.tensor.1d.shared::cta.global.tile."
      "mbarrier::complete_tx::bytes [dst], [tensor_map, {%r0}], [mbar];";
  auto owned = owned_grouped(omitted + "\n" + one + "\n" + two);
  ASSERT_TRUE(owned);
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  ASSERT_EQ(owned->functions.front().body.size(), 3u);
  for (int index = 0; index < 3; ++index) {
    const auto& copy = owned->functions.front().body[index];
    auto inspect = [&](const auto& selected) {
      const auto role = selected.tensor_cta_group_role();
      ASSERT_TRUE(role);
      EXPECT_EQ(role->spelled.has_value(), index != 0);
      EXPECT_EQ(role->effective,
                index == 2 ? TensorCtaGroup::Two : TensorCtaGroup::One);
    };
    if (index == 0) {
      const auto* selected =
          dynamic_cast<const CpAsyncBulkTensor1dSharedCta*>(copy.get());
      ASSERT_NE(selected, nullptr);
      inspect(*selected);
    } else {
      const auto* selected =
          dynamic_cast<const CpAsyncBulkTensor1dSharedCtaCtaGroup*>(copy.get());
      ASSERT_NE(selected, nullptr);
      inspect(*selected);
    }
  }
}

/** Static target constraints intersect the base load's mode and topology. */
TEST(TensorCtaGroup, QualifiedTargetsAndExcludedProfiles) {
  struct Case {
    std::string_view target;
    std::string_view source_version;
    checker::PtxVersion version;
    bool accepted;
  };
  constexpr std::array cases{
      Case{"sm_100a", "8.6", {8, 6}, true},
      Case{"sm_100f", "8.8", {8, 8}, true},
      Case{"sm_103a", "8.8", {8, 8}, true},
      Case{"sm_103f", "8.8", {8, 8}, true},
      Case{"sm_110a", "9.0", {9, 0}, true},
      Case{"sm_110f", "9.0", {9, 0}, true},
      Case{"sm_100", "8.6", {8, 6}, false},
      Case{"sm_103", "8.8", {8, 8}, false},
      Case{"sm_110", "9.0", {9, 0}, false},
      Case{"sm_120a", "9.0", {9, 0}, false},
  };
  for (const auto& item : cases) {
    auto owned = owned_grouped(group_copy(1, "tile", "cta", false, false, 2),
                               item.target, item.source_version);
    ASSERT_TRUE(owned) << item.target;
    auto& copy = owned->functions.front().body.front();
    EXPECT_EQ(copy->check(group_context(item.target, item.version)).has_value(),
              item.accepted)
        << item.target;
    EXPECT_EQ(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value(),
        item.accepted)
        << item.target;
  }
}

/** Damaged enum, missing token, and pointer class fail both owned checks. */
TEST(TensorCtaGroup, ReleasedAstMutationsRejectAndRestore) {
  auto owned = owned_grouped(group_copy(1, "tile", "cta", false, false, 2));
  ASSERT_TRUE(owned);
  auto& copy = owned->functions.front().body.front();
  auto direct = [&] {
    return copy->check(group_context());
  };
  auto module = [&] {
    return validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext);
  };
  ASSERT_TRUE(direct());
  ASSERT_TRUE(module());
  auto* selected =
      dynamic_cast<CpAsyncBulkTensor1dSharedCtaCtaGroup*>(copy.get());
  ASSERT_NE(selected, nullptr);
  {
    const auto saved_group = selected->cta_group;
    selected->cta_group.value = static_cast<TensorCtaGroup>(3);
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    selected->cta_group = saved_group;
    selected->cta_group.locs.clear();
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    selected->cta_group = saved_group;
    for (auto* address : {&selected->tensor.value.tensor_map.address,
                          &selected->dst.value, &selected->mbar.value}) {
      const auto base = address->base;
      address->base =
          ResolvedRegisterRef{.spelling = "%bad",
                              .register_class = ResolvedRegisterClass::General,
                              .declared_type = ScalarType::B16};
      EXPECT_FALSE(direct());
      EXPECT_FALSE(module());
      address->base = base;
    }
  }
  EXPECT_TRUE(direct());
  EXPECT_TRUE(module());
}

/** Grouped multicast keeps mask, info, coordinate, and layout checks active. */
TEST(TensorCtaGroup, ReleasedAstCompositeMutationsRejectAndRestore) {
  auto owned = owned_grouped(group_copy(3, "im2col", "cluster", true, true, 2));
  ASSERT_TRUE(owned);
  auto& copy = owned->functions.front().body.front();
  auto direct = [&] {
    return copy->check(group_context());
  };
  auto module = [&] {
    return validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext);
  };
  ASSERT_TRUE(direct());
  ASSERT_TRUE(module());
  auto* selected =
      dynamic_cast<CpAsyncBulkTensor3dSharedClusterIm2colMulticastCtaGroup*>(
          copy.get());
  ASSERT_NE(selected, nullptr);
  {
    const auto saved_layout = selected->operand_layout;
    selected->operand_layout.value = 0;
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    selected->operand_layout = saved_layout;
    bool inspected = false;
    auto mutate_payload = [&](auto& payload) {
      if constexpr (requires {
                      payload.im2col_info;
                      payload.cta_mask;
                      payload.tensor;
                    }) {
        inspected = true;
        auto* mask = std::get_if<ResolvedRegisterRef>(&payload.cta_mask.value);
        ASSERT_NE(mask, nullptr);
        const auto saved_mask = *mask;
        mask->vector_width = 2;
        EXPECT_FALSE(direct());
        EXPECT_FALSE(module());
        *mask = saved_mask;
        RegOrImm* info_ptr = nullptr;
        if constexpr (requires { payload.im2col_info.has_value(); }) {
          ASSERT_TRUE(payload.im2col_info);
          info_ptr = &payload.im2col_info->value.elements.front();
        } else {
          info_ptr = &payload.im2col_info.value.elements.front();
        }
        auto& info = *info_ptr;
        const auto saved_info = info;
        info = ResolvedImmediate{.bits = 1,
                                 .type = ScalarType::U16,
                                 .integer_source_bits = uint64_t{65536}};
        EXPECT_FALSE(direct());
        EXPECT_FALSE(module());
        info = saved_info;
        auto& coordinate = std::get<ResolvedRegisterRef>(
            payload.tensor.value.coordinates.elements.front());
        const auto saved_coordinate = coordinate;
        ASSERT_TRUE(coordinate.symbol_id);
        auto reject_coordinate = [&] {
          EXPECT_FALSE(direct());
          EXPECT_FALSE(module());
        };
        coordinate.vector_width = 2;
        reject_coordinate();
        coordinate = saved_coordinate;
        coordinate.register_class = ResolvedRegisterClass::Predicate;
        reject_coordinate();
        coordinate = saved_coordinate;
        coordinate.declared_type = ScalarType::B16;
        reject_coordinate();
        coordinate = saved_coordinate;
        coordinate.declared_type.reset();
        reject_coordinate();
        coordinate = saved_coordinate;
        EXPECT_TRUE(direct());
        EXPECT_TRUE(module());
        coordinate.symbol_id.reset();
        coordinate.declared_type.reset();
        EXPECT_TRUE(direct());
        coordinate = saved_coordinate;
      }
    };
    auto visit_payload = [&](auto& form) {
      if constexpr (requires { form.operands; })
        std::visit(mutate_payload, form.operands);
      else
        mutate_payload(form);
    };
    visit_payload(*selected);
    EXPECT_TRUE(inspected);
  }
  EXPECT_TRUE(direct());
  EXPECT_TRUE(module());
}

/** Unsupported CTA-group values and nonload placements fail selection. */
TEST(TensorCtaGroup, AdjacentInvalidSpellings) {
  const auto valid = group_copy(1, "tile", "cluster", true, false, 1);
  for (const auto bad : {"cta_group::0", "cta_group::3"}) {
    auto source = valid;
    source.replace(source.find("cta_group::1"), 12, bad);
    EXPECT_FALSE(owned_grouped(source));
  }
  auto duplicate = valid;
  duplicate.replace(duplicate.find(".cta_group::1"), 13,
                    ".cta_group::1.cta_group::2");
  EXPECT_FALSE(owned_grouped(duplicate));
  auto missing = valid;
  missing.erase(missing.find(".mbarrier::complete_tx::bytes"), 29);
  EXPECT_FALSE(owned_grouped(missing));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
