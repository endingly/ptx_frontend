#include <array>
#include <concepts>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Verify installed legacy forms have no synthesized synchronization controls. */
template <typename T>
concept InstalledMaskFreeVote = !requires(T instruction) {
  instruction.sync;
} && !requires(T instruction) { instruction.membermask; };
static_assert(InstalledMaskFreeVote<ptx_frontend::resolved_ir::VoteAllPred> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::VoteAnyPred> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::VoteUniPred> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::VoteBallotB32>);

/** Exercise all installed legacy vote modes and complements across their catalog. */
bool checkInstalledLegacyVote() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const auto sm : {12u, 13u, 20u, 30u, 50u, 52u}) {
    std::optional<ir::ResolvedModule> owned;
    {
      std::string source =
          ".version 1.2\n.target sm_" + std::to_string(sm) + R"ptx(
.func f() { .reg .pred %p<2>; .reg .b32 %b;
 vote.all.pred %p1, %p0; vote.all.pred %p1, !%p0;
 vote.any.pred %p1, %p0; vote.any.pred %p1, !%p0;
 vote.uni.pred %p1, %p0; vote.uni.pred %p1, !%p0;
)ptx";
      if (sm >= 20)
        source += "vote.ballot.b32 %b, %p0; vote.ballot.b32 %b, !%p0;\n";
      source += "}\n";
      ptx_frontend::PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto resolved = ir::resolveAndValidateModule(*ast);
      if (!resolved || !ir::validateModule(*ast, *resolved))
        return false;
      owned.emplace(std::move(*resolved));
    }
    if (!ir::validateModule(*owned))
      return false;
    const auto& body = owned->functions.front().body;
    if (body.size() != (sm >= 20 ? 8u : 6u))
      return false;
    for (size_t index = 0; index < body.size(); ++index) {
      const ir::ResolvedPredicate* predicate = nullptr;
      if (index < 2) {
        const auto* vote =
            dynamic_cast<const ir::VoteAllPred*>(body[index].get());
        if (!vote || !ir::VoteAllPred::all ||
            ir::VoteAllPred::type != ir::ScalarType::Pred)
          return false;
        predicate = &vote->predicate.value;
      } else if (index < 4) {
        const auto* vote =
            dynamic_cast<const ir::VoteAnyPred*>(body[index].get());
        if (!vote || !ir::VoteAnyPred::any ||
            ir::VoteAnyPred::type != ir::ScalarType::Pred)
          return false;
        predicate = &vote->predicate.value;
      } else if (index < 6) {
        const auto* vote =
            dynamic_cast<const ir::VoteUniPred*>(body[index].get());
        if (!vote || !ir::VoteUniPred::uni ||
            ir::VoteUniPred::type != ir::ScalarType::Pred)
          return false;
        predicate = &vote->predicate.value;
      } else {
        const auto* vote =
            dynamic_cast<const ir::VoteBallotB32*>(body[index].get());
        if (!vote || !ir::VoteBallotB32::ballot ||
            ir::VoteBallotB32::type != ir::ScalarType::B32 ||
            vote->dst.value.declared_type != ir::ScalarType::B32)
          return false;
        predicate = &vote->predicate.value;
      }
      if (predicate->negated != (index % 2 == 1) ||
          predicate->register_ref.declared_type != ir::ScalarType::Pred)
        return false;
      ir::checker::Context context{
          .target = {.ptx_version = {1, 2},
                     .sm_version = sm,
                     .identity = ptx_frontend::base::parse_target_identity(
                         "sm_" + std::to_string(sm))}};
      if (!body[index]->check(context))
        return false;
      context.target.sm_version = 70;
      context.target.identity =
          ptx_frontend::base::parse_target_identity("sm_70");
      if (body[index]->check(context))
        return false;
    }
  }
  return true;
}

static_assert(InstalledMaskFreeVote<ptx_frontend::resolved_ir::ShflUpB32> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::ShflDownB32> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::ShflBflyB32> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::ShflIdxB32>);

