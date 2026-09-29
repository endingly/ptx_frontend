#include "test_module_projection_detail.hpp"

namespace ptx_frontend::resolved_ir::test_support {

template std::expected<TypedModuleSnapshot<Atom, Cp, Ldmatrix, Mma, Vote>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Atom, Cp, Ldmatrix, Mma, Vote>(const syntax_ast::AstModule&,
                                                  ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
