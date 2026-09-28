#include <gtest/gtest.h>

#include <array>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Exercise all baseline bulk-copy directions and their completion identity. */
TEST(BulkAsync, CopyDirectionsAndCompletion) {
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
}

/** Bulk-group waits retain the source-read qualifier and immediate count. */
TEST(BulkAsync, GroupCompletionTopology) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  cp.async.bulk.commit_group;
  cp.async.bulk.wait_group.read 0;
  cp.async.bulk.wait_group 2;
}

/** Shared and global reductions retain distinct element and completion forms. */
TEST(BulkAsync, ReductionTopologies) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry kernel() {
  cp.reduce.async.bulk.shared::cluster.shared::cta.mbarrier::complete_tx::bytes.add.u64 [s], [s], 16, [bar];
  cp.reduce.async.bulk.global.shared::cta.bulk_group.min.f16 [g], [s], 16;
  cp.reduce.async.bulk.global.shared::cta.bulk_group.add.noftz.bf16 [g], [s], 16;
}

/** Prefetch retains its optional policy operand without inventing a barrier. */
TEST(BulkAsync, PrefetchPolicyTopology) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 16 .b8 g[64];
.entry kernel() {
  .reg .b64 %policy;
  cp.async.bulk.prefetch.L2.global [g], 16;
  cp.async.bulk.prefetch.L2.global.L2::cache_hint [g], 32, %policy;
}

/** Shared stores use a barrier while global release stores use scope alone. */
TEST(BulkAsync, AsyncStoreTopologies) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry kernel() {
  .reg .u32 %r<4>;
  st.async.shared::cluster.mbarrier::complete_tx::bytes.u32 [s], %r0, [bar];
  st.async.shared::cluster.mbarrier::complete_tx::bytes.v2.u32 [s], {%r0, %r1}, [bar];
  st.async.release.gpu.global.u32 [g], %r2;
  st.async.mmio.release.sys.global.u32 [g], %r3;
}

/** Bulk zero-fill checks literal bounds and the PTX version of word sizes. */
TEST(BulkAsync, ZeroFillSizeContract) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.shared .align 16 .b8 s[64];
.entry kernel() {
  .reg .u32 %count32;
  .reg .u64 %count64;
  st.bulk.weak.shared::cta [s], 64, 0;
  st.bulk [s], %count32, 0;
  st.bulk [s], %count64, 0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const checker::Context current{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
  };
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<St>(item), current).has_value());
  const checker::Context old{
      .target = {.ptx_version = {8, 6}, .sm_version = 100},
  };
  EXPECT_TRUE(checker::check(std::get<St>(body[0]), old).has_value());
  EXPECT_FALSE(checker::check(std::get<St>(body[1]), old).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[2]), old).has_value());
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  EXPECT_TRUE(std::holds_alternative<St::AsyncSharedScalar>(
      std::get<St>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<St::AsyncSharedV2>(
      std::get<St>(body[1]).variant));
  EXPECT_TRUE(std::holds_alternative<St::AsyncGlobalRelease>(
      std::get<St>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<St::AsyncGlobalMmioRelease>(
      std::get<St>(body[3]).variant));
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
  };
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<St>(item), context).has_value());
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkPrefetchGlobal>(
      std::get<Cp>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkPrefetchGlobalCacheHint>(
      std::get<Cp>(body[1]).variant));
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(item), context).has_value());
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<Cp::ReduceAsyncBulkSharedAdd>(
      std::get<Cp>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::ReduceAsyncBulkGlobalMin>(
      std::get<Cp>(body[1]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::ReduceAsyncBulkGlobalAddNoftz>(
      std::get<Cp>(body[2]).variant));
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(item), context).has_value());

  const auto invalid = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .u64 %rd<2>;
  cp.reduce.async.bulk.global.shared::cta.bulk_group.add.f16 [%rd0], [%rd1], 16;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(invalid);
  EXPECT_FALSE(resolveModule(*invalid).has_value());
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkCommitGroup>(
      std::get<Cp>(body[0]).variant));
  const auto& read_wait =
      std::get<Cp::AsyncBulkWaitGroup>(std::get<Cp>(body[1]).variant);
  const auto& whole_wait =
      std::get<Cp::AsyncBulkWaitGroup>(std::get<Cp>(body[2]).variant);
  EXPECT_TRUE(read_wait.read.value);
  EXPECT_FALSE(whole_wait.read.value);
  EXPECT_EQ(read_wait.n.value.bits, 0u);
  EXPECT_EQ(whole_wait.n.value.bits, 2u);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(item), context).has_value());
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkGlobalSharedCluster>(
      std::get<Cp>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkGlobalSharedCta>(
      std::get<Cp>(body[1]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkSharedCtaSharedCluster>(
      std::get<Cp>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncBulkSharedCtaGlobal>(
      std::get<Cp>(body[3]).variant));
  EXPECT_EQ(Cp::AsyncBulkSharedCtaGlobal::completion_kind,
            base::AsyncCompletionKind::BulkGroup);
  EXPECT_EQ(Cp::AsyncBulkGlobalSharedCluster::completion_kind,
            base::AsyncCompletionKind::MbarrierCompleteTxBytes);

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  for (const auto& item : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(item), context).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