/** Exercise both installed shuffle layouts and b/c controls after AST destruction. */
bool checkInstalledLegacyShuffle() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const auto target : {"sm_30", "sm_50", "sm_52"}) {
    std::optional<ir::ResolvedModule> owned;
    {
      std::string source =
          ".version 3.0\n.target " + std::string(target) +
          "\n.func f() { .reg .b32 %b<2>; .reg .pred %p; .reg .u32 %u<2>;\n";
      for (const auto mode : {"up", "down", "bfly", "idx"}) {
        source += "shfl." + std::string(mode) + ".b32 %b0, %b1, 1, 31;\n";
        source += "shfl." + std::string(mode) + ".b32 %b0|%p, %b1, %u0, %u1;\n";
      }
      source += "}\n";
      ptx_frontend::PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto resolved = ir::resolveAndValidateModule(*ast);
      if (!resolved || !ir::validateModule(*ast, *resolved))
        return false;
      owned.emplace(std::move(*resolved));
    }
    if (!ir::validateModule(*owned))
      return false;
    const auto& body = owned->functions.front().body;
    if (body.size() != 8)
      return false;
    for (size_t index = 0; index < body.size(); ++index) {
      const bool paired = index % 2 == 1;
      /** Inspect only one of the four installed legacy concrete forms. */
      const auto inspect = [&]<typename T>(const T* form)
        requires(std::same_as<T, ir::ShflUpB32> ||
                 std::same_as<T, ir::ShflDownB32> ||
                 std::same_as<T, ir::ShflBflyB32> ||
                 std::same_as<T, ir::ShflIdxB32>)
      {
        if (!form || T::type != ir::ScalarType::B32 ||
            form->dst_register.has_value() == paired ||
            form->dst_shfl_destination.has_value() != paired ||
            form->src.value.spelling != "%b1" || !form->src.value.symbol_id ||
            form->src.locs.empty())
          return false;
        if (paired) {
          const auto& destination = *form->dst_shfl_destination;
          if (!destination.value.data || !destination.value.predicate ||
              destination.value.data->value.spelling != "%b0" ||
              destination.value.predicate->value.register_ref.spelling !=
                  "%p" ||
              destination.value.predicate->value.register_ref.declared_type !=
                  ir::ScalarType::Pred ||
              destination.locs.empty())
            return false;
        } else if (form->dst_register->value.spelling != "%b0" ||
                   form->dst_register->value.declared_type !=
                       ir::ScalarType::B32 ||
                   form->dst_register->locs.empty()) {
          return false;
        }
        return std::holds_alternative<ir::ResolvedRegisterRef>(
                   form->lane.value) == paired &&
               std::holds_alternative<ir::ResolvedRegisterRef>(
                   form->clamp.value) == paired &&
               !form->lane.locs.empty() && !form->clamp.locs.empty();
      };
      bool valid = false;
      switch (index / 2) {
        case 0:
          valid =
              inspect(dynamic_cast<const ir::ShflUpB32*>(body[index].get()));
          break;
        case 1:
          valid =
              inspect(dynamic_cast<const ir::ShflDownB32*>(body[index].get()));
          break;
        case 2:
          valid =
              inspect(dynamic_cast<const ir::ShflBflyB32*>(body[index].get()));
          break;
        case 3:
          valid =
              inspect(dynamic_cast<const ir::ShflIdxB32*>(body[index].get()));
          break;
      }
      if (!valid)
        return false;
      const ir::checker::Context modern{
          .target = {
              .ptx_version = {9, 3},
              .sm_version = 70,
              .identity = ptx_frontend::base::parse_target_identity("sm_70")}};
      if (body[index]->check(modern))
        return false;
    }
  }
  return true;
}

/** Detect whether an installed form exposes a programmer rounding member. */
template <typename T>
concept HasInstalledRounding =
    requires(T instruction) { instruction.rounding; };
static_assert(!HasInstalledRounding<ptx_frontend::resolved_ir::MadImplicitF32>);

/** Exercise every implicit MAD target/flag combination using the installed form. */
bool checkInstalledImplicitMad() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const auto target : {"sm_10", "sm_11", "sm_12", "sm_13"}) {
    for (const auto flags : {"", ".ftz", ".sat", ".ftz.sat"}) {
      std::optional<ir::ResolvedModule> owned;
      {
        const auto source = ".version 1.0\n.target " + std::string(target) +
                            "\n.func f() { .reg .f32 %f<4>; .reg .b32 %r; mad" +
                            std::string(flags) + ".f32 %r, %f1, 1.0, %f3; }\n";
        ptx_frontend::PtxSyntaxParser parser{source};
        const auto ast = parser.parseModule();
        if (!ast || !ast.diagnostics.empty())
          return false;
        auto resolved = ir::resolveAndValidateModule(*ast);
        if (!resolved || !ir::validateModule(*ast, *resolved))
          return false;
        owned.emplace(std::move(*resolved));
      }
      if (!ir::validateModule(*owned))
        return false;
      const auto& function = owned->functions.front();
      const auto* mad =
          dynamic_cast<const ir::MadImplicitF32*>(function.body[0].get());
      if (!mad || function.instruction_opcodes.front() != "mad" ||
          ir::MadImplicitF32::type != ir::ScalarType::F32 ||
          mad->dst.value.declared_type != ir::ScalarType::B32 ||
          std::get<ir::ResolvedImmediate>(mad->src2.value).type !=
              ir::ScalarType::F32 ||
          mad->ftz.value != (std::string_view(flags).find(".ftz") !=
                             std::string_view::npos) ||
          mad->saturate.value !=
              (std::string_view(flags).find(".sat") != std::string_view::npos))
        return false;
      ir::checker::Context context{
          .target = {
              .ptx_version = {1, 0},
              .sm_version = static_cast<unsigned>(std::stoi(target + 3)),
              .identity = ptx_frontend::base::parse_target_identity(target)}};
      if (!mad->check(context))
        return false;
      context.target.sm_version = 20;
      context.target.identity =
          ptx_frontend::base::parse_target_identity("sm_20");
      if (mad->check(context))
        return false;
    }
  }
  return true;
}

