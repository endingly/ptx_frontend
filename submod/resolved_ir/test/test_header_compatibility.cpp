#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/base/ptx_target.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a header fixture without relying on parser or source lifetime. */
std::optional<syntax_ast::AstModule> parseHeader(std::string_view source) {
  PtxSyntaxParser parser{source};
  auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Compare AST-backed and source-free header validation under both policies. */
void expectHeader(std::string_view source, bool accepted) {
  const auto ast = parseHeader(source);
  ASSERT_TRUE(ast);
  const auto owned = resolveModuleOnly(*ast);
  ASSERT_TRUE(owned) << owned.error().front().message;
  const auto strict = resolveAndValidateModule(*ast);
  EXPECT_EQ(strict.has_value(), accepted) << source;
  for (const auto policy : {ModuleValidationPolicy::RequireCompleteContext,
                            ModuleValidationPolicy::AvailableContext}) {
    for (const auto& result : {validateModule(*ast, *owned, policy),
                               validateModule(*owned, policy)}) {
      EXPECT_EQ(result.has_value(), accepted)
          << source << "\n"
          << (result ? "accepted" : result.error().front().message);
      if (!accepted && !result)
        EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& item) {
          return item.kind ==
                 checker::CheckDiagnosticKind::UnsupportedAvailability;
        }));
    }
  }
}

/** Source header features are validated even when the module has no functions. */
TEST(HeaderCompatibility, RejectsKnownHeaderContradictions) {
  for (const auto source :
       {".version 1.0\n.target sm_80\n", ".version 0.9\n.target sm_10\n",
        ".version 2.2\n.target sm_20\n.address_size 32\n",
        ".version 2.2\n.target sm_20\n.address_size 64\n",
        ".version 2.3\n.target sm_20, debug\n",
        ".version 1.4\n.target sm_10, texmode_unified\n",
        ".version 1.4\n.target sm_10, texmode_independent\n",
        ".version 9.3\n.target sm_80, map_f64_to_f32\n",
        ".version 1.5\n.target sm_13, map_f64_to_f32\n",
        ".version 7.0\n.target sm_80\n.target sm_90\n"}) {
    SCOPED_TRACE(source);
    expectHeader(source, false);
  }
}

/** Explicit features accept their first version and omission keeps the 32-bit default. */
TEST(HeaderCompatibility, AcceptsHeaderFeatureBoundaries) {
  for (const auto source :
       {".version 1.0\n.target sm_10\n",
        ".version 2.3\n.target sm_20\n.address_size 32\n",
        ".version 2.3\n.target sm_20\n.address_size 64\n",
        ".version 3.0\n.target sm_20, debug\n",
        ".version 1.5\n.target sm_10, texmode_unified\n",
        ".version 1.5\n.target sm_10, texmode_independent\n",
        ".version 1.5\n.target sm_10, map_f64_to_f32\n",
        ".version 1.5\n.target compute_12, map_f64_to_f32\n"}) {
    SCOPED_TRACE(source);
    expectHeader(source, true);
  }
}

