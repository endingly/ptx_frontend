#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>

#include <concepts>

/** A generated opcode leaf must not complete parser-owned AST records. */
template <typename T>
concept CompleteAstType = requires { sizeof(T); };

static_assert(!CompleteAstType<ptx_frontend::syntax_ast::AstInstruction>);
static_assert(!CompleteAstType<ptx_frontend::syntax_ast::AstModule>);
