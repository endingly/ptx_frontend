#include <string_view>
#include <variant>

#include <ptx_ir/cst/ptx_cst_parser.hpp>
#include <ptx_ir/resolved/ptx_resolved_ir.hpp>
#include <ptx_ir/syntax/ptx_syntax_parser.hpp>

int main() {
  constexpr std::string_view source = "add.u32 %r0, %r1, 1;";
  ptx_frontend::PtxCstParser cst_parser(source);
  const auto cst = cst_parser.parseInstruction();
  if (!cst || cst->sourceText() != source)
    return 1;

  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseInstruction();
  if (!ast)
    return 2;

  const auto resolved =
      ptx_frontend::resolved_ir::resolve<ptx_frontend::resolved_ir::Add>(*ast);
  if (!resolved)
    return 3;

  constexpr std::string_view module_source =
      ".version 8.0\n.target sm_80\n.address_size 64\n"
      ".visible .global .u32 counter;\n"
      ".entry kernel(.param .u32 n) { .reg .u32 %r<2>; start: "
      "add.u32 %r0, %r1, 1; }";
  ptx_frontend::PtxSyntaxParser module_parser(module_source);
  const auto module = module_parser.parseModule();
  if (!module || module->items.size() != 5 ||
      !std::holds_alternative<ptx_frontend::syntax_ast::AstVariableDeclaration>(
          module->items[3]))
    return 4;
  const auto& function =
      std::get<ptx_frontend::syntax_ast::AstFunction>(module->items[4]);
  return function.parameters.size() == 1 && function.body.size() == 3 ? 0 : 5;
}
