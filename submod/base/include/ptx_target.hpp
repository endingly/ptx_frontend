#pragma once

#include <cstdint>
#include <optional>
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

/** Parse one PTX target spelling without consulting the supported-target catalog. */
[[nodiscard]] std::optional<TargetIdentity> parse_target_identity(
    std::string_view spelling);

}  // namespace ptx_frontend::base
