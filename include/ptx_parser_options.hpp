#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>

namespace ptx_frontend {

struct PtxVersion {
  uint16_t major{};
  uint16_t minor{};

  auto operator<=>(const PtxVersion&) const = default;
};

/**
 * Optional target used for instruction availability validation.
 *
 * A missing field is not validated. Leaving the complete target empty keeps
 * the parser in syntax-only mode.
 */
struct ParserTarget {
  std::optional<PtxVersion> ptx;
  std::optional<uint16_t> sm;
  std::optional<std::string> family;
};

struct ParserOptions {
  ParserTarget target;
};

}  // namespace ptx_frontend
