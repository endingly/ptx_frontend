#pragma once

#include <cstddef>
#include <expected>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

namespace ptx_frontend::resolved_ir::detail {

/** Selects one syntax layout and retains its generated ordinal for resolution. */
struct SelectedOperandLayout {
  /** Borrowed generated layout descriptor; it outlives one resolution call. */
  const check_end::SyntaxOperandLayoutDescriptor& descriptor;
  /** Ordinal shared with the corresponding resolved operand-binding layout. */
  std::size_t index;
};

/** Select the unique most-specific operand layout accepted by an instruction. */
std::expected<SelectedOperandLayout, ResolveDiagnostic> select_operand_layout(
    const check_end::SyntaxVariantDescriptor& variant,
    const syntax_ast::AstInstruction& ast);

/** Locate a syntax variant by its generated semantic name. */
const check_end::SyntaxVariantDescriptor& find_syntax_variant_descriptor(
    const check_end::SyntaxInstructionDescriptor& instruction,
    std::string_view name);

}  // namespace ptx_frontend::resolved_ir::detail
