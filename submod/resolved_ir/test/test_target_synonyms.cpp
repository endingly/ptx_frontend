#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
       {"10",   "11",  "12",   "13",   "20",  "30",   "32",   "35",  "37",
        "50",   "52",  "53",   "60",   "61",  "62",   "70",   "72",  "75",
        "80",   "86",  "87",   "88",   "89",  "90",   "90a",  "100", "100a",
        "100f", "103", "103a", "103f", "101", "101a", "101f", "110", "110a",
        "110f", "120", "120a", "120f", "121", "121a", "121f"}) {
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
       {"compute_14", "compute_90f", "compute_102a", "compute_102f",
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

/** Construct initialized nonempty ordinary and feature fixtures under exact source spelling. */
std::string target101Source(std::string target, std::string version,
                            std::string body = "") {
  return ".version " + version + "\n.target " + target +
         "\n.address_size 64\n.global .align 64 .b8 mm[256];\n.entry k(){.reg "
         ".b32 %b<8>;.reg .u32 %u;mov.b32 %b0,1;" +
         body + "ret;}";
}
/** Return a fully owned value or the exact source-stage diagnostic after AST destruction. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> target101Owned(
    const std::string& source) {
  PtxSyntaxParser p(source);
  auto a = p.parseModule();
  if (!a || !a.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*a);
}
/** Project a recognized source identity into explicit concrete checker facts without rewriting it. */
checker::Context target101Context(std::string_view name,
                                  checker::PtxVersion version) {
  const auto profile = base::find_target_profile(name);
  EXPECT_TRUE(profile);
  if (!profile)
    return {};
  return {.target = {
              .ptx_version = version,
              .sm_version =
                  base::target_feature_architecture(profile->identity).number,
              .enabled_family_features = profile->enabled_family_features,
              .identity = profile->identity,
              .capabilities = profile->capabilities}};
}
/** All original source windows and prefixes remain independent from feature correspondence. */
TEST(HistoricalTarget101, SourceSpellingsProfilesAndFeatureIdentity) {
  for (auto prefix : {"sm_", "compute_"})
    for (auto suffix : {"101", "101a", "101f", "110", "110a", "110f"}) {
      const std::string target = std::string(prefix) + suffix;
      const bool old = std::string_view(suffix).starts_with("101");
      const int floor =
          old ? (std::string_view(suffix).ends_with('f') ? 8 : 6) : 90;
      auto profile = base::find_target_profile(target);
      ASSERT_TRUE(profile);
      EXPECT_EQ(profile->identity.source_spelling, target);
      EXPECT_EQ(profile->identity.architecture.number, old ? 101u : 110u);
      EXPECT_EQ(base::target_feature_architecture(profile->identity).number,
                110u);
      EXPECT_EQ(profile->minimum_ptx_version.major, old ? 8 : 9);
      EXPECT_EQ(profile->minimum_ptx_version.minor, old ? floor : 0);
      EXPECT_FALSE(profile->supports_map_f64_to_f32);
      EXPECT_EQ(profile->enabled_family_features.empty(),
                std::string_view(suffix).back() != 'a' &&
                    std::string_view(suffix).back() != 'f');
      if (!profile->enabled_family_features.empty()) {
        ASSERT_EQ(profile->enabled_family_features.size(), 1u);
        EXPECT_EQ(profile->enabled_family_features[0], "sm_110f");
      }
      for (auto cap :
           {"cluster", "aggregate_smem", "reserved_smem", "graph_exec"})
        EXPECT_TRUE(base::target_has_capability(*profile, cap));
      EXPECT_EQ(profile->capabilities.size(), 4u);
      for (auto [version, number] : {std::pair{"8.5", 85},
                                     {"8.6", 86},
                                     {"8.7", 87},
                                     {"8.8", 88},
                                     {"9.0", 90},
                                     {"9.3", 93}}) {
        if (!old && number < 88)
          continue;
        const bool accepted = number >= (old ? 80 + floor : 90);
        SCOPED_TRACE(target + version);
        auto source = target101Source(target, version);
        auto result = target101Owned(source);
        EXPECT_EQ(bool(result), accepted);
        if (result) {
          EXPECT_EQ(result->functions[0].source_target, target);
          EXPECT_TRUE(validateModule(*result));
        }
      }
    }
  auto old = base::find_target_profile("sm_101a"),
       alias = base::find_target_profile("compute_101a"),
       modern = base::find_target_profile("sm_110a");
  ASSERT_TRUE(old && alias && modern);
  EXPECT_TRUE(base::same_target_identity(old->identity, alias->identity));
  EXPECT_FALSE(base::same_target_identity(old->identity, modern->identity));
  EXPECT_EQ(base::target_feature_architecture(old->identity),
            base::target_feature_architecture(modern->identity));
  EXPECT_FALSE(base::same_target_identity(
      base::find_target_profile("sm_101f")->identity,
      base::find_target_profile("sm_110f")->identity));
  for (auto bad : {"sm_999", "sm_102", "sm_80a", "sm_90f"})
    EXPECT_FALSE(base::find_target_profile(bad));
  EXPECT_EQ(base::target_feature_architecture({.architecture = {102}}).number,
            102u);
}
/** Historical permissions retain B32 carriers, real source minima and exact flavor boundaries. */
TEST(HistoricalTarget101, InitializedMovMultimemFenceAndMatrixSources) {
  for (auto target :
       {"sm_101", "sm_101a", "sm_101f", "compute_101a", "compute_101f"})
    for (auto version : {"8.6", "8.8", "9.0", "9.3"}) {
      const bool generic = std::string_view(target) == "sm_101";
      const bool family = std::string_view(target).ends_with('f');
      const bool header_ok = !(family && std::string_view(version) == "8.6");
      for (auto body : {"", "multimem.st.e4m3x4 [mm],%b0;",
                        "tcgen05.fence::before_thread_sync;",
                        ".shared .align 16 .b8 "
                        "tile[256];ldmatrix.sync.aligned.m16n16.x1.trans."
                        "shared.b8 {%b0,%b1},[tile];",
                        ".shared .align 16 .b8 "
                        "tile[256];stmatrix.sync.aligned.m16n8.x1.trans.shared."
                        "b8 [tile],{%b0};"}) {
        const bool expected =
            header_ok && (std::string_view(body).empty() || !generic);
        auto result = target101Owned(target101Source(target, version, body));
        SCOPED_TRACE(std::string(target) + version + body);
        EXPECT_EQ(bool(result), expected);
        if (result) {
          auto copy = *result;
          EXPECT_TRUE(validateModule(copy));
          for (auto& f : copy.functions)
            for (auto& i : f.body)
              i = i->clone();
          EXPECT_TRUE(validateModule(copy));
        }
      }
    }
  for (auto target : {"sm_101", "compute_101"})
    EXPECT_TRUE(
        target101Owned(target101Source(target, "8.6", "mov.u32 %u,1;")));
  EXPECT_TRUE(target101Owned(target101Source(
      "sm_101a", "8.6", "multimem.st.e4m3x4 [mm],0f00000001;")));
  for (auto body : {"multimem.st.e4m3x4 [mm],1.5;",
                    "multimem.st.e4m3x4 [mm],0d3ff0000000000000;"})
    EXPECT_FALSE(target101Owned(target101Source("sm_101a", "8.6", body)));
}
/** All ten original condition shapes recheck flavor, floor, capabilities and caller-owned numeric SM. */
TEST(HistoricalTarget101, TenShapeExactAndFamilyPredicates) {
  /** One original clause shape; direct units do not certify source profile/version pairs. */
  struct Shape {
    /** Independent instruction floor. */ checker::PtxVersion floor;
    /** Concrete feature-SM minimum, zero when absent. */ uint32_t sm;
    /** Require A identity rather than canonical F-family membership. */ bool
        exact;
    /** Preserve the cluster capability conjunct. */ bool cluster;
  };
  const Shape shapes[] = {
      {{8, 6}, 110, true, false},  {{8, 8}, 110, false, false},
      {{9, 0}, 110, true, false},  {{9, 0}, 110, false, true},
      {{9, 0}, 110, false, false}, {{9, 0}, 0, true, false},
      {{9, 1}, 110, true, false},  {{9, 1}, 110, false, false},
      {{9, 2}, 110, false, false}, {{9, 3}, 110, false, false}};
  const SourceRange range{{4, 3}, {4, 17}};
  for (const auto& shape : shapes)
    for (auto target : {"sm_101", "sm_101a", "sm_101f"}) {
      checker::AvailabilityDescriptor gate{.any_of_count = 1};
      auto& clause = gate.any_of[0];
      clause.minimum_ptx_version = shape.floor;
      clause.minimum_sm_version = shape.sm;
      if (shape.exact) {
        clause.has_exact_target = true;
        clause.exact_target_architecture = {110};
        clause.exact_target_flavor = base::TargetFlavor::ArchitectureSpecific;
      } else
        clause.required_family = "sm_110f";
      if (shape.cluster) {
        clause.capability_count = 1;
        clause.capabilities[0] = "cluster";
      }
      auto context = target101Context(target, shape.floor);
      context.instruction_range = range;
      const bool expected = shape.exact
                                ? std::string_view(target).ends_with('a')
                                : std::string_view(target) != "sm_101";
      EXPECT_EQ(checker::is_available(gate, context.target), expected);
      auto checked = checker::check_availability(
          {.variant_name = "Shape", .availability = gate}, context);
      EXPECT_EQ(bool(checked), expected);
      if (!expected) {
        ASSERT_FALSE(checked);
        EXPECT_EQ(checked.error()[0].kind,
                  checker::CheckDiagnosticKind::UnsupportedAvailability);
        EXPECT_EQ(checked.error()[0].range, range);
      }
      if (shape.cluster) {
        auto missing = context;
        missing.target.capabilities = {};
        EXPECT_FALSE(checker::is_available(gate, missing.target));
      }
      auto early = context;
      if (early.target.ptx_version == checker::PtxVersion{9, 0})
        early.target.ptx_version = {8, 8};
      else
        early.target.ptx_version.minor--;
      EXPECT_FALSE(checker::is_available(gate, early.target));
      auto numeric = context;
      numeric.target.sm_version = 0;
      EXPECT_EQ(checker::is_available(gate, numeric.target),
                expected && shape.sm == 0);
    }
  const checker::AvailabilityTarget required[] = {
      {.architecture = {101},
       .flavor = base::TargetFlavor::ArchitectureSpecific}};
  checker::AvailabilityDescriptor span{
      .any_of = {{{.exact_targets = required}}}, .any_of_count = 1};
  EXPECT_TRUE(
      checker::is_available(span, target101Context("sm_110a", {9, 0}).target));
  EXPECT_FALSE(
      checker::is_available(span, target101Context("sm_101f", {9, 0}).target));
}
/** Current caches are joined independently from supplied effective source context, without a history veto. */
TEST(HistoricalTarget101,
     CurrentCacheCoherenceAndBidirectionalEffectiveRetarget) {
  auto text =
      target101Source("sm_101a", "8.6", "tcgen05.fence::before_thread_sync;");
  PtxSyntaxParser p(text);
  auto ast = p.parseModule();
  ASSERT_TRUE(ast);
  auto m = resolveAndValidateModule(*ast);
  ASSERT_TRUE(m);
  for (int edit : {0, 1, 2}) {
    auto bad = *m;
    if (edit == 0)
      bad.functions[0].source_target = "sm_110a";
    if (edit == 1)
      bad.functions[0].source_version = checker::PtxVersion{9, 0};
    if (edit == 2)
      bad.functions[0].source_region = bad.header.regions.size();
    for (auto policy : {ModuleValidationPolicy::AvailableContext,
                        ModuleValidationPolicy::RequireCompleteContext})
      for (auto result :
           {validateModule(bad, policy), validateModule(*ast, bad, policy)}) {
        ASSERT_FALSE(result);
        EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
          return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
      }
  }
  auto replacement_text =
      target101Source("sm_110a", "9.0", "tcgen05.fence::before_thread_sync;");
  PtxSyntaxParser pp(replacement_text);
  auto replacement = pp.parseModule();
  ASSERT_TRUE(replacement);
  EXPECT_TRUE(validateModule(*replacement, *m));
  auto modern = resolveAndValidateModule(*replacement);
  ASSERT_TRUE(modern);
  EXPECT_TRUE(validateModule(*ast, *modern));
  EXPECT_TRUE(validateModule(*ast, *m));
  auto copy = *m;
  ResolvedModule assigned;
  assigned = copy;
  auto moved = std::move(assigned);
  for (auto& f : moved.functions)
    for (auto& i : f.body)
      i = i->clone();
  EXPECT_EQ(moved.functions[0].instruction_ranges,
            m->functions[0].instruction_ranges);
  EXPECT_TRUE(validateModule(moved));
  auto ordinary_text = target101Source("sm_101a", "8.6");
  auto ordinary = target101Owned(ordinary_text);
  ASSERT_TRUE(ordinary);
  auto below = *ordinary;
  for (auto& r : below.header.regions)
    r.version = checker::PtxVersion{8, 5};
  for (auto& f : below.functions)
    f.source_version = checker::PtxVersion{8, 5};
  auto bad_text = target101Source("sm_101a", "8.5");
  PtxSyntaxParser bp(bad_text);
  auto ba = bp.parseModule();
  ASSERT_TRUE(ba);
  for (auto policy : {ModuleValidationPolicy::AvailableContext,
                      ModuleValidationPolicy::RequireCompleteContext})
    for (auto result : {validateModule(below, policy),
                        validateModule(*ba, *ordinary, policy)}) {
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
        return d.kind ==
                   checker::CheckDiagnosticKind::UnsupportedAvailability &&
               d.message == "sm_101a requires PTX ISA >= 8.6.";
      }));
      EXPECT_FALSE(std::ranges::any_of(result.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
      }));
    }
}
/** Partial fragments retain known header checks, strict context diagnostics and exact region spellings. */
TEST(HistoricalTarget101, MissingContextRegionsUnknownAndMapControls) {
  const std::string partial =
      ".target sm_101a\n.entry k(){.reg .b32 %b;mov.b32 %b,1;ret;}";
  PtxSyntaxParser p(partial);
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  auto m = resolveModuleOnly(*a);
  ASSERT_TRUE(m);
  EXPECT_TRUE(validateModule(*m, ModuleValidationPolicy::AvailableContext));
  EXPECT_TRUE(validateModule(*a, *m, ModuleValidationPolicy::AvailableContext));
  for (auto result :
       {validateModule(*m, ModuleValidationPolicy::RequireCompleteContext),
        validateModule(*a, *m,
                       ModuleValidationPolicy::RequireCompleteContext)}) {
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::MissingValidationContext;
    }));
  }
  for (auto target : {"sm_101", "sm_101a", "sm_101f"})
    EXPECT_FALSE(target101Owned(
        target101Source(std::string(target) + ", map_f64_to_f32", "9.3")));
  const std::string regions =
      ".version 9.0\n.target sm_101\n.entry a(){ret;}\n.target "
      "compute_101a\n.entry b(){ret;}\n.target sm_101f\n.entry "
      "c(){ret;}\n.target sm_110a\n.entry d(){ret;}";
  auto historical_regions =
      regions.substr(0, regions.find("\n.target sm_110a"));
  historical_regions.replace(historical_regions.find("9.0"), 3, "8.8");
  auto historical = target101Owned(historical_regions);
  ASSERT_TRUE(historical);
  EXPECT_EQ(historical->functions.size(), 3u);
  EXPECT_TRUE(validateModule(*historical));
  auto r = target101Owned(regions);
  ASSERT_TRUE(r);
  EXPECT_EQ(r->functions.size(), 4u);
  EXPECT_EQ(r->functions[1].source_target, "compute_101a");
  EXPECT_EQ(r->functions[3].source_target, "sm_110a");
  EXPECT_TRUE(validateModule(*r));
  const auto unknown = regions + "\n.target sm_999\n.entry e(){ret;}";
  PtxSyntaxParser up(unknown);
  auto ua = up.parseModule();
  ASSERT_TRUE(ua);
  auto draft = resolveModuleOnly(*ua);
  ASSERT_TRUE(draft);
  for (auto result : {validateModule(*draft), validateModule(*ua, *draft)}) {
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::UnknownTarget;
    }));
  }
}
/** Later conversion and bulk-copy floors remain independent from historical source recognition. */
TEST(HistoricalTarget101, CvtExactFamilyAndLaterFloorsKeepMatchedCarriers) {
  const std::string declarations =
      ".reg .b16 %h0,%h1;.reg .f32 %f0,%f1;mov.b16 %h0,0;mov.b16 %h1,0;mov.f32 "
      "%f0,1.0;mov.f32 %f1,2.0;";
  for (auto target : {"sm_101", "sm_101a", "sm_101f"})
    for (auto version : {"9.0", "9.1"})
      for (auto op : {"cvt.rn.satfinite.s2f6x2.f32 %h0,%f0,%f1;",
                      "cvt.rn.satfinite.s2f6x2.bf16x2 %h0,%b0;",
                      "cvt.rn.bf16x2.s2f6x2 %b0,%h0;"}) {
        auto result =
            target101Owned(target101Source(target, version, declarations + op));
        SCOPED_TRACE(std::string(target) + version + op);
        EXPECT_EQ(bool(result), std::string_view(target) == "sm_101a" &&
                                    std::string_view(version) == "9.1");
      }
  for (auto target : {"sm_101a", "sm_101f"}) {
    EXPECT_FALSE(target101Owned(target101Source(
        target, "9.0",
        declarations + "cvt.rn.satfinite.e4m3x2.bf16x2 %h0,%b0;")));
    EXPECT_TRUE(target101Owned(target101Source(
        target, "9.1",
        declarations + "cvt.rn.satfinite.e4m3x2.bf16x2 %h0,%b0;")));
    EXPECT_FALSE(target101Owned(target101Source(
        target, "9.1", declarations + "cvt.rn.bf16x2.e4m3x2 %b0,%h0;")));
    EXPECT_TRUE(target101Owned(target101Source(
        target, "9.2", declarations + "cvt.rn.bf16x2.e4m3x2 %b0,%h0;")));
    const std::string bulk =
        ".shared .align 16 .b8 "
        "sh[256];multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_"
        "group.b128 [mm],[sh],16;";
    EXPECT_FALSE(target101Owned(target101Source(target, "9.2", bulk)));
    EXPECT_TRUE(target101Owned(target101Source(target, "9.3", bulk)));
  }
}
/** Actual known I8 words keep A-only gates and do not certify missing runtime operands. */
TEST(HistoricalTarget101,
     I8OperationalFactsHistoricalAAndSourceProfileMinimum) {
  TcgenI8KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction = {0x20U | (4U << 24) | (1U << 17), TcgenMmaKind::I8},
      .a_shared_word = TcgenSharedWord{1ULL << 46},
      .b_shared_word = TcgenSharedWord{1ULL << 46},
      .a_context = {.major = TcgenMajor::K},
      .b_context = {.major = TcgenMajor::K}};
  for (auto [name, version, accepted] :
       {std::tuple{"sm_101a", checker::PtxVersion{8, 6}, true},
        {"sm_101f", checker::PtxVersion{8, 8}, false},
        {"sm_101", checker::PtxVersion{8, 6}, false},
        {"sm_110a", checker::PtxVersion{8, 8}, false},
        {"sm_110a", checker::PtxVersion{9, 0}, true}}) {
    auto profile = base::find_target_profile(name);
    ASSERT_TRUE(profile);
    facts.target = profile->identity;
    facts.ptx_version = version;
    const auto report = check_tcgen_i8_known_operation(facts);
    EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
    EXPECT_EQ(
        std::ranges::count(report.violations, TcgenI8Violation::Target) == 0,
        accepted);
    if (accepted)
      EXPECT_TRUE(report.supplied_facts_ok());
  }
}
/** Restrict target-only operational checks to report functions and their explicit violation domain. */
template <typename Facts, typename Check, typename Violation>
  requires std::invocable<Check, const Facts&> && std::is_enum_v<Violation> &&
           requires(Facts f) {
             f.target;
             f.ptx_version;
             f.b_shared_word;
             f.b_context;
           }
