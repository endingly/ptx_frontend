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

/** One explicitly supported validation target and its M11 capabilities. */
struct TargetProfile {
  TargetIdentity identity;
  std::span<const std::string_view> families;
  std::span<const std::string_view> capabilities;
};

/** Parse one PTX target spelling without consulting the supported-target catalog. */
[[nodiscard]] std::optional<TargetIdentity> parse_target_identity(
    std::string_view spelling);

/** Look up one explicitly cataloged validation target; lexical validity is insufficient. */
[[nodiscard]] std::optional<TargetProfile> find_target_profile(
    std::string_view spelling);

[[nodiscard]] bool target_has_capability(const TargetProfile& profile,
                                         std::string_view capability) noexcept;

}  // namespace ptx_frontend::base
