#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;
using test_helpers::parseModule;

/** Resolve one store and require checker acceptance at the supplied target. */
void expect_store(std::string_view source, checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<St>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto checked = checker::check(
      *resolved,
      checker::Context{.target = target, .instruction_range = ast->range});
  ASSERT_TRUE(checked.has_value())
      << (checked.error().empty()
              ? "store checker rejected without a diagnostic"
              : checked.error().front().message);
}

/** Require a syntactically valid store to fail resolution or checking. */
void expect_store_rejected(std::string_view source,
                           checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<St>(*ast);
  if (!resolved)
    return;
  EXPECT_FALSE(checker::check(
      *resolved,
      checker::Context{.target = target, .instruction_range = ast->range}));
}

/** Require a resolved store to fail only the target-aware checker boundary. */
void expect_store_unavailable(std::string_view source,
                              checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<St>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  EXPECT_FALSE(checker::check(
      *resolved,
      checker::Context{.target = target, .instruction_range = ast->range}));
}

/** Scalar, vector, explicit-space, and shared sub-qualifier families resolve. */
TEST(StCompleteness, ResolvesCoreAndSharedFamilies) {
  constexpr std::array<std::string_view, 9> sources{
      "st.u32 [%rd0], %r0;",
      "st.global.u32 [%rd0], %r0;",
      "st.local.b16 [%rd0], %h0;",
      "st.v2.u32 [%rd0], {%r0, %r1};",
      "st.global.v4.u64 [%rd4], {%rd0, %rd1, %rd2, %rd3};",
      "st.shared::cta.u32 [%rd0], %r0;",
      "st.shared::cta.v2.u32 [%rd0], {%r0, %r1};",
      "st.shared::cluster.u32 [%rd0], %r0;",
      "st.shared::cluster.v2.u32 [%rd0], {%r0, %r1};",
  };
  constexpr std::array<std::string_view, 1> cluster{"cluster"};
  const checker::TargetInfo target{
      .ptx_version = {9, 3}, .sm_version = 100, .capabilities = cluster};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    expect_store(source, target);
  }
}

/** Store cache controls cover weak/strong and generic/global combinations. */
TEST(StCompleteness, ResolvesCacheControlFamilies) {
  constexpr std::array<std::string_view, 30> sources{
      "st.global.L1::evict_unchanged.u32 [%rd0], %r0;",
      "st.global.L1::evict_last.v2.u32 [%rd0], {%r0, %r1};",
      "st.global.L2::cache_hint.u32 [%rd0], %r0, %rd1;",
      "st.global.L2::cache_hint.v2.u32 [%rd0], {%r0, %r1}, %rd1;",
      "st.global.L2::evict_first.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};",
      "st.global.L2::evict_last.v8.u32 [%rd0], "
      "{%r0, _, %r2, %r3, %r4, %r5, %r6, %r7};",
      "st.global.L1::evict_first.L2::evict_last.v4.u64 [%rd4], "
      "{%rd0, %rd1, %rd2, %rd3};",
      "st.global.wb.u32 [%rd0], %r0;",
      "st.global.cs.v2.u32 [%rd0], {%r0, %r1};",
      "st.global.wb.L2::cache_hint.u32 [%rd0], %r0, %rd1;",
      "st.release.gpu.global.L1::evict_last.L2::cache_hint.u32 "
      "[%rd0], %r0, %rd1;",
      "st.global.release.gpu.L1::evict_last.L2::cache_hint.u32 "
      "[%rd0], %r0, %rd1;",
      "st.relaxed.gpu.L1::evict_first.v2.u32 [%rd0], {%r0, %r1};",
      "st.release.sys.global.L2::evict_first.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};",
      "st.release.sys.global.L1::evict_first.L2::evict_last.v8.u32 "
      "[%rd0], {%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};",
      "st.global.L2::cache_hint.b128 [%rd0], %b0, %rd1;",
      "st.L2::cache_hint.u32 [%rd0], %r0, %rd1;",
      "st.L1::evict_first.u32 [%rd0], %r0;",
      "st.L1::evict_first.L2::cache_hint.u32 [%rd0], %r0, %rd1;",
      "st.weak.global.L1::evict_first.u32 [%rd0], %r0;",
      "st.weak.global.wb.L2::cache_hint.u32 [%rd0], %r0, %rd1;",
      "st.weak.global.wb.L2::cache_hint.v2.u32 "
      "[%rd0], {%r0, %r1}, %rd1;",
      "st.release.sys.global.L1::evict_first.L2::evict_last."
      "L2::cache_hint.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, %rd1;",
      "st.global.L2::evict_first.L2::cache_hint.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, %rd1;",
      "st.release.gpu.L1::evict_last.L2::cache_hint.u32 "
      "[%rd0], %r0, %rd1;",
      "st.L2::evict_first.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};",
      "st.L1::evict_first.L2::evict_last.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};",
      "st.L1::evict_first.L2::evict_last.L2::cache_hint.v8.u32 "
      "[%rd0], {%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, %rd1;",
      "st.weak.wb.L2::cache_hint.v2.u32 "
      "[%rd0], {%r0, %r1}, %rd1;",
      "st.release.sys.L2::evict_first.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};",
  };
  const checker::TargetInfo target{.ptx_version = {9, 3}, .sm_version = 100};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    expect_store(source, target);
  }

  for (const auto source : {
           "st.global.wb.L1::evict_first.u32 [%rd0], %r0;",
           "st.release.sys.global.wb.u32 [%rd0], %r0;",
           "st.global.L1::evict_first.wb.u32 [%rd0], %r0;",
           "st.global.L2::evict_last.L1::evict_first.v8.u32 [%rd0], "
           "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};",
           "st.global.u32 [%rd0], %r0, %rd1;",
           "st.global.L2::cache_hint.u32 [%rd0], %r0;",
           "st.volatile.global.L1::evict_first.u32 [%rd0], %r0;",
           "st.volatile.global.L2::cache_hint.u32 [%rd0], %r0, %rd1;",
           "st.release.gpu.global.L1::evict_last.L2::cache_hint.u32 "
           "[%rd0], %r0;",
       }) {
    SCOPED_TRACE(source);
    expect_store_rejected(source, target);
  }
}

