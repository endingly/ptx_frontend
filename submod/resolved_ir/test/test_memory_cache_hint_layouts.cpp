#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** One source-selected cache partition and its unchanged availability floor. */
struct CacheHintCase {
  /** Hint-only spelling; policy tests append one 64-bit register. */
  std::string_view source;
  /** Existing generated form identity shared by both layouts. */
  InstructionKind kind;
  /** True for the L2-eviction partition requiring PTX 8.8 / SM 100. */
  bool modern;
};

/** Representative source for every coherent, noncoherent, and store partition. */
constexpr std::array cache_hint_cases{
    CacheHintCase{"ld.global.L2::evict_first.L2::cache_hint.v8.u32 "
                  "{%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7}, [%rd0];",
                  LdGlobalL1L2CacheHintPrefetchVector::kind, true},
    CacheHintCase{"ld.L2::evict_last.L2::cache_hint.v8.u32 "
                  "{%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7}, [%rd0];",
                  LdL2EvictCacheHintVector::kind, true},
    CacheHintCase{"ld.acquire.sys.global.L1::evict_first.L2::cache_hint.L2::"
                  "128B.u32 %r0, [%rd0];",
                  LdGlobalL1CacheHintPrefetchScalar::kind, false},
    CacheHintCase{"ld.global.L1::evict_last.L2::cache_hint.L2::64B.v2.u32 "
                  "{%r0,%r1}, [%rd0];",
                  LdGlobalL1CacheHintPrefetchVector::kind, false},
    CacheHintCase{"ld.L1::evict_first.L2::cache_hint.u32 %r0, [%rd0];",
                  LdL1CacheHintScalar::kind, false},
    CacheHintCase{"ld.L1::evict_last.L2::cache_hint.v2.u32 {%r0,%r1}, [%rd0];",
                  LdL1CacheHintVector::kind, false},
    CacheHintCase{"ld.global.ca.L2::cache_hint.u32 %r0, [%rd0];",
                  LdGlobalU32L2CacheHint::kind, false},
    CacheHintCase{
        "ld.weak.global.cg.L2::cache_hint.L2::64B.v2.u32 {%r0,%r1}, [%rd0];",
        LdGlobalL2CacheHintVector::kind, false},
    CacheHintCase{"ld.L2::cache_hint.u32 %r0, [%rd0];",
                  LdL2CacheHintScalar::kind, false},
    CacheHintCase{"ld.L2::cache_hint.v2.u32 {%r0,%r1}, [%rd0];",
                  LdL2CacheHintVector::kind, false},
    CacheHintCase{"ld.global.nc.L1::evict_first.L2::evict_last.L2::cache_hint."
                  "L2::64B.v8.u32 {%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7}, [%rd0];",
                  LdGlobalNcL2EvictCacheHintVector::kind, true},
    CacheHintCase{
        "ld.global.nc.L1::evict_first.L2::cache_hint.u32 %r0, [%rd0];",
        LdGlobalNcL1CacheHintScalar::kind, false},
    CacheHintCase{"ld.global.nc.L1::evict_first.L2::cache_hint.L2::128B.v2.u32 "
                  "{%r0,%r1}, [%rd0];",
                  LdGlobalNcL1CacheHintPrefetchLegacyVector::kind, false},
    CacheHintCase{"ld.global.cs.nc.L2::cache_hint.L2::64B.u32 %r0, [%rd0];",
                  LdGlobalNcCacheHintScalar::kind, false},
    CacheHintCase{"ld.global.ca.nc.L2::cache_hint.v2.u32 {%r0,%r1}, [%rd0];",
                  LdGlobalNcCacheHintVector::kind, false},
    CacheHintCase{
        "st.release.sys.global.L1::evict_first.L2::evict_last.L2::cache_hint."
        "v8.u32 [%rd0], {%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7};",
        StGlobalL1L2CacheHintVector::kind, true},
    CacheHintCase{"st.L2::evict_first.L2::cache_hint.v8.u32 [%rd0], "
                  "{%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7};",
                  StL2EvictCacheHintVector::kind, true},
    CacheHintCase{
        "st.release.gpu.global.L1::evict_last.L2::cache_hint.u32 [%rd0], %r0;",
        StGlobalL1CacheHintScalar::kind, false},
    CacheHintCase{
        "st.global.L1::evict_first.L2::cache_hint.v2.u32 [%rd0], {%r0,%r1};",
        StGlobalL1CacheHintVector::kind, false},
    CacheHintCase{"st.L1::evict_first.L2::cache_hint.u32 [%rd0], %r0;",
                  StL1CacheHintScalar::kind, false},
    CacheHintCase{"st.L1::evict_last.L2::cache_hint.v2.u32 [%rd0], {%r0,%r1};",
                  StL1CacheHintVector::kind, false},
    CacheHintCase{"st.global.wb.L2::cache_hint.u32 [%rd0], %r0;",
                  StGlobalU32L2CacheHint::kind, false},
    CacheHintCase{"st.weak.global.wb.L2::cache_hint.v2.u32 [%rd0], {%r0,%r1};",
                  StGlobalL2CacheHintVector::kind, false},
    CacheHintCase{"st.L2::cache_hint.u32 [%rd0], %r0;",
                  StL2CacheHintScalar::kind, false},
    CacheHintCase{"st.L2::cache_hint.v2.u32 [%rd0], {%r0,%r1};",
                  StL2CacheHintVector::kind, false},
};

