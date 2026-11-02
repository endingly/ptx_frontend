#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
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
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->check(context).has_value());
  }
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());

  auto* dense = dynamic_cast<WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain*>(
      body[1].get());
  ASSERT_NE(dense, nullptr);
  ASSERT_NE(dense->matrix_descriptor(), nullptr);
  EXPECT_EQ(dense->matrix_descriptor()->family, MatrixFamily::WGMMA);
  EXPECT_EQ(dense->matrix_descriptor()->source_placement,
            WgmmaSourcePlacement::SHARED);
  EXPECT_TRUE(dense->matrix_descriptor()->warpgroup.has_value());
  EXPECT_EQ(WarpGroup128::thread_count, 128);
  EXPECT_EQ(dense->matrix_descriptor()->fragments[0].register_count, 2);
  auto* register_a =
      dynamic_cast<WgmmaMmaAsyncDenseM64n8k8F32Tf32Tf32RegisterPlain*>(
          body[2].get());
  ASSERT_NE(register_a, nullptr);
  ASSERT_NE(register_a->matrix_descriptor(), nullptr);
  EXPECT_EQ(register_a->matrix_descriptor()->source_placement,
            WgmmaSourcePlacement::REGISTER);
  EXPECT_EQ(register_a->matrix_descriptor()->fragments[1].register_count, 4);
  auto* sparse =
      dynamic_cast<WgmmaMmaAsyncSpM64n8k64S32S8U8SharedPlain*>(body[3].get());
  ASSERT_NE(sparse, nullptr);
  ASSERT_NE(sparse->matrix_descriptor(), nullptr);
  EXPECT_EQ(sparse->matrix_descriptor()->family, MatrixFamily::WGMMA_SPARSE);
  EXPECT_EQ(sparse->matrix_descriptor()->sparse_metadata_kind,
            WgmmaSparseMetadataKind::TWO_OF_FOUR);
  EXPECT_NE(dynamic_cast<WgmmaFenceSyncAligned*>(body[0].get()), nullptr);
  EXPECT_NE(dynamic_cast<WgmmaCommitGroupSyncAligned*>(body[4].get()), nullptr);
  EXPECT_NE(dynamic_cast<WgmmaWaitGroupSyncAligned*>(body[5].get()), nullptr);

  dense->scale_a.value.bits = 0;
  EXPECT_FALSE(dense->check(context).has_value());
  dense->scale_a.value.bits = UINT32_MAX;
  dense->scale_a.value.integer_source_bits = 1;
  EXPECT_FALSE(dense->check(context).has_value());
  dense->scale_a.value.integer_source_bits = UINT64_MAX;
  EXPECT_TRUE(dense->check(context).has_value());
  EXPECT_EQ(dense->matrix_descriptor()->shape.n, 8);
  EXPECT_EQ(dense->matrix_descriptor(), &dense->matrix_topology);
  const auto saved_layout = dense->operand_layout;
  dense->operand_layout.value = 255;
  EXPECT_FALSE(dense->check(context).has_value());
  dense->operand_layout = saved_layout;

  auto* wait = dynamic_cast<WgmmaWaitGroupSyncAligned*>(body[5].get());
  ASSERT_NE(wait, nullptr);
  auto& count = wait->count.value;
  count.integer_source_bits = 3;
  EXPECT_FALSE(wait->check(context).has_value());
}

