#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;
using test_helpers::parseModule;

/** Resolve one load and require checker acceptance at the supplied target. */
void expect_load(std::string_view source, checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<Ld>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto checked = checker::check(
      *resolved,
      checker::Context{.target = target, .instruction_range = ast->range});
  ASSERT_TRUE(checked.has_value())
      << (checked.error().empty() ? "load checker rejected without a diagnostic"
                                  : checked.error().front().message);
}

/** Require a syntactically valid load to fail resolution or checking. */
void expect_load_rejected(std::string_view source, checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<Ld>(*ast);
  if (!resolved)
    return;
  EXPECT_FALSE(checker::check(
      *resolved,
      checker::Context{.target = target, .instruction_range = ast->range}));
}

/** Require a resolved load to fail only the target-aware checker boundary. */
void expect_load_unavailable(std::string_view source,
                             checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<Ld>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  EXPECT_FALSE(checker::check(
      *resolved,
      checker::Context{.target = target, .instruction_range = ast->range}));
}

/** Scalar, vector, explicit-space, and shared sub-qualifier families resolve. */
TEST(LdCompleteness, ResolvesCoreAndSharedFamilies) {
  constexpr std::array<std::string_view, 10> sources{
      "ld.u32 %r0, [%rd0];",
      "ld.global.u32 %r0, [%rd0];",
      "ld.local.b16 %h0, [%rd0];",
      "ld.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.global.v4.u64 {%rd0, %rd1, %rd2, %rd3}, [%rd4];",
      "ld.shared::cta.u32 %r0, [%rd0];",
      "ld.shared::cta.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.shared::cluster.u32 %r0, [%rd0];",
      "ld.shared::cluster.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.const.f64 %fd0, [%rd0];",
  };
  constexpr std::array<std::string_view, 1> cluster{"cluster"};
  const checker::TargetInfo target{
      .ptx_version = {9, 3}, .sm_version = 100, .capabilities = cluster};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    expect_load(source, target);
  }
}

