#include "test_module_projection_detail.hpp"

namespace ptx_frontend::resolved_ir::test_support {

template std::expected<
    TypedModuleSnapshot<Barrier, Clusterlaunchcontrol, Fence, Mbarrier>,
    std::vector<ResolveDiagnostic>>
resolveTypedModule<Barrier, Clusterlaunchcontrol, Fence, Mbarrier>(
    const syntax_ast::AstModule&, ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
