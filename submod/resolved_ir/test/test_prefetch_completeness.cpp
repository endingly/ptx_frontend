#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/prefetch.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;
using test_helpers::parseModule;

/** Require a standalone prefetch form to resolve and pass target checking. */
void expect_prefetch(std::string_view source, checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolvePrefetch(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto checked = (*resolved)->check(
      checker::Context{.target = target, .instruction_range = ast->range});
  ASSERT_TRUE(checked.has_value())
      << (checked.error().empty() ? "prefetch rejected without diagnostic"
                                  : checked.error().front().message);
}

/** Require a standalone prefetch form to fail resolution or target checking. */
void expect_prefetch_rejected(std::string_view source,
                              checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolvePrefetch(*ast);
  if (!resolved)
    return;
  EXPECT_FALSE((*resolved)->check(
      checker::Context{.target = target, .instruction_range = ast->range}));
}

/** Ordinary levels, global eviction, and tensor-map topology all resolve. */
TEST(PrefetchCompleteness, ResolvesDocumentedForms) {
  const checker::TargetInfo target{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto source : {
           "prefetch.L1 [%rd0];",
           "prefetch.L2 [%rd0];",
           "prefetch.global.L1 [%rd0];",
           "prefetch.global.L2 [%rd0];",
           "prefetch.local.L1 [%rd0];",
           "prefetch.local.L2 [%rd0];",
           "prefetch.global.L2::evict_last [%rd0];",
           "prefetch.global.L2::evict_normal [%rd0];",
           "prefetch.const.tensormap [%rd0];",
           "prefetch.param.tensormap [%rd0];",
           "prefetch.tensormap [%rd0];",
       }) {
    SCOPED_TRACE(source);
    expect_prefetch(source, target);
  }
}

/** Target gates apply to each family without admitting unsupported suffixes. */
TEST(PrefetchCompleteness, ChecksTargetsAndRejectsUnsupportedSyntax) {
  expect_prefetch_rejected("prefetch.L1 [%rd0];",
                           {.ptx_version = {1, 9}, .sm_version = 90});
  expect_prefetch_rejected("prefetch.local.L2 [%rd0];",
                           {.ptx_version = {2, 0}, .sm_version = 19});
  expect_prefetch("prefetch.local.L2 [%rd0];",
                  {.ptx_version = {2, 0}, .sm_version = 20});
  expect_prefetch_rejected("prefetch.global.L2::evict_last [%rd0];",
                           {.ptx_version = {7, 3}, .sm_version = 90});
  expect_prefetch_rejected("prefetch.global.L2::evict_normal [%rd0];",
                           {.ptx_version = {7, 4}, .sm_version = 79});
  expect_prefetch("prefetch.global.L2::evict_normal [%rd0];",
                  {.ptx_version = {7, 4}, .sm_version = 80});
  expect_prefetch_rejected("prefetch.const.tensormap [%rd0];",
                           {.ptx_version = {7, 8}, .sm_version = 90});
  expect_prefetch_rejected("prefetch.param.tensormap [%rd0];",
                           {.ptx_version = {8, 0}, .sm_version = 89});
  expect_prefetch("prefetch.const.tensormap [%rd0];",
                  {.ptx_version = {8, 0}, .sm_version = 90});
  expect_prefetch_rejected("prefetch.tensormap [%rd0];",
                           {.ptx_version = {7, 8}, .sm_version = 90});
  expect_prefetch_rejected("prefetch.tensormap [%rd0];",
                           {.ptx_version = {8, 0}, .sm_version = 89});
  expect_prefetch("prefetch.tensormap [%rd0];",
                  {.ptx_version = {8, 0}, .sm_version = 90});

  const checker::TargetInfo target{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto source : {
           "prefetch.shared.L1 [%rd0];",
           "prefetch.const.L2 [%rd0];",
           "prefetch.local.L2::evict_last [%rd0];",
           "prefetch.L2::evict_last [%rd0];",
           "prefetch.global.tensormap [%rd0];",
           "prefetch.local.tensormap [%rd0];",
           "prefetch.tensormap.L1 [%rd0];",
           "prefetch.tensormap.L2 [%rd0];",
           "prefetch.tensormap.L2::evict_last [%rd0];",
           "prefetch.tensormap.L2::evict_normal [%rd0];",
           "prefetch.param.L1 [%rd0];",
           "prefetch.global.L1;",
           "prefetch.global.L1 [%rd0], [%rd0];",
       }) {
    SCOPED_TRACE(source);
    expect_prefetch_rejected(source, target);
  }
}

/** Generic prefetch and tensor-map forms enforce their separate provenance. */
TEST(PrefetchCompleteness, ChecksBoundAddressTopology) {
  const auto valid_ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 global_value[64];
.const .align 64 .b8 const_map[64];
.visible .entry kernel(.param .align 64 .b8 param_map[64]) {
  .local .align 64 .b8 local_value[64];
  .shared .align 64 .b8 shared_value[64];
  .reg .u64 %rd0;
  prefetch.L1 [global_value];
  prefetch.L2 [local_value];
  prefetch.L1 [shared_value];
  prefetch.global.L2 [global_value];
  prefetch.local.L1 [local_value];
  prefetch.global.L2::evict_last [global_value];
  prefetch.const.tensormap [const_map];
  prefetch.param.tensormap [param_map];
  prefetch.tensormap [global_value];
  prefetch.tensormap [shared_value];
  prefetch.tensormap [%rd0];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(valid_ast);
  const auto valid = resolveModule(*valid_ast);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(validateModule(*valid));

  for (const auto source : {
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.entry kernel() {
  .local .u32 local_value;
  prefetch.global.L2 [local_value];
})ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.global .u32 global_value;
.entry kernel() { prefetch.local.L1 [global_value]; })ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.global .u32 global_value;
.entry kernel() { prefetch.const.tensormap [global_value]; })ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.const .u32 const_value;
.entry kernel() { prefetch.param.tensormap [const_value]; })ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.const .u32 const_value;
.entry kernel() { prefetch.L1 [const_value]; })ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.entry kernel(.param .u32 param_value) {
  prefetch.L2 [param_value];
})ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.const .u32 const_value;
.entry kernel() { prefetch.tensormap [const_value]; })ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.entry kernel(.param .u32 param_value) {
  prefetch.tensormap [param_value];
})ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.entry kernel() {
  .local .u32 local_value;
  prefetch.tensormap [local_value];
})ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.entry kernel() {
  .shared .u32 shared_value;
  prefetch.shared.tensormap [shared_value];
})ptx",
       }) {
    SCOPED_TRACE(source);
    const auto ast = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto resolved = resolveModule(*ast);
    if (resolved)
      EXPECT_FALSE(validateModule(*resolved));
  }
}

