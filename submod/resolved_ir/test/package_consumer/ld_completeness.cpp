#include <optional>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise installed public LD resolution and target-aware checking. */
int main() {
  using namespace ptx_frontend;
  using namespace resolved_ir;
  PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .attribute(.unified(1, 2)) .b128 unified_value;
.visible .entry kernel() {
  .reg .b128 %wide;
  ld.global.b128 %wide, [unified_value].unified;
}
)ptx");
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return 1;
  const auto module = resolveModule(*ast);
  if (!module || module->functions.size() != 1 ||
      module->functions.front().body.size() != 1)
    return 2;
  const auto checked = checker::check(
      std::get<Ld>(module->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90},
                       .instruction_range = ast->range});
  if (!checked)
    return 3;

  // Revalidation has no syntax AST: retained declaration identity must still
  // require the source `.unified` suffix for a unified global declaration.
  auto mutated = *module;
  auto& load = std::get<Ld>(mutated.functions.front().body.front());
  std::get<Ld::ExplicitScalar>(load.variant).address.value.unified = false;
  if (validateModule(mutated))
    return 4;

  // Address construction is not a memory read: it retains declaration identity
  // but must not require a suffix that the MOV syntax does not admit.
  std::optional<ResolvedModule> address_module;
  {
    PtxSyntaxParser address_parser(R"ptx(
.version 8.0
.target sm_90
.address_size 64
.global .attribute(.unified(1, 2)) .b8 data[16];
.visible .entry address_kernel() {
  .reg .u64 %rd0;
  mov.u64 %rd0, data+8;
}
)ptx");
    const auto address_ast = address_parser.parseModule();
    if (!address_ast || !address_ast.diagnostics.empty())
      return 5;
    auto resolved = resolveModuleOnly(*address_ast);
    if (!resolved)
      return 6;
    address_module.emplace(std::move(*resolved));
  }
  return validateModule(*address_module) ? 0 : 7;
}
