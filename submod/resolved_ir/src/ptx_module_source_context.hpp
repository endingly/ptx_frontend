#pragma once

#include <charconv>
#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::resolved_ir::detail {

/** Own typed top-level traversal events without deriving order from coordinates. */
inline std::vector<ResolvedHeaderEvent> module_header_order(
    const syntax_ast::AstModule& module) {
  std::vector<ResolvedHeaderEvent> events;
  events.reserve(module.items.size());
  for (const auto& item : module.items) {
    const auto kind =
        std::holds_alternative<syntax_ast::AstVersionDirective>(item)
            ? ResolvedHeaderEventKind::Version
        : std::holds_alternative<syntax_ast::AstTargetDirective>(item)
            ? ResolvedHeaderEventKind::Target
        : std::holds_alternative<syntax_ast::AstAddressSizeDirective>(item)
            ? ResolvedHeaderEventKind::AddressSize
            : ResolvedHeaderEventKind::Other;
    events.push_back(
        {.kind = kind,
         .range =
             std::visit([](const auto& value) { return value.range; }, item)});
  }
  return events;
}

/** Parse the module-wide version used by resolution and validation. */
inline std::optional<checker::PtxVersion> module_version(
    const syntax_ast::AstModule& module) {
  for (const auto& item : module.items) {
    const auto* version = std::get_if<syntax_ast::AstVersionDirective>(&item);
    if (!version)
      continue;
    const std::string_view text = version->version.text;
    const size_t dot = text.find('.');
    if (dot == std::string_view::npos)
      return std::nullopt;
    checker::PtxVersion result;
    /** Require a complete, representable decimal version component. */
    const auto parse = [](std::string_view value, uint16_t& output) {
      const auto [end, error] =
          std::from_chars(value.data(), value.data() + value.size(), output);
      return !value.empty() && error == std::errc{} &&
             end == value.data() + value.size();
    };
    if (!parse(text.substr(0, dot), result.major) ||
        !parse(text.substr(dot + 1), result.minor))
      return std::nullopt;
    return result;
  }
  return std::nullopt;
}

}  // namespace ptx_frontend::resolved_ir::detail
