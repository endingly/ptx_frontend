#include <iostream>
#include <optional>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Read installed vector signature data after releasing the source and AST. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  using ptx_frontend::call_argument_compatibility::CallArgumentVectorShape;
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_80
.func (.reg .v2 .u32 r) f(.reg .v4 .u32 p) { ret; }
.func caller(.reg .v4 .u32 p) {
  .reg .v2 .u32 r;
  call (r), f, (p);
  ret;
}
)ptx");
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return 1;
    auto module = ir::resolveAndValidateModule(*ast);
    if (!module)
      return 2;
    owned = std::move(*module);
  }
  const auto& signature = owned->functions.front().contract.signature;
  if (signature.return_parameters[0].vector_shape !=
          CallArgumentVectorShape::V2 ||
      signature.parameters[0].vector_shape != CallArgumentVectorShape::V4 ||
      !owned->functions.front().parameter_declarations.empty() ||
      !ir::validateModule(*owned))
    return 3;
  owned->functions.front().contract.signature.parameters[0].vector_shape =
      CallArgumentVectorShape::Scalar;
  if (ir::validateModule(*owned))
    return 4;
  std::cout << "installed vector formal contracts passed\n";
}
