#include <ptx_frontend/base/ptx_target.hpp>

#include <charconv>
#include <cstddef>

namespace ptx_frontend::base {

std::optional<TargetIdentity> parse_target_identity(std::string_view spelling) {
  constexpr std::string_view kPrefix = "sm_";
  if (!spelling.starts_with(kPrefix))
    return std::nullopt;

  const std::string_view number_text = spelling.substr(kPrefix.size());
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

}  // namespace ptx_frontend::base
