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

}  // namespace
}  // namespace ptx_frontend::base