/** A generic cache-policy store must still address global memory. */
TEST(StCompleteness, RejectsKnownNonglobalCacheHintAddress) {
  const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd1;
  .shared .u32 shared_value;
  st.L1::evict_first.u32 [shared_value], %r0;
  st.L2::cache_hint.u32 [shared_value], %r0, %rd1;
  st.release.gpu.L1::evict_last.u32 [shared_value], %r0;
  st.release.gpu.L2::cache_hint.u32 [shared_value], %r0, %rd1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  EXPECT_FALSE(resolveModule(*ast));
}

/** L2 eviction is confined to the two 256-bit vector shapes and modern target. */
TEST(StCompleteness, EnforcesModernL2EvictionBoundary) {
  const checker::TargetInfo modern{.ptx_version = {9, 3}, .sm_version = 100};
  for (const auto source : {
           "st.global.L2::evict_first.u32 [%rd0], %r0;",
           "st.global.L2::evict_first.v4.u32 [%rd0], "
           "{%r0, %r1, %r2, %r3};",
           "st.global.L2::evict_first.v2.u64 [%rd2], {%rd0, %rd1};",
       }) {
    SCOPED_TRACE(source);
    expect_store_rejected(source, modern);
  }
  constexpr std::string_view modern_vector =
      "st.global.L2::evict_first.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};";
  expect_store_unavailable(
      modern_vector,
      checker::TargetInfo{.ptx_version = {8, 7}, .sm_version = 100});
  expect_store_unavailable(
      modern_vector,
      checker::TargetInfo{.ptx_version = {8, 8}, .sm_version = 99});
}

/** B128 and cache hints retain independent version and architecture minima. */
TEST(StCompleteness, EnforcesTypeAndCacheControlAvailability) {
  constexpr std::string_view qualified_parameter =
      "st.param::func.u32 [%rd0], %r0;";
  expect_store(qualified_parameter,
               checker::TargetInfo{.ptx_version = {8, 3}, .sm_version = 70});
  expect_store_unavailable(
      qualified_parameter,
      checker::TargetInfo{.ptx_version = {8, 2}, .sm_version = 70});

  constexpr std::string_view b128 = "st.global.b128 [%rd0], %b0;";
  expect_store(b128,
               checker::TargetInfo{.ptx_version = {8, 3}, .sm_version = 70});
  expect_store_unavailable(
      b128, checker::TargetInfo{.ptx_version = {8, 2}, .sm_version = 70});
  constexpr std::string_view system_b128 =
      "st.global.release.sys.b128 [%rd0], %b0;";
  expect_store(system_b128,
               checker::TargetInfo{.ptx_version = {8, 4}, .sm_version = 70});
  expect_store_unavailable(
      system_b128,
      checker::TargetInfo{.ptx_version = {8, 3}, .sm_version = 70});
  expect_store_unavailable(
      "st.global.L2::cache_hint.u32 [%rd0], %r0, %rd1;",
      checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 79});
}

