#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Exercise semantic explicit-context matching while preserving catalog integrity. */
template <typename Facts, typename Check, typename Violation>
  requires std::invocable<Check, const Facts&> && std::is_enum_v<Violation>
void checkKnownTargetAliases(Facts facts, Check check,
                             Violation target_violation,
                             Violation context_violation) {
  facts.ptx_version = checker::PtxVersion{8, 6};
  facts.b_shared_word = TcgenSharedWord{1ULL << 46};
  facts.b_context.major = TcgenMajor::K;
  for (const std::string_view target : {"sm_100a", "compute_100a"}) {
    facts.target = base::parse_target_identity(target);
    for (const std::string_view explicit_target : {"sm_100a", "compute_100a"}) {
      facts.b_context.target = base::parse_target_identity(explicit_target);
      const auto report = check(facts);
      EXPECT_EQ(std::ranges::count(report.violations, target_violation), 0);
      EXPECT_EQ(std::ranges::count(report.violations, context_violation), 0);
    }
    facts.b_context.target = base::parse_target_identity("compute_100f");
    EXPECT_GT(std::ranges::count(check(facts).violations, context_violation),
              0);
    facts.b_context.target = base::parse_target_identity("compute_110a");
    EXPECT_GT(std::ranges::count(check(facts).violations, context_violation),
              0);
    facts.b_context.target = base::parse_target_identity("compute_100a");
    facts.b_context.target->source_spelling = "compute_110a";
    EXPECT_GT(std::ranges::count(check(facts).violations, context_violation),
              0);
    facts.b_context.target.reset();
    facts.target->source_spelling = "compute_110a";
    EXPECT_GT(std::ranges::count(check(facts).violations, target_violation), 0);
  }
}

/** Each generated MMA target matcher accepts aliases and rejects forged identity. */
TEST(TargetSynonyms, MatchesGeneratedKnownTargetAndExplicitContext) {
  checkKnownTargetAliases(
      TcgenTf32KnownFacts{.group = TcgenCtaGroup::One,
                          .a_in_tmem = true,
                          .instruction = {0x910U | (4U << 24) | (1U << 17),
                                          TcgenMmaKind::Tf32}},
      check_tcgen_tf32_known_operation, TcgenTf32Violation::Target,
      TcgenTf32Violation::InvalidContext);
  checkKnownTargetAliases(
      TcgenSparseKnownFacts{.source_kind = TcgenMmaKind::F16,
                            .group = TcgenCtaGroup::One,
                            .a_in_tmem = true,
                            .instruction = {(4U << 24) | (1U << 17) | (1U << 2),
                                            TcgenMmaKind::F16}},
      check_tcgen_sparse_known_operation, TcgenSparseViolation::Target,
      TcgenSparseViolation::InvalidContext);
  checkKnownTargetAliases(
      TcgenSparseMxKnownFacts{
          .source_kind = TcgenMmaKind::MxF8F6F4,
          .group = TcgenCtaGroup::One,
          .a_in_tmem = true,
          .scale_selector = TcgenScaleVectorSize::Absent,
          .instruction = {(1U << 27) | (2U << 17) | (1U << 23) | (1U << 10) |
                              (1U << 2),
                          TcgenMmaKind::MxF8F6F4}},
      check_tcgen_sparse_mx_known_operation, TcgenSparseMxViolation::Target,
      TcgenSparseMxViolation::InvalidContext);
  checkKnownTargetAliases(
      TcgenWsKnownFacts{
          .source_kind = TcgenMmaKind::F16,
          .group = TcgenCtaGroup::One,
          .a_in_tmem = true,
          .instruction = {(2U << 24) | (8U << 17), TcgenMmaKind::F16}},
      check_tcgen_ws_known_operation, TcgenWsViolation::Target,
      TcgenWsViolation::InvalidContext);
}

