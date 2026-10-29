#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>

#include <ptx_frontend/base/ptx_target.hpp>

namespace ptx_frontend::base {
namespace {

TEST(TargetIdentity, ParsesAndKeepsEachFlavorDistinct) {
  const auto sm80 = parse_target_identity("sm_80");
  const auto generic = parse_target_identity("sm_90");
  const auto architecture_specific = parse_target_identity("sm_90a");
  // This is lexical parsing only; catalog support belongs to a later layer.
  const auto sm90f = parse_target_identity("sm_90f");
  const auto sm100a = parse_target_identity("sm_100a");
  const auto family_specific = parse_target_identity("sm_100f");
  const auto unknown_architecture = parse_target_identity("sm_123a");

  ASSERT_TRUE(sm80.has_value());
  ASSERT_TRUE(generic.has_value());
  ASSERT_TRUE(architecture_specific.has_value());
  ASSERT_TRUE(sm90f.has_value());
  ASSERT_TRUE(sm100a.has_value());
  ASSERT_TRUE(family_specific.has_value());
  ASSERT_TRUE(unknown_architecture.has_value());

  EXPECT_EQ(sm80->architecture.number, 80U);
  EXPECT_EQ(sm80->flavor, TargetFlavor::Generic);
  EXPECT_EQ(sm80->source_spelling, "sm_80");
  EXPECT_EQ(generic->architecture.number, 90U);
  EXPECT_EQ(generic->flavor, TargetFlavor::Generic);
  EXPECT_EQ(generic->source_spelling, "sm_90");
  EXPECT_EQ(architecture_specific->architecture.number, 90U);
  EXPECT_EQ(architecture_specific->flavor, TargetFlavor::ArchitectureSpecific);
  EXPECT_EQ(architecture_specific->source_spelling, "sm_90a");
  EXPECT_EQ(sm90f->architecture.number, 90U);
  EXPECT_EQ(sm90f->flavor, TargetFlavor::FamilySpecific);
  EXPECT_EQ(sm90f->source_spelling, "sm_90f");
  EXPECT_EQ(sm100a->architecture.number, 100U);
  EXPECT_EQ(sm100a->flavor, TargetFlavor::ArchitectureSpecific);
  EXPECT_EQ(sm100a->source_spelling, "sm_100a");
  EXPECT_EQ(family_specific->architecture.number, 100U);
  EXPECT_EQ(family_specific->flavor, TargetFlavor::FamilySpecific);
  EXPECT_EQ(family_specific->source_spelling, "sm_100f");
  EXPECT_EQ(unknown_architecture->architecture.number, 123U);
  EXPECT_EQ(unknown_architecture->flavor, TargetFlavor::ArchitectureSpecific);
  EXPECT_EQ(unknown_architecture->source_spelling, "sm_123a");

  EXPECT_NE(*generic, *architecture_specific);
  EXPECT_NE(*architecture_specific, *sm90f);
  EXPECT_NE(*sm100a, *family_specific);
}

TEST(TargetIdentity, RejectsMalformedSpellings) {
  constexpr std::array<std::string_view, 10> invalid{
      "",       "sm",      "sm_",    "SM_90",   "sm_09",
      "sm_90b", "sm_90af", "sm_90_", "sm_90a0", "sm_4294967296",
  };

  for (const std::string_view spelling : invalid)
    EXPECT_FALSE(parse_target_identity(spelling).has_value()) << spelling;
  for (const std::string_view suffix :
       {"", "09", "90b", "90af", "90_", "90a0", "4294967296", "+90", "-90"}) {
    const std::string spelling = "compute_" + std::string{suffix};
    EXPECT_FALSE(parse_target_identity(spelling)) << spelling;
    EXPECT_FALSE(find_target_profile(spelling)) << spelling;
  }
}

TEST(TargetProfile, CatalogsExactIdentityEnabledFamilyFeaturesAndCapabilities) {
  constexpr std::array<std::string_view, 0> none{};
  constexpr std::array<std::string_view, 1> sm100f_features{"sm_100f"};
  constexpr std::array<std::string_view, 2> sm103f_features{"sm_100f",
                                                            "sm_103f"};
  constexpr std::array<std::string_view, 1> sm120f_features{"sm_120f"};
  constexpr std::array<std::string_view, 2> sm121f_features{"sm_120f",
                                                            "sm_121f"};
  constexpr std::array<std::string_view, 1> sm110f_features{"sm_110f"};
  constexpr std::array<std::string_view, 1> graph_capabilities{"graph_exec"};
  constexpr std::array<std::string_view, 2> sm80_capabilities{"reserved_smem",
                                                              "graph_exec"};
  constexpr std::array<std::string_view, 4> sm90_capabilities{
      "cluster", "aggregate_smem", "reserved_smem", "graph_exec"};
  struct ExpectedProfile {
    std::string_view spelling;
    uint32_t architecture;
    TargetFlavor flavor;
    std::span<const std::string_view> enabled_family_features;
    std::span<const std::string_view> capabilities;
  };
  const std::array profiles{
      ExpectedProfile{"sm_10", 10, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_11", 11, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_12", 12, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_13", 13, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_20", 20, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_30", 30, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_32", 32, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_35", 35, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_37", 37, TargetFlavor::Generic, none, none},
      ExpectedProfile{"sm_50", 50, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_52", 52, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_53", 53, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_60", 60, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_61", 61, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_62", 62, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_70", 70, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_72", 72, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_75", 75, TargetFlavor::Generic, none,
                      graph_capabilities},
      ExpectedProfile{"sm_80", 80, TargetFlavor::Generic, none,
                      sm80_capabilities},
      ExpectedProfile{"sm_86", 86, TargetFlavor::Generic, none,
                      sm80_capabilities},
      ExpectedProfile{"sm_87", 87, TargetFlavor::Generic, none,
                      sm80_capabilities},
      ExpectedProfile{"sm_88", 88, TargetFlavor::Generic, none,
                      sm80_capabilities},
      ExpectedProfile{"sm_89", 89, TargetFlavor::Generic, none,
                      sm80_capabilities},
      ExpectedProfile{"sm_90", 90, TargetFlavor::Generic, none,
                      sm90_capabilities},
      ExpectedProfile{"sm_90a", 90, TargetFlavor::ArchitectureSpecific, none,
                      sm90_capabilities},
      ExpectedProfile{"sm_100", 100, TargetFlavor::Generic, none,
                      sm90_capabilities},
      ExpectedProfile{"sm_100a", 100, TargetFlavor::ArchitectureSpecific,
                      sm100f_features, sm90_capabilities},
      ExpectedProfile{"sm_100f", 100, TargetFlavor::FamilySpecific,
                      sm100f_features, sm90_capabilities},
      ExpectedProfile{"sm_103", 103, TargetFlavor::Generic, none,
                      sm90_capabilities},
      ExpectedProfile{"sm_103a", 103, TargetFlavor::ArchitectureSpecific,
                      sm103f_features, sm90_capabilities},
      ExpectedProfile{"sm_103f", 103, TargetFlavor::FamilySpecific,
                      sm103f_features, sm90_capabilities},
      ExpectedProfile{"sm_110", 110, TargetFlavor::Generic, none,
                      sm90_capabilities},
      ExpectedProfile{"sm_110a", 110, TargetFlavor::ArchitectureSpecific,
                      sm110f_features, sm90_capabilities},
      ExpectedProfile{"sm_110f", 110, TargetFlavor::FamilySpecific,
                      sm110f_features, sm90_capabilities},
      ExpectedProfile{"sm_120", 120, TargetFlavor::Generic, none,
                      sm90_capabilities},
      ExpectedProfile{"sm_120a", 120, TargetFlavor::ArchitectureSpecific,
                      sm120f_features, sm90_capabilities},
      ExpectedProfile{"sm_120f", 120, TargetFlavor::FamilySpecific,
                      sm120f_features, sm90_capabilities},
      ExpectedProfile{"sm_121", 121, TargetFlavor::Generic, none,
                      sm90_capabilities},
      ExpectedProfile{"sm_121a", 121, TargetFlavor::ArchitectureSpecific,
                      sm121f_features, sm90_capabilities},
      ExpectedProfile{"sm_121f", 121, TargetFlavor::FamilySpecific,
                      sm121f_features, sm90_capabilities},
  };

  for (const auto& expected : profiles) {
    SCOPED_TRACE(expected.spelling);
    const auto profile = find_target_profile(expected.spelling);
    ASSERT_TRUE(profile.has_value()) << expected.spelling;
    EXPECT_EQ(profile->identity.architecture.number, expected.architecture);
    EXPECT_EQ(profile->identity.flavor, expected.flavor);
    EXPECT_EQ(profile->identity.source_spelling, expected.spelling);
    EXPECT_TRUE(std::ranges::equal(profile->enabled_family_features,
                                   expected.enabled_family_features));
    EXPECT_TRUE(
        std::ranges::equal(profile->capabilities, expected.capabilities));

    const std::string alias =
        "compute_" + std::string{expected.spelling.substr(3)};
    const auto alias_profile = find_target_profile(alias);
    ASSERT_TRUE(alias_profile) << alias;
    EXPECT_EQ(alias_profile->identity.architecture,
              profile->identity.architecture);
    EXPECT_EQ(alias_profile->identity.flavor, profile->identity.flavor);
    EXPECT_EQ(alias_profile->identity.source_spelling, alias);
    EXPECT_NE(alias_profile->identity, profile->identity);
    EXPECT_TRUE(std::ranges::equal(alias_profile->enabled_family_features,
                                   profile->enabled_family_features));
    EXPECT_TRUE(
        std::ranges::equal(alias_profile->capabilities, profile->capabilities));
  }

  EXPECT_TRUE(find_target_profile("sm_30")->capabilities.empty());
  for (const std::string_view target : {"sm_10a", "sm_11f", "sm_12a", "sm_14",
                                        "sm_31", "sm_34", "sm_59", "sm_63"}) {
    EXPECT_TRUE(parse_target_identity(target).has_value()) << target;
    EXPECT_FALSE(find_target_profile(target).has_value()) << target;
  }
  for (const std::string_view target : {"sm_50a", "sm_52a", "sm_52f", "sm_53a"})
    EXPECT_FALSE(find_target_profile(target).has_value()) << target;
  EXPECT_FALSE(find_target_profile("sm_30a").has_value());
  EXPECT_FALSE(find_target_profile("sm_30f").has_value());
  EXPECT_FALSE(find_target_profile("sm_90f").has_value());
  for (const std::string_view target : {"sm_101a", "sm_101f"}) {
    EXPECT_TRUE(parse_target_identity(target).has_value()) << target;
    EXPECT_FALSE(find_target_profile(target).has_value()) << target;
  }
  EXPECT_FALSE(find_target_profile("sm_123a").has_value());
  for (const std::string_view target :
       {"compute_14", "compute_31", "compute_50a", "compute_90f",
        "compute_101a", "compute_101f", "compute_123a"}) {
    const auto identity = parse_target_identity(target);
    ASSERT_TRUE(identity) << target;
    EXPECT_EQ(identity->source_spelling, target);
    EXPECT_FALSE(find_target_profile(target)) << target;
  }
}

TEST(TargetProfile, KeepsEnabledFamilyFeaturesExplicit) {
  const auto sm90 = find_target_profile("sm_90");
  const auto sm90a = find_target_profile("sm_90a");
  const auto sm100 = find_target_profile("sm_100");
  const auto sm100a = find_target_profile("sm_100a");
  const auto sm100f = find_target_profile("sm_100f");
  const auto sm103 = find_target_profile("sm_103");
  const auto sm103a = find_target_profile("sm_103a");
  const auto sm103f = find_target_profile("sm_103f");
  const auto sm110 = find_target_profile("sm_110");
  const auto sm110a = find_target_profile("sm_110a");
  const auto sm110f = find_target_profile("sm_110f");
  const auto sm120 = find_target_profile("sm_120");
  const auto sm120a = find_target_profile("sm_120a");
  const auto sm120f = find_target_profile("sm_120f");
  const auto sm121 = find_target_profile("sm_121");
  const auto sm121a = find_target_profile("sm_121a");
  const auto sm121f = find_target_profile("sm_121f");
  ASSERT_TRUE(sm90.has_value());
  ASSERT_TRUE(sm90a.has_value());
  ASSERT_TRUE(sm100.has_value());
  ASSERT_TRUE(sm100a.has_value());
  ASSERT_TRUE(sm100f.has_value());
  ASSERT_TRUE(sm103.has_value());
  ASSERT_TRUE(sm103a.has_value());
  ASSERT_TRUE(sm103f.has_value());
  ASSERT_TRUE(sm110.has_value());
  ASSERT_TRUE(sm110a.has_value());
  ASSERT_TRUE(sm110f.has_value());
  ASSERT_TRUE(sm120.has_value());
  ASSERT_TRUE(sm120a.has_value());
  ASSERT_TRUE(sm120f.has_value());
  ASSERT_TRUE(sm121.has_value());
  ASSERT_TRUE(sm121a.has_value());
  ASSERT_TRUE(sm121f.has_value());

  EXPECT_TRUE(sm90->enabled_family_features.empty());
  EXPECT_TRUE(sm90a->enabled_family_features.empty());
  EXPECT_TRUE(sm100->enabled_family_features.empty());
  EXPECT_TRUE(std::ranges::equal(sm100a->enabled_family_features,
                                 std::array<std::string_view, 1>{"sm_100f"}));
  EXPECT_TRUE(std::ranges::equal(sm100f->enabled_family_features,
                                 std::array<std::string_view, 1>{"sm_100f"}));
  EXPECT_TRUE(sm103->enabled_family_features.empty());
  EXPECT_TRUE(std::ranges::equal(
      sm103a->enabled_family_features,
      std::array<std::string_view, 2>{"sm_100f", "sm_103f"}));
  EXPECT_TRUE(std::ranges::equal(
      sm103f->enabled_family_features,
      std::array<std::string_view, 2>{"sm_100f", "sm_103f"}));
  EXPECT_TRUE(sm110->enabled_family_features.empty());
  EXPECT_TRUE(std::ranges::equal(sm110a->enabled_family_features,
                                 std::array<std::string_view, 1>{"sm_110f"}));
  EXPECT_TRUE(std::ranges::equal(sm110f->enabled_family_features,
                                 std::array<std::string_view, 1>{"sm_110f"}));
  EXPECT_TRUE(sm120->enabled_family_features.empty());
  EXPECT_TRUE(std::ranges::equal(sm120a->enabled_family_features,
                                 std::array<std::string_view, 1>{"sm_120f"}));
  EXPECT_TRUE(std::ranges::equal(sm120f->enabled_family_features,
                                 std::array<std::string_view, 1>{"sm_120f"}));
  EXPECT_TRUE(sm121->enabled_family_features.empty());
  EXPECT_TRUE(std::ranges::equal(
      sm121a->enabled_family_features,
      std::array<std::string_view, 2>{"sm_120f", "sm_121f"}));
  EXPECT_TRUE(std::ranges::equal(
      sm121f->enabled_family_features,
      std::array<std::string_view, 2>{"sm_120f", "sm_121f"}));
}

TEST(TargetProfile, ExposesOnlyExplicitCapabilityBoundaries) {
  const auto sm80 = find_target_profile("sm_80");
  const auto sm89 = find_target_profile("sm_89");
  const auto sm90 = find_target_profile("sm_90");
  ASSERT_TRUE(sm80.has_value());
  ASSERT_TRUE(sm89.has_value());
  ASSERT_TRUE(sm90.has_value());

  EXPECT_TRUE(target_has_capability(*sm80, "reserved_smem"));
  EXPECT_TRUE(target_has_capability(*sm80, "graph_exec"));
  EXPECT_FALSE(target_has_capability(*sm80, "cluster"));
  EXPECT_FALSE(target_has_capability(*sm80, "aggregate_smem"));
  EXPECT_TRUE(target_has_capability(*sm89, "reserved_smem"));
  EXPECT_TRUE(target_has_capability(*sm89, "graph_exec"));
  EXPECT_FALSE(target_has_capability(*sm89, "cluster"));
  EXPECT_FALSE(target_has_capability(*sm89, "aggregate_smem"));
  EXPECT_TRUE(target_has_capability(*sm90, "cluster"));
  EXPECT_TRUE(target_has_capability(*sm90, "aggregate_smem"));
  EXPECT_TRUE(target_has_capability(*sm90, "reserved_smem"));
  EXPECT_TRUE(target_has_capability(*sm90, "graph_exec"));
}

}  // namespace
}  // namespace ptx_frontend::base
