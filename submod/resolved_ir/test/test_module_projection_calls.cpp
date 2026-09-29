#include "test_module_projection_detail.hpp"

namespace ptx_frontend::resolved_ir::test_support {

template std::expected<TypedModuleSnapshot<Call, Ld, St>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Call, Ld, St>(const syntax_ast::AstModule&, ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
