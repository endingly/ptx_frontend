#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

syntax_ast::AstModule parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value()) << module.diagnostics.front().message;
  return std::move(*module);
}

std::string readCorpusFile(const std::filesystem::path& file) {
  std::ifstream input(file);
  if (!input)
    throw std::runtime_error("cannot read corpus file: " + file.string());
  return {std::istreambuf_iterator<char>{input}, {}};
}

struct CorpusCase {
  std::string_view file_name;
  std::string_view target;
};

void expectM13CorpusModule(const CorpusCase& corpus_case) {
  const auto corpus = std::filesystem::path{__FILE__}
                          .parent_path()
                          .parent_path()
                          .parent_path()
                          .parent_path() /
                      "corpus" / "m13";
  const auto file = corpus / corpus_case.file_name;
  SCOPED_TRACE(file.string());
  PtxSyntaxParser parser(readCorpusFile(file));
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value()) << file;
  ASSERT_GE(parsed->items.size(), 4u);
  EXPECT_EQ(std::get<syntax_ast::AstVersionDirective>(parsed->items[0]).version.text,
            "9.3");
  const auto& target = std::get<syntax_ast::AstTargetDirective>(parsed->items[1]);
  ASSERT_EQ(target.targets.size(), 1u);
  EXPECT_EQ(target.targets[0].text, corpus_case.target);
  EXPECT_EQ(std::get<syntax_ast::AstAddressSizeDirective>(parsed->items[2])
                .bit_width.text,
            "64");

  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value())
      << (resolved.error().empty() ? "resolution failed"
                                   : resolved.error().front().message);
  ASSERT_EQ(resolved->functions.size(), 1u);
  ASSERT_FALSE(resolved->functions.front().body.empty());
  const auto availability = checkModuleAvailability(*parsed, *resolved);
  ASSERT_TRUE(availability.has_value())
      << (availability.error().empty() ? "availability check failed"
                                        : availability.error().front().message);
}

TEST(ResolvedModule, ResolvesAndChecksEveryM13SynchronizationCorpusModule) {
  for (const CorpusCase corpus_case : std::array{
           CorpusCase{"synchronization_sm90a.ptx", "sm_90a"},
           CorpusCase{"synchronization_sm100.ptx", "sm_100"},
       })
    expectM13CorpusModule(corpus_case);
}

TEST(ResolvedModule, RejectsM13SynchronizationCorpusLocalLegalityFailures) {
  const auto mbarrier = resolveModule(parseModule(R"ptx(
.global .align 8 .b64 global_barrier[2];
.entry kernel() { mbarrier.inval.b64 [global_barrier]; }
)ptx"));
  ASSERT_TRUE(mbarrier.has_value());
  const auto mbarrier_checked = checker::check(
      std::get<Mbarrier>(mbarrier->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90}});
  ASSERT_FALSE(mbarrier_checked.has_value());
  EXPECT_EQ(mbarrier_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  EXPECT_GT(mbarrier_checked.error().front().range.start.line, 0);

  const auto proxy = resolveModule(parseModule(R"ptx(
.global .align 16 .b8 global_value[128];
.entry kernel() { fence.proxy.tensormap::generic.acquire.cluster [global_value], 64; }
)ptx"));
  ASSERT_TRUE(proxy.has_value());
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const auto proxy_checked = checker::check(
      std::get<Fence>(proxy->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3},
                                  .sm_version = 90,
                                  .capabilities = cluster_capabilities}});
  ASSERT_FALSE(proxy_checked.has_value());
  EXPECT_EQ(proxy_checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
  EXPECT_GT(proxy_checked.error().front().range.start.line, 0);

  const auto barrier = resolveModule(parseModule(R"ptx(
.entry kernel() { barrier.cluster.arrive; }
)ptx"));
  ASSERT_TRUE(barrier.has_value());
  const auto no_capability = checker::check(
      std::get<Barrier>(barrier->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90}});
  ASSERT_FALSE(no_capability.has_value());
  EXPECT_EQ(no_capability.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  const auto old_target = checker::check(
      std::get<Barrier>(barrier->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 7},
                                  .sm_version = 90,
                                  .capabilities = cluster_capabilities}});
  ASSERT_FALSE(old_target.has_value());
  EXPECT_EQ(old_target.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  const auto query = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p0;
  .reg .b32 %r0;
  clusterlaunchcontrol.query_cancel.is_canceled.pred.b128 %p0, %r0;
}
)ptx"));
  ASSERT_TRUE(query.has_value());
  const auto sm100 = base::find_target_profile("sm_100");
  ASSERT_TRUE(sm100.has_value());
  const auto query_checked = checker::check(
      std::get<Clusterlaunchcontrol>(query->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3},
                                  .sm_version = 100,
                                  .enabled_family_features = sm100->enabled_family_features,
                                  .identity = sm100->identity,
                                  .capabilities = sm100->capabilities}});
  ASSERT_FALSE(query_checked.has_value());
  EXPECT_EQ(query_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_GT(query_checked.error().front().range.start.line, 0);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
