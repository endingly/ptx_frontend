#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap a candidate in a complete module with typed memory declarations. */
std::string multimem_source(std::string_view body,
                            std::string_view target = "sm_100a",
                            std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.global .align 16 .b8 mm[256];
.shared .align 16 .b8 sh[256];
.local .align 16 .b8 local_value[32];
.visible .entry kernel() {
  .reg .b32 %b<8>;
  .reg .u32 %u<8>;
  .reg .u64 %u64;
  .reg .u16 %mask;
  .reg .f32 %f<8>;
  .reg .f64 %d;
  .reg .b64 %addr;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse and check the owned module with no assembler or GPU execution. */
bool accepts_multimem(std::string_view source) {
  const auto parsed = test_helpers::parseModule(source);
  if (!parsed.has_value() || !parsed.diagnostics.empty())
    return false;
  return resolveAndValidateModule(*parsed).has_value();
}

/** Seven source families retain distinct exact classes and completion identities. */
TEST(Multimem, SevenFamiliesAndOwnedCompletion) {
  const auto parsed = test_helpers::parseModule(multimem_source(R"ptx(
  multimem.ld_reduce.and.b32 %b0, [mm];
  multimem.st.weak.b32 [mm], %b0;
  multimem.red.relaxed.gpu.add.u32 [mm], %u0;
  multimem.st.async.release.gpu.global.u32 [%addr], %u0;
  multimem.red.async.release.sys.add.u64 [%addr], %u64;
  multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [sh], 16;
  multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.add.u32 [mm], [sh], 16;
)ptx"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  EXPECT_NE(
      dynamic_cast<MultimemLdReduceIntegerAndScalarDefaultWeak*>(body[0].get()),
      nullptr);
  EXPECT_NE(
      dynamic_cast<MultimemStIntegerNoneScalarDefaultWeak*>(body[1].get()),
      nullptr);
  EXPECT_NE(dynamic_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[2].get()),
            nullptr);
  EXPECT_NE(dynamic_cast<MultimemStAsyncRelease*>(body[3].get()), nullptr);
  EXPECT_NE(dynamic_cast<MultimemRedAsyncRelease*>(body[4].get()), nullptr);
  EXPECT_NE(dynamic_cast<MultimemCpAsyncBulkWeak*>(body[5].get()), nullptr);
  EXPECT_NE(dynamic_cast<MultimemCpReduceAsyncBulkAddDefault*>(body[6].get()),
            nullptr);
  EXPECT_EQ(MultimemStAsyncRelease::completion_kind,
            base::AsyncCompletionKind::None);
  EXPECT_EQ(MultimemRedAsyncRelease::completion_kind,
            base::AsyncCompletionKind::None);
  EXPECT_EQ(MultimemCpAsyncBulkWeak::completion_kind,
            base::AsyncCompletionKind::BulkGroup);
}

/** Normative Syntax and operation tables outrank the conflicting examples. */
TEST(Multimem, SourceExclusionsAndTables) {
  for (std::string_view candidate : {
           "multimem.red.relaxed.gpu.max.f64 [mm], %d;",
           "multimem.ld_reduce.max.f64 %d, [mm];",
           "multimem.ld_reduce.min.f32 %f0, [mm];",
           "multimem.ld_reduce.add.acc::f16.f32 %f0, [mm];",
           "multimem.st.v8.f64 [mm], {%f0,%f1,%f2,%f3,%f4,%f5,%f6,%f7};",
           "multimem.st.weak.gpu.b32 [mm], %b0;",
           "multimem.ld_reduce.gpu.add.u32 %u0, [mm];",
           "multimem.ld_reduce.relaxed.add.u32 %u0, [mm];",
           "multimem.red.async.release.gpu.add.s64 [%addr], %u64;",
           "multimem.st.async.relaxed.gpu.u32 [%addr], %u0;",
           "multimem.st.async.release.cta.u32 [%addr], %u0;",
           "multimem.st.async.release.gpu.u32 [mm], %u0;",
           "multimem.red.async.release.gpu.add.u32 [mm], %u0;",
           "multimem.cp.async.bulk.global.bulk_group [mm], [sh], 16;",
           "multimem.cp.reduce.async.bulk.global.bulk_group.add.u32 [mm], "
           "[sh], 16;",
           "multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.add."
           "u32 [mm], [sh], 16, [mm];",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group.cp_mask [mm], "
           "[sh], 16;",
           "multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group "
           "[mm], [sh], 16;",
           "multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.add."
           "f16 [mm], [sh], 16;",
           "multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.inc."
           "s32 [mm], [sh], 16;",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_FALSE(accepts_multimem(multimem_source(candidate)));
  }
  for (std::string_view candidate : {
           "multimem.ld_reduce.add.f32 %f0, [mm];",
           "multimem.red.add.f32 [mm], %f0;",
           "multimem.red.add.u32 [mm], %u0;",
           "multimem.red.release.sys.add.u32 [mm], %u0;",
           "multimem.red.gpu.add.u32 [mm], %u0;",
           "multimem.red.release.add.u32 [mm], %u0;",
           "multimem.ld_reduce.add.acc::f32.v2.f16x2 {%b0,%b1}, [mm];",
           "multimem.st.v4.f32 [mm], {%f0,%f1,%f2,%f3};",
           "multimem.st.v4.f32 [mm+16], {%f0,%f1,%f2,%f3};",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group.cp_mask [mm], "
           "[sh], 16, %mask;",
           "multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group."
           "b128 [mm], [sh], 16;",
           "multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.add."
           "noftz.f16 [mm], [sh], 16;",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_TRUE(accepts_multimem(multimem_source(candidate)));
  }
}

/** Known address spaces, widths, alignments, and immediate size remain checked. */
TEST(Multimem, StaticAddressAndSizeConstraints) {
  for (std::string_view candidate : {
           "multimem.st.u32 [local_value], %u0;",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group [sh], [sh], "
           "16;",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [mm], "
           "16;",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group [mm+4], [sh], "
           "16;",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [sh], "
           "15;",
           "multimem.st.v4.f32 [mm+4], {%f0,%f1,%f2,%f3};",
           "multimem.st.u32 [mm+2147483648], %u0;",
           "multimem.st.async.release.gpu.u64 [%addr], %u0;",
           "multimem.st.f32 [mm], 1;",
           "multimem.st.f16x2 [mm], 1;",
           "multimem.red.add.f16x2 [mm], 1;",
           "multimem.st.bf16x2 [mm], 0f3f800000;",
           "multimem.st.e4m3x4 [mm], 1.5;",
           "multimem.st.e5m2x4 [mm], 0d3ff0000000000000;",
           "multimem.ld_reduce.add.u32 1, [mm];",
           "multimem.st.v2.f32 [mm], {1,2};",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_FALSE(accepts_multimem(multimem_source(candidate)));
  }
  EXPECT_TRUE(
      accepts_multimem(multimem_source("multimem.cp.async.bulk.global.shared::"
                                       "cta.bulk_group [mm+16], [sh], 32;")));
}

/** Scalar source literals retain typed use bits after syntax AST destruction. */
TEST(Multimem, ScalarLiteralSourcesAfterAstDeath) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(multimem_source(R"ptx(
  multimem.st.u32 [mm], 1;
  multimem.red.add.u32 [mm], 1;
  multimem.st.async.release.gpu.b8 [%addr], 0x1ff;
  multimem.red.async.release.gpu.add.u32 [%addr], 1;
  multimem.st.f32 [mm], 1.5;
  multimem.red.add.f64 [mm], 1.5;
  multimem.st.async.release.gpu.f64 [%addr], 1.5;
)ptx"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(owned);
  EXPECT_TRUE(validateModule(*owned));
  const auto& body = owned->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  auto* narrow = dynamic_cast<MultimemStAsyncRelease*>(body[2].get());
  ASSERT_NE(narrow, nullptr);
  auto* immediate = std::get_if<ResolvedImmediate>(&narrow->src.value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->type, ScalarType::B8);
  EXPECT_EQ(immediate->bits, 0xffu);
  ASSERT_TRUE(immediate->integer_source_bits);
  EXPECT_EQ(*immediate->integer_source_bits, 0x1ffu);
  immediate->bits = 0xfe;
  EXPECT_FALSE(validateModule(*owned));
}

/** Raw packed FP8 literals retain their exact 32-bit use representation. */
TEST(Multimem, PackedFp8LiteralBitsAfterAstDeath) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(multimem_source(R"ptx(
  multimem.st.e4m3x4 [mm], 0x100000001;
  multimem.st.e5m2x4 [mm], 0f3f800000;
)ptx"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(owned);
  ASSERT_TRUE(validateModule(*owned));
  const auto& body = owned->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  auto* first = dynamic_cast<MultimemStFloatFp8PackedNoneScalarDefaultWeak*>(
      body[0].get());
  ASSERT_NE(first, nullptr);
  auto* second = dynamic_cast<MultimemStFloatFp8PackedNoneScalarDefaultWeak*>(
      body[1].get());
  ASSERT_NE(second, nullptr);
  auto* integer = std::get_if<ResolvedImmediate>(&first->src.value);
  auto* float_bits = std::get_if<ResolvedImmediate>(&second->src.value);
  ASSERT_NE(integer, nullptr);
  ASSERT_NE(float_bits, nullptr);
  EXPECT_EQ(integer->type, ScalarType::E4m3x4);
  EXPECT_EQ(integer->bits, 1u);
  ASSERT_TRUE(integer->integer_source_bits);
  EXPECT_EQ(*integer->integer_source_bits, 0x100000001u);
  EXPECT_EQ(float_bits->type, ScalarType::E5m2x4);
  EXPECT_EQ(float_bits->bits, 0x3f800000u);
  EXPECT_FALSE(float_bits->integer_source_bits);
  integer->bits = 2;
  EXPECT_FALSE(validateModule(*owned));
  integer->bits = 1;
  float_bits->bits = 0x100000000u;
  EXPECT_FALSE(validateModule(*owned));
}

/** Omitted red qualifiers retain absent source ranges and typed defaults. */
TEST(Multimem, RedQualifierProvenance) {
  auto parsed = test_helpers::parseModule(multimem_source(R"ptx(
  multimem.red.add.u32 [mm], %u0;
  multimem.red.sys.add.u32 [mm], %u0;
  multimem.red.release.add.u32 [mm], %u0;
)ptx"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto owned = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(owned.has_value()) << owned.error().front().message;
  const auto& body = owned->functions.front().body;
  ASSERT_GE(body.size(), 3u);
  for (std::size_t i = 0; i < 3; ++i)
    ASSERT_NE(
        dynamic_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[i].get()),
        nullptr);
  const auto& bare =
      *static_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[0].get());
  const auto& scope_only =
      *static_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[1].get());
  const auto& semantics_only =
      *static_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[2].get());
  EXPECT_EQ(bare.scope.value, MemoryScope::None);
  EXPECT_TRUE(bare.scope.locs.empty());
  EXPECT_EQ(bare.semantics.value, MemoryConsistency::Omitted);
  EXPECT_TRUE(bare.semantics.locs.empty());
  EXPECT_EQ(scope_only.scope.value, MemoryScope::Sys);
  EXPECT_FALSE(scope_only.scope.locs.empty());
  EXPECT_EQ(scope_only.semantics.value, MemoryConsistency::Omitted);
  EXPECT_EQ(semantics_only.scope.value, MemoryScope::None);
  EXPECT_TRUE(semantics_only.scope.locs.empty());
  EXPECT_EQ(semantics_only.semantics.value, MemoryConsistency::Release);
  EXPECT_FALSE(semantics_only.semantics.locs.empty());
}

/** A vector address uses the total byte width even after typed IR mutation. */
TEST(Multimem, VectorAlignmentAfterResolution) {
  auto parsed = test_helpers::parseModule(
      multimem_source("multimem.st.v4.f32 [mm+16], {%f0,%f1,%f2,%f3};"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto owned = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(owned.has_value()) << owned.error().front().message;
  auto* form = dynamic_cast<MultimemStFloatNoneV4DefaultWeak*>(
      owned->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  ASSERT_TRUE(form->address.value.offset);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  EXPECT_TRUE(form->check(context));
  form->address.value.offset->value.bits = 4;
  form->address.value.offset->value.integer_source_bits = 4;
  EXPECT_FALSE(form->check(context));
}

/** Source/PTX floors and architectural qualifiers are independent. */
TEST(Multimem, TargetAndVersionIntersections) {
  constexpr std::string_view base =
      "multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [sh], 16;";
  EXPECT_TRUE(accepts_multimem(multimem_source(base, "sm_90", "9.1")));
  EXPECT_FALSE(accepts_multimem(multimem_source(base, "sm_90", "9.0")));
  constexpr std::string_view explicit_weak =
      "multimem.cp.async.bulk.weak.global.shared::cta.bulk_group [mm], [sh], "
      "16;";
  EXPECT_FALSE(accepts_multimem(multimem_source(explicit_weak, "sm_90")));
  EXPECT_TRUE(accepts_multimem(multimem_source(explicit_weak, "sm_90a")));
  constexpr std::string_view relaxed =
      "multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group.b128 "
      "[mm], [sh], 16;";
  constexpr std::string_view relaxed_mask =
      "multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group.cp_"
      "mask.b128 [mm], [sh], 16, %mask;";
  EXPECT_TRUE(accepts_multimem(multimem_source(relaxed, "sm_90a")));
  EXPECT_FALSE(accepts_multimem(multimem_source(relaxed_mask, "sm_90a")));
  EXPECT_TRUE(accepts_multimem(multimem_source(relaxed_mask, "sm_100f")));
  constexpr std::string_view fp8 = "multimem.st.e4m3x4 [mm], %b0;";
  EXPECT_FALSE(accepts_multimem(multimem_source(fp8, "sm_90", "9.3")));
  EXPECT_TRUE(accepts_multimem(multimem_source(fp8, "sm_100a", "9.3")));
  constexpr std::string_view acc_f32 =
      "multimem.ld_reduce.add.acc::f32.f16x2 %b0, [mm];";
  EXPECT_FALSE(accepts_multimem(multimem_source(acc_f32, "sm_90", "8.1")));
  EXPECT_TRUE(accepts_multimem(multimem_source(acc_f32, "sm_90", "8.2")));
  constexpr std::string_view scoped_reduce =
      "multimem.cp.reduce.async.bulk.relaxed.cta.global.shared::cta.bulk_group."
      "add.u32 [mm], [sh], 16;";
  EXPECT_FALSE(
      accepts_multimem(multimem_source(scoped_reduce, "sm_90", "9.1")));
  EXPECT_TRUE(accepts_multimem(multimem_source(scoped_reduce, "sm_90", "9.3")));
  EXPECT_FALSE(accepts_multimem(multimem_source(
      "multimem.st.async.release.gpu.u32 [%addr], %u0;", "sm_90")));
  EXPECT_FALSE(accepts_multimem(multimem_source(
      "multimem.st.async.release.gpu.u32 [%addr], %u0;", "sm_100", "9.2")));
}

/** Bound references and metadata survive AST destruction and reject tampering. */
TEST(Multimem, OwnedModuleBindingAfterAstDeath) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(
        multimem_source("multimem.cp.async.bulk.global.shared::cta.bulk_group "
                        "[mm], [sh], 16;"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(owned);
  EXPECT_TRUE(validateModule(*owned));
  auto* form = dynamic_cast<MultimemCpAsyncBulkWeak*>(
      owned->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  auto* symbol = std::get_if<ResolvedSymbolRef>(&form->dst.value.base);
  ASSERT_NE(symbol, nullptr);
  ASSERT_TRUE(symbol->address_alignment);
  symbol->address_alignment = 1;
  EXPECT_FALSE(validateModule(*owned));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
