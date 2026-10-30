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
/** Source family feature levels enabled by supported sm_121 f/a targets. */
constexpr std::array<std::string_view, 2> kSm121fEnabledFamilyFeatures{
    "sm_120f", "sm_121f"};
constexpr std::array<std::string_view, 1> kSm110fEnabledFamilyFeatures{
    "sm_110f"};
/** Process-lifetime storage for graph-only catalog capability spans. */
constexpr std::array<std::string_view, 1> kGraphExecCapabilities{"graph_exec"};
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

/** One explicit semantic target and its process-lifetime feature spans. */
struct CatalogEntry {
  /** Numeric architecture and flavor are the catalog key, independent of prefix. */
  TargetArchitecture architecture;
  TargetFlavor flavor;
  std::span<const std::string_view> enabled_family_features;
  std::span<const std::string_view> capabilities;
  /** First ISA version of this exact catalog key; never inferred by number. */
  PtxIsaVersion minimum_ptx_version;
  /** Explicit legacy option support; absent initialization means unsupported. */
  bool supports_map_f64_to_f32 = false;
};

// Keep this an explicit validation allowlist: do not infer profiles by number
// or flavor suffix from lexically valid target spellings.
constexpr CatalogEntry kTargetProfiles[]{
    {{10},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {1, 0},
     true},
    {{11},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {1, 0},
     true},
    {{12},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {1, 2},
     true},
    {{13},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {1, 2}},
    {{20},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {2, 0}},
    {{30},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {3, 0}},
    {{32},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {4, 0}},
    {{35},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {3, 1}},
    {{37},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kNoCapabilities,
     {4, 1}},
    {{50},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {4, 0}},
    {{52},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {4, 1}},
    {{53},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {4, 2}},
    {{60},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {5, 0}},
    {{61},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {5, 0}},
    {{62},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {5, 0}},
    {{70},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {6, 0}},
    {{72},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {6, 1}},
    {{75},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kGraphExecCapabilities,
     {6, 3}},
    {{80},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm80Capabilities,
     {7, 0}},
    {{86},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm80Capabilities,
     {7, 1}},
    {{87},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm80Capabilities,
     {7, 4}},
    {{88},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm80Capabilities,
     {9, 0}},
    {{89},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm80Capabilities,
     {7, 8}},
    {{90},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {7, 8}},
    {{90},
     TargetFlavor::ArchitectureSpecific,
     kNoEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 0}},
    {{100},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 6}},
    {{100},
     TargetFlavor::ArchitectureSpecific,
     kSm100fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 6}},
    {{100},
     TargetFlavor::FamilySpecific,
     kSm100fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 8}},
    {{103},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 8}},
    {{103},
     TargetFlavor::ArchitectureSpecific,
     kSm103fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 8}},
    {{103},
     TargetFlavor::FamilySpecific,
     kSm103fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 8}},
    {{110},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {9, 0}},
    {{110},
     TargetFlavor::ArchitectureSpecific,
     kSm110fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {9, 0}},
    {{110},
     TargetFlavor::FamilySpecific,
     kSm110fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {9, 0}},
    {{120},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 7}},
    {{120},
     TargetFlavor::ArchitectureSpecific,
     kSm120fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 7}},
    {{120},
     TargetFlavor::FamilySpecific,
     kSm120fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 8}},
    {{121},
     TargetFlavor::Generic,
     kNoEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 8}},
    {{121},
     TargetFlavor::ArchitectureSpecific,
     kSm121fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 8}},
    {{121},
     TargetFlavor::FamilySpecific,
     kSm121fEnabledFamilyFeatures,
     kSm90AndLaterCapabilities,
     {8, 8}},
};

}  // namespace

std::optional<TargetIdentity> parse_target_identity(std::string_view spelling) {
  const std::size_t prefix_size = spelling.starts_with("sm_")        ? 3
                                  : spelling.starts_with("compute_") ? 8
                                                                     : 0;
  if (prefix_size == 0)
    return std::nullopt;

  const std::string_view number_text = spelling.substr(prefix_size);
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

bool same_target_identity(const TargetIdentity& left,
                          const TargetIdentity& right) noexcept {
  return left.architecture == right.architecture && left.flavor == right.flavor;
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
          .enabled_family_features = entry.enabled_family_features,
          .capabilities = entry.capabilities,
          .minimum_ptx_version = entry.minimum_ptx_version,
          .supports_map_f64_to_f32 = entry.supports_map_f64_to_f32,
      };
    }
  }
  return std::nullopt;
}

TargetDirectiveItemKind classify_target_directive_item(
    std::string_view spelling) {
  if (spelling == "texmode_unified" || spelling == "texmode_independent")
    return TargetDirectiveItemKind::TextureMode;
  if (spelling == "debug")
    return TargetDirectiveItemKind::Debug;
  if (spelling == "map_f64_to_f32")
    return TargetDirectiveItemKind::MapF64ToF32;
  if (parse_target_identity(spelling))
    return TargetDirectiveItemKind::Architecture;
  return TargetDirectiveItemKind::Invalid;
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
