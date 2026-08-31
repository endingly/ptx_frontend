#include <string_view>
#include <variant>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_call_argument_compatibility.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

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

  constexpr std::string_view valid_call_module = R"ptx(
.func callee(.reg .u32 register_input, .param .u32 parameter_input,
             .param .s16 literal_input);
.entry caller() {
  .reg .u32 %r;
  .param .u32 parameter_argument;
  call callee, (%r, parameter_argument, -4);
}
)ptx";
  ptx_frontend::PtxSyntaxParser valid_call_parser(valid_call_module);
  const auto valid_call_ast = valid_call_parser.parseModule();
  if (!valid_call_ast)
    return 14;
  const auto valid_call_resolved =
      ptx_frontend::resolved_ir::resolveModule(*valid_call_ast);
  if (!valid_call_resolved || valid_call_resolved->functions.size() != 2 ||
      valid_call_resolved->functions[1].body.size() != 1 ||
      !std::holds_alternative<ptx_frontend::resolved_ir::Call>(
          valid_call_resolved->functions[1].body.front())) {
    return 15;
  }

  constexpr std::string_view invalid_call_module = R"ptx(
.func callee(.reg .u32 input);
.entry caller() {
  .reg .u64 %wide;
  call callee, (%wide);
}
)ptx";
  ptx_frontend::PtxSyntaxParser invalid_call_parser(invalid_call_module);
  const auto invalid_call_ast = invalid_call_parser.parseModule();
  if (!invalid_call_ast)
    return 16;
  const auto invalid_call_resolved =
      ptx_frontend::resolved_ir::resolveModule(*invalid_call_ast);
  if (invalid_call_resolved || invalid_call_resolved.error().size() != 1 ||
      invalid_call_resolved.error().front().message !=
          "Direct call input argument 1 for 'callee' has type or vector "
          "shape mismatch.") {
    return 17;
  }

  constexpr std::string_view control_flow_corpus = R"ptx(
.version 9.3
.target sm_30
.address_size 64
.func direct_callee(.reg .u32 input);
.func listed_callee(.reg .u32 input);
.entry caller() {
  .reg .u64 %fptr;
  .reg .u32 %direct_input, %indirect_input, %index;
empty: .callprototype _;
targets: .calltargets listed_callee;
branches: .branchtargets done;
  call direct_callee, (%direct_input);
  call %fptr, empty;
  call %fptr, (%indirect_input), targets;
  brx.idx %index, branches;
done:
}
)ptx";
  ptx_frontend::PtxSyntaxParser control_flow_parser(control_flow_corpus);
  const auto control_flow_ast = control_flow_parser.parseModule();
  if (!control_flow_ast)
    return 18;
  const auto control_flow_resolved =
      ptx_frontend::resolved_ir::resolveModule(*control_flow_ast);
  if (!control_flow_resolved || control_flow_resolved->functions.size() != 3 ||
      control_flow_resolved->functions[2].body.size() != 4)
    return 19;
  const ptx_frontend::resolved_ir::checker::Context control_flow_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 30},
      .instruction_range = {},
  };
  for (const auto& instruction : control_flow_resolved->functions[2].body) {
    const auto checked = std::visit(
        [&](const auto& value) {
          return ptx_frontend::resolved_ir::checker::check(
              value, control_flow_context);
        },
        instruction);
    if (!checked)
      return 20;
  }

  constexpr std::string_view directive_corpus = R"ptx(
.version 9.3
.target sm_30
.address_size 64
.file 0x1U "directive-corpus.ptx"
.section .debug_str { debug_name: .b8 0; };
.pragma "module";
.entry directives() .pragma "entry"; .maxnreg 32 .maxntid 32, 1, 1
    .minnctapersm 1 {
  .reg .u32 %r0, %r1;
  .loc 0x1U 1 0
  {
    .pragma "nested";
    .loc 1 2 0, function_name debug_name, inlined_at 1 1 0;
    add.u32 %r0, %r1, 1;
  }
}
)ptx";
  ptx_frontend::PtxCstParser directive_cst_parser(directive_corpus);
  const auto directive_cst = directive_cst_parser.parseModule();
  if (!directive_cst || !directive_cst.diagnostics.empty() ||
      directive_cst->sourceText() != directive_corpus) {
    return 21;
  }
  ptx_frontend::PtxSyntaxParser directive_parser(directive_corpus);
  const auto directive_ast = directive_parser.parseModule();
  if (!directive_ast || !directive_ast.diagnostics.empty())
    return 22;
  const auto directive_symbols =
      ptx_frontend::binding::bindSymbols(*directive_ast);
  if (!directive_symbols.diagnostics.empty() ||
      !ptx_frontend::declaration_semantics::checkDeclarations(
          *directive_ast, directive_symbols.table)
           .empty()) {
    return 23;
  }
  const auto directive_resolved =
      ptx_frontend::resolved_ir::resolveModule(*directive_ast);
  if (!directive_resolved || directive_resolved->functions.size() != 1 ||
      directive_resolved->functions.front().body.size() != 1 ||
      !std::holds_alternative<ptx_frontend::resolved_ir::Add>(
          directive_resolved->functions.front().body.front())) {
    return 24;
  }
  const ptx_frontend::resolved_ir::checker::Context directive_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 30},
      .instruction_range = {},
  };
  if (!ptx_frontend::resolved_ir::checker::check(
          std::get<ptx_frontend::resolved_ir::Add>(
              directive_resolved->functions.front().body.front()),
          directive_context)) {
    return 25;
  }

  ptx_frontend::PtxSyntaxParser overflow_parser(
      ".file 18446744073709551616U \"overflow.ptx\"\n"
      ".entry overflow() { }");
  const auto overflow_ast = overflow_parser.parseModule();
  if (!overflow_ast || !overflow_ast.diagnostics.empty())
    return 26;
  const auto overflow_resolved =
      ptx_frontend::resolved_ir::resolveModule(*overflow_ast);
  if (overflow_resolved || overflow_resolved.error().size() != 1 ||
      overflow_resolved.error().front().message !=
          "Debug file index must be an unsigned 64-bit integer.") {
    return 27;
  }

  constexpr std::string_view unknown_directive_corpus = R"ptx(
.version 9.3
.language "C++";
.entry after_unknown() { .reg .u32 %r0, %r1; add.u32 %r0, %r1, 1; }
)ptx";
  ptx_frontend::PtxCstParser unknown_cst_parser(unknown_directive_corpus);
  const auto unknown_cst = unknown_cst_parser.parseModule();
  if (!unknown_cst || unknown_cst.diagnostics.size() != 1 ||
      unknown_cst->sourceText() != unknown_directive_corpus ||
      unknown_cst.diagnostics.front().message !=
          "expected module directive, variable declaration, or function") {
    return 28;
  }
  ptx_frontend::PtxSyntaxParser unknown_parser(unknown_directive_corpus);
  const auto unknown_ast = unknown_parser.parseModule();
  if (!unknown_ast || unknown_ast.diagnostics.size() != 1 ||
      unknown_ast.diagnostics.front().message !=
          unknown_cst.diagnostics.front().message ||
      unknown_ast->items.size() != 2 ||
      !std::holds_alternative<ptx_frontend::syntax_ast::AstFunction>(
          unknown_ast->items.back())) {
    return 29;
  }
  return 0;
}
