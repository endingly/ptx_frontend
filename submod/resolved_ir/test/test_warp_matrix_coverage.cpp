#include <gtest/gtest.h>

#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

TEST(WarpMatrixCoverage, ResolvesOwnedMatrixMovementAndChecksTarget) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_120a
.address_size 64
.shared .align 16 .b32 tile[64];
.entry kernel() {
  .reg .b32 %d<4>;
  .reg .b32 %s<2>;
  ldmatrix.sync.aligned.m16n16.x2.trans.shared::cta.b8x16.b6x16_p32
    {%d0, %d1, %d2, %d3}, [tile];
  stmatrix.sync.aligned.m16n8.x2.trans.shared::cta.b8
    [tile], {%s0, %s1};
  movmatrix.sync.aligned.m8n8.trans.b16 %d0, %s0;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_EQ(owned->functions.front().body.size(), 3u);
  const auto& load = std::get<Ldmatrix>(owned->functions.front().body[0]);
  const auto& store = std::get<Stmatrix>(owned->functions.front().body[1]);
  const auto& move = std::get<Movmatrix>(owned->functions.front().body[2]);
  const auto load_matrix = std::visit([](const auto& variant) {
    return variant.matrix.value;
  }, load.variant);
  EXPECT_EQ(load_matrix.family, MatrixFamily::LDMATRIX);
  EXPECT_EQ(load_matrix.shape, (MatrixShape{16, 16, 0}));
  EXPECT_EQ(load_matrix.matrix_count, 2);
  EXPECT_EQ(load_matrix.address_qualifier, MatrixAddressQualifier::SHARED_CTA);
  EXPECT_EQ(load_matrix.source_packing, MatrixElementType::B6X16_P32);
  EXPECT_EQ(load_matrix.destination_packing, MatrixElementType::B8X16);
  EXPECT_EQ(load_matrix.fragments[0].register_count, 4);
  EXPECT_EQ(std::visit([](const auto& variant) {
    return variant.matrix.value.fragments[0].register_count;
  }, store.variant), 2);
  EXPECT_EQ(std::get<Movmatrix::SyncAlignedM8n8TransB16>(move.variant)
                .matrix.value.fragments[0].register_count, 1);

  const auto target = base::find_target_profile("sm_120a");
  ASSERT_TRUE(target.has_value());
  const checker::Context supported{
      .target = {.ptx_version = {9, 3},
                 .sm_version = target->identity.architecture.number,
                 .enabled_family_features = target->enabled_family_features,
                 .identity = target->identity,
                 .capabilities = target->capabilities},
  };
  EXPECT_TRUE(checker::check(load, supported).has_value());
  EXPECT_TRUE(checker::check(store, supported).has_value());
  EXPECT_TRUE(checker::check(move, supported).has_value());
  const auto generic_target = base::find_target_profile("sm_120");
  ASSERT_TRUE(generic_target.has_value());
  const checker::Context unsupported{
      .target = {.ptx_version = {9, 3},
                 .sm_version = generic_target->identity.architecture.number,
                 .enabled_family_features = generic_target->enabled_family_features,
                 .identity = generic_target->identity,
                 .capabilities = generic_target->capabilities},
  };
  EXPECT_FALSE(checker::check(load, unsupported).has_value());
  EXPECT_FALSE(checker::check(store, unsupported).has_value());
  EXPECT_TRUE(checker::check(move, unsupported).has_value());

  /** Check every architecture/family branch and the renamed 110 target floor. */
  const auto expect_modern_target = [&](std::string_view name, int major,
                                        int minor, bool expected) {
    const auto profile = base::find_target_profile(name);
    ASSERT_TRUE(profile.has_value()) << name;
    const checker::Context selected{
        .target = {.ptx_version = {static_cast<uint16_t>(major),
                                   static_cast<uint16_t>(minor)},
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities},
    };
    EXPECT_EQ(checker::check(load, selected).has_value(), expected) << name;
    EXPECT_EQ(checker::check(store, selected).has_value(), expected) << name;
  };
  expect_modern_target("sm_100a", 8, 6, true);
  expect_modern_target("sm_110a", 8, 9, false);
  expect_modern_target("sm_110a", 9, 0, true);
  expect_modern_target("sm_120a", 8, 6, true);
  expect_modern_target("sm_100f", 8, 7, false);
  expect_modern_target("sm_100f", 8, 8, true);
  expect_modern_target("sm_110f", 8, 9, false);
  expect_modern_target("sm_110f", 9, 0, true);
  expect_modern_target("sm_120f", 8, 8, true);
}

TEST(WarpMatrixCoverage, RejectsInvalidMovementTopologyAndAddressSpace) {
  for (const std::string_view source : {
           ".entry kernel() { .reg .b32 %r<4>; .shared .align 16 .b32 x[16]; "
           "ldmatrix.sync.aligned.m16n16.x2.shared.b8 "
           "{%r0,%r1,%r2,%r3}, [x]; }",
           ".entry kernel() { .reg .b32 %r<4>; .shared .align 16 .b32 x[16]; "
           "ldmatrix.sync.aligned.m16n16.x2.trans.shared.b8 "
           "{%r0,%r1}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .align 16 .b32 x[16]; "
           "stmatrix.sync.aligned.m16n8.x2.shared.b8 [x], {%r0,%r1}; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .align 16 .b32 x[16]; "
           "movmatrix.sync.aligned.m8n8.b16 %r0, %r1; }",
       }) {
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModule(*parsed).has_value()) << source;
  }
  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 16 .b32 tile[16];
.entry kernel() { .reg .b32 %r0;
  ldmatrix.sync.aligned.m8n8.x1.shared.b16 {%r0}, [tile]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
  };
  const auto checked = checker::check(
      std::get<Ldmatrix>(resolved->functions.front().body.front()), context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