void target101Operational(Facts facts, Check check, Violation violation) {
  facts.b_shared_word = TcgenSharedWord{1ULL << 46};
  facts.b_context.major = TcgenMajor::K;
  for (auto [name, version, accepted] :
       {std::tuple{"sm_101a", checker::PtxVersion{8, 6}, true},
        {"sm_101f", checker::PtxVersion{8, 8}, true},
        {"sm_101", checker::PtxVersion{9, 3}, false},
        {"sm_110a", checker::PtxVersion{8, 8}, false},
        {"sm_110a", checker::PtxVersion{9, 0}, true}}) {
    auto profile = base::find_target_profile(name);
    ASSERT_TRUE(profile);
    facts.target = profile->identity;
    facts.ptx_version = version;
    auto report = check(facts);
    EXPECT_EQ(std::ranges::count(report.violations, violation) == 0, accepted)
        << name;
  }
}
/** Dense, sparse, WS and sparse-MX generated matchers all use the same source-minimum and feature relation. */
TEST(HistoricalTarget101,
     FourOperationalTemplateFamiliesAndBlockVersusVectorGates) {
  target101Operational(
      TcgenTf32KnownFacts{.group = TcgenCtaGroup::One,
                          .a_in_tmem = true,
                          .instruction = {0x910U | (4U << 24) | (1U << 17),
                                          TcgenMmaKind::Tf32}},
      check_tcgen_tf32_known_operation, TcgenTf32Violation::Target);
  target101Operational(
      TcgenSparseKnownFacts{.source_kind = TcgenMmaKind::F16,
                            .group = TcgenCtaGroup::One,
                            .a_in_tmem = true,
                            .instruction = {(4U << 24) | (1U << 17) | (1U << 2),
                                            TcgenMmaKind::F16}},
      check_tcgen_sparse_known_operation, TcgenSparseViolation::Target);
  target101Operational(
      TcgenWsKnownFacts{
          .source_kind = TcgenMmaKind::F16,
          .group = TcgenCtaGroup::One,
          .a_in_tmem = true,
          .instruction = {(2U << 24) | (8U << 17), TcgenMmaKind::F16}},
      check_tcgen_ws_known_operation, TcgenWsViolation::Target);
  target101Operational(
      TcgenSparseMxKnownFacts{
          .source_kind = TcgenMmaKind::MxF8F6F4,
          .group = TcgenCtaGroup::One,
          .a_in_tmem = true,
          .scale_selector = TcgenScaleVectorSize::Absent,
          .instruction = {(1U << 27) | (2U << 17) | (1U << 23) | (1U << 10) |
                              (1U << 2),
                          TcgenMmaKind::MxF8F6F4}},
      check_tcgen_sparse_mx_known_operation, TcgenSparseMxViolation::Target);
  TcgenTf32KnownFacts scaled{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .scaled_d = true,
      .instruction = {0x910U | (4U << 24) | (1U << 17), TcgenMmaKind::Tf32}};
  for (auto name : {"sm_100a", "sm_101a", "sm_101f", "sm_110a"}) {
    scaled.target = base::find_target_profile(name)->identity;
    scaled.ptx_version = checker::PtxVersion{9, 3};
    EXPECT_EQ(
        std::ranges::count(check_tcgen_tf32_known_operation(scaled).violations,
                           TcgenTf32Violation::Target) == 0,
        std::string_view(name) == "sm_100a");
  }
  TcgenMx8KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .scale_selector = TcgenScaleVectorSize::Block32,
      .instruction = {(1U << 27) | (2U << 17) | (1U << 23) | (1U << 10),
                      TcgenMmaKind::MxF8F6F4}};
  for (auto name : {"sm_101a", "sm_101f"})
    for (auto version : {checker::PtxVersion{8, 7}, {8, 8}}) {
      facts.target = base::find_target_profile(name)->identity;
      facts.ptx_version = version;
      auto report = check_tcgen_mx8_known_operation(facts);
      EXPECT_EQ(
          std::ranges::count(report.violations, TcgenMx8Violation::Target) == 0,
          (version >= checker::PtxVersion{8, 8}));
      facts.scale_selector = TcgenScaleVectorSize::Vec1X;
      EXPECT_GT(
          std::ranges::count(check_tcgen_mx8_known_operation(facts).violations,
                             TcgenMx8Violation::Target),
          0);
      facts.scale_selector = TcgenScaleVectorSize::Block32;
    }
}
/** Cluster cancellation retains both the architectural grant and its independent cluster capability. */
TEST(HistoricalTarget101, ClusterCapabilityAndReductionSourceControls) {
  for (auto [target, version] :
       {std::pair{"sm_101a", "8.6"}, {"sm_101f", "8.8"}}) {
    for (auto qualifier : {"", "shared::cta."}) {
      auto body = std::string(
                      ".shared .align 16 .b8 response[16];.shared .align 8 .b8 "
                      "barrier[8];clusterlaunchcontrol.try_cancel.async.") +
                  qualifier +
                  "mbarrier::complete_tx::bytes.multicast::cluster::all.b128 "
                  "[response],[barrier];";
      auto m = target101Owned(target101Source(target, version, body));
      ASSERT_TRUE(m) << (m.error().empty() ? "" : m.error()[0].message);
      auto context = target101Context(target, std::string_view(version) == "8.6"
                                                  ? checker::PtxVersion{8, 6}
                                                  : checker::PtxVersion{8, 8});
      context.instruction_range = m->functions[0].instruction_ranges[1];
      EXPECT_TRUE(m->functions[0].body[1]->check(context));
      constexpr std::array<std::string_view, 3> no_cluster{
          "aggregate_smem", "reserved_smem", "graph_exec"};
      context.target.capabilities = no_cluster;
      auto rejected = m->functions[0].body[1]->check(context);
      ASSERT_FALSE(rejected);
      EXPECT_EQ(rejected.error()[0].kind,
                checker::CheckDiagnosticKind::UnsupportedAvailability);
      EXPECT_FALSE(target101Owned(target101Source("sm_101", version, body)));
    }
    const std::string reduction =
        ".reg .b32 %t;.reg .u32 %red;mov.b32 %t,0;mov.u32 "
        "%red,0;tcgen05.ld.red.sync.aligned.32x32b.x2.min.u32 "
        "{%b0,%b1},%red,[%t];tcgen05.wait::ld.sync.aligned;";
    auto loaded = target101Owned(target101Source(target, "8.8", reduction));
    ASSERT_TRUE(loaded) << (loaded.error().empty() ? ""
                                                   : loaded.error()[0].message);
    EXPECT_TRUE(validateModule(*loaded));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
