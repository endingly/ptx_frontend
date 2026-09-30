#include "test_module_projection_detail.hpp"

#include <ptx_frontend/resolved_ir/model/arithmetic/abs/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/add/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/div/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/neg/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/rcp/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/rsqrt/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/slct/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sqrt/model.gen.hpp>

namespace ptx_frontend::resolved_ir::test_support {

template std::expected<TypedModuleSnapshot<Slct>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Slct>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Div>, std::vector<ResolveDiagnostic>>
resolveTypedModule<Div>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Abs, Neg>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Abs, Neg>(const syntax_ast::AstModule&, ModulePipeline);
template std::expected<TypedModuleSnapshot<Rcp, Sqrt, Rsqrt>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Rcp, Sqrt, Rsqrt>(const syntax_ast::AstModule&,
                                     ModulePipeline);
template std::expected<TypedModuleSnapshot<Mov, Add>,
                       std::vector<ResolveDiagnostic>>
resolveTypedModule<Mov, Add>(const syntax_ast::AstModule&, ModulePipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