/** Both operand layouts select the same form and retain every target floor. */
TEST(MemoryCacheHintLayouts, EveryPartitionAcceptsBothPolicyStates) {
  for (const auto& fixture : cache_hint_cases) {
    for (const bool with_policy : {false, true}) {
      std::string source{fixture.source};
      if (with_policy)
        source.insert(source.size() - 1, ", %rd9");
      SCOPED_TRACE(source);
      const auto ast = test_helpers::parseInstruction(source);
      ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
      const auto resolved =
          source.starts_with("ld") ? resolveLd(*ast) : resolveSt(*ast);
      ASSERT_TRUE(resolved) << resolved.error().message;
      EXPECT_EQ((*resolved)->instruction_kind(), fixture.kind);
      const checker::Context floor{
          .target = {.ptx_version = fixture.modern ? checker::PtxVersion{8, 8}
                                                   : checker::PtxVersion{7, 4},
                     .sm_version = fixture.modern ? 100u : 80u}};
      const auto checked = (*resolved)->check(floor);
      ASSERT_TRUE(checked) << checked.error().front().message;
      auto below_ptx = floor;
      below_ptx.target.ptx_version = fixture.modern ? checker::PtxVersion{8, 7}
                                                    : checker::PtxVersion{7, 3};
      EXPECT_FALSE((*resolved)->check(below_ptx));
      auto below_sm = floor;
      --below_sm.target.sm_version;
      EXPECT_FALSE((*resolved)->check(below_sm));
    }
  }
}

/** Hint-only ordinary vectors and scalar B128 retain their independent gates. */
TEST(MemoryCacheHintLayouts, RetainsPayloadAvailabilityGates) {
  for (const auto source : {
           "ld.global.L2::cache_hint.v8.u32 "
           "{%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7}, [%rd0];",
           "ld.global.nc.L2::cache_hint.v8.u32 "
           "{%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7}, [%rd0];",
           "st.global.L2::cache_hint.v8.u32 [%rd0], "
           "{%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7};",
       }) {
    SCOPED_TRACE(source);
    const auto ast = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
    const auto resolved = std::string_view{source}.starts_with("ld")
                              ? resolveLd(*ast)
                              : resolveSt(*ast);
    ASSERT_TRUE(resolved) << resolved.error().message;
    EXPECT_TRUE((*resolved)->check(checker::Context{
        .target = {.ptx_version = {8, 8}, .sm_version = 100}}));
    EXPECT_FALSE((*resolved)->check(checker::Context{
        .target = {.ptx_version = {8, 7}, .sm_version = 100}}));
    EXPECT_FALSE((*resolved)->check(
        checker::Context{.target = {.ptx_version = {8, 8}, .sm_version = 99}}));
  }
  for (const auto source : {
           "ld.global.L2::cache_hint.b128 %b0, [%rd0];",
           "ld.global.nc.L2::cache_hint.b128 %b0, [%rd0];",
           "st.global.L2::cache_hint.b128 [%rd0], %b0;",
       }) {
    SCOPED_TRACE(source);
    const auto ast = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
    const auto resolved = std::string_view{source}.starts_with("ld")
                              ? resolveLd(*ast)
                              : resolveSt(*ast);
    ASSERT_TRUE(resolved) << resolved.error().message;
    EXPECT_TRUE((*resolved)->check(
        checker::Context{.target = {.ptx_version = {8, 3}, .sm_version = 80}}));
    EXPECT_FALSE((*resolved)->check(
        checker::Context{.target = {.ptx_version = {8, 2}, .sm_version = 80}}));
  }
}