/** Check actual mapped forms and strict owned revalidation through installed APIs. */
bool checkInstalledMappedF64() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const auto target : {"sm_10", "sm_11", "sm_12"}) {
    std::optional<ir::ResolvedModule> owned;
    {
      const std::string source = ".version 1.4\n.target " +
                                 std::string(target) + R"ptx(, map_f64_to_f32
.func f() { .reg .f64 %d<3>; .reg .b64 %rd;
  div.rn.f64 %rd, %d1, 1.0;
  rcp.rn.f64 %d0, %rd;
  sqrt.rn.f64 %d0, %d1;
}
)ptx";
      ptx_frontend::PtxSyntaxParser parser{source};
      auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto resolved = ir::resolveAndValidateModule(*ast);
      if (!resolved || !ir::validateModule(*ast, *resolved))
        return false;
      owned.emplace(std::move(*resolved));
    }
    if (!ir::validateModule(*owned))
      return false;
    const auto& body = owned->functions.front().body;
    const auto* div = dynamic_cast<const ir::DivRnF64*>(body[0].get());
    const auto* rcp = dynamic_cast<const ir::RcpRnF64*>(body[1].get());
    const auto* sqrt = dynamic_cast<const ir::SqrtRnF64*>(body[2].get());
    if (!div || !rcp || !sqrt || ir::DivRnF64::type != ir::ScalarType::F64 ||
        ir::RcpRnF64::type != ir::ScalarType::F64 ||
        ir::SqrtRnF64::type != ir::ScalarType::F64 ||
        div->dst.value.declared_type != ir::ScalarType::B64 ||
        std::get<ir::ResolvedImmediate>(div->src2.value).type !=
            ir::ScalarType::F64)
      return false;
    ir::checker::Context context{
        .target = {
            .ptx_version = {1, 4},
            .sm_version = static_cast<unsigned>(std::stoi(target + 3)),
            .identity = ptx_frontend::base::parse_target_identity(target),
            .map_f64_to_f32 = true}};
    for (const auto& instruction : body) {
      if (!instruction->check(context))
        return false;
      context.target.map_f64_to_f32 = false;
      if (instruction->check(context))
        return false;
      context.target.map_f64_to_f32 = true;
    }
    auto& region = owned->header.regions[1];
    region.map_f64_to_f32 = false;
    if (ir::validateModule(*owned))
      return false;
    region.map_f64_to_f32 = true;
    region.target_options.back() = "map_f64_to_f32x";
    if (ir::validateModule(*owned))
      return false;
    region.target_options.back() = "map_f64_to_f32";
    if (!ir::validateModule(*owned))
      return false;
  }
  return true;
}

