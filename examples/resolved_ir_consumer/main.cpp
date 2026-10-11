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
  .reg .v2 .u32 V;
  .reg .u64 %rd;
  add.u32 V.x, V.g, 7;
  ld.v2.u32 V, [%rd];
  mov.v2.u32 V, {V.x, 1+2};
  mov.v2.u32 {%r0,%r1}, V;
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
      module->functions.front().body.size() != 4 ||
      dynamic_cast<const ir::AddIntegerNoSat*>(
          module->functions.front().body.front().get()) == nullptr) {
    std::cerr << "installed module fixture did not resolve and validate\n";
    return 1;
  }
  const auto& selected = dynamic_cast<const ir::AddIntegerNoSat&>(
      *module->functions.front().body.front());
  const auto& component = selected.dst.value;
  if (!component.component || component.vector_width ||
      component.component->origin !=
          ir::RegisterComponentOrigin::ExplicitSelector ||
      component.component->lane != 0 ||
      component.component->declaration_width != 2 ||
      !ir::valid_register_component(component) ||
      ir::ordinary_register_lane(".r") != component.component->lane) {
    std::cerr << "installed component metadata is inconsistent\n";
    return 1;
  }
  const auto& named = dynamic_cast<const ir::LdGenericVector&>(
      *module->functions.front().body[1]);
  const auto& source = named.dst.value.source;
  if (source.kind != ir::ResolvedVectorSourceKind::NamedVector ||
      !source.whole_base ||
      !ir::valid_register_vector_source(named.dst.value, named.dst.locs) ||
      named.dst.value.elements[0]->component->origin !=
          ir::RegisterComponentOrigin::NamedProjection ||
      named.dst.value.elements[0]->component->selector ||
      ir::valid_register_component(*named.dst.value.elements[0])) {
    std::cerr << "installed named vector provenance is inconsistent\n";
    return 1;
  }
  /** Inspect complete named provenance through the installed public visitor. */
  struct NamedVectorObserver final : ir::detail::IReferenceObserver {
    /** Number of complete named-vector payloads observed synchronously. */
    size_t named_count{};
    /** Consume the container without retaining any borrowed payload or ranges. */
    void register_vector(const ir::ResolvedRegisterVector& value,
                         std::span<const ptx_frontend::SourceRange> locations,
                         ir::checker::AddressSymbolResolutionPolicy) override {
      if (value.source.whole_base &&
          ir::valid_register_vector_source(value, locations))
        ++named_count;
    }
  } named_observer;
  named.visit_references(named_observer);
  if (named_observer.named_count != 1)
    return 1;
  const auto& values_mov =
      dynamic_cast<const ir::MovV4U32&>(*module->functions.front().body[2]);
  const auto& values =
      std::get<ir::ResolvedMovValueVector>(values_mov.src.value);
  if (values.source.kind != ir::ResolvedVectorSourceKind::BraceList ||
      std::get<ir::ResolvedImmediate>(values.elements[1]).bits != 3 ||
      !ir::valid_mov_vector_source(values, values_mov.src.locs))
    return 1;
  /** Observe the installed bounded MOV source without retaining borrowed values. */
  struct MovVectorObserver final : ir::detail::IReferenceObserver {
    /** Count complete named MOV sources, not fabricated individual registers. */
    size_t named_count{};
    /** Validate the owned source during its synchronous visitor callback. */
    void mov_vector_source(
        const ir::ResolvedMovVectorSource& value,
        std::span<const ptx_frontend::SourceRange> locations,
        ir::checker::AddressSymbolResolutionPolicy) override {
      if (const auto* vector = std::get_if<ir::ResolvedMovValueVector>(&value);
          vector &&
          vector->source.kind == ir::ResolvedVectorSourceKind::NamedVector &&
          ir::valid_mov_vector_source(*vector, locations))
        ++named_count;
    }
  } mov_observer;
  module->functions.front().body[3]->visit_references(mov_observer);
  if (mov_observer.named_count != 1 || !ir::validateModule(*module))
    return 1;
  ptx_frontend::PtxSyntaxParser table_parser{R"ptx(
.version 9.3
.target sm_80
.address_size 64
.func f(.reg .u32 x);
.global .u32 targets[4] = {f,f};
.entry caller() { .reg .u64 fp; call fp, (1+2), targets; }
)ptx"};
  auto table_ast = table_parser.parseModule();
  if (!table_ast || !table_ast.diagnostics.empty())
    return 1;
  auto table_module = ir::resolveAndValidateModule(*table_ast);
  if (!table_module || table_module->call_tables.size() != 1)
    return 1;
  const auto& table = table_module->call_tables.front();
  if (table.symbol_id != table_module->storage_declarations.front().symbol_id ||
      table.slots.size() != 2 || table.slots[1].byte_offset != 4 ||
      table.slots[0].canonical_function != table.slots[1].canonical_function ||
      table.signature.parameters.size() != 1)
    return 1;
  /** Exercise the installed indirect union without treating storage as metadata. */
  struct TableObserver final : ir::detail::IReferenceObserver {
    /** Owned identity copied during a synchronous visit. */
    std::optional<ptx_frontend::binding::SymbolId> table;
    /** Observe the real storage branch through the existing call visitor. */
    void indirect_callee(const ir::ResolvedIndirectCallee& callee,
                         std::span<const ptx_frontend::SourceRange>,
                         ir::checker::AddressSymbolResolutionPolicy) override {
      if (const auto* reference =
              std::get_if<ir::ResolvedCallTableRef>(&callee))
        table = reference->symbol_id;
    }
  } table_observer;
  table_module->functions.back().body.front()->visit_references(table_observer);
  if (table_observer.table != table.symbol_id ||
      !ir::validateModule(*table_module))
    return 1;
  return 0;
}