/** Resolve owned module state with source and AST both released before return. */
std::optional<ResolvedModule> owned_cache_module(
    std::string_view instructions) {
  const std::string source =
      ".version 9.3\n.target sm_100\n.address_size 64\n"
      ".global .align 32 .u32 data[8];\n"
      ".shared .u32 shared_data;\n"
      ".entry kernel() {\n"
      ".reg .u32 %r<10>;\n.reg .u64 %rd<10>;\n"
      ".reg .f64 %fd;\n.reg .pred %p;\n" +
      std::string(instructions) + "\n}\n";
  const auto ast = test_helpers::parseModule(source);
  if (!ast)
    return std::nullopt;
  auto resolved = resolveAndValidateModule(*ast);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Owned policy presence, layout, source ranges, and public mutations agree. */
TEST(MemoryCacheHintLayouts, RevalidatesOwnedPolicyPresenceAndLayout) {
  auto module = owned_cache_module(
      "ld.global.L2::cache_hint.u32 %r0, [data];\n"
      "ld.global.L2::cache_hint.u32 %r0, [data], %rd9;\n"
      "ld.global.nc.L2::cache_hint.v2.u32 {%r0,%r1}, [data];\n"
      "ld.global.nc.L2::cache_hint.v2.u32 {%r0,%r1}, [data], %rd9;\n"
      "st.global.L2::cache_hint.u32 [data], %r0;\n"
      "st.global.L2::cache_hint.u32 [data], %r0, %rd9;");
  ASSERT_TRUE(module);
  ASSERT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  auto& body = module->functions.front().body;
  auto& absent = dynamic_cast<LdGlobalU32L2CacheHint&>(*body[0]);
  auto& present = dynamic_cast<LdGlobalU32L2CacheHint&>(*body[1]);
  const auto& nc_absent =
      dynamic_cast<const LdGlobalNcCacheHintVector&>(*body[2]);
  const auto& nc_present =
      dynamic_cast<const LdGlobalNcCacheHintVector&>(*body[3]);
  const auto& store_absent =
      dynamic_cast<const StGlobalU32L2CacheHint&>(*body[4]);
  const auto& store_present =
      dynamic_cast<const StGlobalU32L2CacheHint&>(*body[5]);
  EXPECT_TRUE(absent.cache_hint);
  EXPECT_TRUE(present.cache_hint);
  EXPECT_EQ(absent.operand_layout.value, 1);
  EXPECT_EQ(present.operand_layout.value, 0);
  EXPECT_FALSE(absent.cache_policy);
  ASSERT_TRUE(present.cache_policy);
  EXPECT_EQ(present.cache_policy->value.declared_type, ScalarType::U64);
  EXPECT_FALSE(present.cache_policy->locs.empty());
  EXPECT_FALSE(nc_absent.cache_policy);
  EXPECT_TRUE(nc_present.cache_policy);
  EXPECT_FALSE(store_absent.cache_policy);
  EXPECT_TRUE(store_present.cache_policy);

  absent.cache_policy = present.cache_policy;
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  absent.cache_policy.reset();
  const auto saved_policy = present.cache_policy;
  present.cache_policy.reset();
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  present.cache_policy = saved_policy;
  present.cache_policy->value.declared_type = ScalarType::U32;
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  present.cache_policy = saved_policy;
  absent.operand_layout.value = 0;
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  absent.operand_layout.value = 1;
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Omission does not relax policy shape, payload, vector, space, or modifier rules. */
TEST(MemoryCacheHintLayouts, RejectsInvalidPoliciesAndAdjacentForms) {
  for (const auto source : {
           "ld.global.u32 %r0, [data], %rd9;",
           "ld.global.nc.v2.u32 {%r0,%r1}, [data], %rd9;",
           "st.global.u32 [data], %r0, %rd9;",
           "ld.global.L2::cache_hint.u32 %r0, [data], %r1;",
           "ld.global.nc.L2::cache_hint.u32 %r0, [data], %p;",
           "st.global.L2::cache_hint.u32 [data], %r0, %r1;",
           "ld.global.L2::cache_hint.u32 %r0, [data], 1;",
           "st.global.L2::cache_hint.u32 [data], %r0, {%rd8,%rd9};",
           "ld.global.L2::cache_hint.pred %p, [data];",
           "st.global.L2::cache_hint.u64 [data], %r0;",
           "ld.global.nc.L2::cache_hint.v2.u32 {%r0}, [data];",
           "st.global.L2::cache_hint.v2.u32 [data], {%r0,%r1,%r2};",
           "ld.shared.L2::cache_hint.u32 %r0, [shared_data];",
           "ld.L2::cache_hint.u32 %r0, [shared_data];",
           "ld.global.nc.L2::cache_hint.u32 %r0, [shared_data];",
           "st.L2::cache_hint.u32 [shared_data], %r0;",
           "ld.volatile.global.L2::cache_hint.u32 %r0, [data];",
           "ld.acquire.sys.global.ca.L2::cache_hint.u32 %r0, [data];",
           "ld.global.nc.ca.L2::cache_hint.u32 %r0, [data];",
           "st.volatile.global.L2::cache_hint.u32 [data], %r0;",
           "st.release.sys.global.wb.L2::cache_hint.u32 [data], %r0;",
           "st.global.L2::cache_hint.L2::64B.u32 [data], %r0;",
           "ld.global.L2::evict_first.L2::cache_hint.v4.u32 {%r0,%r1,%r2,%r3}, "
           "[data];",
           "st.global.L2::evict_first.L2::cache_hint.v2.u64 [data], "
           "{%rd0,%rd1};",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(owned_cache_module(source));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
