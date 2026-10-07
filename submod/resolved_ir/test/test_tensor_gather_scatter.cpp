#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap one transfer in aligned descriptor, shared storage and barrier symbols. */
std::string gather_module(std::string_view instruction,
                          std::string_view version = "9.3",
                          std::string_view target = "sm_110a") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) +
         "\n.address_size 64\n"
         ".global .align 64 .b8 tensor_map[128];\n"
         ".shared .align 16 .b8 dst[1024];\n"
         ".shared .align 16 .b8 src[1024];\n"
         ".shared .align 8 .b64 mbar;\n.entry kernel() {\n"
         ".reg .s32 %r<5>;\n.reg .b64 %ptr;\n" +
         std::string(instruction) + "\n}\n";
}

/** Match direct checks to the exact catalog profile used by module checks. */
checker::Context gather_context(checker::PtxVersion version = {9, 3},
                                std::string_view target = "sm_110a") {
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

/** Destroy parsed syntax before returning a resolved transfer. */
std::optional<ResolvedModule> owned_gather(
    std::string_view instruction, std::string_view version = "9.3",
    std::string_view target = "sm_110a") {
  const auto parsed =
      test_helpers::parseModule(gather_module(instruction, version, target));
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** One physical layout per identity retains a rank-two, five-element payload. */
TEST(TensorGatherScatter, AllFourOwnedFormsAndRoles) {
  constexpr std::array<std::string_view, 4> instructions{
      "cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], "
      "[tensor_map, {%r0, %r1, %r2, %r3, %r4}], [mbar];",
      "cp.async.bulk.tensor.2d.shared::cluster.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], "
      "[tensor_map, {%r0, %r1, %r2, %r3, %r4}], [mbar];",
      "cp.async.bulk.prefetch.tensor.2d.L2.global.tile::gather4 "
      "[tensor_map, {%r0, %r1, %r2, %r3, %r4}];",
      "cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4.bulk_group "
      "[tensor_map, {%r0, %r1, %r2, %r3, %r4}], [src];"};
  constexpr std::array roles{TensorGatherScatterCoordinateRole::Column,
                             TensorGatherScatterCoordinateRole::Row0,
                             TensorGatherScatterCoordinateRole::Row1,
                             TensorGatherScatterCoordinateRole::Row2,
                             TensorGatherScatterCoordinateRole::Row3};
  std::set<InstructionKind> alternatives;
  for (size_t form = 0; form < instructions.size(); ++form) {
    auto owned = owned_gather(instructions[form]);
    ASSERT_TRUE(owned.has_value()) << instructions[form];
    auto& instruction = owned->functions.front().body.front();
    auto& copy = instruction;
    alternatives.insert(copy->instruction_kind());
    EXPECT_TRUE(copy->check(gather_context()).has_value());
    EXPECT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
    class ReferenceCounter final : public detail::IReferenceObserver {
     public:
      /** Count borrowed tensor and pointer references. */
      void tensor_operand(const ResolvedTensorOperand&,
                          std::span<const SourceRange>,
                          checker::AddressSymbolResolutionPolicy) override {
        ++count;
      }
      /** Count borrowed address references. */
      void address(const ResolvedAddress&, std::span<const SourceRange>,
                   checker::AddressSymbolResolutionPolicy) override {
        ++count;
      }
      /** Number of references seen. */
      int count = 0;
    } observer;
    instruction->visit_references(observer);
    EXPECT_GE(observer.count, form == 2 ? 1 : 2);
    const ResolvedTensorOperand* tensor_ptr = nullptr;
    if (form == 0) {
      const auto* selected =
          dynamic_cast<CpAsyncBulkTensor2dSharedCtaTileGather4*>(copy.get());
      ASSERT_NE(selected, nullptr);
      tensor_ptr = &selected->tensor.value;
    } else if (form == 1) {
      const auto* selected =
          dynamic_cast<CpAsyncBulkTensor2dSharedClusterTileGather4*>(
              copy.get());
      ASSERT_NE(selected, nullptr);
      tensor_ptr = &selected->tensor.value;
    } else if (form == 2) {
      const auto* selected =
          dynamic_cast<CpAsyncBulkPrefetchTensor2dTileGather4*>(copy.get());
      ASSERT_NE(selected, nullptr);
      tensor_ptr = &selected->tensor.value;
    } else {
      const auto* selected =
          dynamic_cast<CpAsyncBulkTensor2dGlobalSharedCtaTileScatter4*>(
              copy.get());
      ASSERT_NE(selected, nullptr);
      tensor_ptr = &selected->tensor.value;
    }
    {
      const auto& tensor = *tensor_ptr;
      EXPECT_EQ(tensor.rank, TensorRank::Two);
      EXPECT_EQ(tensor.coordinates.elements.size(), 5u);
      EXPECT_EQ(tensor.coordinate_ranges.size(), 5u);
      EXPECT_EQ(tensor.mode, form == 3 ? TensorAccessMode::TileScatter4
                                       : TensorAccessMode::TileGather4);
      EXPECT_NE(tensor.tensor_map.range, SourceRange{});
      for (size_t index = 0; index < 5; ++index) {
        EXPECT_NE(tensor.coordinate_ranges[index], SourceRange{});
        EXPECT_EQ(tensor_gather_scatter_coordinate_role(tensor, index),
                  roles[index]);
      }
      EXPECT_FALSE(tensor_gather_scatter_coordinate_role(tensor, 5));
    }
  }
  EXPECT_EQ(alternatives.size(), 4u);
}

/** Nearby ranks, arities, directions, controls and completion spellings fail. */
TEST(TensorGatherScatter, RejectsAdjacentSourceForms) {
  constexpr std::array<std::string_view, 12> invalid{
      "cp.async.bulk.tensor.1d.shared::cta.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], [tensor_map, {0, 1, 2, 3, 4}], "
      "[mbar];",
      "cp.async.bulk.tensor.3d.shared::cta.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], [tensor_map, {0, 1, 2, 3, 4}], "
      "[mbar];",
      "cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], [tensor_map, {0, 1, 2, 3}], [mbar];",
      "cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], [tensor_map, {0, 1, 2, 3, 4, 5}], "
      "[mbar];",
      "cp.async.bulk.tensor.2d.shared::cta.global.tile::scatter4."
      "mbarrier::complete_tx::bytes [dst], [tensor_map, {0, 1, 2, 3, 4}], "
      "[mbar];",
      "cp.async.bulk.tensor.2d.global.shared::cta.tile::gather4.bulk_group "
      "[tensor_map, {0, 1, 2, 3, 4}], [src];",
      "cp.reduce.async.bulk.tensor.2d.global.shared::cta.add.tile::scatter4."
      "bulk_group [tensor_map, {0, 1, 2, 3, 4}], [src];",
      "cp.async.bulk.prefetch.tensor.2d.L2.global.tile::gather4 "
      "[tensor_map, {0, 1, 2, 3, 4}], [mbar];",
      "cp.async.bulk.prefetch.tensor.2d.L2.global.tile::gather4 "
      "[tensor_map, {0, 1, 2, 3, 4}], {0};",
      "cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4.bulk_group "
      "[dst], [tensor_map, {0, 1, 2, 3, 4}], [mbar];",
      "cp.async.bulk.tensor.2d.global.shared::cta.tile.bulk_group "
      "[tensor_map, {0, 1, 2, 3, 4}], [src];",
      "cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4."
      "multicast::cluster.mbarrier::complete_tx::bytes [dst], "
      "[tensor_map, {0, 1, 2, 3, 4}], [mbar];",
  };
  for (const auto source : invalid) {
    const auto parsed = test_helpers::parseModule(gather_module(source));
    if (!parsed)
      continue;
    EXPECT_FALSE(resolveModuleOnly(*parsed).has_value()) << source;
  }
}

/** Missing or impossible generated rank metadata fails before owned lowering. */
TEST(TensorGatherScatter, MalformedGeneratedRankBinding) {
  const auto parsed = test_helpers::parseInstruction(
      "cp.async.bulk.tensor.2d.shared::cluster.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], "
      "[tensor_map, {0, 1, 2, 3, 4}], [mbar];");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  const auto& canonical = cp_resolved_descriptor();
  std::vector<check_end::ResolvedVariantDescriptor> variants(
      canonical.variants.begin(), canonical.variants.end());
  auto selected =
      std::find_if(variants.begin(), variants.end(), [](const auto& candidate) {
        return candidate.variant_name ==
               "AsyncBulkTensor2dSharedClusterTileGather4";
      });
  ASSERT_NE(selected, variants.end());
  ASSERT_EQ(selected->operand_layouts.size(), 1u);
  std::vector<check_end::ResolvedOperandLayoutDescriptor> layouts(
      selected->operand_layouts.begin(), selected->operand_layouts.end());
  std::vector<check_end::ResolvedOperandBindingDescriptor> bindings(
      layouts.front().bindings.begin(), layouts.front().bindings.end());
  ASSERT_EQ(bindings.size(), 3u);
  ASSERT_EQ(bindings[1].expected_tensor_rank, TensorRank::Two);
  ASSERT_EQ(bindings[1].minimum_elements, 5u);
  layouts.front().bindings = bindings;
  selected->operand_layouts = layouts;
  auto descriptor = canonical;
  descriptor.variants = variants;
  for (const auto rank :
       {std::optional<TensorRank>{},
        std::optional<TensorRank>{static_cast<TensorRank>(6)}}) {
    bindings[1].expected_tensor_rank = rank;
    layouts.front().bindings = bindings;
    selected->operand_layouts = layouts;
    descriptor.variants = variants;
    const auto fields =
        resolve_fields(*parsed, cp_syntax_descriptor(), descriptor,
                       "AsyncBulkTensor2dSharedClusterTileGather4");
    ASSERT_FALSE(fields.has_value());
    EXPECT_NE(fields.error().message.find("no valid access mode"),
              std::string::npos);
  }
}

/** Target profiles preserve CTA numeric availability and qualified families. */
TEST(TensorGatherScatter, TargetFloorsAndFamilyInheritance) {
  constexpr std::string_view gather =
      "cp.async.bulk.tensor.2d.shared::cluster.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], "
      "[tensor_map, {0, 0, 0, 0, 0}], [mbar];";
  constexpr std::string_view cta =
      "cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], "
      "[tensor_map, {0, 0, 0, 0, 0}], [mbar];";
  constexpr std::string_view prefetch =
      "cp.async.bulk.prefetch.tensor.2d.L2.global.tile::gather4 "
      "[tensor_map, {0, 0, 0, 0, 0}];";
  constexpr std::string_view scatter =
      "cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4.bulk_group "
      "[tensor_map, {0, 0, 0, 0, 0}], [src];";
  /** A catalog target/version and expected result for qualified forms. */
  struct Case {
    std::string_view version;
    checker::PtxVersion parsed_version;
    std::string_view target;
    bool qualified;
    bool cta;
  };
  constexpr std::array cases{
      Case{"8.6", {8, 6}, "sm_90", false, false},
      Case{"8.6", {8, 6}, "sm_100", false, true},
      Case{"8.6", {8, 6}, "sm_100a", true, true},
      Case{"8.6", {8, 6}, "sm_103a", false, true},
      Case{"8.8", {8, 8}, "sm_103a", true, true},
      Case{"8.8", {8, 8}, "sm_103f", true, true},
      Case{"8.8", {8, 8}, "sm_110a", false, true},
      Case{"8.9", {8, 9}, "sm_110f", false, true},
      Case{"9.0", {9, 0}, "sm_110a", true, true},
      Case{"9.0", {9, 0}, "sm_110f", true, true},
      Case{"9.0", {9, 0}, "sm_110", false, true},
      Case{"9.3", {9, 3}, "sm_120a", false, true},
      Case{"9.3", {9, 3}, "sm_120f", false, true},
  };
  for (const auto instruction : {gather, cta, prefetch, scatter}) {
    for (const auto& item : cases) {
      auto owned = owned_gather(instruction, item.version, item.target);
      ASSERT_TRUE(owned.has_value()) << instruction << " / " << item.target;
      const auto& copy = owned->functions.front().body.front();
      const bool expected = instruction == cta ? item.cta : item.qualified;
      EXPECT_EQ(copy->check(gather_context(item.parsed_version, item.target))
                    .has_value(),
                expected)
          << instruction << " / " << item.version << " / " << item.target;
      EXPECT_EQ(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value(),
          expected)
          << instruction << " / " << item.version << " / " << item.target;
    }
  }
}

/** Owned rank, count, mode, scalar register and source metadata are independent. */
TEST(TensorGatherScatter, OwnedCoordinateMutations) {
  auto owned = owned_gather(
      "cp.async.bulk.tensor.2d.shared::cluster.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], "
      "[tensor_map, {%r0, 4294967296, %r2, %r3, %r4}], [mbar];");
  ASSERT_TRUE(owned.has_value());
  auto& copy = owned->functions.front().body.front();
  auto* payload =
      dynamic_cast<CpAsyncBulkTensor2dSharedClusterTileGather4*>(copy.get());
  ASSERT_NE(payload, nullptr);
  auto& tensor = payload->tensor.value;
  const auto context = gather_context();
  const auto valid = [&] {
    return copy->check(context).has_value() &&
           validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext)
               .has_value();
  };
  const auto invalid = [&] {
    EXPECT_FALSE(copy->check(context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
  };
  ASSERT_TRUE(valid());
  tensor.rank = TensorRank::Five;
  invalid();
  tensor.rank = TensorRank::Two;
  tensor.mode = TensorAccessMode::Tiled;
  invalid();
  EXPECT_FALSE(tensor_gather_scatter_coordinate_role(tensor, 0));
  tensor.mode = TensorAccessMode::TileGather4;
  auto final_coordinate = tensor.coordinates.elements.back();
  tensor.coordinates.elements.pop_back();
  invalid();
  EXPECT_FALSE(tensor_gather_scatter_coordinate_role(tensor, 0));
  tensor.coordinates.elements.push_back(final_coordinate);
  const auto last_range = tensor.coordinate_ranges.back();
  tensor.coordinate_ranges.back() = {};
  invalid();
  tensor.coordinate_ranges.back() = last_range;
  auto& reg =
      std::get<ResolvedRegisterRef>(tensor.coordinates.elements.front());
  const auto original_reg = reg;
  reg.vector_width = 2;
  invalid();
  reg = original_reg;
  reg.declared_type = ScalarType::B16;
  invalid();
  reg = original_reg;
  reg.register_class = ResolvedRegisterClass::Predicate;
  invalid();
  reg = original_reg;
  reg.declared_type.reset();
  invalid();
  reg = original_reg;
  auto& immediate = std::get<ResolvedImmediate>(tensor.coordinates.elements[1]);
  const auto original_immediate = immediate;
  EXPECT_EQ(immediate.bits, 0u);
  EXPECT_EQ(immediate.integer_source_bits, 0x100000000ULL);
  immediate.bits = 1;
  invalid();
  immediate = original_immediate;
  immediate.integer_source_bits.reset();
  invalid();
  immediate = original_immediate;
  immediate.type = ScalarType::U32;
  invalid();
  immediate = original_immediate;
  EXPECT_TRUE(valid());
}

/** Scatter tests the converted S32 sign in every coordinate position. */
TEST(TensorGatherScatter, ScatterConvertedSignAndPointerRoles) {
  constexpr std::array<std::string_view, 4> literals{
      "4294967296", "-4294967296", "-2147483649", "4294967295"};
  for (size_t index = 0; index < 5; ++index) {
    for (const auto literal : literals) {
      std::string coordinates = "0, 0, 0, 0, 0";
      const size_t start = index * 3;
      coordinates.replace(start, 1, literal);
      auto owned = owned_gather(
          "cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4."
          "bulk_group [tensor_map, {" +
          coordinates + "}], [src];");
      ASSERT_TRUE(owned.has_value()) << coordinates;
      const auto& copy = owned->functions.front().body.front();
      EXPECT_EQ(copy->check(gather_context()).has_value(),
                literal != "4294967295")
          << coordinates;
      EXPECT_EQ(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value(),
          literal != "4294967295")
          << coordinates;
    }
  }
  auto owned = owned_gather(
      "cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4."
      "bulk_group [tensor_map, {0, 0, 0, 0, 0}], [src];");
  ASSERT_TRUE(owned.has_value());
  auto& copy = owned->functions.front().body.front();
  auto* payload =
      dynamic_cast<CpAsyncBulkTensor2dGlobalSharedCtaTileScatter4*>(copy.get());
  ASSERT_NE(payload, nullptr);
  const auto context = gather_context();
  for (auto* address :
       {&payload->tensor.value.tensor_map.address, &payload->src.value}) {
    const auto original = address->base;
    address->base =
        ResolvedRegisterRef{.spelling = "%ptr",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B16};
    EXPECT_FALSE(copy->check(context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
    address->base =
        ResolvedRegisterRef{.spelling = "%ptr",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B64,
                            .vector_width = 2};
    EXPECT_FALSE(copy->check(context).has_value());
    address->base = original;
  }
  EXPECT_TRUE(copy->check(context).has_value());
}

/** The original unsigned 64-bit source is retained before signed-32 use. */
TEST(TensorGatherScatter, UnsignedWideSourceConversion) {
  constexpr std::string_view literal = "18446744073709551615";
  auto read = owned_gather(
      "cp.async.bulk.prefetch.tensor.2d.L2.global.tile::gather4 "
      "[tensor_map, {18446744073709551615, 0, 0, 0, 0}];");
  ASSERT_TRUE(read.has_value());
  auto& read_copy = read->functions.front().body.front();
  const auto* payload =
      dynamic_cast<CpAsyncBulkPrefetchTensor2dTileGather4*>(read_copy.get());
  ASSERT_NE(payload, nullptr);
  const auto& immediate = std::get<ResolvedImmediate>(
      payload->tensor.value.coordinates.elements.front());
  EXPECT_EQ(immediate.bits, 0xffffffffu);
  EXPECT_EQ(immediate.integer_source_bits, 0xffffffffffffffffULL);
  EXPECT_FALSE(immediate.is_negative);
  EXPECT_TRUE(read_copy->check(gather_context()).has_value());
  EXPECT_TRUE(
      validateModule(*read, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());

  auto write = owned_gather(
      "cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4.bulk_group "
      "[tensor_map, {18446744073709551615, 0, 0, 0, 0}], [src];");
  ASSERT_TRUE(write.has_value()) << literal;
  auto& write_copy = write->functions.front().body.front();
  EXPECT_FALSE(write_copy->check(gather_context()).has_value());
  EXPECT_FALSE(
      validateModule(*write, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

/** New read forms recheck each owned map, destination and barrier pointer. */
TEST(TensorGatherScatter, ReadPointerRolesAfterAstDestruction) {
  auto owned = owned_gather(
      "cp.async.bulk.tensor.2d.shared::cluster.global.tile::gather4."
      "mbarrier::complete_tx::bytes [dst], "
      "[tensor_map, {0, 1, 2, 3, 4}], [mbar];");
  ASSERT_TRUE(owned.has_value());
  auto& copy = owned->functions.front().body.front();
  auto* payload =
      dynamic_cast<CpAsyncBulkTensor2dSharedClusterTileGather4*>(copy.get());
  ASSERT_NE(payload, nullptr);
  const auto context = gather_context();
  for (auto* address : {&payload->tensor.value.tensor_map.address,
                        &payload->dst.value, &payload->mbar.value}) {
    const auto original = address->base;
    address->base =
        ResolvedRegisterRef{.spelling = "%ptr",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B16};
    EXPECT_FALSE(copy->check(context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
    address->base =
        ResolvedRegisterRef{.spelling = "%ptr",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B64,
                            .vector_width = 2};
    EXPECT_FALSE(copy->check(context).has_value());
    address->base = original;
  }
  auto& map = std::get<ResolvedSymbolRef>(
      payload->tensor.value.tensor_map.address.base);
  const auto map_alignment = map.address_alignment;
  map.address_alignment = 32;
  EXPECT_FALSE(copy->check(context).has_value());
  map.address_alignment = map_alignment;
  const auto map_space = map.address_state_space;
  map.address_state_space = base::DeclarationStateSpace::Local;
  EXPECT_FALSE(copy->check(context).has_value());
  map.address_state_space = map_space;
  auto& dst = std::get<ResolvedSymbolRef>(payload->dst.value.base);
  const auto dst_alignment = dst.address_alignment;
  dst.address_alignment = 8;
  EXPECT_FALSE(copy->check(context).has_value());
  dst.address_alignment = dst_alignment;
  auto& mbar = std::get<ResolvedSymbolRef>(payload->mbar.value.base);
  const auto mbar_alignment = mbar.address_alignment;
  mbar.address_alignment = 4;
  EXPECT_FALSE(copy->check(context).has_value());
  mbar.address_alignment = mbar_alignment;
  EXPECT_TRUE(copy->check(context).has_value());

  auto prefetch = owned_gather(
      "cp.async.bulk.prefetch.tensor.2d.L2.global.tile::gather4 "
      "[tensor_map, {0, 1, 2, 3, 4}];");
  ASSERT_TRUE(prefetch.has_value());
  auto& prefetch_copy = prefetch->functions.front().body.front();
  auto* prefetch_payload =
      dynamic_cast<CpAsyncBulkPrefetchTensor2dTileGather4*>(
          prefetch_copy.get());
  ASSERT_NE(prefetch_payload, nullptr);
  auto& prefetch_base = prefetch_payload->tensor.value.tensor_map.address.base;
  const auto original = prefetch_base;
  prefetch_base =
      ResolvedRegisterRef{.spelling = "%ptr",
                          .register_class = ResolvedRegisterClass::General,
                          .declared_type = ScalarType::B16};
  EXPECT_FALSE(prefetch_copy->check(context).has_value());
  EXPECT_FALSE(
      validateModule(*prefetch, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  prefetch_base = original;
  EXPECT_TRUE(prefetch_copy->check(context).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
