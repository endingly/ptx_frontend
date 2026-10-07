#include <ptx_frontend/resolved_ir/ptx_resolved_ir.gen.hpp>

#include <concepts>

/** The generated all-form model remains independent of parser definitions. */
template <typename T>
concept CompleteAstType = requires { sizeof(T); };

static_assert(!CompleteAstType<ptx_frontend::syntax_ast::AstInstruction>);
static_assert(!CompleteAstType<ptx_frontend::syntax_ast::AstModule>);