/** Retain the source-only scale-d predicate domain after the AST is released. */
TEST(WgmmaCoverage, RejectsOwnedScaleDPredicateMutation) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.entry kernel() {
  .reg .pred %p;
  .reg .b32 %d<2>;
  .reg .b64 %a, %b;
  wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16
    {%d0,%d1}, %a, %b, %p, 1, 1, 0, 0;
  wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16
    {%d0,%d1}, %a, %b, 0, 1, 1, 0, 0;
  wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16
    {%d0,%d1}, %a, %b, 1, 1, 1, 0, 0;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  auto& body = owned->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
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
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->check(context).has_value());
  }
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());

  auto* wgmma = dynamic_cast<WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain*>(
      body[0].get());
  ASSERT_NE(wgmma, nullptr);
  auto& predicate = std::get<ResolvedPredicate>(wgmma->scale_d.value);
  const ResolvedPredicate original = predicate;
  const auto rejects_mutation = [&]() {
    EXPECT_FALSE(wgmma->check(context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
    predicate = original;
  };
  predicate.negated = true;
  rejects_mutation();
  predicate.register_ref.register_class = ResolvedRegisterClass::General;
  rejects_mutation();
  predicate.register_ref.declared_type = ScalarType::F32;
  rejects_mutation();
  predicate.register_ref.vector_width = 2;
  rejects_mutation();
  EXPECT_TRUE(wgmma->check(context).has_value());
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());

  const auto* zero =
      dynamic_cast<const WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain*>(
          body[1].get());
  const auto* one =
      dynamic_cast<const WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain*>(
          body[2].get());
  ASSERT_NE(zero, nullptr);
  ASSERT_NE(one, nullptr);
  EXPECT_FALSE(std::get<ResolvedPredicateConstant>(zero->scale_d.value).value);
  EXPECT_TRUE(std::get<ResolvedPredicateConstant>(one->scale_d.value).value);
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

/** One of the finite dense integer boundary source identities. */
struct IntegerBoundaryCase {
  /** Accumulator width N; per-thread scalar S32 arity is N/2. */
  unsigned n;
  /** Independent A/B integer signedness. */
  bool a_unsigned, b_unsigned;
  /** A placement and saturation select distinct generated classes. */
  bool registers, saturated;
  /** Exact canonical raw key and concrete class identity. */
  std::string_view key;
  InstructionKind kind;
  /** Static topology of the selected final class. */
  const MatrixInstructionDescriptor* topology;
};
constexpr IntegerBoundaryCase integer_boundary_cases[] = {
    {240, false, false, false, false,
     "wgmma_mma_async_dense_m64n240k32_s32_s8_s8_shared_plain",
     WgmmaMmaAsyncDenseM64n240k32S32S8S8SharedPlain::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32S8S8SharedPlain::matrix_topology},
    {240, false, false, true, false,
     "wgmma_mma_async_dense_m64n240k32_s32_s8_s8_register_plain",
     WgmmaMmaAsyncDenseM64n240k32S32S8S8RegisterPlain::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32S8S8RegisterPlain::matrix_topology},
    {240, false, false, false, true,
     "wgmma_mma_async_dense_m64n240k32_s32_s8_s8_shared_satfinite",
     WgmmaMmaAsyncDenseM64n240k32S32S8S8SharedSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32S8S8SharedSatfinite::matrix_topology},
    {240, false, false, true, true,
     "wgmma_mma_async_dense_m64n240k32_s32_s8_s8_register_satfinite",
     WgmmaMmaAsyncDenseM64n240k32S32S8S8RegisterSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32S8S8RegisterSatfinite::matrix_topology},
    {240, false, true, false, false,
     "wgmma_mma_async_dense_m64n240k32_s32_s8_u8_shared_plain",
     WgmmaMmaAsyncDenseM64n240k32S32S8U8SharedPlain::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32S8U8SharedPlain::matrix_topology},
    {240, false, true, true, false,
     "wgmma_mma_async_dense_m64n240k32_s32_s8_u8_register_plain",
     WgmmaMmaAsyncDenseM64n240k32S32S8U8RegisterPlain::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32S8U8RegisterPlain::matrix_topology},
    {240, false, true, false, true,
     "wgmma_mma_async_dense_m64n240k32_s32_s8_u8_shared_satfinite",
     WgmmaMmaAsyncDenseM64n240k32S32S8U8SharedSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32S8U8SharedSatfinite::matrix_topology},
    {240, false, true, true, true,
     "wgmma_mma_async_dense_m64n240k32_s32_s8_u8_register_satfinite",
     WgmmaMmaAsyncDenseM64n240k32S32S8U8RegisterSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32S8U8RegisterSatfinite::matrix_topology},
    {240, true, false, false, false,
     "wgmma_mma_async_dense_m64n240k32_s32_u8_s8_shared_plain",
     WgmmaMmaAsyncDenseM64n240k32S32U8S8SharedPlain::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32U8S8SharedPlain::matrix_topology},
    {240, true, false, true, false,
     "wgmma_mma_async_dense_m64n240k32_s32_u8_s8_register_plain",
     WgmmaMmaAsyncDenseM64n240k32S32U8S8RegisterPlain::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32U8S8RegisterPlain::matrix_topology},
    {240, true, false, false, true,
     "wgmma_mma_async_dense_m64n240k32_s32_u8_s8_shared_satfinite",
     WgmmaMmaAsyncDenseM64n240k32S32U8S8SharedSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32U8S8SharedSatfinite::matrix_topology},
    {240, true, false, true, true,
     "wgmma_mma_async_dense_m64n240k32_s32_u8_s8_register_satfinite",
     WgmmaMmaAsyncDenseM64n240k32S32U8S8RegisterSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32U8S8RegisterSatfinite::matrix_topology},
    {240, true, true, false, false,
     "wgmma_mma_async_dense_m64n240k32_s32_u8_u8_shared_plain",
     WgmmaMmaAsyncDenseM64n240k32S32U8U8SharedPlain::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32U8U8SharedPlain::matrix_topology},
    {240, true, true, true, false,
     "wgmma_mma_async_dense_m64n240k32_s32_u8_u8_register_plain",
     WgmmaMmaAsyncDenseM64n240k32S32U8U8RegisterPlain::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32U8U8RegisterPlain::matrix_topology},
    {240, true, true, false, true,
     "wgmma_mma_async_dense_m64n240k32_s32_u8_u8_shared_satfinite",
     WgmmaMmaAsyncDenseM64n240k32S32U8U8SharedSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32U8U8SharedSatfinite::matrix_topology},
    {240, true, true, true, true,
     "wgmma_mma_async_dense_m64n240k32_s32_u8_u8_register_satfinite",
     WgmmaMmaAsyncDenseM64n240k32S32U8U8RegisterSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n240k32S32U8U8RegisterSatfinite::matrix_topology},
    {256, false, false, false, false,
     "wgmma_mma_async_dense_m64n256k32_s32_s8_s8_shared_plain",
     WgmmaMmaAsyncDenseM64n256k32S32S8S8SharedPlain::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32S8S8SharedPlain::matrix_topology},
    {256, false, false, true, false,
     "wgmma_mma_async_dense_m64n256k32_s32_s8_s8_register_plain",
     WgmmaMmaAsyncDenseM64n256k32S32S8S8RegisterPlain::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32S8S8RegisterPlain::matrix_topology},
    {256, false, false, false, true,
     "wgmma_mma_async_dense_m64n256k32_s32_s8_s8_shared_satfinite",
     WgmmaMmaAsyncDenseM64n256k32S32S8S8SharedSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32S8S8SharedSatfinite::matrix_topology},
    {256, false, false, true, true,
     "wgmma_mma_async_dense_m64n256k32_s32_s8_s8_register_satfinite",
     WgmmaMmaAsyncDenseM64n256k32S32S8S8RegisterSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32S8S8RegisterSatfinite::matrix_topology},
    {256, false, true, false, false,
     "wgmma_mma_async_dense_m64n256k32_s32_s8_u8_shared_plain",
     WgmmaMmaAsyncDenseM64n256k32S32S8U8SharedPlain::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32S8U8SharedPlain::matrix_topology},
    {256, false, true, true, false,
     "wgmma_mma_async_dense_m64n256k32_s32_s8_u8_register_plain",
     WgmmaMmaAsyncDenseM64n256k32S32S8U8RegisterPlain::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32S8U8RegisterPlain::matrix_topology},
    {256, false, true, false, true,
     "wgmma_mma_async_dense_m64n256k32_s32_s8_u8_shared_satfinite",
     WgmmaMmaAsyncDenseM64n256k32S32S8U8SharedSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32S8U8SharedSatfinite::matrix_topology},
    {256, false, true, true, true,
     "wgmma_mma_async_dense_m64n256k32_s32_s8_u8_register_satfinite",
     WgmmaMmaAsyncDenseM64n256k32S32S8U8RegisterSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32S8U8RegisterSatfinite::matrix_topology},
    {256, true, false, false, false,
     "wgmma_mma_async_dense_m64n256k32_s32_u8_s8_shared_plain",
     WgmmaMmaAsyncDenseM64n256k32S32U8S8SharedPlain::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32U8S8SharedPlain::matrix_topology},
    {256, true, false, true, false,
     "wgmma_mma_async_dense_m64n256k32_s32_u8_s8_register_plain",
     WgmmaMmaAsyncDenseM64n256k32S32U8S8RegisterPlain::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32U8S8RegisterPlain::matrix_topology},
    {256, true, false, false, true,
     "wgmma_mma_async_dense_m64n256k32_s32_u8_s8_shared_satfinite",
     WgmmaMmaAsyncDenseM64n256k32S32U8S8SharedSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32U8S8SharedSatfinite::matrix_topology},
    {256, true, false, true, true,
     "wgmma_mma_async_dense_m64n256k32_s32_u8_s8_register_satfinite",
     WgmmaMmaAsyncDenseM64n256k32S32U8S8RegisterSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32U8S8RegisterSatfinite::matrix_topology},
    {256, true, true, false, false,
     "wgmma_mma_async_dense_m64n256k32_s32_u8_u8_shared_plain",
     WgmmaMmaAsyncDenseM64n256k32S32U8U8SharedPlain::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32U8U8SharedPlain::matrix_topology},
    {256, true, true, true, false,
     "wgmma_mma_async_dense_m64n256k32_s32_u8_u8_register_plain",
     WgmmaMmaAsyncDenseM64n256k32S32U8U8RegisterPlain::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32U8U8RegisterPlain::matrix_topology},
    {256, true, true, false, true,
     "wgmma_mma_async_dense_m64n256k32_s32_u8_u8_shared_satfinite",
     WgmmaMmaAsyncDenseM64n256k32S32U8U8SharedSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32U8U8SharedSatfinite::matrix_topology},
    {256, true, true, true, true,
     "wgmma_mma_async_dense_m64n256k32_s32_u8_u8_register_satfinite",
     WgmmaMmaAsyncDenseM64n256k32S32U8U8RegisterSatfinite::kind,
     &WgmmaMmaAsyncDenseM64n256k32S32U8U8RegisterSatfinite::matrix_topology},
};

/** Typed generated forms in this finite integer accumulator boundary cohort. */
template <typename T>
concept IntegerBoundaryForm =
    std::derived_from<T, Instruction> &&
    requires(T value) {
      value.d.value.elements;
      value.b_desc.value.register_ref;
      value.scale_d.value;
      value.matrix_descriptor();
    } && (T::matrix_topology.family == MatrixFamily::WGMMA) &&
    (T::matrix_topology.shape.m == 64) && (T::matrix_topology.shape.k == 32) &&
    (T::matrix_topology.shape.n == 240 || T::matrix_topology.shape.n == 256) &&
    (T::matrix_topology.fragments[0].register_type == ScalarType::S32);

/** Borrow only real mutable members; no fixed topology is replaced or edited. */
struct IntegerBoundaryFields {
  /** Static generated topology belonging to this exact class. */
  const MatrixInstructionDescriptor& topology;
  /** Scalar accumulator pack and original per-lane ranges. */
  WithLocs<ResolvedRegisterVector>& d;
  /** Shared A is present only on descriptor-source classes. */
  WithLocs<ResolvedSharedMatrixDescriptor>* a_desc;
  /** Register A is present only on four-register-source classes. */
  WithLocs<ResolvedRegisterVector>* a;
  /** Opaque B descriptor and the real selected operand layout. */
  WithLocs<ResolvedSharedMatrixDescriptor>& b_desc;
  /** Current mutable layout tag; fixed A placement remains generated identity. */
  ResolvedOperandLayoutTag& layout;
};

/** Preserve the distinct A representations when borrowing a concrete form. */
template <IntegerBoundaryForm T>
IntegerBoundaryFields integerBoundaryFields(T& form) {
  if constexpr (requires { form.a_desc; })
    return {
        *form.matrix_descriptor(), form.d, &form.a_desc, nullptr, form.b_desc,
        form.operand_layout};
  else
    return {*form.matrix_descriptor(), form.d, nullptr, &form.a, form.b_desc,
            form.operand_layout};
}

/** Require one of the existing 32 exact generated classes before borrowing fields. */
IntegerBoundaryFields integerBoundaryFields(Instruction& instruction) {
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32S8S8SharedPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32S8S8RegisterPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32S8S8SharedSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32S8S8RegisterSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32S8U8SharedPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32S8U8RegisterPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32S8U8SharedSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32S8U8RegisterSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32U8S8SharedPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32U8S8RegisterPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32U8S8SharedSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32U8S8RegisterSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32U8U8SharedPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32U8U8RegisterPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32U8U8SharedSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n240k32S32U8U8RegisterSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32S8S8SharedPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32S8S8RegisterPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32S8S8SharedSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32S8S8RegisterSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32S8U8SharedPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32S8U8RegisterPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32S8U8SharedSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32S8U8RegisterSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32U8S8SharedPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32U8S8RegisterPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32U8S8SharedSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32U8S8RegisterSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32U8U8SharedPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32U8U8RegisterPlain*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32U8U8SharedSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  if (auto* form =
          dynamic_cast<WgmmaMmaAsyncDenseM64n256k32S32U8U8RegisterSatfinite*>(
              &instruction))
    return integerBoundaryFields(*form);
  throw std::logic_error("Unexpected integer WGMMA boundary owner.");
}

/** Spell distinct scalar brace lanes, including the exact 128-register boundary. */
std::string integerBoundaryPack(char role, unsigned count) {
  std::string text = "{";
  for (unsigned lane = 0; lane < count; ++lane)
    text += (lane ? ",%" : "%") + std::string(1, role) + std::to_string(lane);
  return text + "}";
}

/** Build canonical source without inferring integer shapes from another family. */
std::string integerBoundaryInstruction(const IntegerBoundaryCase& item,
                                       unsigned count = 0, unsigned n = 0,
                                       unsigned k = 32) {
  const auto width = n ? n : item.n;
  return "wgmma.mma_async.sync.aligned.m64n" + std::to_string(width) + "k" +
         std::to_string(k) + (item.saturated ? ".satfinite" : "") + ".s32." +
         (item.a_unsigned ? "u8" : "s8") + "." +
         (item.b_unsigned ? "u8" : "s8") + " " +
         integerBoundaryPack('d', count ? count : width / 2) + ", " +
         (item.registers ? integerBoundaryPack('a', 4) : "%adesc") +
         ", %bdesc, 1;";
}

/** Initialize real scalar declarations; descriptors remain opaque runtime values. */
std::string integerBoundaryModule(std::string_view instruction,
                                  std::string_view version = "9.3",
                                  std::string_view target = "sm_90a",
                                  bool protocol = true) {
  std::string text =
      ".version " + std::string(version) + "\n.target " + std::string(target) +
      "\n.address_size 64\n.entry kernel(){\n"
      ".reg .s32 %d<132>,%spare;\n.reg .b32 %a<4>;\n.reg .u32 %wrong;\n"
      ".reg .b64 %adesc,%bdesc,%alt;\nmov.b32 %spare,0;\nmov.b32 %wrong,0;\n"
      "mov.b64 %alt,0;\nmov.b64 %adesc,0;\nmov.b64 %bdesc,0;\n";
  for (unsigned lane = 0; lane < 132; ++lane)
    text += "mov.b32 %d" + std::to_string(lane) + ",0;\n";
  for (unsigned lane = 0; lane < 4; ++lane)
    text += "mov.b32 %a" + std::to_string(lane) + ",0;\n";
  return text + (protocol ? "wgmma.fence.sync.aligned;\n" : "") +
         std::string(instruction) +
         (protocol ? "\nwgmma.commit_group.sync.aligned;\nwgmma.wait_group."
                     "sync.aligned 0;\n}"
                   : "\n}");
}

/** Own checked state only after its actual parsing buffer/parser/AST have died. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> ownedIntegerBoundary(
    std::string text) {
  const auto parsed = test_helpers::parseModule(text);
  EXPECT_TRUE(parsed && parsed.diagnostics.empty());
  if (!parsed || !parsed.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModule(*parsed);
}

/** Select the issue between the real fence and commit/wait control instructions. */
Instruction& integerBoundaryIssue(ResolvedModule& module) {
  auto& body = module.functions.front().body;
  return *body[body.size() - 3];
}

/** Reuse the public target catalogue rather than numeric SM inheritance. */
checker::Context integerBoundaryContext() {
  const auto profile = base::find_target_profile("sm_90a");
  EXPECT_TRUE(profile);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** All 32 boundary spellings select distinct classes and exact scalar fragments. */
TEST(WgmmaCoverage, IntegerBoundarySourcesAndOwnedLifetimes) {
  std::set<InstructionKind> identities;
  for (const auto& item : integer_boundary_cases) {
    SCOPED_TRACE(item.key);
    auto owned = ownedIntegerBoundary(
        integerBoundaryModule(integerBoundaryInstruction(item)));
    ASSERT_TRUE(owned) << owned.error().front().message;
    auto& instruction = integerBoundaryIssue(*owned);
    auto fields = integerBoundaryFields(instruction);
    EXPECT_EQ(instruction.instruction_kind(), item.kind);
    EXPECT_TRUE(identities.insert(instruction.instruction_kind()).second);
    EXPECT_EQ(&fields.topology, item.topology);
    EXPECT_EQ(fields.topology.shape,
              (MatrixShape{64, static_cast<uint16_t>(item.n), 32}));
    EXPECT_EQ(fields.topology.source_placement,
              item.registers ? WgmmaSourcePlacement::REGISTER
                             : WgmmaSourcePlacement::SHARED);
    EXPECT_EQ(fields.topology.fragments[0].register_count, item.n / 2);
    EXPECT_EQ(fields.d.value.elements.size(), item.n / 2);
    EXPECT_EQ(fields.d.locs.size(), item.n / 2);
    EXPECT_EQ(fields.topology.fragments[0].element_type,
              MatrixElementType::S32);
    EXPECT_EQ(fields.topology.fragments[0].register_type, ScalarType::S32);
    EXPECT_EQ(fields.d.value.source.kind, ResolvedVectorSourceKind::BraceList);
    for (const auto& lane : fields.d.value.elements) {
      ASSERT_TRUE(lane);
      EXPECT_EQ(lane->declared_type, ScalarType::S32);
      EXPECT_EQ(lane->register_class, ResolvedRegisterClass::General);
      EXPECT_FALSE(lane->vector_width);
      EXPECT_FALSE(lane->component);
      EXPECT_TRUE(lane->symbol_id);
    }
    if (item.registers) {
      ASSERT_NE(fields.a, nullptr);
      EXPECT_EQ(fields.a_desc, nullptr);
      EXPECT_EQ(fields.a->value.elements.size(), 4u);
      EXPECT_EQ(fields.topology.fragments[1].register_count, 4);
      EXPECT_EQ(fields.topology.fragments[1].register_type, ScalarType::B32);
    } else {
      ASSERT_NE(fields.a_desc, nullptr);
      EXPECT_EQ(fields.a, nullptr);
      EXPECT_EQ(fields.a_desc->value.register_ref.declared_type,
                ScalarType::B64);
    }
    EXPECT_EQ(fields.b_desc.value.register_ref.declared_type, ScalarType::B64);
    auto& body = owned->functions.front().body;
    EXPECT_NE(dynamic_cast<WgmmaFenceSyncAligned*>(body[body.size() - 4].get()),
              nullptr);
    EXPECT_NE(
        dynamic_cast<WgmmaCommitGroupSyncAligned*>(body[body.size() - 2].get()),
        nullptr);
    EXPECT_NE(dynamic_cast<WgmmaWaitGroupSyncAligned*>(body.back().get()),
              nullptr);
    EXPECT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
    ResolvedModule copied = *owned;
    ResolvedModule assigned;
    assigned = copied;
    ResolvedModule moved = std::move(assigned);
    EXPECT_TRUE(
        validateModule(moved, ModuleValidationPolicy::RequireCompleteContext));
    auto fresh = integerBoundaryFields(integerBoundaryIssue(moved));
    EXPECT_EQ(&fresh.topology, item.topology);
    EXPECT_EQ(fresh.d, fields.d);
    auto clone = integerBoundaryIssue(moved).clone();
    EXPECT_EQ(integerBoundaryFields(*clone).b_desc, fields.b_desc);
    EXPECT_TRUE(clone->check(integerBoundaryContext()));
    ResolvedFunction function_copy = owned->functions.front();
    ResolvedFunction function_assigned;
    function_assigned = function_copy;
    auto& function_issue =
        *function_assigned.body[function_assigned.body.size() - 3];
    EXPECT_EQ(integerBoundaryFields(function_issue).d, fields.d);
    EXPECT_TRUE(function_issue.check(integerBoundaryContext()));
  }
  EXPECT_EQ(identities.size(), 32u);
}

/** Wrong 120/128-boundary counts and real descriptor widths fail independently. */
TEST(WgmmaCoverage, IntegerBoundarySourceAndOwnedCounts) {
  for (const unsigned n : {240u, 256u}) {
    const auto& item = integer_boundary_cases[n == 240 ? 0 : 17];
    ASSERT_EQ(item.n, n);
    const auto text = integerBoundaryModule(integerBoundaryInstruction(item));
    auto owned = ownedIntegerBoundary(text);
    ASSERT_TRUE(owned);
    auto& issue = integerBoundaryIssue(*owned);
    auto fields = integerBoundaryFields(issue);
    const auto saved = fields.d;
    auto& body = owned->functions.front().body;
    const auto spare = dynamic_cast<MovScalar&>(*body[0]).dst_register->value;
    const auto wrong = dynamic_cast<MovScalar&>(*body[1]).dst_register->value;
    const auto alt = dynamic_cast<MovScalar&>(*body[2]).dst_register->value;
    const auto good_ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(good_ast);
    for (const unsigned count : {n / 2 - 1, n / 2 + 1}) {
      const auto ast = test_helpers::parseModule(
          integerBoundaryModule(integerBoundaryInstruction(item, count)));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto bad_source = resolveModule(*ast);
      ASSERT_FALSE(bad_source);
      EXPECT_EQ(bad_source.error().front().stage(),
                ResolveDiagnosticStage::Resolution);
      EXPECT_EQ(bad_source.error().front().message,
                "Vector operand requires " + std::to_string(n / 2) + " to " +
                    std::to_string(n / 2) + " elements.");
      const auto expected_end = SourcePos{
          saved.value.source.range.start.line,
          saved.value.source.range.start.column +
              static_cast<int32_t>(integerBoundaryPack('d', count).size())};
      EXPECT_EQ(bad_source.error().front().range,
                (SourceRange{saved.value.source.range.start, expected_end}));
      if (count < n / 2) {
        fields.d.value.elements.pop_back();
        fields.d.locs.pop_back();
      } else {
        fields.d.value.elements.push_back(spare);
        fields.d.locs.push_back(saved.locs.front());
      }
      ASSERT_TRUE(
          valid_register_vector_source(fields.d.value, fields.d.locs, true));
      const auto current = issue.check(integerBoundaryContext());
      ASSERT_FALSE(current);
      EXPECT_TRUE(std::ranges::any_of(current.error(), [&](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::InvalidVectorOperand &&
               d.range == saved.locs.front();
      }));
      const auto module = validateModule(
          *owned, ModuleValidationPolicy::RequireCompleteContext);
      ASSERT_FALSE(module);
      EXPECT_TRUE(std::ranges::any_of(module.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::InvalidVectorOperand;
      }));
      const auto provided = validateModule(
          *good_ast, *owned, ModuleValidationPolicy::RequireCompleteContext);
      ASSERT_FALSE(provided);
      EXPECT_TRUE(std::ranges::any_of(provided.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::InvalidVectorOperand;
      }));
      fields.d = saved;
      EXPECT_TRUE(validateModule(
          *good_ast, *owned, ModuleValidationPolicy::RequireCompleteContext));
    }
    fields.d.value.elements.front() = spare;
    EXPECT_TRUE(validateModule(*good_ast, *owned,
                               ModuleValidationPolicy::RequireCompleteContext));
    fields.d = saved;
    const auto original_layout = fields.layout;
    fields.layout.value = 9999;
    const auto invalid_layout = issue.check(integerBoundaryContext());
    ASSERT_FALSE(invalid_layout);
    EXPECT_EQ(invalid_layout.error().back().kind,
              checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
    EXPECT_FALSE(validateModule(
        *good_ast, *owned, ModuleValidationPolicy::RequireCompleteContext));
    fields.layout = original_layout;
    EXPECT_TRUE(validateModule(*good_ast, *owned,
                               ModuleValidationPolicy::RequireCompleteContext));
    const auto descriptor = fields.b_desc;
    fields.b_desc.value.register_ref = wrong;
    const auto bad_width = issue.check(integerBoundaryContext());
    ASSERT_FALSE(bad_width);
    EXPECT_TRUE(std::ranges::any_of(bad_width.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::OperandTypeMismatch;
    }));
    EXPECT_FALSE(validateModule(
        *good_ast, *owned, ModuleValidationPolicy::RequireCompleteContext));
    fields.b_desc = descriptor;
    fields.b_desc.value.register_ref = alt;
    EXPECT_TRUE(validateModule(*good_ast, *owned,
                               ModuleValidationPolicy::RequireCompleteContext));
    fields.b_desc = descriptor;
    if (fields.a) {
      const auto original_a = *fields.a;
      fields.a->value.elements.pop_back();
      fields.a->locs.pop_back();
      const auto bad_a = issue.check(integerBoundaryContext());
      ASSERT_FALSE(bad_a);
      EXPECT_TRUE(std::ranges::any_of(bad_a.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::InvalidVectorOperand;
      }));
      EXPECT_FALSE(validateModule(
          *good_ast, *owned, ModuleValidationPolicy::RequireCompleteContext));
      *fields.a = original_a;
      EXPECT_TRUE(validateModule(
          *good_ast, *owned, ModuleValidationPolicy::RequireCompleteContext));
    }
  }
}

