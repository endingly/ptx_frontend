#include <gtest/gtest.h>

#include <array>
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
}

TEST(TargetProfile, CatalogsOnlyExplicitM11ValidationTargets) {
  constexpr std::array<std::string_view, 6> supported{
      "sm_80", "sm_90", "sm_90a", "sm_100", "sm_100a", "sm_100f",
  };
  for (const std::string_view spelling : supported) {
    const auto profile = find_target_profile(spelling);
    ASSERT_TRUE(profile.has_value()) << spelling;
    EXPECT_EQ(profile->identity.source_spelling, spelling);
  }

  const auto sm100 = find_target_profile("sm_100");
  const auto sm100a = find_target_profile("sm_100a");
  const auto sm100f = find_target_profile("sm_100f");
  ASSERT_TRUE(sm100.has_value());
  ASSERT_TRUE(sm100a.has_value());
  ASSERT_TRUE(sm100f.has_value());
  EXPECT_NE(sm100->identity, sm100a->identity);
  EXPECT_NE(sm100a->identity, sm100f->identity);

  EXPECT_FALSE(find_target_profile("sm_90f").has_value());
  EXPECT_FALSE(find_target_profile("sm_123a").has_value());
}

TEST(TargetProfile, ExposesOnlyExplicitCapabilityBoundaries) {
  const auto sm80 = find_target_profile("sm_80");
  const auto sm90 = find_target_profile("sm_90");
  ASSERT_TRUE(sm80.has_value());
  ASSERT_TRUE(sm90.has_value());

  EXPECT_TRUE(target_has_capability(*sm80, "reserved_smem"));
  EXPECT_TRUE(target_has_capability(*sm80, "graph_exec"));
  EXPECT_FALSE(target_has_capability(*sm80, "cluster"));
  EXPECT_FALSE(target_has_capability(*sm80, "aggregate_smem"));
  EXPECT_TRUE(target_has_capability(*sm90, "cluster"));
  EXPECT_TRUE(target_has_capability(*sm90, "aggregate_smem"));
  EXPECT_TRUE(target_has_capability(*sm90, "reserved_smem"));
  EXPECT_TRUE(target_has_capability(*sm90, "graph_exec"));
}

}  // namespace
}  // namespace ptx_frontend::base
