#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve all protocol actions with source-destroyed owned matrix payloads. */
TEST(WgmmaCoverage, OwnsDenseSparseAndProtocolContracts) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.entry kernel() {
  .reg .b32 %d<2>, %a<4>, %meta;
  .reg .f32 %f<4>;
  .reg .s32 %s<4>;
  .reg .b64 %adesc, %bdesc;
  wgmma.fence.sync.aligned;
  wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16
    {%d0,%d1}, %adesc, %bdesc, 1, -1, 1, 0, 1;
  wgmma.mma_async.sync.aligned.m64n8k8.f32.tf32.tf32
    {%f0,%f1,%f2,%f3}, {%a0,%a1,%a2,%a3}, %bdesc, 1, 1, -1;
  wgmma.mma_async.sp.sync.aligned.m64n8k64.s32.s8.u8
    {%s0,%s1,%s2,%s3}, %adesc, %bdesc, %meta, 0, 1;
  wgmma.commit_group.sync.aligned;
  wgmma.wait_group.sync.aligned 4294967296;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  auto& body = owned->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto profile = base::find_target_profile("sm_90a");
  ASSERT_TRUE(profile.has_value());
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
  };
  for (const auto& item : body) {
    const auto* wgmma = item.get_if<Wgmma>();
    ASSERT_NE(wgmma, nullptr);
    EXPECT_TRUE(checker::check(*wgmma, context).has_value());
  }
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());

  const auto& dense = *body[1].get_if<Wgmma>();
  ASSERT_NE(dense.matrix_descriptor(), nullptr);
  EXPECT_EQ(dense.matrix_descriptor()->family, MatrixFamily::WGMMA);
  EXPECT_EQ(dense.matrix_descriptor()->source_placement,
            WgmmaSourcePlacement::SHARED);
  EXPECT_TRUE(dense.matrix_descriptor()->warpgroup.has_value());
  EXPECT_EQ(WarpGroup128::thread_count, 128);
  EXPECT_EQ(dense.matrix_descriptor()->fragments[0].register_count, 2);
  const auto& register_a = *body[2].get_if<Wgmma>();
  ASSERT_NE(register_a.matrix_descriptor(), nullptr);
  EXPECT_EQ(register_a.matrix_descriptor()->source_placement,
            WgmmaSourcePlacement::REGISTER);
  EXPECT_EQ(register_a.matrix_descriptor()->fragments[1].register_count, 4);
  const auto& sparse = *body[3].get_if<Wgmma>();
  ASSERT_NE(sparse.matrix_descriptor(), nullptr);
  EXPECT_EQ(sparse.matrix_descriptor()->family, MatrixFamily::WGMMA_SPARSE);
  EXPECT_EQ(sparse.matrix_descriptor()->sparse_metadata_kind,
            WgmmaSparseMetadataKind::TWO_OF_FOUR);
  EXPECT_EQ(body[0].get_if<Wgmma>()->matrix_descriptor(), nullptr);
  EXPECT_EQ(body[4].get_if<Wgmma>()->matrix_descriptor(), nullptr);
  EXPECT_EQ(body[5].get_if<Wgmma>()->matrix_descriptor(), nullptr);

  auto& mutable_dense = *body[1].get_if<Wgmma>();
  auto& selected = std::get<Wgmma::MmaAsyncDenseM64n8k16F16F16F16SharedPlain>(
      mutable_dense.variant);
  selected.scale_a.value.bits = 0;
  EXPECT_FALSE(checker::check(mutable_dense, context).has_value());
  selected.scale_a.value.bits = UINT32_MAX;
  selected.scale_a.value.integer_source_bits = 1;
  EXPECT_FALSE(checker::check(mutable_dense, context).has_value());
  selected.scale_a.value.integer_source_bits = UINT64_MAX;
  EXPECT_TRUE(checker::check(mutable_dense, context).has_value());
  selected.matrix.value.shape.n = 16;
  EXPECT_FALSE(checker::check(mutable_dense, context).has_value());
  selected.matrix.value.shape.n = 8;
  mutable_dense.semantic_form.value = Wgmma::VariantType::FenceSyncAligned;
  EXPECT_FALSE(checker::check(mutable_dense, context).has_value());

  auto& wait = *body[5].get_if<Wgmma>();
  auto& count = std::get<Wgmma::WaitGroupSyncAligned>(wait.variant).count.value;
  count.integer_source_bits = 3;
  EXPECT_FALSE(checker::check(wait, context).has_value());
}

/** A malformed register pack retains its topology selection and lane error. */
TEST(WgmmaCoverage, RejectsMalformedRegisterFragment) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.entry kernel() {
  .reg .f32 %f<4>;
  .reg .b32 %a<4>;
  .reg .b64 %bdesc;
  wgmma.mma_async.sync.aligned.m64n8k8.f32.tf32.tf32
    {%f0,%f1,%f2,%f3}, {%a0,%a1,%a2}, %bdesc, 1, 1, 1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_NE(resolved.error().front().message.find("element"),
            std::string::npos);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
