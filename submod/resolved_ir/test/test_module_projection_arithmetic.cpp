#include "test_module_projection_detail.hpp"

namespace ptx_frontend::resolved_ir::test_support {

template std::expected<TypedModuleSnapshot<Mul>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Mul>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Add>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Add>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Set>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Set>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Setp>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Setp>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Selp>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Selp>(const syntax_ast::AstModule&, ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