/** Check every catalog minimum against independently transcribed PTX ISA 9.3 data. */
TEST(HeaderCompatibility, CoversEveryExactProfileAndComputeSynonym) {
  /** Expected exact profile introduction, independently of production metadata. */
  struct ProfileCase {
    /** Architecture spelling without the equivalent source prefixes. */
    std::string_view suffix;
    /** First supported decimal PTX ISA version components. */
    uint16_t major;
    uint16_t minor;
  };
  constexpr ProfileCase cases[]{
      {"10", 1, 0},   {"11", 1, 0},   {"12", 1, 2},   {"13", 1, 2},
      {"20", 2, 0},   {"30", 3, 0},   {"32", 4, 0},   {"35", 3, 1},
      {"37", 4, 1},   {"50", 4, 0},   {"52", 4, 1},   {"53", 4, 2},
      {"60", 5, 0},   {"61", 5, 0},   {"62", 5, 0},   {"70", 6, 0},
      {"72", 6, 1},   {"75", 6, 3},   {"80", 7, 0},   {"86", 7, 1},
      {"87", 7, 4},   {"88", 9, 0},   {"89", 7, 8},   {"90", 7, 8},
      {"90a", 8, 0},  {"100", 8, 6},  {"100a", 8, 6}, {"100f", 8, 8},
      {"103", 8, 8},  {"103a", 8, 8}, {"103f", 8, 8}, {"110", 9, 0},
      {"110a", 9, 0}, {"110f", 9, 0}, {"120", 8, 7},  {"120a", 8, 7},
      {"120f", 8, 8}, {"121", 8, 8},  {"121a", 8, 8}, {"121f", 8, 8},
  };
  static_assert(std::size(cases) == 40);
  for (const auto& item : cases) {
    for (const auto prefix : {"sm_", "compute_"}) {
      const std::string target = std::string{prefix} + std::string{item.suffix};
      SCOPED_TRACE(target);
      const auto profile = base::find_target_profile(target);
      ASSERT_TRUE(profile);
      EXPECT_EQ(profile->minimum_ptx_version.major, item.major);
      EXPECT_EQ(profile->minimum_ptx_version.minor, item.minor);
      EXPECT_EQ(profile->supports_map_f64_to_f32, item.suffix == "10" ||
                                                      item.suffix == "11" ||
                                                      item.suffix == "12");
      const std::string header = "\n.target " + target + "\n";
      expectHeader(".version " + std::to_string(item.major) + "." +
                       std::to_string(item.minor) + header,
                   true);
      // A minor zero boundary uses the final decimal minor of the prior major.
      const auto prior_major = item.minor == 0 ? item.major - 1 : item.major;
      const auto prior_minor = item.minor == 0 ? 9 : item.minor - 1;
      expectHeader(".version " + std::to_string(prior_major) + "." +
                       std::to_string(prior_minor) + header,
                   false);
      expectHeader(".version 9.3\n.target " + target + ", map_f64_to_f32\n",
                   profile->supports_map_f64_to_f32);
    }
  }
}

/** Unknown and unsupported catalog keys never acquire guessed header minima. */
TEST(HeaderCompatibility, UnknownProfilesRemainUnknown) {
  for (const auto suffix : {"999", "101", "101a", "101f", "80a", "90f"}) {
    for (const auto prefix : {"sm_", "compute_"}) {
      const auto ast = parseHeader(".version 9.3\n.target " +
                                   std::string{prefix} + suffix + "\n");
      ASSERT_TRUE(ast);
      const auto owned = resolveModuleOnly(*ast);
      ASSERT_TRUE(owned);
      for (const auto& result :
           {validateModule(*ast, *owned), validateModule(*owned)}) {
        ASSERT_FALSE(result);
        EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& item) {
          return item.kind == checker::CheckDiagnosticKind::UnknownTarget;
        }));
        EXPECT_FALSE(std::ranges::any_of(result.error(), [](const auto& item) {
          return item.kind ==
                 checker::CheckDiagnosticKind::UnsupportedAvailability;
        }));
      }
    }
  }
}

/** Replacement validation reads the supplied header rather than old owned options. */
TEST(HeaderCompatibility, ReplacementAstSelectsItsOwnHeader) {
  const auto valid = parseHeader(".version 9.3\n.target sm_80\n");
  const auto invalid = parseHeader(".version 1.0\n.target sm_80\n");
  ASSERT_TRUE(valid && invalid);
  auto owned = resolveModuleOnly(*valid);
  ASSERT_TRUE(owned);
  EXPECT_FALSE(validateModule(*invalid, *owned));
  EXPECT_TRUE(validateModule(*owned));
  for (auto& region : owned->header.regions)
    region.version = checker::PtxVersion{1, 0};
  EXPECT_TRUE(validateModule(*valid, *owned));
  EXPECT_FALSE(validateModule(*owned));
}

