#include <gtest/gtest.h>

#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/prefetch/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/prefetch/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/prefetch/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;

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

}  // namespace
}  // namespace ptx_frontend::resolved_ir
