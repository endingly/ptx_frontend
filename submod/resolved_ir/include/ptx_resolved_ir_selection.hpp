#pragma once

#include <expected>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_diagnostics.hpp>

namespace ptx_frontend::syntax_ast {
struct AstInstruction;
}  // namespace ptx_frontend::syntax_ast

namespace ptx_frontend::resolved_ir {

/** Match syntax modifiers to one generated variant name. */
std::expected<std::string_view, ResolveDiagnostic> select_variant_name(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& instruction);

}  // namespace ptx_frontend::resolved_ir
