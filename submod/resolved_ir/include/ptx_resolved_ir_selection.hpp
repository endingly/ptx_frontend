#pragma once

#include <cstddef>
#include <expected>
#include <string_view>

#include <fmt/core.h>

#include <ptx_frontend/common/utils.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>

namespace ptx_frontend::resolved_ir {

/** Match syntax modifiers to one generated variant name. */
std::expected<std::string_view, ResolveDiagnostic> select_variant_name(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& instruction);

namespace detail {

using ::ptx_frontend::resolved_ir::select_variant_name;

/** Select the generated variant named by syntax descriptor matching. */
template <::ptx_frontend::resolved_ir::PtxOperator T>
std::expected<typename T::VariantType, ResolveDiagnostic> selectVariant(
    const syntax_ast::AstInstruction& ast) {
  const auto variant_name =
      select_variant_name(ast, T::get_syntax_descriptor());
  if (!variant_name)
    return std::unexpected(variant_name.error());
  /** Generated descriptor and enum entries share one canonical source order. */
  size_t index = 0;
  for (const auto& variant : T::get_syntax_descriptor().variants) {
    if (variant.variant_name == *variant_name)
      return static_cast<typename T::VariantType>(index);
    ++index;
  }
  throw ResolveException(fmt::format(
      "Descriptor variant '{}.{}' has no matching VariantType enumerator.",
      utils::type_name<T>(), *variant_name));
}

}  // namespace detail

using detail::selectVariant;

}  // namespace ptx_frontend::resolved_ir
