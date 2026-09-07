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

TEST(ResolvedModule, RejectsM13MatchSinkBoundaryPairsAtTheirDestination) {
  const auto reject = [](std::string_view source) {
    const auto ast = parseModule(source);
    const auto& instruction = std::get<syntax_ast::AstInstruction>(
        std::get<syntax_ast::AstFunction>(ast.items.back()).body.back());
    const auto resolved = resolveModule(ast);
    ASSERT_FALSE(resolved.has_value());
    ASSERT_FALSE(resolved.error().empty());
    EXPECT_EQ(resolved.error().front().range,
              sourceRange(instruction.operands.front()));
  };
  reject(R"ptx(
.entry kernel() { .reg .b32 %b<2>; match.all.sync.b32 _|_, %b1, 0xffffffff; }
)ptx");
  reject(R"ptx(
.entry kernel() { .reg .b32 %b<2>; match.any.sync.b32 _, %b1, 0xffffffff; }
)ptx");
}

TEST(ResolvedModule, RejectsM13SynchronizationCorpusLocalLegalityFailures) {
  const auto mbarrier_ast = parseModule(R"ptx(
.global .align 8 .b64 global_barrier[2];
.entry kernel() { mbarrier.inval.b64 [global_barrier]; }
)ptx");
  const auto mbarrier = resolveModule(mbarrier_ast);
  ASSERT_TRUE(mbarrier.has_value());
  const auto& mbarrier_instruction = std::get<syntax_ast::AstInstruction>(
      std::get<syntax_ast::AstFunction>(mbarrier_ast.items.back()).body.front());
  const auto mbarrier_checked = checker::check(
      std::get<Mbarrier>(mbarrier->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90},
                       .instruction_range = mbarrier_instruction.range});
  ASSERT_FALSE(mbarrier_checked.has_value());
  EXPECT_EQ(mbarrier_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  EXPECT_EQ(mbarrier_checked.error().front().range,
            std::get<syntax_ast::AstAddress>(mbarrier_instruction.operands.front())
                .range);

  const auto proxy_ast = parseModule(R"ptx(
.global .align 16 .b8 global_value[128];
.entry kernel() { fence.proxy.tensormap::generic.acquire.cluster [global_value], 64; }
)ptx");
  const auto proxy = resolveModule(proxy_ast);
  ASSERT_TRUE(proxy.has_value());
  const auto& proxy_instruction = std::get<syntax_ast::AstInstruction>(
      std::get<syntax_ast::AstFunction>(proxy_ast.items.back()).body.front());
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const auto proxy_checked = checker::check(
      std::get<Fence>(proxy->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3},
                                  .sm_version = 90,
                                  .capabilities = cluster_capabilities},
                       .instruction_range = proxy_instruction.range});
  ASSERT_FALSE(proxy_checked.has_value());
  EXPECT_EQ(proxy_checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
  EXPECT_EQ(proxy_checked.error().front().range,
            std::get<syntax_ast::AstImmediate>(proxy_instruction.operands.back())
                .syntax.range);

  const auto barrier_ast = parseModule(R"ptx(
.entry kernel() { barrier.cluster.arrive; }
)ptx");
  const auto barrier = resolveModule(barrier_ast);
  ASSERT_TRUE(barrier.has_value());
  const auto& barrier_instruction = std::get<syntax_ast::AstInstruction>(
      std::get<syntax_ast::AstFunction>(barrier_ast.items.back()).body.front());
  const auto no_capability = checker::check(
      std::get<Barrier>(barrier->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90},
                       .instruction_range = barrier_instruction.range});
  ASSERT_FALSE(no_capability.has_value());
  EXPECT_EQ(no_capability.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  EXPECT_EQ(no_capability.error().front().range, barrier_instruction.range);
  const auto old_target = checker::check(
      std::get<Barrier>(barrier->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 7},
                                  .sm_version = 90,
                                  .capabilities = cluster_capabilities},
                       .instruction_range = barrier_instruction.range});
  ASSERT_FALSE(old_target.has_value());
  EXPECT_EQ(old_target.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  EXPECT_EQ(old_target.error().front().range, barrier_instruction.range);

  const auto query_ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p0;
  .reg .b32 %r0;
clusterlaunchcontrol.query_cancel.is_canceled.pred.b128 %p0, %r0;
}
)ptx");
  const auto query = resolveModule(query_ast);
  ASSERT_TRUE(query.has_value());
  const auto& query_instruction = std::get<syntax_ast::AstInstruction>(
      std::get<syntax_ast::AstFunction>(query_ast.items.back()).body.back());
  const auto sm100 = base::find_target_profile("sm_100");
  ASSERT_TRUE(sm100.has_value());
  const auto query_checked = checker::check(
      std::get<Clusterlaunchcontrol>(query->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3},
                                  .sm_version = 100,
                                  .enabled_family_features = sm100->enabled_family_features,
                                  .identity = sm100->identity,
                                  .capabilities = sm100->capabilities},
                       .instruction_range = query_instruction.range});
  ASSERT_FALSE(query_checked.has_value());
  EXPECT_EQ(query_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(query_checked.error().front().range,
            sourceRange(query_instruction.operands.back()));
}

