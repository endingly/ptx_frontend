#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <optional>
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

/** Cluster and CTA tiled loads retain mbarrier completion and target gates. */
TEST(TensorAsync, LoadDirectionsAndRanks) {
  const std::string prefix = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 128 .b8 tile_data[1024];
.shared .align 8 .b64 barrier;
.entry kernel() {
  .reg .s32 %r<5>;
)ptx";
  for (std::string_view direction : {"cluster", "cta"}) {
    for (int rank = 1; rank <= 5; ++rank) {
      std::string coords = "%r0";
      for (int index = 1; index < rank; ++index)
        coords += ", %r" + std::to_string(index);
      const std::string instruction =
          "cp.async.bulk.tensor." + std::to_string(rank) + "d.shared::" +
          std::string(direction) + ".global" +
          (rank % 2 == 0 ? ".tile" : "") +
          ".mbarrier::complete_tx::bytes [tile_data], [tensor_map, {" +
          coords + "}], [barrier];";
      const auto parsed = test_helpers::parseModule(prefix + instruction + "\n}");
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      const auto resolved = resolveModule(*parsed);
      ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
      const auto& copy = std::get<Cp>(resolved->functions.front().body.front());
      const checker::Context available{
          .target = {.ptx_version = {9, 3}, .sm_version = 90}};
      EXPECT_TRUE(checker::check(copy, available).has_value()) << instruction;
      const checker::Context too_old{
          .target = {.ptx_version = {8, 0}, .sm_version = 90}};
      EXPECT_EQ(checker::check(copy, too_old).has_value(),
                direction == "cluster");
      const checker::Context too_small{
          .target = {.ptx_version = {9, 3}, .sm_version = 89}};
      EXPECT_FALSE(checker::check(copy, too_small).has_value());
    }
  }
}

/** Bulk-group stores accept five ranks and reject static negative coordinates. */
TEST(TensorAsync, StoreRanksAndSignedCoordinates) {
  const std::string prefix = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.const .align 64 .b8 tensor_map[128];
.shared .align 128 .b8 tile_data[1024];
.entry kernel() {
  .reg .s32 %r<5>;
)ptx";
  for (int rank = 1; rank <= 5; ++rank) {
    std::string coords = "%r0";
    for (int index = 1; index < rank; ++index)
      coords += ", %r" + std::to_string(index);
    const std::string instruction =
        "cp.async.bulk.tensor." + std::to_string(rank) +
        "d.global.shared::cta" + (rank % 2 == 0 ? ".tile" : "") +
        ".bulk_group [tensor_map, {" + coords + "}], [tile_data];";
    const auto parsed = test_helpers::parseModule(prefix + instruction + "\n}");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    const auto& copy = std::get<Cp>(resolved->functions.front().body.front());
    const checker::Context available{
        .target = {.ptx_version = {8, 0}, .sm_version = 90}};
    EXPECT_TRUE(checker::check(copy, available).has_value()) << instruction;
  }
  const auto negative = test_helpers::parseModule(
      prefix + "cp.async.bulk.tensor.1d.global.shared::cta.bulk_group "
               "[tensor_map, {-1}], [tile_data];\n}");
  ASSERT_MODULE_PARSE_SUCCEEDS(negative);
  EXPECT_FALSE(resolveModule(*negative).has_value());
}

/** Descriptor spaces, alignment, rank arity, and unsupported modes fail. */
TEST(TensorAsync, RejectsInvalidDescriptorAndCoordinateForms) {
  const std::string prefix = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 global_map[128];
.global .align 32 .b8 misaligned_map[128];
.shared .align 64 .b8 shared_map[128];
.local .align 64 .b8 local_map[128];
.entry kernel() {
  .reg .s32 %coord;
  .reg .u64 %wide;
)ptx";
  for (std::string_view instruction : {
           "cp.async.bulk.prefetch.tensor.1d.L2.global [shared_map, {%coord}];",
           "cp.async.bulk.prefetch.tensor.1d.L2.global [local_map, {%coord}];",
           "cp.async.bulk.prefetch.tensor.1d.L2.global [misaligned_map, {%coord}];",
           "cp.async.bulk.prefetch.tensor.2d.L2.global [global_map, {%coord}];",
           "cp.async.bulk.prefetch.tensor.1d.L2.global [global_map, {%wide}];",
           "cp.async.bulk.prefetch.tensor.1d.L2.global [global_map, {2147483648}];",
           "cp.async.bulk.prefetch.tensor.1d.L2.global.im2col [global_map, {%coord}];",
           "cp.async.bulk.prefetch.tensor.1d.L2.global.L2::cache_hint [global_map, {%coord}];",
       }) {
    const auto parsed =
        test_helpers::parseModule(prefix + std::string(instruction) + "\n}");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed) << instruction;
    EXPECT_FALSE(resolveModule(*parsed).has_value()) << instruction;
  }
}

/** Owned tensor metadata remains checked when the parser and AST are gone. */
TEST(TensorAsync, RevalidatesOwnedTensorMetadata) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 tensor_map[128];
.entry kernel() {
  .reg .s32 %coord;
  cp.async.bulk.prefetch.tensor.1d.L2.global [tensor_map, {%coord}];
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(validateModule(*owned,
                             ModuleValidationPolicy::RequireCompleteContext));
  auto& copy = std::get<Cp>(owned->functions.front().body.front());
  auto& prefetch = std::get<Cp::AsyncBulkPrefetchTensor1d>(copy.variant);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  const auto original_rank = prefetch.tensor.value.rank;
  prefetch.tensor.value.rank = TensorRank::Two;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  prefetch.tensor.value.rank = original_rank;
  auto& coordinate = std::get<ResolvedRegisterRef>(
      prefetch.tensor.value.coordinates.elements.front());
  const auto original_type = coordinate.declared_type;
  coordinate.declared_type = base::ScalarType::U64;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  coordinate.declared_type = original_type;
  auto& symbol = std::get<ResolvedSymbolRef>(
      prefetch.tensor.value.tensor_map.address.base);
  const auto original_space = symbol.address_state_space;
  symbol.address_state_space = base::DeclarationStateSpace::Shared;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  symbol.address_state_space = original_space;
  const auto original_alignment = symbol.address_alignment;
  symbol.address_alignment = 32;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  symbol.address_alignment = original_alignment;
  EXPECT_TRUE(validateModule(*owned,
                             ModuleValidationPolicy::RequireCompleteContext));
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