/** Cache controls cover generic/global, NC, combined, and strong families. */
TEST(LdCompleteness, ResolvesCachePrefetchAndNoncoherentFamilies) {
  constexpr std::array<std::string_view, 57> sources{
      "ld.L1::evict_first.u32 %r0, [%rd0];",
      "ld.L1::evict_last.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.L2::64B.u32 %r0, [%rd0];",
      "ld.L2::128B.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.L2::cache_hint.u32 %r0, [%rd0], %rd1;",
      "ld.L2::cache_hint.v2.u32 {%r0, %r1}, [%rd0], %rd1;",
      "ld.global.L1::evict_unchanged.u32 %r0, [%rd0];",
      "ld.global.L1::evict_last.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.global.L2::cache_hint.u32 %r0, [%rd0], %rd1;",
      "ld.global.L2::cache_hint.v2.u32 {%r0, %r1}, [%rd0], %rd1;",
      "ld.global.L2::64B.u32 %r0, [%rd0];",
      "ld.global.L2::128B.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.global.L2::evict_first.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];",
      "ld.global.L2::evict_last.v8.u32 "
      "{%r0, _, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];",
      "ld.global.L2::evict_last.L1::evict_first.v4.u64 "
      "{%rd0, %rd1, %rd2, %rd3}, [%rd4];",
      "ld.global.L1::evict_first.L2::evict_last.v4.u64 "
      "{%rd0, %rd1, %rd2, %rd3}, [%rd4];",
      "ld.global.nc.u32 %r0, [%rd0];",
      "ld.global.ca.nc.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.global.nc.L1::evict_first.u32 %r0, [%rd0];",
      "ld.global.nc.L1::evict_last.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.global.nc.L2::evict_first.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];",
      "ld.global.nc.L1::evict_first.L2::evict_last.v4.u64 "
      "{%rd0, %rd1, %rd2, %rd3}, [%rd4];",
      "ld.global.nc.L2::64B.u32 %r0, [%rd0];",
      "ld.global.cs.nc.L2::128B.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.global.nc.L2::cache_hint.v2.u32 "
      "{%r0, %r1}, [%rd0], %rd1;",
      "ld.global.L2::cache_hint.L2::64B.u32 %r0, [%rd0], %rd1;",
      "ld.acquire.sys.global.L2::cache_hint.L2::64B.u32 "
      "%r0, [%rd0], %rd1;",
      "ld.global.L1::evict_first.L2::cache_hint.L2::128B.v2.u32 "
      "{%r0, %r1}, [%rd0], %rd1;",
      "ld.global.L1::evict_first.L2::64B.u32 %r0, [%rd0];",
      "ld.global.L1::evict_first.L2::cache_hint.u32 "
      "%r0, [%rd0], %rd1;",
      "ld.global.ca.nc.L2::cache_hint.L2::64B.u32 "
      "%r0, [%rd0], %rd1;",
      "ld.global.nc.L1::evict_first.L2::cache_hint.L2::128B.v2.u32 "
      "{%r0, %r1}, [%rd0], %rd1;",
      "ld.global.nc.L1::evict_first.L2::64B.u32 %r0, [%rd0];",
      "ld.global.nc.L1::evict_first.L2::cache_hint.u32 "
      "%r0, [%rd0], %rd1;",
      "ld.global.ca.L2::cache_hint.u32 %r0, [%rd0], %rd1;",
      "ld.global.ca.L2::64B.u32 %r0, [%rd0];",
      "ld.relaxed.gpu.global.L1::evict_last.L2::cache_hint.L2::64B.u32 "
      "%r0, [%rd0], %rd1;",
      "ld.global.acquire.sys.L1::evict_last.L2::cache_hint.u32 "
      "%r0, [%rd0], %rd1;",
      "ld.acquire.sys.L2::128B.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.global.L2::cache_hint.L2::256B.b128 %b0, [%rd0], %rd1;",
      "ld.acquire.sys.global.L2::evict_first.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];",
      "ld.global.L2::evict_first.L2::64B.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];",
      "ld.global.L2::evict_first.L2::cache_hint.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0], %rd1;",
      "ld.L2::evict_first.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];",
      "ld.L1::evict_first.L2::evict_last.L2::cache_hint.L2::64B.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0], %rd1;",
      "ld.global.nc.L2::evict_first.L2::64B.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];",
      "ld.global.nc.L2::evict_first.L2::cache_hint.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0], %rd1;",
      "ld.volatile.global.L2::64B.u32 %r0, [%rd0];",
      "ld.weak.global.ca.L2::cache_hint.L2::64B.u32 "
      "%r0, [%rd0], %rd1;",
      "ld.weak.global.ca.L2::cache_hint.v2.u32 "
      "{%r0, %r1}, [%rd0], %rd1;",
      "ld.weak.global.ca.L2::64B.v2.u32 {%r0, %r1}, [%rd0];",
      "ld.weak.global.ca.L2::cache_hint.L2::64B.v2.u32 "
      "{%r0, %r1}, [%rd0], %rd1;",
      "ld.weak.global.L1::evict_first.u32 %r0, [%rd0];",
      "ld.L1::evict_first.L2::64B.u32 %r0, [%rd0];",
      "ld.L1::evict_first.L2::cache_hint.u32 %r0, [%rd0], %rd1;",
      "ld.acquire.sys.global.L1::evict_first.L2::evict_last."
      "L2::cache_hint.L2::64B.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0], %rd1;",
      "ld.global.nc.L1::evict_first.L2::evict_last."
      "L2::cache_hint.L2::64B.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0], %rd1;",
  };
  const checker::TargetInfo target{.ptx_version = {9, 3}, .sm_version = 100};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    expect_load(source, target);
  }

  for (const auto source : {
           "ld.global.ca.L1::evict_first.u32 %r0, [%rd0];",
           "ld.global.cg.nc.L1::evict_first.u32 %r0, [%rd0];",
           "ld.global.nc.ca.u32 %r0, [%rd0];",
           "ld.global.nc.L2::evict_last.L1::evict_first.v4.u64 "
           "{%rd0, %rd1, %rd2, %rd3}, [%rd4];",
           "ld.relaxed.gpu.global.ca.u32 %r0, [%rd0];",
           "ld.global.u32 %r0, [%rd0], %rd1;",
           "ld.global.L2::cache_hint.u32 %r0, [%rd0];",
           "ld.global.nc.L2::cache_hint.v2.u32 {%r0, %r1}, [%rd0];",
           "ld.global.L2::cache_hint.L2::64B.u32 %r0, [%rd0];",
           "ld.volatile.global.L1::evict_first.u32 %r0, [%rd0];",
           "ld.volatile.global.L2::cache_hint.u32 %r0, [%rd0], %rd1;",
           "ld.acquire.sys.global.L1::evict_last.L2::cache_hint.u32 "
           "%r0, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    expect_load_rejected(source, target);
  }
}