/** Omitted L1 eviction state remains explicit in the modern L2 family. */
TEST(StCompleteness, MaterializesOptionalL1EvictionDefault) {
  const auto ast = parseInstruction(
      "st.global.L2::evict_first.v8.u32 [%rd0], "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7};");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<St>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* store = std::get_if<St::GlobalL2EvictVector>(&resolved->variant);
  ASSERT_NE(store, nullptr);
  EXPECT_EQ(store->l1_eviction_priority.value, EvictionPriority::Invalid);
}

/** Stores reject the load-only suffix and all writes to known unified data. */
TEST(StCompleteness, RejectsUnifiedAddressWrites) {
  const checker::TargetInfo current{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto source : {
           "st.u32 [%rd0].unified, %r0;",
           "st.global.u32 [%rd0].unified, %r0;",
           "st.shared.u32 [%rd0].unified, %r0;",
       }) {
    SCOPED_TRACE(source);
    expect_store_rejected(source, current);
  }

  const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .attribute(.unified(1, 2)) .u32 unified_value;
.entry kernel() {
  .reg .u32 %r0;
  st.global.u32 [unified_value], %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  EXPECT_FALSE(resolveModule(*ast));
}

/** MMIO admits store semantics and target gates from the PTX 9.3 extension. */
TEST(StCompleteness, EnforcesMmioSemanticsAndAvailability) {
  constexpr std::string_view relaxed =
      "st.global.mmio.relaxed.sys.u32 [%rd0], %r0;";
  expect_store(relaxed,
               checker::TargetInfo{.ptx_version = {8, 2}, .sm_version = 70});
  constexpr std::string_view release =
      "st.global.mmio.release.sys.u32 [%rd0], %r0;";
  expect_store(release,
               checker::TargetInfo{.ptx_version = {9, 3}, .sm_version = 75});
  expect_store("st.mmio.release.sys.global.u32 [%rd0], %r0;",
               checker::TargetInfo{.ptx_version = {9, 3}, .sm_version = 75});
  expect_store_unavailable(
      release, checker::TargetInfo{.ptx_version = {9, 2}, .sm_version = 75});
  expect_store_unavailable(
      release, checker::TargetInfo{.ptx_version = {9, 3}, .sm_version = 74});
  const checker::TargetInfo current{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto source : {
           "st.global.mmio.acquire.sys.u32 [%rd0], %r0;",
           "st.global.mmio.release.gpu.u32 [%rd0], %r0;",
           "st.shared.mmio.release.sys.u32 [%rd0], %r0;",
           "st.global.mmio.release.sys.v2.u32 [%rd0], {%r0, %r1};",
       }) {
    SCOPED_TRACE(source);
    expect_store_rejected(source, current);
  }
}

/** Parameter return direction and AST-free MMIO revalidation stay enforced. */
TEST(StCompleteness, PreservesParameterPolicyAndRevalidatesMutation) {
  const auto parameter_ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.func (.param .u32 result) device(.param .u32 input) {
  .reg .u32 %r0;
  ld.param::func.u32 %r0, [input];
  st.param::func.u32 [result], %r0;
  st.param.u32 [result], %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parameter_ast);
  const auto parameter_module = resolveModule(*parameter_ast);
  ASSERT_TRUE(parameter_module.has_value())
      << parameter_module.error().front().message;
  EXPECT_TRUE(validateModule(*parameter_module));

  const auto mmio_ast =
      parseInstruction("st.global.mmio.release.sys.u32 [%rd0], %r0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(mmio_ast);
  auto mmio = resolve<St>(*mmio_ast);
  ASSERT_TRUE(mmio.has_value()) << mmio.error().message;
  auto& store = std::get<St::ExplicitScalar>(mmio->variant);
  store.semantics.value = MemoryConsistency::Acquire;
  const auto checked = checker::check(
      *mmio,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90},
                       .instruction_range = mmio_ast->range});
  EXPECT_FALSE(checked);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
