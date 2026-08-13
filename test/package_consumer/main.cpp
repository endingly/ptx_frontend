#include <ptx_ir/resolved/ptx_resolved_ir.hpp>
#include <ptx_ir/syntax/ptx_syntax_parser.hpp>

int main() {
  ptx_frontend::PtxSyntaxParser parser("add.u32 %r0, %r1, 1;");
  const auto ast = parser.parseInstruction();
  if (!ast)
    return 1;

  const auto resolved =
      ptx_frontend::resolved_ir::resolve<ptx_frontend::resolved_ir::Add>(*ast);
  return resolved ? 0 : 2;
}
