#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "ptx_ir/base.hpp"

namespace ptx_frontend::special_registers {

/** Target requirements and declared element type of one predefined sreg. */
struct Info {
  ScalarType element_type = ScalarType::Invalid;
  uint8_t vector_width = 1;
  uint16_t minimum_ptx_major = 0;
  uint16_t minimum_ptx_minor = 0;
  uint32_t minimum_sm = 0;
  bool operator==(const Info&) const = default;
};

/** Return semantic metadata for an exact predefined special-register spelling. */
[[nodiscard]] std::optional<Info> lookup(std::string_view spelling) noexcept;

}  // namespace ptx_frontend::special_registers
