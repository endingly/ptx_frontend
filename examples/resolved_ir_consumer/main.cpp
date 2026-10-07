#include <array>
#include <iostream>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise exact-form and module resolution from the installed component. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  ptx_frontend::PtxSyntaxParser parser("add.u32 %r0, %r1, 7;");
  auto ast = parser.parseInstruction();
  if (!ast) {
    std::cerr << "installed Add fixture did not parse\n";
    return 1;
  }
  auto instruction = ir::resolveAdd(*ast);
  if (!instruction || (*instruction)->instruction_kind() !=
                          ir::InstructionKind::AddIntegerNoSat) {
    std::cerr << "installed Add fixture did not resolve\n";
    return 1;
  }
  static constexpr std::array<std::string_view, 1> families{"sm_120f"};
  const ir::checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = 120,
                 .enabled_family_features = families}};
  if (!(*instruction)->check(context)) {
    std::cerr << "installed Add fixture did not check\n";
    return 1;
  }
  auto clone = (*instruction)->clone();
  ir::detail::IReferenceObserver observer;
  clone->visit_references(observer);
  if (clone->instruction_kind() != ir::InstructionKind::AddIntegerNoSat)
    return 1;

  constexpr std::string_view module_source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .u32 %r<2>;
  add.u32 %r0, %r1, 7;
}
)ptx";
  ptx_frontend::PtxSyntaxParser module_parser{module_source};
  auto module_ast = module_parser.parseModule();
  if (!module_ast || !module_ast.diagnostics.empty()) {
    std::cerr << "installed module fixture did not parse\n";
    return 1;
  }
  auto module = ir::resolveAndValidateModule(*module_ast);
  if (!module || module->functions.size() != 1 ||
      module->functions.front().body.size() != 1 ||
      dynamic_cast<const ir::AddIntegerNoSat*>(
          module->functions.front().body.front().get()) == nullptr) {
    std::cerr << "installed module fixture did not resolve and validate\n";
    return 1;
  }
  return 0;
}
