#include "test_module_projection_detail.hpp"

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/barrier/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/clusterlaunchcontrol/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fence/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/mbarrier/model.gen.hpp>

namespace ptx_frontend::resolved_ir::test_support {

template std::expected<
    TypedModuleSnapshot<Barrier, Clusterlaunchcontrol, Fence, Mbarrier>,
    std::vector<ResolveDiagnostic>>
resolveTypedModule<Barrier, Clusterlaunchcontrol, Fence, Mbarrier>(
    const syntax_ast::AstModule&, ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
