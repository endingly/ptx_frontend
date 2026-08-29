#include <ptx_frontend/base/ptx_target.hpp>

#include <array>
#include <charconv>
#include <cstddef>

namespace ptx_frontend::base {
namespace {

constexpr std::array<std::string_view, 0> kNoCapabilities{};
constexpr std::array<std::string_view, 0> kNoEnabledFamilyFeatures{};
constexpr std::array<std::string_view, 1> kSm100fEnabledFamilyFeatures{
    "sm_100f"};
constexpr std::array<std::string_view, 2> kSm103fEnabledFamilyFeatures{
    "sm_100f", "sm_103f"};
constexpr std::array<std::string_view, 1> kSm120fEnabledFamilyFeatures{
    "sm_120f"};
constexpr std::array<std::string_view, 2> kSm80Capabilities{
    "reserved_smem",
    "graph_exec",
};
constexpr std::array<std::string_view, 4> kSm90AndLaterCapabilities{
    "cluster",
    "aggregate_smem",
    "reserved_smem",
    "graph_exec",
};

struct CatalogEntry {
  std::string_view spelling;
  std::span<const std::string_view> enabled_family_features;
  std::span<const std::string_view> capabilities;
};

// Keep this an explicit validation allowlist: do not infer profiles by number
// or flavor suffix from lexically valid target spellings.
constexpr CatalogEntry kTargetProfiles[]{
    {"sm_30", kNoEnabledFamilyFeatures, kNoCapabilities},
    {"sm_80", kNoEnabledFamilyFeatures, kSm80Capabilities},
    {"sm_90", kNoEnabledFamilyFeatures, kSm90AndLaterCapabilities},
    {"sm_90a", kNoEnabledFamilyFeatures, kSm90AndLaterCapabilities},
    {"sm_100", kNoEnabledFamilyFeatures, kSm90AndLaterCapabilities},
    {"sm_100a", kSm100fEnabledFamilyFeatures, kSm90AndLaterCapabilities},
    {"sm_100f", kSm100fEnabledFamilyFeatures, kSm90AndLaterCapabilities},
    {"sm_103", kNoEnabledFamilyFeatures, kSm90AndLaterCapabilities},
    {"sm_103a", kSm103fEnabledFamilyFeatures, kSm90AndLaterCapabilities},
    {"sm_103f", kSm103fEnabledFamilyFeatures, kSm90AndLaterCapabilities},
    {"sm_120f", kSm120fEnabledFamilyFeatures, kSm90AndLaterCapabilities},
};

}  // namespace

std::optional<TargetIdentity> parse_target_identity(std::string_view spelling) {
  constexpr std::string_view kPrefix = "sm_";
  if (!spelling.starts_with(kPrefix))
    return std::nullopt;

  const std::string_view number_text = spelling.substr(kPrefix.size());
  if (number_text.empty() || number_text.front() == '0')
    return std::nullopt;

  uint32_t number = 0;
  const auto [end, error] = std::from_chars(
      number_text.data(), number_text.data() + number_text.size(), number);
  if (error != std::errc{} || end == number_text.data())
    return std::nullopt;

  const std::string_view suffix{
      end,
      static_cast<std::size_t>(number_text.data() + number_text.size() - end)};
  TargetFlavor flavor = TargetFlavor::Generic;
  if (suffix == "a")
    flavor = TargetFlavor::ArchitectureSpecific;
  else if (suffix == "f")
    flavor = TargetFlavor::FamilySpecific;
  else if (!suffix.empty())
    return std::nullopt;

  return TargetIdentity{
      .architecture = {.number = number},
      .flavor = flavor,
      .source_spelling = std::string{spelling},
  };
}

std::optional<TargetProfile> find_target_profile(std::string_view spelling) {
  const auto identity = parse_target_identity(spelling);
  if (!identity)
    return std::nullopt;

  for (const CatalogEntry& entry : kTargetProfiles) {
    if (entry.spelling == spelling) {
      return TargetProfile{
          .identity = *identity,
          .enabled_family_features = entry.enabled_family_features,
          .capabilities = entry.capabilities,
      };
    }
  }
  return std::nullopt;
}

bool target_has_capability(const TargetProfile& profile,
                           std::string_view capability) noexcept {
  for (const std::string_view candidate : profile.capabilities) {
    if (candidate == capability)
      return true;
  }
  return false;
}

}  // namespace ptx_frontend::base
