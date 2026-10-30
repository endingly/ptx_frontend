#pragma once

#include <algorithm>
#include <string_view>

namespace ptx_frontend::base {

/**
 * Recognize the ASCII identifier grammar of PTX ISA 9.3 section 4.4.
 * Letter-leading names may contain one character; _, $, and % prefixes require
 * a following identifier character. Contextual sinks are handled by callers.
 */
[[nodiscard]] constexpr bool isIdentifier(std::string_view text) noexcept {
  const auto letter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  };
  if (text.empty() ||
      (!letter(text.front()) && text.front() != '_' && text.front() != '$' &&
       text.front() != '%') ||
      (text.size() == 1 && !letter(text.front())))
    return false;
  return std::ranges::all_of(text.substr(1), [&](char c) {
    return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '$';
  });
}

}  // namespace ptx_frontend::base
