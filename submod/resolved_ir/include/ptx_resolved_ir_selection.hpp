#pragma once

#include <expected>
#include <string_view>

#include <fmt/core.h>
#include <magic_enum/magic_enum.hpp>

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
  const auto variant =
      magic_enum::enum_cast<typename T::VariantType>(*variant_name);
  if (!variant) {
    throw ResolveException(fmt::format(
        "Descriptor variant '{}.{}' has no matching VariantType enumerator.",
        utils::type_name<T>(), *variant_name));
  }
  return *variant;
}

}  // namespace detail

using detail::selectVariant;

}  // namespace ptx_frontend::resolved_ir