/** Off-grid shapes, wrong K and exact-target/version constraints stay separate. */
TEST(WgmmaCoverage, IntegerBoundaryGridAndAvailability) {
  const auto& same = integer_boundary_cases[0];
  const auto& mixed = integer_boundary_cases[4];
  ASSERT_NE(mixed.a_unsigned, mixed.b_unsigned);
  auto canonical_owner = ownedIntegerBoundary(
      integerBoundaryModule(integerBoundaryInstruction(same)));
  ASSERT_TRUE(canonical_owner);
  for (const unsigned n : {232u, 248u, 264u}) {
    const auto text =
        integerBoundaryModule(integerBoundaryInstruction(same, 0, n));
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto result = resolveModule(*ast);
    ASSERT_FALSE(result);
    const auto association = validateModule(
        *ast, *canonical_owner, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(association);
    EXPECT_TRUE(std::ranges::any_of(association.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
    EXPECT_EQ(result.error().front().stage(),
              ResolveDiagnosticStage::Resolution);
    EXPECT_EQ(result.error().front().message,
              n == 264 ? "Unknown modifier '.m64n264k32'."
                       : "No variant of instruction 'wgmma' accepts this "
                         "modifier combination.");
  }
  const auto wrong_k = test_helpers::parseModule(
      integerBoundaryModule(integerBoundaryInstruction(same, 0, 0, 16)));
  ASSERT_MODULE_PARSE_SUCCEEDS(wrong_k);
  const auto bad_k = resolveModule(*wrong_k);
  ASSERT_FALSE(bad_k);
  EXPECT_EQ(bad_k.error().front().stage(), ResolveDiagnosticStage::Resolution);
  EXPECT_EQ(
      bad_k.error().front().message,
      "No variant of instruction 'wgmma' accepts this modifier combination.");
  for (const auto target : {"sm_90", "sm_100a"}) {
    const auto ast = test_helpers::parseModule(integerBoundaryModule(
        integerBoundaryInstruction(same), "9.3", target, false));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto result = resolveModule(*ast);
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
      return d.stage() == ResolveDiagnosticStage::Checking &&
             d.checker_kind ==
                 checker::CheckDiagnosticKind::UnsupportedAvailability &&
             d.message ==
                 "Instruction variant "
                 "'MmaAsyncDenseM64n240k32S32S8S8SharedPlain' has no matching "
                 "availability clause.";
    }));
  }
  EXPECT_TRUE(ownedIntegerBoundary(
      integerBoundaryModule(integerBoundaryInstruction(same), "8.0")));
  EXPECT_TRUE(ownedIntegerBoundary(
      integerBoundaryModule(integerBoundaryInstruction(mixed), "8.4")));
  const auto lower = test_helpers::parseModule(
      integerBoundaryModule(integerBoundaryInstruction(mixed), "8.3"));
  ASSERT_MODULE_PARSE_SUCCEEDS(lower);
  const auto too_old = resolveModule(*lower);
  ASSERT_FALSE(too_old);
  EXPECT_EQ(too_old.error().front().stage(), ResolveDiagnosticStage::Checking);
  EXPECT_EQ(too_old.error().front().checker_kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  EXPECT_EQ(too_old.error().front().message,
            "Instruction variant 'MmaAsyncDenseM64n240k32S32S8U8SharedPlain' "
            "has no matching availability clause.");
  auto wrong_descriptor = integerBoundaryInstruction(same);
  wrong_descriptor.replace(wrong_descriptor.find("%bdesc"), 6, "%wrong");
  const auto bad_descriptor = test_helpers::parseModule(
      integerBoundaryModule(wrong_descriptor, "9.3", "sm_90a", false));
  ASSERT_MODULE_PARSE_SUCCEEDS(bad_descriptor);
  const auto descriptor_result = resolveModule(*bad_descriptor);
  ASSERT_FALSE(descriptor_result);
  EXPECT_TRUE(std::ranges::any_of(descriptor_result.error(), [](const auto& d) {
    return d.checker_kind == checker::CheckDiagnosticKind::OperandTypeMismatch;
  }));
  const auto& reg = integer_boundary_cases[1];
  auto wrong_a = integerBoundaryInstruction(reg);
  const auto a_pack = integerBoundaryPack('a', 4);
  wrong_a.replace(wrong_a.find(a_pack), a_pack.size(),
                  integerBoundaryPack('a', 3));
  const auto bad_a = test_helpers::parseModule(integerBoundaryModule(wrong_a));
  ASSERT_MODULE_PARSE_SUCCEEDS(bad_a);
  EXPECT_FALSE(resolveModule(*bad_a));
}

/** Build real physical donor symbols and independent same-width bit-data controls. */
std::string descriptor_role_source(std::string_view statement) {
  return R"ptx(.version 9.3
.target sm_90a
.address_size 64
.entry descriptor_roles() {
.reg .b32 %d<2>, %b32;
.reg .u32 %u32;
.reg .s32 %s32;
.reg .f32 %f32;
.reg .b64 %b64;
.reg .u64 %u64;
.reg .s64 %s64;
.reg .f64 %f64;
.reg .v2 .b32 %v32;
.reg .v2 .b64 %v64;
.reg .pred %p;
mov.b32 %f32, 0;
mov.b64 %f64, 0;
mov.b32 %b32, %f32;
mov.b64 %b64, %f64;
mov.u32 %u32, 0;
mov.s32 %s32, 0;
mov.u64 %u64, 0;
mov.s64 %s64, 0;
add.rn.f64 %f64,%f64,%f64;
)ptx" + std::string(statement) +
         "\nret;\n}\n";
}

/** Parse owned source without retaining parser storage. */
std::optional<syntax_ast::AstModule> descriptor_role_ast(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty())
    return std::nullopt;
  return std::move(*parsed);
}

