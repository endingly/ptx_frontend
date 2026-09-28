#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Check copy directions and group completion keep distinct typed identities. */
TEST(BulkAsync, CopyAndGroupTopology) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry kernel() {
  cp.async.bulk.shared::cluster.global.mbarrier::complete_tx::bytes [s], [g], 16, [bar];
  cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes [s], [g], 16, [bar];
  cp.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes [s], [s], 16, [bar];
  cp.async.bulk.global.shared::cta.bulk_group [g], [s], 16;
  cp.async.bulk.commit_group;
  cp.async.bulk.wait_group.read 0;
  cp.async.bulk.wait_group 2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 7u);
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkGlobalSharedCluster>(
      std::get<Cp>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkGlobalSharedCta>(
      std::get<Cp>(body[1]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkSharedCtaSharedCluster>(
      std::get<Cp>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkSharedCtaGlobal>(
      std::get<Cp>(body[3]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkCommitGroup>(
      std::get<Cp>(body[4]).variant));
  const auto& read_wait =
      std::get<Cp::AsyncBulkWaitGroup>(std::get<Cp>(body[5]).variant);
  const auto& whole_wait =
      std::get<Cp::AsyncBulkWaitGroup>(std::get<Cp>(body[6]).variant);
  EXPECT_TRUE(read_wait.read.value);
  EXPECT_FALSE(whole_wait.read.value);
  EXPECT_EQ(read_wait.n.value.bits, 0u);
  EXPECT_EQ(whole_wait.n.value.bits, 2u);
  EXPECT_EQ(Cp::AsyncBulkSharedCtaGlobal::completion_kind,
            base::AsyncCompletionKind::BulkGroup);
  EXPECT_EQ(Cp::AsyncBulkGlobalSharedCluster::completion_kind,
            base::AsyncCompletionKind::MbarrierCompleteTxBytes);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(item), context).has_value());
}

/** Check reduction element domains and optional prefetch policy. */
TEST(BulkAsync, ReductionAndPrefetch) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry kernel() {
  .reg .b64 %policy;
  cp.reduce.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes.add.u64 [s], [s], 16, [bar];
  cp.reduce.async.bulk.global.shared::cta.bulk_group.min.f16 [g], [s], 16;
  cp.reduce.async.bulk.global.shared::cta.bulk_group.add.noftz.bf16 [g], [s], 16;
  cp.async.bulk.prefetch.L2.global [g], 16;
  cp.async.bulk.prefetch.L2.global.L2::cache_hint [g], 32, %policy;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  EXPECT_TRUE(std::holds_alternative<Cp::ReduceAsyncBulkSharedAdd>(
      std::get<Cp>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::ReduceAsyncBulkGlobalMin>(
      std::get<Cp>(body[1]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::ReduceAsyncBulkGlobalAddNoftz>(
      std::get<Cp>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkPrefetchGlobal>(
      std::get<Cp>(body[3]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkPrefetchGlobalCacheHint>(
      std::get<Cp>(body[4]).variant));
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(item), context).has_value());
}

/** Check store completion topology and the word-size PTX version gate. */
TEST(BulkAsync, StoreTopologiesAndSizeVersion) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry kernel() {
  .reg .u32 %r<4>;
  .reg .u32 %count32;
  .reg .u64 %count64;
  st.async.shared::cluster.mbarrier::complete_tx::bytes.u32 [s], %r0, [bar];
  st.async.shared::cluster.mbarrier::complete_tx::bytes.v2.u32 [s], {%r0, %r1}, [bar];
  st.async.release.gpu.global.u32 [g], %r2;
  st.async.mmio.release.sys.global.u32 [g], %r3;
  st.bulk.weak.shared::cta [s], 64, 0;
  st.bulk [s], %count32, 0;
  st.bulk [s], %count64, 0;
  st.async.weak.shared::cluster.mbarrier::complete_tx::bytes.u32 [s], %r0, [bar];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  EXPECT_TRUE(std::holds_alternative<St::AsyncSharedClusterScalar>(
      std::get<St>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<St::AsyncSharedClusterV2>(
      std::get<St>(body[1]).variant));
  EXPECT_TRUE(std::holds_alternative<St::AsyncGlobalRelease>(
      std::get<St>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<St::AsyncGlobalMmioRelease>(
      std::get<St>(body[3]).variant));
  EXPECT_TRUE(std::holds_alternative<St::AsyncSharedScalar>(
      std::get<St>(body[7]).variant));
  const checker::Context current{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<St>(item), current).has_value());
  const checker::Context old{
      .target = {.ptx_version = {8, 6}, .sm_version = 100}};
  EXPECT_TRUE(checker::check(std::get<St>(body[4]), old).has_value());
  EXPECT_FALSE(checker::check(std::get<St>(body[5]), old).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[6]), old).has_value());
}

/** Check optional operand layouts and family-gated copy semantics. */
TEST(BulkAsync, CopyQualifierMatrix) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100f
.address_size 64
.global .align 16 .b8 g[128];
.shared .align 16 .b8 s[128];
.shared .align 8 .b64 bar;
.entry kernel() {
  .reg .b16 %mask;
  .reg .b64 %policy;
  cp.async.bulk.weak.shared::cta.global.mbarrier::complete_tx::bytes [s], [g], 16, [bar];
  cp.async.bulk.relaxed.cta.shared::cta.global.mbarrier::complete_tx::bytes.b128 [s], [g], 16, [bar];
  cp.async.bulk.shared::cluster.global.mbarrier::complete_tx::bytes.multicast::cluster.L2::cache_hint [s], [g], 16, [bar], %mask, %policy;
  cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes.L2::cache_hint.ignore_oob [s], [g], 16, 1, 2, [bar], %policy;
  cp.async.bulk.global.shared::cta.bulk_group.L2::cache_hint.cp_mask [g], [s], 16, %policy, %mask;
  cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group.cp_mask.b128 [g], [s], 16, %mask;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  EXPECT_TRUE(
      std::holds_alternative<Cp::AsyncBulkGlobalSharedCtaCacheHintIgnoreOob>(
          std::get<Cp>(body[3]).variant));
  EXPECT_TRUE(
      std::holds_alternative<Cp::AsyncBulkSharedCtaGlobalCacheHintCpMask>(
          std::get<Cp>(body[4]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkSharedCtaGlobalCpMaskRelaxed>(
      std::get<Cp>(body[5]).variant));
  for (const std::string_view target : {"sm_100f", "sm_100", "sm_90a"}) {
    const auto profile = base::find_target_profile(target);
    ASSERT_TRUE(profile.has_value());
    const checker::Context context{
        .target = {.ptx_version = {9, 3},
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities},
    };
    EXPECT_EQ(checker::check(std::get<Cp>(body[0]), context).has_value(),
              target != "sm_100");
    EXPECT_EQ(checker::check(std::get<Cp>(body[1]), context).has_value(),
              target != "sm_100");
    for (size_t i = 2; i < 4; ++i)
      EXPECT_TRUE(checker::check(std::get<Cp>(body[i]), context).has_value());
    EXPECT_EQ(checker::check(std::get<Cp>(body[4]), context).has_value(),
              target != "sm_90a");
    EXPECT_EQ(checker::check(std::get<Cp>(body[5]), context).has_value(),
              target == "sm_100f");
  }
  auto& bounded = std::get<Cp::AsyncBulkGlobalSharedCtaCacheHintIgnoreOob>(
      std::get<Cp>(resolved->functions.front().body[3]).variant);
  auto& ignore_left =
      std::get<ResolvedImmediate>(bounded.ignore_bytes_left.value);
  ignore_left.bits = 16;
  ignore_left.integer_source_bits = 16;
  const checker::Context base_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  EXPECT_FALSE(checker::check(std::get<Cp>(body[3]), base_context).has_value());
}

/** Reduction scopes use the base SM gate and a separate PTX 9.3 gate. */
TEST(BulkAsync, ReductionScopeAndCachePolicy) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry kernel() {
  .reg .b64 %policy;
  cp.reduce.async.bulk.relaxed.cta.shared::cluster.shared::cta.mbarrier::complete_tx::bytes.add.u64 [s], [s], 16, [bar];
  cp.reduce.async.bulk.relaxed.gpu.global.shared::cta.bulk_group.L2::cache_hint.min.u64 [g], [s], 16, %policy;
  cp.reduce.async.bulk.global.shared::cta.bulk_group.L2::cache_hint.add.noftz.f16 [g], [s], 16, %policy;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<Cp::ReduceAsyncBulkSharedAddRelaxed>(
      std::get<Cp>(body[0]).variant));
  EXPECT_TRUE(
      std::holds_alternative<Cp::ReduceAsyncBulkGlobalMinCacheHintRelaxed>(
          std::get<Cp>(body[1]).variant));
  const checker::Context current{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  const checker::Context old{
      .target = {.ptx_version = {9, 2}, .sm_version = 90}};
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(item), current).has_value());
  EXPECT_FALSE(checker::check(std::get<Cp>(body[0]), old).has_value());
  EXPECT_FALSE(checker::check(std::get<Cp>(body[1]), old).has_value());
  EXPECT_TRUE(checker::check(std::get<Cp>(body[2]), old).has_value());
}

/** Rechecking owned IR catches altered static byte-count metadata. */
TEST(BulkAsync, OwnedMetadataTamperIsRejected) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry kernel() {
  cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes [s], [g], 16, [bar];
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  auto& copy = std::get<Cp::AsyncBulkGlobalSharedCta>(
      std::get<Cp>(owned->functions.front().body.front()).variant);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  EXPECT_TRUE(checker::check(
                  std::get<Cp>(owned->functions.front().body.front()), context)
                  .has_value());
  auto& size = std::get<ResolvedImmediate>(copy.size.value);
  size.bits = 15;
  size.integer_source_bits = 15;
  EXPECT_FALSE(checker::check(
                   std::get<Cp>(owned->functions.front().body.front()), context)
                   .has_value());
}

/** Invalid modifier/operand pairings fail before they enter owned IR. */
TEST(BulkAsync, RejectsInvalidQualifierPairings) {
  constexpr std::string_view prefix = R"ptx(
.version 9.3
.target sm_100f
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry kernel() {
  .reg .b16 %mask;
  .reg .b64 %policy;
)ptx";
  for (const std::string_view invalid : {
           "cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes.L2::"
           "cache_hint [s], [g], 16, [bar];",
           "cp.async.bulk.shared::cluster.global.mbarrier::complete_tx::bytes."
           "ignore_oob [s], [g], 16, 1, 1, [bar];",
           "cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes.cp_"
           "mask [s], [g], 16, [bar], %mask;",
           "cp.reduce.async.bulk.relaxed.gpu.shared::cluster.shared::cta."
           "mbarrier::complete_tx::bytes.add.u64 [s], [s], 16, [bar];",
           "cp.reduce.async.bulk.global.shared::cta.bulk_group.add.f16 [g], "
           "[s], 16;",
           "cp.async.bulk.prefetch.L2.global.L2::cache_hint [g], 16;",
       }) {
    const std::string source =
        std::string{prefix} + std::string{invalid} + "\n}\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModule(*parsed).has_value()) << invalid;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
