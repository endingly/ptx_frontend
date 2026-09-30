#include "test_module_projection_detail.hpp"

#include <ptx_frontend/resolved_ir/model/data_movement/cp/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/matrix/ldmatrix/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/matrix/mma/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/atom/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/vote/model.gen.hpp>

namespace ptx_frontend::resolved_ir::test_support {

template std::expected<TypedModuleSnapshot<Atom, Cp, Ldmatrix, Mma, Vote>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Atom, Cp, Ldmatrix, Mma, Vote>(const syntax_ast::AstModule&,
                                                  ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