/** Replacement version and options select each feature gate independently. */
TEST(HeaderCompatibility, ReplacementAstChecksEachHeaderFeature) {
  const std::pair<std::string_view, std::string_view> cases[]{
      {".version 2.3\n.target sm_20\n.address_size 32\n",
       ".version 2.2\n.target sm_20\n.address_size 32\n"},
      {".version 3.0\n.target sm_20, debug\n",
       ".version 2.3\n.target sm_20, debug\n"},
      {".version 1.5\n.target sm_10, texmode_unified\n",
       ".version 1.4\n.target sm_10, texmode_unified\n"},
      {".version 1.5\n.target sm_10, texmode_independent\n",
       ".version 1.4\n.target sm_10, texmode_independent\n"},
      {".version 9.3\n.target sm_12, map_f64_to_f32\n",
       ".version 9.3\n.target sm_80, map_f64_to_f32\n"},
  };
  for (const auto& [valid_source, invalid_source] : cases) {
    SCOPED_TRACE(invalid_source);
    const auto valid = parseHeader(valid_source);
    const auto invalid = parseHeader(invalid_source);
    ASSERT_TRUE(valid && invalid);
    const auto owned = resolveModuleOnly(*valid);
    const auto invalid_owned = resolveModuleOnly(*invalid);
    ASSERT_TRUE(owned && invalid_owned);
    for (const auto policy : {ModuleValidationPolicy::RequireCompleteContext,
                              ModuleValidationPolicy::AvailableContext}) {
      EXPECT_FALSE(validateModule(*invalid, *owned, policy));
      EXPECT_TRUE(validateModule(*valid, *invalid_owned, policy));
      EXPECT_TRUE(validateModule(*owned, policy));
      EXPECT_FALSE(validateModule(*invalid_owned, policy));
    }
  }
}

/** Header feature checks survive source destruction and coherent owned mutation. */
TEST(HeaderCompatibility, RechecksOwnedHeaderAfterAstDestruction) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = ".version 9.3\n.target sm_80\n";
    const auto ast = parseHeader(source);
    ASSERT_TRUE(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved);
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(validateModule(*owned));
  for (auto& region : owned->header.regions)
    region.version = checker::PtxVersion{1, 0};
  EXPECT_FALSE(validateModule(*owned));
  for (auto& region : owned->header.regions)
    region.version = checker::PtxVersion{9, 3};
  owned->header.regions.back().target_options.push_back("map_f64_to_f32");
  owned->header.regions.back().map_f64_to_f32 = true;
  EXPECT_FALSE(validateModule(*owned));
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::AvailableContext));
}

/** Partial validation defers absent inputs but still rejects known option restrictions. */
TEST(HeaderCompatibility, AvailableContextDefersOnlyMissingInputs) {
  for (const auto source : {".target sm_80\n", ".version 1.0\n"}) {
    const auto ast = parseHeader(source);
    ASSERT_TRUE(ast);
    const auto owned = resolveModuleOnly(*ast);
    ASSERT_TRUE(owned);
    EXPECT_TRUE(
        validateModule(*ast, *owned, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(validateModule(*owned));
  }
  const auto ast = parseHeader(".target sm_80, map_f64_to_f32\n");
  ASSERT_TRUE(ast);
  const auto owned = resolveModuleOnly(*ast);
  ASSERT_TRUE(owned);
  EXPECT_FALSE(
      validateModule(*ast, *owned, ModuleValidationPolicy::AvailableContext));
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::AvailableContext));
}

/** Retained explicit source events cannot be hidden by mutating address provenance. */
TEST(HeaderCompatibility, RejectsExplicitAddressProvenanceLaundering) {
  const auto ast =
      parseHeader(".version 2.2\n.target sm_20\n.address_size 32\n");
  ASSERT_TRUE(ast);
  auto owned = resolveModuleOnly(*ast);
  ASSERT_TRUE(owned);
  for (auto& region : owned->header.regions)
    region.address_size_provenance = SourceConfigurationProvenance::Defaulted;
  EXPECT_FALSE(validateModule(*owned));
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::AvailableContext));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
