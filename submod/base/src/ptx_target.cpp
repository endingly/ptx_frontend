#include <ptx_frontend/base/ptx_target.hpp>

#include <array>
#include <charconv>
#include <cstddef>

namespace ptx_frontend::base {
namespace {

constexpr std::array<std::string_view, 0> kNoCapabilities{};
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
  TargetArchitecture architecture;
  TargetFlavor flavor;
  std::span<const std::string_view> capabilities;
};

// Keep this an explicit validation allowlist: do not infer profiles by number
// or flavor suffix from lexically valid target spellings.
constexpr CatalogEntry kTargetProfiles[]{
    {{30}, TargetFlavor::Generic, kNoCapabilities},
    {{80}, TargetFlavor::Generic, kSm80Capabilities},
    {{90}, TargetFlavor::Generic, kSm90AndLaterCapabilities},
    {{90}, TargetFlavor::ArchitectureSpecific, kSm90AndLaterCapabilities},
    {{100}, TargetFlavor::Generic, kSm90AndLaterCapabilities},
    {{100}, TargetFlavor::ArchitectureSpecific, kSm90AndLaterCapabilities},
    {{100}, TargetFlavor::FamilySpecific, kSm90AndLaterCapabilities},
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
    if (entry.architecture == identity->architecture &&
        entry.flavor == identity->flavor) {
      return TargetProfile{
          .identity = *identity,
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
