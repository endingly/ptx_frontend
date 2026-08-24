#include <string_view>
#include <variant>

#include <ptx_ir/bind/ptx_symbol_table.hpp>
#include <ptx_ir/cst/ptx_cst_parser.hpp>
#include <ptx_ir/resolved/ptx_resolved_ir.hpp>
#include <ptx_ir/semantic/ptx_declaration_semantics.hpp>
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
  if (function.parameters.size() != 1 || function.body.size() != 3)
    return 5;

  const auto symbols = ptx_frontend::binding::bindSymbols(*module);
  if (!symbols.diagnostics.empty() || symbols.table.scopes().size() != 2)
    return 6;
  if (!ptx_frontend::declaration_semantics::checkDeclarations(*module,
                                                              symbols.table)
           .empty())
    return 12;
  const auto kernel =
      symbols.table.lookup(symbols.table.moduleScope(), "kernel");
  if (!kernel)
    return 7;
  const auto counter =
      symbols.table.lookup(symbols.table.moduleScope(), "counter");
  if (!counter ||
      symbols.table.symbol(counter->symbol).linkage !=
          ptx_frontend::binding::SymbolLinkage::Visible ||
      !ptx_frontend::binding::isSpecialRegister("%laneid") ||
      ptx_frontend::binding::isSpecialRegister("%envreg32"))
    return 8;
  const auto function_scope = symbols.table.symbol(kernel->symbol).owned_scope;
  if (!function_scope || !symbols.table.lookup(*function_scope, "%r1"))
    return 9;

  const auto resolved_module =
      ptx_frontend::resolved_ir::resolveModule(*module);
  if (!resolved_module || resolved_module->functions.size() != 1 ||
      resolved_module->functions.front().body.size() != 1)
    return 10;
  const auto& resolved_add = std::get<ptx_frontend::resolved_ir::Add>(
      resolved_module->functions.front().body.front());
  const auto& integer_add =
      std::get<ptx_frontend::resolved_ir::Add::IntegerNoSat>(
          resolved_add.variant);
  if (!integer_add.dst.value.symbol_id)
    return 11;

  ptx_frontend::PtxSyntaxParser call_parser(
      "call (%result), callee, (%argument, 1);");
  const auto call = call_parser.parseInstruction();
  if (!call || call->operands.size() != 3 ||
      !std::holds_alternative<ptx_frontend::syntax_ast::AstCallParameterList>(
          call->operands[0]) ||
      !std::holds_alternative<ptx_frontend::syntax_ast::AstCallTarget>(
          call->operands[1]))
    return 13;
  return 0;
}
