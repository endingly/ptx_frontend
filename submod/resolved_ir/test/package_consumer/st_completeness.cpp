#include <iostream>
#include <optional>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise installed public ST resolution and target-aware checking. */
int main() {
  using namespace ptx_frontend;
  using namespace resolved_ir;
  PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .u32 output;
.visible .entry kernel() {
  .reg .u32 %value;
  st.global.mmio.release.sys.u32 [output], %value;
}
)ptx");
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    for (const auto& diagnostic : ast.diagnostics)
      std::cerr << diagnostic.message << '\n';
    if (ast.diagnostics.empty())
      std::cerr << "ST consumer source did not produce a syntax module.\n";
    return 1;
  }
  const auto module = resolveModule(*ast);
  if (!module) {
    for (const auto& diagnostic : module.error())
      std::cerr << diagnostic.message << '\n';
    return 2;
  }
  if (module->functions.size() != 1 ||
      module->functions.front().body.size() != 1) {
    std::cerr << "Expected one function containing one resolved ST, found "
              << module->functions.size() << " function(s) and "
              << (module->functions.empty()
                      ? 0
                      : module->functions.front().body.size())
              << " instruction(s).\n";
    return 2;
  }
  const auto checked = checker::check(
      std::get<St>(module->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 75},
                       .instruction_range = ast->range});
  if (!checked) {
    for (const auto& diagnostic : checked.error())
      std::cerr << diagnostic.message << '\n';
    return 3;
  }

  // MMIO stores admit relaxed/release semantics, never acquire semantics.
  auto mutated = *module;
  auto& store = std::get<St>(mutated.functions.front().body.front());
  std::get<St::ExplicitScalar>(store.variant).semantics.value =
      MemoryConsistency::Acquire;
  if (validateModule(mutated)) {
    std::cerr << "ST checker accepted acquire semantics for an MMIO store.\n";
    return 4;
  }

  // A unified declaration is read-only even though store syntax has no suffix.
  std::optional<ResolvedModule> unified_store;
  {
    PtxSyntaxParser unified_parser(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .attribute(.unified(1, 2)) .u32 unified_output;
.visible .entry unified_store_kernel() {
  .reg .u32 %value;
  st.global.u32 [unified_output], %value;
}
)ptx");
    const auto unified_ast = unified_parser.parseModule();
    if (!unified_ast || !unified_ast.diagnostics.empty()) {
      for (const auto& diagnostic : unified_ast.diagnostics)
        std::cerr << diagnostic.message << '\n';
      if (unified_ast.diagnostics.empty())
        std::cerr << "Unified ST source did not produce a syntax module.\n";
      return 5;
    }
    auto resolved = resolveModuleOnly(*unified_ast);
    if (!resolved) {
      for (const auto& diagnostic : resolved.error())
        std::cerr << diagnostic.message << '\n';
      return 6;
    }
    unified_store.emplace(std::move(*resolved));
  }
  if (validateModule(*unified_store)) {
    std::cerr << "ST validation accepted a write to a unified declaration.\n";
    return 7;
  }
  return 0;
}