/** Generic cache hints and prefetches still require a global-pointing address. */
TEST(LdCompleteness, RejectsKnownNonglobalCacheControlAddresses) {
  for (const auto instruction : {
           "ld.L2::cache_hint.u32 %r0, [local_value], %rd1;",
           "ld.L2::64B.u32 %r0, [shared_value];",
           "ld.L1::evict_first.u32 %r0, [local_value];",
           "ld.acquire.gpu.L2::cache_hint.u32 "
           "%r0, [shared_value], %rd1;",
           "ld.acquire.gpu.L1::evict_last.u32 %r0, [shared_value];",
           "ld.volatile.L2::64B.u32 %r0, [local_value];",
       }) {
    SCOPED_TRACE(instruction);
    std::string module_source{R"ptx(
.version 9.3
.target sm_100
.address_size 64
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd1;
  .local .u32 local_value;
  .shared .u32 shared_value;
)ptx"};
    module_source.append(instruction);
    module_source.append(R"ptx(
}
)ptx");
    const auto ast = parseModule(module_source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveModule(*ast));
  }
}

/** L2 eviction is confined to the two 256-bit vector shapes and modern target. */
TEST(LdCompleteness, EnforcesModernL2EvictionBoundary) {
  const checker::TargetInfo modern{.ptx_version = {9, 3}, .sm_version = 100};
  for (const auto source : {
           "ld.global.L2::evict_first.u32 %r0, [%rd0];",
           "ld.global.L2::evict_first.v4.u32 {%r0, %r1, %r2, %r3}, [%rd0];",
           "ld.global.L2::evict_first.v2.u64 {%rd0, %rd1}, [%rd2];",
           "ld.global.nc.L2::evict_first.v4.u32 "
           "{%r0, %r1, %r2, %r3}, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    expect_load_rejected(source, modern);
  }
  constexpr std::string_view modern_vector =
      "ld.global.L2::evict_first.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];";
  expect_load_unavailable(
      modern_vector,
      checker::TargetInfo{.ptx_version = {8, 7}, .sm_version = 100});
  expect_load_unavailable(
      modern_vector,
      checker::TargetInfo{.ptx_version = {8, 8}, .sm_version = 99});
}

/** B128, prefetch sizes, and cache hints retain their independent minima. */
TEST(LdCompleteness, EnforcesTypeAndCacheControlAvailability) {
  constexpr std::string_view noncoherent = "ld.global.nc.u32 %r0, [%rd0];";
  expect_load(noncoherent,
              checker::TargetInfo{.ptx_version = {3, 1}, .sm_version = 32});
  expect_load_unavailable(
      noncoherent,
      checker::TargetInfo{.ptx_version = {3, 0}, .sm_version = 32});
  expect_load_unavailable(
      noncoherent,
      checker::TargetInfo{.ptx_version = {3, 1}, .sm_version = 31});

  constexpr std::string_view qualified_parameter =
      "ld.param::entry.u32 %r0, [%rd0];";
  expect_load(qualified_parameter,
              checker::TargetInfo{.ptx_version = {8, 3}, .sm_version = 70});
  expect_load_unavailable(
      qualified_parameter,
      checker::TargetInfo{.ptx_version = {8, 2}, .sm_version = 70});

  constexpr std::string_view b128 = "ld.global.b128 %b0, [%rd0];";
  expect_load(b128,
              checker::TargetInfo{.ptx_version = {8, 3}, .sm_version = 70});
  expect_load_unavailable(
      b128, checker::TargetInfo{.ptx_version = {8, 2}, .sm_version = 70});
  constexpr std::string_view system_b128 =
      "ld.global.relaxed.sys.b128 %b0, [%rd0];";
  expect_load(system_b128,
              checker::TargetInfo{.ptx_version = {8, 4}, .sm_version = 70});
  expect_load_unavailable(
      system_b128,
      checker::TargetInfo{.ptx_version = {8, 3}, .sm_version = 70});

  constexpr std::string_view prefetch64 = "ld.global.L2::64B.u32 %r0, [%rd0];";
  expect_load(prefetch64,
              checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 75});
  expect_load_unavailable(
      prefetch64, checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 74});
  constexpr std::string_view prefetch256 =
      "ld.global.L2::256B.u32 %r0, [%rd0];";
  expect_load(prefetch256,
              checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 80});
  expect_load_unavailable(
      prefetch256,
      checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 79});
  expect_load_unavailable(
      "ld.global.L2::cache_hint.u32 %r0, [%rd0], %rd1;",
      checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 79});

  constexpr std::string_view l1_prefetch64 =
      "ld.global.L1::evict_first.L2::64B.u32 %r0, [%rd0];";
  expect_load_unavailable(
      l1_prefetch64,
      checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 74});
  expect_load(l1_prefetch64,
              checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 75});
  expect_load_unavailable(
      "ld.global.nc.L1::evict_first.L2::256B.u32 %r0, [%rd0];",
      checker::TargetInfo{.ptx_version = {7, 4}, .sm_version = 79});
}