/** Exact-width descriptor carriers differ from fundamental bit-data compatibility. */
TEST(WgmmaCoverage, DeclaredDescriptorCarriersAndCurrentRoles) {
  const auto profile = base::find_target_profile("sm_90a");
  ASSERT_TRUE(profile);
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
  {
    SCOPED_TRACE("WGMMA-A");
    const auto source_for = [](std::string_view carrier) {
      std::string statement =
          "wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16 {%d0,%d1}, "
          "CARRIER, %b64,1,1,1,0,0;";
      statement.replace(statement.find("CARRIER"), 7, carrier);
      return descriptor_role_source(statement);
    };
    for (std::string_view carrier : {"%b64", "%u64", "%s64"}) {
      const auto source = source_for(carrier);
      auto ast = descriptor_role_ast(source);
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result) << result.error().front().message;
      auto copy = *result;
      EXPECT_TRUE(
          validateModule(copy, ModuleValidationPolicy::RequireCompleteContext));
    }
    for (std::string_view carrier :
         {"%f64", "%b32", "%v64", "%p", "0", "[%b64]"}) {
      auto ast = descriptor_role_ast(source_for(carrier));
      ASSERT_TRUE(ast) << carrier;
      const auto lowered = resolveModule(*ast);
      ASSERT_FALSE(lowered) << carrier;
      EXPECT_FALSE(lowered.error().empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast)) << carrier;
    }
    const auto good_source = source_for("%b64");
    std::optional<ResolvedModule> owned;
    {
      auto ast = descriptor_role_ast(good_source);
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result);
      owned.emplace(std::move(*result));
    }
    auto& function = owned->functions.front();
    auto* form = dynamic_cast<WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain*>(
        function.body[9].get());
    ASSERT_NE(form, nullptr);
    auto& role = form->a_desc.value.register_ref;
    const auto original = role;
    const auto donor = [&](ScalarType type) {
      for (const auto& item : function.body) {
        if (const auto* mov = dynamic_cast<const MovScalar*>(item.get());
            mov && mov->dst_register &&
            mov->dst_register->value.declared_type == type)
          return mov->dst_register->value;
      }
      ADD_FAILURE() << "Missing real declared donor";
      return original;
    };
    const auto reject_role = [&]() {
      const auto direct = form->check(context);
      ASSERT_FALSE(direct);
      EXPECT_TRUE(
          std::ranges::any_of(direct.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
      const auto current = validateModule(
          *owned, ModuleValidationPolicy::RequireCompleteContext);
      ASSERT_FALSE(current);
      EXPECT_TRUE(
          std::ranges::any_of(current.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
      auto good_ast = descriptor_role_ast(good_source);
      ASSERT_TRUE(good_ast);
      EXPECT_FALSE(validateModule(*good_ast, *owned));
    };
    for (auto type : {ScalarType::F64, ScalarType::B32}) {
      role = donor(type);
      reject_role();
      auto bad_copy = *owned;
      EXPECT_FALSE(validateModule(bad_copy));
      auto bad_function = function;
      EXPECT_FALSE(bad_function.body[9]->check(context));
      auto bad_clone = form->clone();
      EXPECT_FALSE(bad_clone->check(context));
      role = original;
      EXPECT_TRUE(validateModule(*owned));
    }
    role.declared_type.reset();
    reject_role();
    role = original;
    role.declared_type = ScalarType::F64;
    const auto forged_cache = validateModule(*owned);
    ASSERT_FALSE(forged_cache);
    EXPECT_TRUE(
        std::ranges::any_of(forged_cache.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
    role = donor(ScalarType::F64);
    role.declared_type = ScalarType::B64;
    EXPECT_FALSE(validateModule(*owned));
    role.declared_type.reset();
    EXPECT_FALSE(validateModule(*owned));
    role = original;
    role.symbol_id.reset();
    role.declared_type.reset();
    EXPECT_TRUE(form->check(context));
    EXPECT_FALSE(validateModule(*owned));
    role = original;
    for (auto type : {ScalarType::U64, ScalarType::S64}) {
      role = donor(type);
      EXPECT_TRUE(form->check(context));
      EXPECT_TRUE(validateModule(*owned));
    }
    role = original;
    auto copy = *owned;
    ResolvedModule assigned;
    assigned = copy;
    auto moved = std::move(assigned);
    EXPECT_TRUE(validateModule(moved));
    auto function_copy = function;
    function_copy = function;
    auto instruction_copy = form->clone();
    EXPECT_TRUE(instruction_copy->check(context));
    EXPECT_NE(dynamic_cast<WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain*>(
                  function_copy.body[9].get()),
              nullptr);
    auto good_ast = descriptor_role_ast(good_source);
    ASSERT_TRUE(good_ast);
    EXPECT_TRUE(validateModule(*good_ast, *owned));
    auto changed_ast = descriptor_role_ast(source_for("%u64"));
    ASSERT_TRUE(changed_ast);
    const auto changed_result = validateModule(*changed_ast, *owned);
    ASSERT_FALSE(changed_result);
    EXPECT_TRUE(
        std::ranges::any_of(changed_result.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
  {
    SCOPED_TRACE("WGMMA-B");
    const auto source_for = [](std::string_view carrier) {
      std::string statement =
          "wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16 {%d0,%d1}, %b64, "
          "CARRIER,1,1,1,0,0;";
      statement.replace(statement.find("CARRIER"), 7, carrier);
      return descriptor_role_source(statement);
    };
    for (std::string_view carrier : {"%b64", "%u64", "%s64"}) {
      const auto source = source_for(carrier);
      auto ast = descriptor_role_ast(source);
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result) << result.error().front().message;
      auto copy = *result;
      EXPECT_TRUE(
          validateModule(copy, ModuleValidationPolicy::RequireCompleteContext));
    }
    for (std::string_view carrier :
         {"%f64", "%b32", "%v64", "%p", "0", "[%b64]"}) {
      auto ast = descriptor_role_ast(source_for(carrier));
      ASSERT_TRUE(ast) << carrier;
      const auto lowered = resolveModule(*ast);
      ASSERT_FALSE(lowered) << carrier;
      EXPECT_FALSE(lowered.error().empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast)) << carrier;
    }
    const auto good_source = source_for("%b64");
    std::optional<ResolvedModule> owned;
    {
      auto ast = descriptor_role_ast(good_source);
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result);
      owned.emplace(std::move(*result));
    }
    auto& function = owned->functions.front();
    auto* form = dynamic_cast<WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain*>(
        function.body[9].get());
    ASSERT_NE(form, nullptr);
    auto& role = form->b_desc.value.register_ref;
    const auto original = role;
    const auto donor = [&](ScalarType type) {
      for (const auto& item : function.body) {
        if (const auto* mov = dynamic_cast<const MovScalar*>(item.get());
            mov && mov->dst_register &&
            mov->dst_register->value.declared_type == type)
          return mov->dst_register->value;
      }
      ADD_FAILURE() << "Missing real declared donor";
      return original;
    };
    const auto reject_role = [&]() {
      const auto direct = form->check(context);
      ASSERT_FALSE(direct);
      EXPECT_TRUE(
          std::ranges::any_of(direct.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
      const auto current = validateModule(
          *owned, ModuleValidationPolicy::RequireCompleteContext);
      ASSERT_FALSE(current);
      EXPECT_TRUE(
          std::ranges::any_of(current.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
      auto good_ast = descriptor_role_ast(good_source);
      ASSERT_TRUE(good_ast);
      EXPECT_FALSE(validateModule(*good_ast, *owned));
    };
    for (auto type : {ScalarType::F64, ScalarType::B32}) {
      role = donor(type);
      reject_role();
      auto bad_copy = *owned;
      EXPECT_FALSE(validateModule(bad_copy));
      auto bad_function = function;
      EXPECT_FALSE(bad_function.body[9]->check(context));
      auto bad_clone = form->clone();
      EXPECT_FALSE(bad_clone->check(context));
      role = original;
      EXPECT_TRUE(validateModule(*owned));
    }
    role.declared_type.reset();
    reject_role();
    role = original;
    role.declared_type = ScalarType::F64;
    const auto forged_cache = validateModule(*owned);
    ASSERT_FALSE(forged_cache);
    EXPECT_TRUE(
        std::ranges::any_of(forged_cache.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
    role = donor(ScalarType::F64);
    role.declared_type = ScalarType::B64;
    EXPECT_FALSE(validateModule(*owned));
    role.declared_type.reset();
    EXPECT_FALSE(validateModule(*owned));
    role = original;
    role.symbol_id.reset();
    role.declared_type.reset();
    EXPECT_TRUE(form->check(context));
    EXPECT_FALSE(validateModule(*owned));
    role = original;
    for (auto type : {ScalarType::U64, ScalarType::S64}) {
      role = donor(type);
      EXPECT_TRUE(form->check(context));
      EXPECT_TRUE(validateModule(*owned));
    }
    role = original;
    auto copy = *owned;
    ResolvedModule assigned;
    assigned = copy;
    auto moved = std::move(assigned);
    EXPECT_TRUE(validateModule(moved));
    auto function_copy = function;
    function_copy = function;
    auto instruction_copy = form->clone();
    EXPECT_TRUE(instruction_copy->check(context));
    EXPECT_NE(dynamic_cast<WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain*>(
                  function_copy.body[9].get()),
              nullptr);
    auto good_ast = descriptor_role_ast(good_source);
    ASSERT_TRUE(good_ast);
    EXPECT_TRUE(validateModule(*good_ast, *owned));
    auto changed_ast = descriptor_role_ast(source_for("%u64"));
    ASSERT_TRUE(changed_ast);
    const auto changed_result = validateModule(*changed_ast, *owned);
    ASSERT_FALSE(changed_result);
    EXPECT_TRUE(
        std::ranges::any_of(changed_result.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