/** Every catalog synonym owns the original source configuration past AST lifetime. */
TEST(TargetSynonyms, OwnsEveryCatalogAlias) {
  for (const std::string_view suffix :
       {"10",   "11",   "12",   "13",   "20",   "30",   "32",   "35",
        "37",   "50",   "52",   "53",   "60",   "61",   "62",   "70",
        "72",   "75",   "80",   "86",   "87",   "88",   "89",   "90",
        "90a",  "100",  "100a", "100f", "103",  "103a", "103f", "110",
        "110a", "110f", "120",  "120a", "120f", "121",  "121a", "121f"}) {
    const std::string target = "compute_" + std::string{suffix};
    SCOPED_TRACE(target);
    std::optional<ResolvedModule> owned;
    {
      const auto source = fmt::format(
          ".version 9.3\n.target {}\n.address_size 64\n.entry k() {{ ret; }}",
          target);
      PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      auto resolved = resolveAndValidateModule(*ast);
      ASSERT_TRUE(resolved) << resolved.error().front().message;
      EXPECT_TRUE(validateModule(*ast, *resolved));
      EXPECT_EQ(resolved->functions.front().source_target, target);
      ASSERT_TRUE(resolved->functions.front().source_region);
      EXPECT_EQ(
          resolved->header.regions[*resolved->functions.front().source_region]
              .target_options.front(),
          target);
      owned.emplace(std::move(*resolved));
    }
    EXPECT_TRUE(validateModule(*owned));
    // Semantic synonymy must not weaken owned source-provenance integrity.
    owned->functions.front().source_target = "sm_" + std::string{suffix};
    const auto mismatched = validateModule(*owned);
    ASSERT_FALSE(mismatched);
    EXPECT_EQ(mismatched.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
  }
}

/** Real capability, family and exact-target gates keep their original boundaries. */
TEST(TargetSynonyms, AppliesTypedAvailabilityAndOwnedRegions) {
  for (const auto& [target, instruction, accepted] : {
           std::tuple{"compute_90", "mov.u32 %r0, %cluster_ctarank;", true},
           std::tuple{"compute_80", "mov.u32 %r0, %cluster_ctarank;", false},
           std::tuple{"compute_120f", "add.u8x4 %r0, %r1, %r2;", true},
           std::tuple{"compute_120", "add.u8x4 %r0, %r1, %r2;", false},
           std::tuple{"compute_90a", "setmaxnreg.inc.sync.aligned.u32 64;",
                      true},
           std::tuple{"compute_90", "setmaxnreg.inc.sync.aligned.u32 64;",
                      false},
           std::tuple{"compute_100f", "setmaxnreg.inc.sync.aligned.u32 64;",
                      true},
       }) {
    SCOPED_TRACE(target);
    std::optional<ResolvedModule> owned;
    {
      const auto source = fmt::format(R"ptx(.version 9.3
.target compute_50
.address_size 64
.entry first() {{ ret; }}
.target {}
.entry second() {{ .reg .b32 %r<3>; {} }}
)ptx",
                                      target, instruction);
      PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      auto resolved = resolveModuleOnly(*ast);
      ASSERT_TRUE(resolved) << resolved.error().front().message;
      EXPECT_EQ(resolveAndValidateModule(*ast).has_value(), accepted);
      EXPECT_EQ(validateModule(*ast, *resolved).has_value(), accepted);
      EXPECT_EQ(resolved->functions[0].source_target, "compute_50");
      EXPECT_EQ(resolved->functions[1].source_target, target);
      owned.emplace(std::move(*resolved));
    }
    EXPECT_EQ(validateModule(*owned).has_value(), accepted);
  }
}

/** Unknown aliases reset earlier target regions and preserve their diagnostic spelling. */
TEST(TargetSynonyms, RejectsUncatalogedAliasesWithoutInheritingEarlierTarget) {
  for (const std::string_view target :
       {"compute_14", "compute_90f", "compute_101a", "compute_101f",
        "compute_123a"}) {
    const auto source = fmt::format(R"ptx(.version 9.3
.target compute_90
.address_size 64
.entry first() {{ ret; }}
.target {}
.entry second() {{ .reg .u32 %r; mov.u32 %r, %cluster_ctarank; }}
)ptx",
                                    target);
    PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto rejected = resolveAndValidateModule(*ast);
    ASSERT_FALSE(rejected);
    ASSERT_EQ(rejected.error().size(), 2u);
    EXPECT_EQ(rejected.error()[0].checker_kind,
              checker::CheckDiagnosticKind::UnknownTarget);
    EXPECT_EQ(rejected.error()[1].checker_kind,
              checker::CheckDiagnosticKind::MissingValidationContext);
    EXPECT_EQ(rejected.error().front().message,
              fmt::format("Unknown validation target '{}'.", target));
  }
  for (const std::string_view target :
       {"compute_", "compute_09", "compute_90af", "compute_90b",
        "compute_4294967296"}) {
    const auto source = fmt::format(".target {}", target);
    PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    EXPECT_TRUE(!ast || !ast.diagnostics.empty()) << target;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