/** Exercise exact-form and module resolution from the installed component. */
int main() {
  if (!checkInstalledLegacyShuffle())
    return 1;
  if (!checkInstalledLegacyVote())
    return 1;
  if (!checkInstalledImplicitMad())
    return 1;
  if (!checkInstalledMappedF64())
    return 1;
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

  const ir::checker::ParameterAddressConstraint entry_constraint{
      .direction = ir::ParameterDirection::Input, .entry_only = true};
  if (!entry_constraint.entry_only ||
      ir::checker::ParameterAddressConstraint{}.entry_only)
    return 1;
  const auto& tensor_binding = ir::prefetch_resolved_descriptor()
                                   .variants.back()
                                   .operand_layouts.front()
                                   .bindings.front();
  if (!tensor_binding.parameter_constraint.entry_only ||
      tensor_binding.parameter_constraint.direction !=
          entry_constraint.direction ||
      tensor_binding.allowed_address_state_spaces.size() != 4)
    return 1;
  std::optional<ir::ResolvedModule> prefetch_module;
  {
    const std::string prefetch_source = R"ptx(
.version 8.0
.target sm_90
.address_size 64
.global .u32 global_map;
.const .u32 constant_map;
.entry kernel(.param .u32 input_map) {
  .local .u32 local_map;
  .shared .u32 shared_map;
  .reg .u64 %rd;
  prefetch.tensormap [constant_map];
  prefetch.tensormap [input_map];
  prefetch.tensormap [shared_map];
  prefetchu.L1 [global_map];
  prefetchu.L1 [constant_map];
  prefetchu.L1 [local_map];
  prefetchu.L1 [shared_map];
  prefetchu.L1 [%rd];
}
)ptx";
    ptx_frontend::PtxSyntaxParser prefetch_parser(prefetch_source);
    const auto prefetch_ast = prefetch_parser.parseModule();
    if (!prefetch_ast || !prefetch_ast.diagnostics.empty())
      return 1;
    auto resolved = ir::resolveAndValidateModule(*prefetch_ast);
    if (!resolved)
      return 1;
    prefetch_module.emplace(std::move(*resolved));
  }
  const auto strict_prefetch = [&] {
    return ir::validateModule(
        *prefetch_module, ir::ModuleValidationPolicy::RequireCompleteContext);
  };
  if (!strict_prefetch())
    return 1;
  auto& input_hint = dynamic_cast<ir::PrefetchGenericTensormap&>(
      *prefetch_module->functions.front().body[1]);
  auto& input = std::get<ir::ResolvedSymbolRef>(input_hint.address.value.base);
  if (!input.symbol_id ||
      input.symbol_id != prefetch_module->functions.front()
                             .parameter_declarations.front()
                             .symbol_id ||
      input.address_state_space !=
          ptx_frontend::base::DeclarationStateSpace::Parameter ||
      input.enclosing_function_kind != ir::EnclosingFunctionKind::Entry)
    return 1;
  input.enclosing_function_kind = ir::EnclosingFunctionKind::Device;
  input_hint.address.value.enclosing_function_kind =
      ir::EnclosingFunctionKind::Device;
  if (strict_prefetch())
    return 1;
  input.enclosing_function_kind = ir::EnclosingFunctionKind::Entry;
  input_hint.address.value.enclosing_function_kind =
      ir::EnclosingFunctionKind::Entry;
  if (!strict_prefetch())
    return 1;

  constexpr ir::checker::AvailabilityDescriptor map_requirement{
      .any_of = {{{.minimum_ptx_version = {1, 0},
                   .has_exact_target = true,
                   .exact_target_architecture = {10},
                   .requires_map_f64_to_f32 = true}}},
      .any_of_count = 1,
  };
  ir::checker::TargetInfo legacy_target{
      .ptx_version = {1, 5},
      .sm_version = 10,
      .identity = ptx_frontend::base::parse_target_identity("sm_10"),
  };
  if (ir::checker::is_available(map_requirement, legacy_target))
    return 1;
  legacy_target.map_f64_to_f32 = true;
  if (!ir::checker::is_available(map_requirement, legacy_target))
    return 1;
  std::optional<ir::ResolvedModule> legacy_module;
  {
    const std::string legacy_source = R"ptx(.version 1.5
.target sm_10, map_f64_to_f32, texmode_unified
.func first() { ret; }
.target sm_11
.func second() { ret; }
.target sm_12, texmode_unified, map_f64_to_f32
.func third() { ret; }
)ptx";
    ptx_frontend::PtxSyntaxParser legacy_parser{legacy_source};
    auto legacy_ast = legacy_parser.parseModule();
    if (!legacy_ast || !legacy_ast.diagnostics.empty())
      return 1;
    auto legacy_resolved = ir::resolveAndValidateModule(*legacy_ast);
    if (!legacy_resolved)
      return 1;
    legacy_module.emplace(std::move(*legacy_resolved));
  }
  if (legacy_module->header.regions.size() != 4 ||
      !legacy_module->header.regions[1].map_f64_to_f32 ||
      legacy_module->header.regions[2].map_f64_to_f32 ||
      !legacy_module->header.regions[3].map_f64_to_f32 ||
      legacy_module->header.regions[1].target_options[1] != "map_f64_to_f32" ||
      legacy_module->header.regions[3].target_options[2] != "map_f64_to_f32" ||
      !ir::validateModule(*legacy_module))
    return 1;
  legacy_module->header.regions[1].map_f64_to_f32 = false;
  if (ir::validateModule(*legacy_module))
    return 1;
  legacy_module->header.regions[1].map_f64_to_f32 = true;
  legacy_module->header.regions[3].target_options.pop_back();
  if (ir::validateModule(*legacy_module))
    return 1;
  return 0;
}
