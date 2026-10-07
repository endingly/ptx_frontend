#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Own a parsed tensor copy after its syntax tree has been destroyed. */
std::optional<ResolvedModule> owned_multicast(
    std::string_view copy, std::string_view target = "sm_110a",
    std::string_view version = "9.3") {
  const std::string text = ".version " + std::string(version) + "\n.target " +
                           std::string(target) +
                           "\n.address_size 64\n"
                           ".global .align 64 .b8 tensor_map[128];\n"
                           ".shared .align 16 .b8 dst[1024];\n"
                           ".shared .align 8 .b64 mbar;\n"
                           ".entry kernel() {\n"
                           ".reg .s32 %r<5>;\n.reg .u16 %h<3>;\n"
                           ".reg .b16 %mask;\n.reg .u16 %umask;\n"
                           ".reg .s16 %smask;\n.reg .b32 %bad32;\n"
                           ".reg .b64 %bad64;\n" +
                           std::string(copy) + "\n}\n";
  const auto parsed = test_helpers::parseModule(text);
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Use the exact target catalog instead of a numeric-SM approximation. */
checker::Context multicast_context(std::string_view target = "sm_110a",
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

/** Build source spelling from mode and tensor rank without inferring metadata. */
std::string multicast_copy(int rank, std::string_view mode, bool info,
                           std::string_view mask = "%mask") {
  std::string coords = "%r0";
  for (int index = 1; index < (mode == "tile::gather4" ? 5 : rank); ++index)
    coords += ", %r" + std::to_string(index);
  std::string result =
      "cp.async.bulk.tensor." + std::to_string(rank) +
      "d.shared::cluster.global." + std::string(mode) +
      ".mbarrier::complete_tx::bytes.multicast::cluster [dst], "
      "[tensor_map, {" +
      coords + "}], [mbar]";
  if (info) {
    const int count = mode == "im2col" ? rank - 2 : 2;
    result += ", {%h0";
    for (int index = 1; index < count; ++index)
      result += ", %h" + std::to_string(index);
    result += "}";
  }
  return result + ", " + std::string(mask) + ";";
}

/** All fifteen identities preserve the parent's twenty-four info layouts. */
TEST(TensorMulticast, AllModesRanksAndLayoutsOwnMask) {
  std::set<InstructionKind> alternatives;
  for (int rank = 1; rank <= 5; ++rank) {
    for (const auto mode :
         {"tile", "tile::gather4", "im2col", "im2col::w", "im2col::w::128"}) {
      if ((mode == std::string_view("tile::gather4") && rank != 2) ||
          (mode != std::string_view("tile") &&
           mode != std::string_view("tile::gather4") && rank < 3))
        continue;
      const bool im2col = mode != std::string_view("tile") &&
                          mode != std::string_view("tile::gather4");
      for (const bool info : {false, true}) {
        if (!im2col && info)
          continue;
        const auto text = multicast_copy(rank, mode, info);
        auto owned = owned_multicast(text);
        ASSERT_TRUE(owned) << text;
        const auto& copy = owned->functions.front().body.front();
        alternatives.insert(copy->instruction_kind());
        std::string variant_name =
            "AsyncBulkTensor" + std::to_string(rank) + "dSharedCluster";
        if (mode == std::string_view("tile"))
          variant_name += "Multicast";
        else if (mode == std::string_view("tile::gather4"))
          variant_name += "TileGather4Multicast";
        else if (mode == std::string_view("im2col"))
          variant_name += "Im2colMulticast";
        else if (mode == std::string_view("im2col::w"))
          variant_name += "Im2colWMulticast";
        else
          variant_name += "Im2colW128Multicast";
        const auto& variants = cp_resolved_descriptor().variants;
        const auto descriptor =
            std::ranges::find_if(variants, [&](const auto& item) {
              return item.variant_name == variant_name;
            });
        ASSERT_NE(descriptor, variants.end());
        EXPECT_EQ(descriptor->operand_layouts.back()
                      .bindings.back()
                      .tensor_cta_mask_role,
                  TensorCtaMaskRole::MulticastCluster);
        EXPECT_TRUE(copy->check(multicast_context())) << text;
        EXPECT_TRUE(validateModule(
            *owned, ModuleValidationPolicy::RequireCompleteContext))
            << text;
        class MaskObserver final : public detail::IReferenceObserver {
         public:
          /** Record the borrowed multicast mask and its source location. */
          void reg_or_imm(const RegOrImm& value,
                          std::span<const SourceRange> locations,
                          checker::AddressSymbolResolutionPolicy) override {
            seen = true;
            EXPECT_EQ(locations.size(), 1u);
            EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(value));
          }
          /** Whether the direct form exposed a borrowed mask. */
          bool seen = false;
        } observer;
        copy->visit_references(observer);
        EXPECT_TRUE(observer.seen);
      }
    }
  }
  EXPECT_EQ(alternatives.size(), 15u);
}

/** Multicast inherits each base mode's catalog floor without an advice gate. */
TEST(TensorMulticast, ModeAndTargetIntersections) {
  struct Case {
    std::string_view mode;
    int rank;
    std::string_view version;
    checker::PtxVersion parsed_version;
    std::string_view target;
    bool accepted;
  };
  constexpr std::array cases{
      Case{"tile", 1, "8.0", {8, 0}, "sm_90", true},
      Case{"tile", 1, "8.6", {8, 6}, "sm_100", true},
      Case{"tile::gather4", 2, "8.6", {8, 6}, "sm_100a", true},
      Case{"tile::gather4", 2, "8.6", {8, 6}, "sm_100", false},
      Case{"tile::gather4", 2, "8.8", {8, 8}, "sm_103f", true},
      Case{"tile::gather4", 2, "9.0", {9, 0}, "sm_110a", true},
      Case{"tile::gather4", 2, "9.0", {9, 0}, "sm_110", false},
      Case{"im2col", 3, "8.0", {8, 0}, "sm_90", true},
      Case{"im2col::w", 3, "8.6", {8, 6}, "sm_100a", true},
      Case{"im2col::w", 3, "8.6", {8, 6}, "sm_100", false},
      Case{"im2col::w", 3, "9.0", {9, 0}, "sm_110a", true},
      Case{"im2col::w::128", 3, "8.8", {8, 8}, "sm_103f", true},
      Case{"im2col::w::128", 3, "9.0", {9, 0}, "sm_120a", false},
  };
  for (const auto& item : cases) {
    const auto text =
        multicast_copy(item.rank, item.mode, item.mode.starts_with("im2col"));
    auto owned = owned_multicast(text, item.target, item.version);
    ASSERT_TRUE(owned) << text << " / " << item.target;
    const auto& copy = owned->functions.front().body.front();
    EXPECT_EQ(copy->check(multicast_context(item.target, item.parsed_version))
                  .has_value(),
              item.accepted)
        << text << " / " << item.target;
    EXPECT_EQ(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value(),
        item.accepted)
        << text << " / " << item.target;
  }
}

/** Direct literals narrow at use; declared 16-bit integer/bit carriers work. */
TEST(TensorMulticast, SourcesAndCoupling) {
  const auto base = multicast_copy(1, "tile", false);
  for (const auto source : {"%mask", "%umask", "%smask", "0", "1", "65535",
                            "-1", "65536", "65537"}) {
    const auto text = multicast_copy(1, "tile", false, source);
    auto owned = owned_multicast(text, "sm_90");
    ASSERT_TRUE(owned) << text;
    const auto& copy = owned->functions.front().body.front();
    EXPECT_TRUE(copy->check(multicast_context("sm_90"))) << text;
  }
  for (const auto source : {"%bad32", "%bad64"}) {
    auto owned = owned_multicast(multicast_copy(1, "tile", false, source));
    if (!owned)
      continue;
    const auto& copy = owned->functions.front().body.front();
    EXPECT_FALSE(copy->check(multicast_context()));
  }
  std::string missing = base;
  missing.replace(missing.find(", %mask;"), 8, ";");
  EXPECT_FALSE(owned_multicast(missing));
  std::string no_qualifier = base;
  no_qualifier.erase(no_qualifier.find(".multicast::cluster"),
                     std::string(".multicast::cluster").size());
  EXPECT_FALSE(owned_multicast(no_qualifier));
}

/** A register mask contributes one owned reference that a literal lacks. */
TEST(TensorMulticast, MaskReferenceCollectorRetainsSource) {
  auto register_form = owned_multicast(multicast_copy(1, "tile", false));
  auto literal_form = owned_multicast(multicast_copy(1, "tile", false, "1"));
  ASSERT_TRUE(register_form);
  ASSERT_TRUE(literal_form);
  auto count = [](ResolvedModule& owned) {
    class ReferenceCounter final : public detail::IReferenceObserver {
     public:
      /** Count register-valued references with owned source locations. */
      void reg_or_imm(const RegOrImm& value,
                      std::span<const SourceRange> locations,
                      checker::AddressSymbolResolutionPolicy) override {
        if (!locations.empty() &&
            std::holds_alternative<ResolvedRegisterRef>(value))
          ++result;
      }
      /** Number of register references observed. */
      int result = 0;
    } observer;
    owned.functions.front().body.front()->visit_references(observer);
    return observer.result;
  };
  EXPECT_EQ(count(*register_form), 1);
  EXPECT_EQ(count(*literal_form), 0);
}

/** Owned mask metadata cannot be changed after the AST has been released. */
TEST(TensorMulticast, ReleasedAstMaskMutationsFailDirectAndModule) {
  auto owned = owned_multicast(multicast_copy(1, "tile", false));
  ASSERT_TRUE(owned);
  auto& copy = owned->functions.front().body.front();
  auto* selected =
      dynamic_cast<CpAsyncBulkTensor1dSharedClusterMulticast*>(copy.get());
  ASSERT_NE(selected, nullptr);
  auto direct = [&] {
    return copy->check(multicast_context());
  };
  auto module = [&] {
    return validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext);
  };
  ASSERT_TRUE(direct());
  ASSERT_TRUE(module());
  {
    auto& mask = selected->cta_mask.value;
    auto* reg = std::get_if<ResolvedRegisterRef>(&mask);
    ASSERT_NE(reg, nullptr);
    const auto saved = *reg;
    reg->vector_width = 2;
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *reg = saved;
    reg->declared_type = ScalarType::B64;
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *reg = saved;
    reg->declared_type = ScalarType::B32;
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *reg = saved;
    reg->register_class = ResolvedRegisterClass::Predicate;
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *reg = saved;
    reg->declared_type.reset();
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *reg = saved;
    EXPECT_TRUE(direct());
    EXPECT_TRUE(module());
  }

  auto narrowed = owned_multicast(multicast_copy(1, "tile", false, "65536"));
  ASSERT_TRUE(narrowed);
  auto& narrowed_copy = narrowed->functions.front().body.front();
  auto* narrowed_payload =
      dynamic_cast<CpAsyncBulkTensor1dSharedClusterMulticast*>(
          narrowed_copy.get());
  ASSERT_NE(narrowed_payload, nullptr);
  auto& immediate =
      std::get<ResolvedImmediate>(narrowed_payload->cta_mask.value);
  ASSERT_EQ(immediate.bits, 0u);
  ASSERT_EQ(immediate.integer_source_bits, uint64_t{65536});
  const auto saved = immediate;
  immediate.bits = 1;
  EXPECT_FALSE(narrowed_copy->check(multicast_context()));
  EXPECT_FALSE(validateModule(*narrowed,
                              ModuleValidationPolicy::RequireCompleteContext));
  immediate = saved;
  immediate.integer_source_bits.reset();
  EXPECT_FALSE(narrowed_copy->check(multicast_context()));
  EXPECT_FALSE(validateModule(*narrowed,
                              ModuleValidationPolicy::RequireCompleteContext));
  immediate = saved;
  EXPECT_TRUE(narrowed_copy->check(multicast_context()));
  EXPECT_TRUE(validateModule(*narrowed,
                             ModuleValidationPolicy::RequireCompleteContext));
}

