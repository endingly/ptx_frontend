#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>

#include <concepts>

/** Detect whether the module model header alone completes parser AST types. */
template <typename T>
concept CompleteAstType = requires { sizeof(T); };

static_assert(!CompleteAstType<ptx_frontend::syntax_ast::AstInstruction>);
static_assert(!CompleteAstType<ptx_frontend::syntax_ast::AstModule>);
