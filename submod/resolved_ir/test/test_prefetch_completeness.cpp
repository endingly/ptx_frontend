#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;
using test_helpers::parseModule;

/** Require a standalone prefetch form to resolve and pass target checking. */
void expect_prefetch(std::string_view source, checker::TargetInfo target) {
  const auto ast = parseInstruction(source);
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  const auto resolved = resolve<Prefetch>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto checked = checker::check(
      *resolved,
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
  const auto resolved = resolve<Prefetch>(*ast);
  if (!resolved)
    return;
  EXPECT_FALSE(checker::check(
      *resolved,
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

  const checker::TargetInfo target{.ptx_version = {9, 3}, .sm_version = 90};
  for (const auto source : {
           "prefetch.shared.L1 [%rd0];",
           "prefetch.const.L2 [%rd0];",
           "prefetch.local.L2::evict_last [%rd0];",
           "prefetch.L2::evict_last [%rd0];",
           "prefetch.global.tensormap [%rd0];",
           "prefetch.local.tensormap [%rd0];",
           "prefetch.param.L1 [%rd0];",
           "prefetch.global.L1;",
           "prefetch.global.L1 [%rd0], [%rd0];",
       }) {
    SCOPED_TRACE(source);
    expect_prefetch_rejected(source, target);
  }
}

/** Generic prefetch accepts known global/local/shared and typed tensor maps. */
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
  prefetch.L1 [global_value];
  prefetch.L2 [local_value];
  prefetch.L1 [shared_value];
  prefetch.global.L2 [global_value];
  prefetch.local.L1 [local_value];
  prefetch.global.L2::evict_last [global_value];
  prefetch.const.tensormap [const_map];
  prefetch.param.tensormap [param_map];
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
  auto& prefetch = std::get<Prefetch::GlobalL2Evict>(
      std::get<Prefetch>(owned->functions.front().body.front()).variant);
  auto& symbol = std::get<ResolvedSymbolRef>(prefetch.address.value.base);
  ASSERT_TRUE(symbol.address_state_space.has_value());
  symbol.address_state_space = base::DeclarationStateSpace::Local;
  const auto wrong_space = checker::check(
      std::get<Prefetch>(owned->functions.front().body.front()),
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

}  // namespace
}  // namespace ptx_frontend::resolved_ir