/** Omitted optional cache controls retain explicit typed sentinel values. */
TEST(LdCompleteness, MaterializesOptionalCacheControlDefaults) {
  const auto ast = parseInstruction(
      "ld.global.L2::evict_first.v8.u32 "
      "{%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  auto resolved = resolve<Ld>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* load = std::get_if<Ld::GlobalL2EvictVector>(&resolved->variant);
  ASSERT_NE(load, nullptr);
  EXPECT_EQ(load->l1_eviction_priority.value, EvictionPriority::Invalid);
  EXPECT_EQ(load->prefetch_size.value, PrefetchSize::None);

  const auto l1_ast =
      parseInstruction("ld.global.L1::evict_first.u32 %r0, [%rd0];");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(l1_ast);
  auto l1_resolved = resolve<Ld>(*l1_ast);
  ASSERT_TRUE(l1_resolved.has_value()) << l1_resolved.error().message;
  auto* l1_load = std::get_if<Ld::GlobalU32L1Evict>(&l1_resolved->variant);
  ASSERT_NE(l1_load, nullptr);
  EXPECT_EQ(l1_load->prefetch_size.value, PrefetchSize::None);
  const checker::Context sm75{
      .target = {.ptx_version = {9, 3}, .sm_version = 75},
      .instruction_range = l1_ast->range,
  };
  EXPECT_TRUE(checker::check(*l1_resolved, sm75));

  l1_load->prefetch_size.value = PrefetchSize::Bytes256;
  EXPECT_FALSE(checker::check(*l1_resolved, sm75));
}

/** Unified addresses are load-only, global/generic, and target-qualified. */
TEST(LdCompleteness, EnforcesUnifiedAddressPolicyWithoutAffectingMov) {
  constexpr std::string_view generic = "ld.u32 %r0, [%rd0].unified;";
  expect_load(generic,
              checker::TargetInfo{.ptx_version = {8, 0}, .sm_version = 90});
  expect_load_unavailable(
      generic, checker::TargetInfo{.ptx_version = {7, 9}, .sm_version = 90});
  expect_load_unavailable(
      generic, checker::TargetInfo{.ptx_version = {8, 0}, .sm_version = 89});

  const checker::TargetInfo current{.ptx_version = {9, 3}, .sm_version = 90};
  expect_load("ld.global.u32 %r0, [%rd0].unified;", current);
  for (const auto source : {
           "ld.shared.u32 %r0, [%rd0].unified;",
           "ld.shared::cta.u32 %r0, [%rd0].unified;",
           "ld.shared::cluster.u32 %r0, [%rd0].unified;",
           "ld.local.u32 %r0, [%rd0].unified;",
           "ld.const.u32 %r0, [%rd0].unified;",
           "ld.param.u32 %r0, [%rd0].unified;",
           "ld.global.nc.u32 %r0, [%rd0].unified;",
           "ld.acquire.sys.global.u32 %r0, [%rd0].unified;",
           "ld.volatile.global.u32 %r0, [%rd0].unified;",
       }) {
    SCOPED_TRACE(source);
    expect_load_rejected(source, current);
  }

  const auto module_ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .attribute(.unified(1, 2)) .u32 unified_value;
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0, %rd1;
  ld.global.u32 %r0, [unified_value].unified;
  mov.u64 %rd0, unified_value;
  mov.u64 %rd1, unified_value+8;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(module_ast);
  auto module = resolveModule(*module_ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  EXPECT_TRUE(validateModule(*module));

  auto& load = std::get<Ld>(module->functions.front().body.front());
  std::get<Ld::ExplicitScalar>(load.variant).address.value.unified = false;
  EXPECT_FALSE(validateModule(*module));

  const auto mov_suffix = parseInstruction("mov.u64 %rd0, [%rd1].unified;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(mov_suffix);
  const auto mov = resolve<Mov>(*mov_suffix);
  if (mov) {
    EXPECT_FALSE(checker::check(
        *mov, checker::Context{.target = current,
                               .instruction_range = mov_suffix->range}));
  }
}

/** MMIO admits load semantics and target gates from the PTX 9.3 extension. */
TEST(LdCompleteness, EnforcesMmioSemanticsAndAvailability) {
  constexpr std::string_view relaxed =
      "ld.global.mmio.relaxed.sys.u32 %r0, [%rd0];";
  expect_load(relaxed,
              checker::TargetInfo{.ptx_version = {8, 2}, .sm_version = 70});
  constexpr std::string_view acquire =
      "ld.global.mmio.acquire.sys.u32 %r0, [%rd0];";
  expect_load(acquire,
              checker::TargetInfo{.ptx_version = {9, 3}, .sm_version = 75});
  expect_load("ld.mmio.acquire.sys.global.u32 %r0, [%rd0];",
              checker::TargetInfo{.ptx_version = {9, 3}, .sm_version = 75});
  expect_load_unavailable(
      acquire, checker::TargetInfo{.ptx_version = {9, 2}, .sm_version = 75});
  expect_load_unavailable(
      acquire, checker::TargetInfo{.ptx_version = {9, 3}, .sm_version = 74});
  const checker::TargetInfo current{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto source : {
           "ld.global.mmio.release.sys.u32 %r0, [%rd0];",
           "ld.global.mmio.acquire.gpu.u32 %r0, [%rd0];",
           "ld.shared.mmio.acquire.sys.u32 %r0, [%rd0];",
           "ld.global.mmio.acquire.sys.v2.u32 {%r0, %r1}, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    expect_load_rejected(source, current);
  }
}

/** Parameter direction and default sub-qualifier policies remain enforced. */
TEST(LdCompleteness, PreservesParameterPolicy) {
  const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.entry kernel(.param .u32 input) {
  .reg .u32 %r0;
  ld.param::entry.u32 %r0, [input];
  ld.param.u32 %r0, [input];
}
.func device(.param .u32 input) {
  .reg .u32 %r0;
  ld.param::func.u32 %r0, [input];
  ld.param.u32 %r0, [input];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveModule(*ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  EXPECT_TRUE(validateModule(*module));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
