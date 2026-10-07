#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ptx_frontend::base {

/**
 * Decode a non-negative PTX integer spelling into its 64-bit magnitude.
 * Supports decimal, leading-zero octal, and 0x/0X hexadecimal with an optional
 * u/U suffix. Signs and use-site conversion/range policies belong to callers.
 * Invalid digits, empty input, trailing characters, and overflow return nullopt.
 */
inline std::optional<uint64_t> parseIntegerMagnitude(std::string_view text) {
  if (!text.empty() && (text.back() == 'u' || text.back() == 'U'))
    text.remove_suffix(1);
  if (text.empty())
    return std::nullopt;

  const int radix = text.starts_with("0x") || text.starts_with("0X") ? 16
                    : text.front() == '0'                            ? 8
                                                                     : 10;
  if (radix == 16)
    text.remove_prefix(2);
  if (text.empty())
    return std::nullopt;

  uint64_t magnitude = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), magnitude, radix);
  if (error != std::errc{} || end != text.data() + text.size())
    return std::nullopt;
  return magnitude;
}

}  // namespace ptx_frontend::base
