#include "test_module_projection_detail.hpp"

namespace ptx_frontend::resolved_ir::test_support {

template std::expected<TypedModuleSnapshot<Mov>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Mov>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Ld>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Ld>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<St>, std::vector<ResolveDiagnostic>>
resolveTypedModule<St>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Bar>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Bar>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Barrier>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Barrier>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Brx>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Brx>(const syntax_ast::AstModule&, ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
