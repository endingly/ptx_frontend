#pragma once

#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

namespace ptx_frontend::resolved_ir::detail {

/** Identify a variant alternative without requiring its definition. */
template <typename Alternative, typename Variant>
struct VariantContainsAlternative;

/** Evaluate membership for the closed alternative list of one std::variant. */
template <typename Alternative, typename... Members>
struct VariantContainsAlternative<Alternative, std::variant<Members...>>
    : std::bool_constant<(std::same_as<Alternative, Members> || ...)> {};

/** Type whose unqualified form is a std::variant. */
template <typename Variant>
concept VariantValue =
    requires { std::variant_size<std::remove_cvref_t<Variant>>::value; };

/** Visit a present alternative, retaining std::visit's valueless exception. */
template <typename Alternative, VariantValue Variant, typename Callback>
void with_variant_alternative_if_present(Variant& value, Callback&& callback) {
  if (value.valueless_by_exception())
    throw std::bad_variant_access{};
  if constexpr (VariantContainsAlternative<
                    Alternative, std::remove_cvref_t<Variant>>::value) {
    if (auto* selected = std::get_if<Alternative>(&value))
      std::forward<Callback>(callback)(*selected);
  }
}

}  // namespace ptx_frontend::resolved_ir::detail
