#pragma once
#include <array>
#include <ranges>
#include <type_traits>

namespace ptx_frontend::utils {

template <typename T, typename... Ts>
inline constexpr bool is_one_of_v = (std::is_same_v<T, Ts> || ...);

template <std::ranges::input_range Range, typename T>
  requires std::equality_comparable_with<
      std::remove_cvref_t<std::ranges::range_reference_t<Range>>, T>
constexpr bool contains(const Range& r, const T& value) noexcept(noexcept(
    std::declval<
        std::remove_cvref_t<std::ranges::range_reference_t<Range>>>() ==
    std::declval<T>())) {
  for (auto&& e : r) {
    if (e == value) {
      return true;
    }
  }
  return false;
}

};  // namespace ptx_frontend::utils