/** Bound address provenance remains checked after parser-owned data is gone. */
TEST(PrefetchCompleteness, RevalidatesOwnedAddressWithoutAst) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .u32 global_value;
.visible .entry kernel() {
  prefetch.global.L2::evict_last [global_value];
}
)ptx";
    const auto ast = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(owned.has_value());
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& prefetch = dynamic_cast<PrefetchGlobalL2Evict&>(
      *owned->functions.front().body.front());
  auto& symbol = std::get<ResolvedSymbolRef>(prefetch.address.value.base);
  ASSERT_TRUE(symbol.address_state_space.has_value());
  symbol.address_state_space = base::DeclarationStateSpace::Local;
  const auto wrong_space = owned->functions.front().body.front()->check(
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90}});
  ASSERT_FALSE(wrong_space.has_value());
  EXPECT_EQ(wrong_space.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  const auto invalid =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
}

/** Generic tensor-map provenance remains enforceable in AST-free owned IR. */
TEST(PrefetchCompleteness, RevalidatesGenericTensormapWithoutAst) {
  std::optional<ResolvedModule> owned;
  {
    const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 global_map[64];
.visible .entry kernel() { prefetch.tensormap [global_map]; }
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(owned.has_value());
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& prefetch = dynamic_cast<PrefetchGenericTensormap&>(
      *owned->functions.front().body.front());
  auto& symbol = std::get<ResolvedSymbolRef>(prefetch.address.value.base);
  ASSERT_TRUE(symbol.address_state_space.has_value());
  const auto original_space = symbol.address_state_space;
  symbol.address_state_space = base::DeclarationStateSpace::Local;
  const auto invalid = owned->functions.front().body.front()->check(
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90}});
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  symbol.address_state_space = original_space;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
}

/** A bound shared tensor-map address remains valid after syntax destruction. */
TEST(PrefetchCompleteness, GenericSharedTensormapNoopAfterAstDestruction) {
  std::optional<ResolvedModule> owned;
  {
    const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.shared .align 64 .b8 shared_map[128];
.entry kernel() { prefetch.tensormap [shared_map]; }
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(owned.has_value());
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
}

/** All direct-address carriers must retain their bound declaration metadata. */
TEST(PrefetchCompleteness,
     RevalidatesGenericAddressMetadataAfterAstDestruction) {
  std::optional<ResolvedModule> owned;
  {
    const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 32 .b8 low[128];
.global .align 64 .b8 high[128];
.entry kernel() {
  prefetch.L1 [low+4];
  prefetch.L1 [high];
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(owned.has_value());
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& body = owned->functions.front().body;
  auto& low = dynamic_cast<PrefetchGenericL1&>(*body[0]).address.value;
  auto& high = dynamic_cast<PrefetchGenericL1&>(*body[1]).address.value;
  ASSERT_TRUE(low.offset.has_value());
  auto& ref = std::get<ResolvedSymbolRef>(low.base);
  const auto& high_ref = std::get<ResolvedSymbolRef>(high.base);
  ASSERT_EQ(ref.address_alignment, 32u);
  ASSERT_EQ(high_ref.address_alignment, 64u);
  const auto valid = [&] {
    return validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext)
        .has_value();
  };
  const auto mismatch = [&] {
    const auto result =
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& diagnostic) {
      return diagnostic.kind ==
             checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
  };
  ref.address_alignment = 64;
  mismatch();
  ref.address_alignment.reset();
  mismatch();
  ref.address_alignment = 0;
  mismatch();
  ref.address_alignment = 16;
  mismatch();
  ref.address_alignment = 32;
  ref.declared_type = base::ScalarType::U64;
  mismatch();
  ref.declared_type = base::ScalarType::B8;
  const auto original_id = ref.symbol_id;
  ref.symbol_id = high_ref.symbol_id;
  mismatch();
  ref.symbol_id = original_id;
  EXPECT_TRUE(valid());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
