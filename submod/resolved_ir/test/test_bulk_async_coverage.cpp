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