/** Each owned pointer role remains scalar after source syntax is destroyed. */
TEST(TensorMulticast, ReleasedAstPointerMutations) {
  auto owned = owned_multicast(multicast_copy(1, "tile", false));
  ASSERT_TRUE(owned);
  auto& copy = owned->functions.front().body.front();
  auto* payload =
      dynamic_cast<CpAsyncBulkTensor1dSharedClusterMulticast*>(copy.get());
  ASSERT_NE(payload, nullptr);
  for (auto* address : {&payload->tensor.value.tensor_map.address,
                        &payload->dst.value, &payload->mbar.value}) {
    const auto original = address->base;
    address->base =
        ResolvedRegisterRef{.spelling = "%bad32",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B16};
    EXPECT_FALSE(copy->check(multicast_context()));
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
    address->base = original;
  }
  EXPECT_TRUE(copy->check(multicast_context()));
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
}

/** Optional information and gather coordinates retain their separate checks. */
TEST(TensorMulticast, ReleasedAstInfoAndGatherMutations) {
  auto im2col = owned_multicast(multicast_copy(3, "im2col", true));
  ASSERT_TRUE(im2col);
  auto& copy = im2col->functions.front().body.front();
  auto* selected =
      dynamic_cast<CpAsyncBulkTensor3dSharedClusterIm2colMulticast*>(
          copy.get());
  ASSERT_NE(selected, nullptr);
  ASSERT_TRUE(selected->im2col_info);
  {
    auto& info = selected->im2col_info->value.elements.front();
    const auto original = info;
    info = ResolvedImmediate{.bits = 1,
                             .type = ScalarType::U16,
                             .integer_source_bits = uint64_t{65536}};
    EXPECT_FALSE(copy->check(multicast_context()));
    EXPECT_FALSE(validateModule(
        *im2col, ModuleValidationPolicy::RequireCompleteContext));
    info = original;
    EXPECT_TRUE(copy->check(multicast_context()));
  }

  auto gather = owned_multicast(multicast_copy(2, "tile::gather4", false));
  ASSERT_TRUE(gather);
  auto& gathered = gather->functions.front().body.front();
  auto* payload =
      dynamic_cast<CpAsyncBulkTensor2dSharedClusterTileGather4Multicast*>(
          gathered.get());
  ASSERT_NE(payload, nullptr);
  auto& coordinate = std::get<ResolvedRegisterRef>(
      payload->tensor.value.coordinates.elements[0]);
  coordinate.vector_width = 2;
  EXPECT_FALSE(gathered->check(multicast_context()));
  EXPECT_FALSE(
      validateModule(*gather, ModuleValidationPolicy::RequireCompleteContext));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
