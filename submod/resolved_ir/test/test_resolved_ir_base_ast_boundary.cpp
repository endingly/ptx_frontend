#include <ptx_frontend/resolved_ir/ptx_instruction_base.hpp>

#include <concepts>

/** Detect whether a header alone exposes a complete syntax AST record. */
template <typename T>
concept CompleteAstType = requires { sizeof(T); };

static_assert(!CompleteAstType<ptx_frontend::syntax_ast::AstInstruction>);
static_assert(!CompleteAstType<ptx_frontend::syntax_ast::AstModule>);
