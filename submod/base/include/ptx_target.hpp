#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ptx_frontend::base {

struct TargetArchitecture {
  uint32_t number = 0;
  bool operator==(const TargetArchitecture&) const = default;
};

enum class TargetFlavor : uint8_t {
  Generic,
  ArchitectureSpecific,
  FamilySpecific,
};

struct TargetIdentity {
  TargetArchitecture architecture;
  TargetFlavor flavor = TargetFlavor::Generic;
  std::string source_spelling;
  bool operator==(const TargetIdentity&) const = default;
};

/** Compare architecture and flavor while ignoring retained source spelling. */
[[nodiscard]] bool same_target_identity(const TargetIdentity& left,
                                        const TargetIdentity& right) noexcept;

/** One explicitly supported validation target, enabled family feature levels,
 * and capabilities. */
struct TargetProfile {
  TargetIdentity identity;
  std::span<const std::string_view> enabled_family_features;
  std::span<const std::string_view> capabilities;
};

/** Lexical role of one item in a source `.target` directive. */
enum class TargetDirectiveItemKind : uint8_t {
  Invalid,
  Architecture,
  TextureMode,
  Debug,
  MapF64ToF32,
};

/** Classify source syntax without implying a cataloged validation profile. */
[[nodiscard]] TargetDirectiveItemKind classify_target_directive_item(
    std::string_view spelling);

/** Parse an sm_ or compute_ target, retaining spelling without catalog lookup. */
[[nodiscard]] std::optional<TargetIdentity> parse_target_identity(
    std::string_view spelling);

/** Look up one explicitly cataloged validation target; lexical validity is insufficient. */
[[nodiscard]] std::optional<TargetProfile> find_target_profile(
    std::string_view spelling);

[[nodiscard]] bool target_has_capability(const TargetProfile& profile,
                                         std::string_view capability) noexcept;

}  // namespace ptx_frontend::base