TEST(ResolvedModule, ChecksM13MbarrierBoundaryMatrix) {
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const auto sink_ast = parseModule(R"ptx(
.shared .align 8 .b64 bar;
.entry kernel() {
  mbarrier.arrive.b64 _, [bar];
  mbarrier.arrive.shared::cluster.b64 _, [bar];
}
)ptx");
  const auto sink = resolveModule(sink_ast);
  ASSERT_TRUE(sink.has_value()) << sink.error().front().message;
  const auto& sink_function =
      std::get<syntax_ast::AstFunction>(sink_ast.items.back());
  const auto& generic_sink_instruction = std::get<syntax_ast::AstInstruction>(
      sink_function.body[0]);
  const auto& cluster_sink_instruction = std::get<syntax_ast::AstInstruction>(
      sink_function.body[1]);
  const auto old_sink = checker::check(
      std::get<Mbarrier>(sink->functions.front().body[0]),
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80},
                       .instruction_range = generic_sink_instruction.range});
  ASSERT_FALSE(old_sink.has_value());
  EXPECT_EQ(old_sink.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(old_sink.error().front().range,
            sourceRange(generic_sink_instruction.operands.front()));
  EXPECT_TRUE(checker::check(
                  std::get<Mbarrier>(sink->functions.front().body[0]),
                  checker::Context{.target = {.ptx_version = {7, 1},
                                              .sm_version = 80},
                                   .instruction_range =
                                       generic_sink_instruction.range})
                  .has_value());
  const auto no_cluster = checker::check(
      std::get<Mbarrier>(sink->functions.front().body[1]),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90},
                       .instruction_range = cluster_sink_instruction.range});
  ASSERT_FALSE(no_cluster.has_value());
  EXPECT_EQ(no_cluster.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  EXPECT_EQ(no_cluster.error().front().range, cluster_sink_instruction.range);

  const auto count_ast = parseModule(R"ptx(
.shared .align 8 .b64 bar;
.entry kernel() { mbarrier.arrive.shared::cluster.b64 _, [bar], 1048576; }
)ptx");
  const auto count = resolveModule(count_ast);
  ASSERT_TRUE(count.has_value()) << count.error().front().message;
  const auto& count_instruction = std::get<syntax_ast::AstInstruction>(
      std::get<syntax_ast::AstFunction>(count_ast.items.back()).body.back());
  const auto invalid_count = checker::check(
      std::get<Mbarrier>(count->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3},
                                  .sm_version = 90,
                                  .capabilities = cluster_capabilities},
                       .instruction_range = count_instruction.range});
  ASSERT_FALSE(invalid_count.has_value());
  EXPECT_EQ(invalid_count.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
  EXPECT_EQ(invalid_count.error().front().range,
            std::get<syntax_ast::AstImmediate>(count_instruction.operands.back())
                .syntax.range);

  const auto parity_ast = parseModule(R"ptx(
.shared .align 8 .b64 bar;
.entry kernel() {
  .reg .pred %p0;
  mbarrier.test_wait.parity.b64 %p0, [bar], 0;
  mbarrier.test_wait.parity.b64 %p0, [bar], 1;
  mbarrier.test_wait.parity.b64 %p0, [bar], 2;
  mbarrier.try_wait.parity.b64 %p0, [bar], 0;
  mbarrier.try_wait.parity.b64 %p0, [bar], 1;
  mbarrier.try_wait.parity.b64 %p0, [bar], 2;
}
)ptx");
  const auto parity = resolveModule(parity_ast);
  ASSERT_TRUE(parity.has_value()) << parity.error().front().message;
  const checker::Context mbarrier_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  const auto& parity_function =
      std::get<syntax_ast::AstFunction>(parity_ast.items.back());
  for (const auto index : {0u, 1u, 3u, 4u})
    EXPECT_TRUE(checker::check(
                    std::get<Mbarrier>(parity->functions.front().body[index]),
                    mbarrier_context)
                    .has_value());
  for (const auto index : {2u, 5u}) {
    const auto invalid_parity = checker::check(
        std::get<Mbarrier>(parity->functions.front().body[index]), mbarrier_context);
    ASSERT_FALSE(invalid_parity.has_value());
    EXPECT_EQ(invalid_parity.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    const auto& syntax_instruction = std::get<syntax_ast::AstInstruction>(
        parity_function.body[index + 1]);
    EXPECT_EQ(invalid_parity.error().front().range,
              std::get<syntax_ast::AstImmediate>(syntax_instruction.operands.back())
                  .syntax.range);
  }

  const auto reports = resolveModule(parseModule(R"ptx(
.shared .align 8 .b64 bar;
.entry kernel() {
  .reg .pred %p<2>;
  .reg .b8 %report;
  .reg .b64 %state;
  mbarrier.test_wait.phase_type::primary.b64 %p0|%p1, [bar], %state;
  mbarrier.test_wait.phase_type::primary.b64 %p0|%p1, %report, [bar], %state;
  mbarrier.try_wait.phase_type::primary.b64 %p0|%p1, %report, [bar], %state;
  mbarrier.test_wait.parity.phase_type::conditional.b64 %p0, [bar], 1;
}
)ptx"));
  ASSERT_TRUE(reports.has_value()) << reports.error().front().message;
  for (const auto& instruction : reports->functions.front().body)
    EXPECT_TRUE(
        checker::check(std::get<Mbarrier>(instruction), mbarrier_context)
            .has_value());
  const auto invalid_conditional_ast = parseModule(R"ptx(
.shared .align 8 .b64 bar;
.entry kernel() { .reg .pred %p<2>; .reg .b8 %report; .reg .b64 %state;
  mbarrier.test_wait.parity.phase_type::conditional.b64 %p0|%p1, %report, [bar], 1; }
)ptx");
  const auto invalid_conditional = resolveModule(invalid_conditional_ast);
  ASSERT_FALSE(invalid_conditional.has_value());
  const auto& conditional_instruction = std::get<syntax_ast::AstInstruction>(
      std::get<syntax_ast::AstFunction>(invalid_conditional_ast.items.back())
          .body.back());
  EXPECT_EQ(invalid_conditional.error().front().range,
            conditional_instruction.range);
}

TEST(ResolvedModule, ChecksM13ClusterlaunchcontrolBoundaryMatrix) {
  const auto profile = base::find_target_profile("sm_100a");
  const auto family_profile = base::find_target_profile("sm_100f");
  ASSERT_TRUE(profile.has_value());
  ASSERT_TRUE(family_profile.has_value());
  const auto context_for = [](const base::TargetProfile& target,
                              SourceRange instruction_range) {
    return checker::Context{
        .target = {.ptx_version = {9, 3},
                   .sm_version = target.identity.architecture.number,
                   .enabled_family_features = target.enabled_family_features,
                   .identity = target.identity,
                   .capabilities = target.capabilities},
        .instruction_range = instruction_range,
    };
  };
  const auto check_alignment = [&](std::string_view source,
                                   size_t operand_index) {
    const auto ast = parseModule(source);
    const auto resolved = resolveModule(ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    const auto& instruction =
        std::get<syntax_ast::AstInstruction>(
            std::get<syntax_ast::AstFunction>(ast.items.back()).body.back());
    const auto checked = checker::check(
        std::get<Clusterlaunchcontrol>(resolved->functions.front().body.front()),
        context_for(*profile, instruction.range));
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
    EXPECT_EQ(checked.error().front().range,
              std::get<syntax_ast::AstAddress>(instruction.operands[operand_index])
                  .range);
  };
  check_alignment(R"ptx(
.shared .align 8 .b8 response[16];
.shared .align 8 .b8 barrier[8];
.entry kernel() { clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 [response], [barrier]; }
)ptx", 0);
  check_alignment(R"ptx(
.shared .align 16 .b8 response[16];
.shared .align 4 .b8 barrier[8];
.entry kernel() { clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 [response], [barrier]; }
)ptx", 1);

  const auto multicast_ast = parseModule(R"ptx(
.shared .align 16 .b8 response[16];
.shared .align 8 .b8 barrier[8];
.entry kernel() { clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.multicast::cluster::all.b128 [response], [barrier]; }
)ptx");
  const auto multicast = resolveModule(multicast_ast);
  ASSERT_TRUE(multicast.has_value()) << multicast.error().front().message;
  const auto& multicast_instruction =
      std::get<syntax_ast::AstInstruction>(
          std::get<syntax_ast::AstFunction>(multicast_ast.items.back()).body.back());
  const auto& resolved_multicast =
      std::get<Clusterlaunchcontrol>(multicast->functions.front().body.front());
  EXPECT_TRUE(checker::check(resolved_multicast,
                             context_for(*profile, multicast_instruction.range))
                  .has_value());
  EXPECT_TRUE(checker::check(
                  resolved_multicast,
                  context_for(*family_profile, multicast_instruction.range))
                  .has_value());
  const auto no_family = checker::check(
      resolved_multicast,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                       .instruction_range = multicast_instruction.range});
  ASSERT_FALSE(no_family.has_value());
  EXPECT_EQ(no_family.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  EXPECT_EQ(no_family.error().front().range, multicast_instruction.range);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
