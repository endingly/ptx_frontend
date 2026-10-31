#include <array>
#include <concepts>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/base/ptx_target.hpp>
#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Verify installed catalog metadata and owned header gates without source storage. */
bool checkInstalledHeaderCompatibility() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace base = ptx_frontend::base;
  const auto sm = base::find_target_profile("sm_100f");
  const auto compute = base::find_target_profile("compute_100f");
  const auto legacy = base::find_target_profile("compute_12");
  if (!sm || !compute || !legacy ||
      !base::same_target_identity(sm->identity, compute->identity) ||
      sm->minimum_ptx_version.major != 8 ||
      sm->minimum_ptx_version.minor != 8 ||
      compute->minimum_ptx_version.major != sm->minimum_ptx_version.major ||
      compute->minimum_ptx_version.minor != sm->minimum_ptx_version.minor ||
      sm->supports_map_f64_to_f32 || !legacy->supports_map_f64_to_f32)
    return false;
  for (const auto source :
       {".version 1.0\n.target sm_80\n",
        ".version 2.2\n.target sm_20\n.address_size 32\n",
        ".version 2.3\n.target sm_20, debug\n",
        ".version 9.3\n.target compute_80, map_f64_to_f32\n"}) {
    std::optional<ir::ResolvedModule> owned;
    {
      ptx_frontend::PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty() ||
          ir::resolveAndValidateModule(*ast))
        return false;
      auto resolved = ir::resolveModuleOnly(*ast);
      if (!resolved)
        return false;
      owned.emplace(std::move(*resolved));
    }
    if (ir::validateModule(*owned) ||
        ir::validateModule(*owned,
                           ir::ModuleValidationPolicy::AvailableContext))
      return false;
  }
  return true;
}

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
      const auto profile =
          ptx_frontend::base::find_target_profile("sm_" + std::to_string(sm));
      if (!profile)
        return false;
      const auto minimum = profile->minimum_ptx_version;
      std::string source = ".version " + std::to_string(minimum.major) + "." +
                           std::to_string(minimum.minor) + "\n.target sm_" +
                           std::to_string(sm) + R"ptx(
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
      const auto profile = ptx_frontend::base::find_target_profile(target);
      if (!profile)
        return false;
      const auto minimum = profile->minimum_ptx_version;
      std::string source =
          ".version " + std::to_string(minimum.major) + "." +
          std::to_string(minimum.minor) + "\n.target " + std::string(target) +
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
        const auto profile = ptx_frontend::base::find_target_profile(target);
        if (!profile)
          return false;
        const auto minimum = profile->minimum_ptx_version;
        const auto source = ".version " + std::to_string(minimum.major) + "." +
                            std::to_string(minimum.minor) + "\n.target " +
                            std::string(target) +
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

/** Validate independent source-option and entry-parameter metadata together. */
bool checkInstalledCombinedSourceStates() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const bool debug : {false, true}) {
    std::optional<ir::ResolvedModule> owned;
    {
      const std::string source = std::string{".version 9.3\n.target sm_90"} +
                                 (debug ? ", debug" : "") + R"ptx(
.address_size 64
.global .u32 memory;
.entry kernel(.param .u32 input_map) {
  .reg .u32 %r;
  ld.global.L2::cache_hint.u32 %r, [memory];
  multimem.ld_reduce.gpu.add.u32 %r, [memory];
  prefetch.tensormap [input_map];
}
)ptx";
      ptx_frontend::PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto resolved = ir::resolveAndValidateModule(*ast);
      if (!resolved || !ir::validateModule(*ast, *resolved))
        return false;
      owned.emplace(std::move(*resolved));
    }
    const auto strict = [&] {
      return ir::validateModule(
          *owned, ir::ModuleValidationPolicy::RequireCompleteContext);
    };
    auto& region = owned->header.regions.back();
    const auto& body = owned->functions.front().body;
    const auto& load =
        dynamic_cast<const ir::LdGlobalU32L2CacheHint&>(*body[0]);
    const auto& reduction =
        dynamic_cast<const ir::MultimemLdReduceIntegerAddScalarDefaultWeak&>(
            *body[1]);
    if (!strict() || region.map_f64_to_f32 ||
        region.target_options.size() != (debug ? 2u : 1u) ||
        load.cache_policy ||
        reduction.semantics.value != ir::MemoryConsistency::Omitted ||
        !reduction.semantics.locs.empty() ||
        reduction.scope.value != ir::MemoryScope::Gpu ||
        reduction.scope.locs.empty())
      return false;
    region.map_f64_to_f32 = true;
    if (strict())
      return false;
    region.map_f64_to_f32 = false;
    auto& hint = dynamic_cast<ir::PrefetchGenericTensormap&>(*body[2]);
    auto& parameter = std::get<ir::ResolvedSymbolRef>(hint.address.value.base);
    parameter.enclosing_function_kind = ir::EnclosingFunctionKind::Device;
    hint.address.value.enclosing_function_kind =
        ir::EnclosingFunctionKind::Device;
    if (strict())
      return false;
    parameter.enclosing_function_kind = ir::EnclosingFunctionKind::Entry;
    hint.address.value.enclosing_function_kind =
        ir::EnclosingFunctionKind::Entry;
    if (!strict())
      return false;
  }
  return true;
}

/** Inspect exported packed element and vector parameter contracts after source release. */
bool checkInstalledParameterDomain() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace base = ptx_frontend::base;
  if (ptx_frontend::declaration_semantics::parameterScalarType(".f16x2") !=
      base::ScalarType::F16x2)
    return false;
  const auto owned = []() -> std::optional<ir::ResolvedModule> {
    ptx_frontend::PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.extern .func (.param .v2 .f16x2 result) exchange(.param .v2 .f16x2 input);
.entry kernel() { .param .v2 .f16x2 a, result; call (result), exchange, (a); }
)ptx");
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::nullopt;
    auto module = ir::resolveAndValidateModule(*ast);
    if (!module)
      return std::nullopt;
    return std::move(*module);
  }();
  if (!owned)
    return false;
  auto copy = *owned;
  const auto& formal =
      copy.functions.front().contract.signature.parameters.front();
  const auto& record = copy.functions.back().parameter_declarations.front();
  return formal.scalar_type == base::ScalarType::F16x2 &&
         formal.vector_shape == ptx_frontend::call_argument_compatibility::
                                    CallArgumentVectorShape::V2 &&
         record.scalar_type == base::ScalarType::F16x2 &&
         record.vector_width == 2 && record.byte_extent == 8 &&
         record.alignment == 8 && ir::validateModule(copy).has_value();
}

/** Exercise migrated public dimensions and derived ABI totals after syntax death. */
bool checkInstalledHeaderArrayRank() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace ast = ptx_frontend::syntax_ast;
  namespace cst = ptx_frontend::syntax_cst;
  namespace semantics = ptx_frontend::declaration_semantics;
  const auto owned = []() -> std::optional<ir::ResolvedModule> {
    const std::string source = R"ptx(
.version 9.3
.target sm_80, texmode_independent
.address_size 64
.extern .func take(.param .v2 .u32 input[2][3]);
.entry kernel(.param .texref texture[2][3], .param .u32 number[3][2]) {
  .param .v2 .u32 a[6];
  .reg .b64 target;
  proto: .callprototype _ (.param .v2 .u32 input[3][2]);
  call take, (a);
  call target, (a), proto;
}
)ptx";
    ptx_frontend::PtxCstParser cst_parser(source);
    const auto concrete = cst_parser.parseModule();
    if (!concrete || !concrete.diagnostics.empty() ||
        concrete->sourceText() != source)
      return std::nullopt;
    const auto& concrete_function =
        std::get<cst::CstFunction>(concrete->module()->items.back());
    if (!concrete_function.parameters ||
        concrete_function.parameters->parameters[0].array_dimensions.size() !=
            2)
      return std::nullopt;
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto parsed = parser.parseModule();
    if (!parsed || !parsed.diagnostics.empty())
      return std::nullopt;
    const auto& function = std::get<ast::AstFunction>(parsed->items.back());
    if (function.parameters[0].array_dimensions.size() != 2)
      return std::nullopt;
    auto module = ir::resolveAndValidateModule(*parsed);
    if (!module)
      return std::nullopt;
    return std::move(*module);
  }();
  if (!owned)
    return false;
  auto copy = *owned;
  const auto& function = copy.functions.back();
  const auto& prototype =
      function.call_prototypes.front().signature.parameters.front();
  return function.opaque_entry_parameters.front().array_extents ==
             std::vector<std::optional<uint64_t>>{2, 3} &&
         function.parameter_declarations.front().array_extents ==
             std::vector<std::optional<uint64_t>>{3, 2} &&
         function.parameter_declarations.front().byte_extent == 24 &&
         prototype.array_extents.size() == 2 &&
         semantics::call_argument_properties(prototype).array_size == 6 &&
         ir::validateModule(copy).has_value();
}

/** Exercise installed value-vector provenance after source destruction and copies. */
bool checkInstalledValueVectorSources() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.address_size 64
.global .surfref surf;
.entry kernel() {
.reg .v2 .b32 V<2>;
.reg .s32 x;
sust.b.1d.v2.b32.trap [surf,x], V1;
sust.b.1d.v2.b32.trap [surf,x], {V1.r,1};
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned)
    return false;
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  assigned.functions.front().body.front() =
      copied.functions.front().body.front()->clone();
  /** Inspect actual payload pointers only during each synchronous callback. */
  struct ValueObserver final : ir::detail::IReferenceObserver {
    /** Count complete named and brace sources separately. */
    size_t named{}, brace{};
    /** Accumulate validity without retaining borrowed payload storage. */
    bool valid = true;
    /** Check the installed source helper, copy equality, and borrowed view field. */
    void value_vector(const ir::ResolvedValueVector& value,
                      std::span<const ptx_frontend::SourceRange> ranges,
                      ir::checker::AddressSymbolResolutionPolicy) override {
      const auto copy = value;
      ir::checker::OperandView view{
          .field_id = "data",
          .actual_shape = ir::checker::OperandShape::Vector,
          .value_vector = &value,
          .vector_arity = value.elements.size(),
          .locations = ranges};
      valid = valid && copy == value && view.value_vector == &value &&
              ir::valid_value_vector_source(value, ranges, true);
      if (value.source.kind == ir::ResolvedVectorSourceKind::NamedVector) {
        ++named;
        valid = valid && value.source.whole_base &&
                std::get<ir::ResolvedRegisterRef>(value.elements.front())
                        .component->origin ==
                    ir::RegisterComponentOrigin::NamedProjection;
      } else {
        ++brace;
        valid = valid && !value.source.whole_base &&
                std::holds_alternative<ir::ResolvedImmediate>(
                    value.elements.back());
      }
    }
  } observer;
  for (const auto& instruction : assigned.functions.front().body)
    instruction->visit_references(observer);
  return observer.valid && observer.named == 1 && observer.brace == 1 &&
         ir::validateModule(copied) && ir::validateModule(assigned);
}

/** Verify the installed optional token policy and owned form parity without AST storage. */
bool checkInstalledMbarrierTokenForms() {
  namespace ir = ptx_frontend::resolved_ir;
  std::array<size_t, 3> forms{};
  size_t reports = 0;
  for (const auto& variant : ir::mbarrier_resolved_descriptor().variants)
    for (const auto& layout : variant.operand_layouts)
      for (const auto& binding : layout.bindings) {
        if (binding.mbarrier_state_token_form)
          ++forms[static_cast<size_t>(*binding.mbarrier_state_token_form)];
        else if (binding.target_field_id == "report_value")
          ++reports;
      }
  if (forms != std::array<size_t, 3>{49, 32, 12} || reports != 24)
    return false;
  ir::checker::OperandDescriptor generic{
      .target_field_id = "data",
      .type_expression =
          {.kind = ir::checker::OperandTypeExpressionKind::FixedScalar,
           .fixed_scalar_type = ir::ScalarType::B64},
      .role = ir::checker::OperandRole::Destination,
      .access = ir::checker::OperandAccess::Write,
      .allowed_shapes = ir::checker::OperandShape::Register,
      .allow_destination_sink = true};
  if (generic.mbarrier_state_token_form.has_value())
    return false;
  static constexpr std::array<std::string_view, 1> capabilities{"cluster"};
  const ir::checker::Context context{.target = {.ptx_version = {9, 3},
                                                .sm_version = 90,
                                                .capabilities = capabilities}};
  const ir::checker::OperandView sink{
      .field_id = "data",
      .actual_shape = ir::checker::OperandShape::Register,
      .is_sink = true};
  if (!ir::checker::check_operands(std::span{&generic, 1}, {},
                                   std::span{&sink, 1}, {}, context))
    return false;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.shared .align 8 .b64 barrier;
.entry kernel() {
.reg .b64 q;
.reg .pred p;
mbarrier.arrive.shared::cta.b64 q,[barrier];
mbarrier.arrive.shared::cluster.b64 _,[barrier];
mbarrier.test_wait.b64 p,[barrier],q;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned)
    return false;
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  assigned.functions.front().body.back() =
      copied.functions.front().body.back()->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  /** Borrow one actual token while its owned instruction remains alive. */
  struct TokenObserver final : ir::detail::IReferenceObserver {
    /** Borrowed only until that instruction is changed or destroyed. */
    const ir::ResolvedMbarrierStateToken* token = nullptr;
    /** Inspect the installed optional-register payload synchronously. */
    void mbarrier_state_token(
        const ir::ResolvedMbarrierStateToken& value,
        std::span<const ptx_frontend::SourceRange>,
        ir::checker::AddressSymbolResolutionPolicy) override {
      token = &value;
    }
  } observer;
  assigned.functions.front().body.back()->visit_references(observer);
  if (!observer.token || !observer.token->register_ref)
    return false;
  const_cast<ir::ResolvedMbarrierStateToken&>(*observer.token)
      .register_ref.reset();
  const auto direct = assigned.functions.front().body.back()->check(context);
  const auto invalid = ir::validateModule(assigned);
  return !direct && !invalid && direct.error().size() == 1 &&
         direct.error().front().kind ==
             ir::checker::CheckDiagnosticKind::UnsupportedOperandShape;
}

/** Exercise migrated half RegOrImm fields and formal-driven values after AST death. */
bool checkInstalledScalarHalfImmediates() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_100a
.extern .func take(.param .f16 input);
.entry kernel() {
.reg .f16 h;
.reg .f32 f;
.reg .u32 u;
.reg .pred p;
.reg .b64 target;
proto: .callprototype _ (.param .f16 input);
cvt.f32.f16 f,1.5;
fma.rn.f32.f16 f,1.5,h,0.0;
set.eq.u32.f16 u,1.5,h;
setp.eq.f16 p,1.5,h;
call take,(1.5);
call target,(1.0+0.5),proto;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned || owned->functions.back().body.size() != 6)
    return false;
  const auto& body = owned->functions.back().body;
  const auto& cvt = dynamic_cast<const ir::CvtUnroundedBase&>(*body[0]);
  const auto& fma = dynamic_cast<const ir::FmaMixedF32F16&>(*body[1]);
  const auto& set = dynamic_cast<const ir::SetHalfIntegerF16&>(*body[2]);
  const auto& setp = dynamic_cast<const ir::SetpF16&>(*body[3]);
  for (const auto* value :
       {std::get_if<ir::ResolvedImmediate>(&cvt.src.value),
        std::get_if<ir::ResolvedImmediate>(&fma.src1.value),
        std::get_if<ir::ResolvedImmediate>(&set.src1.value),
        std::get_if<ir::ResolvedImmediate>(&setp.src1.value)})
    if (!value || value->type != ir::ScalarType::F16 || value->bits != 0x3e00 ||
        value->integer_source_bits || value->is_negative)
      return false;
  if (!std::holds_alternative<ir::ResolvedRegisterRef>(fma.src2.value))
    return false;
  for (size_t index : {4U, 5U}) {
    const auto& call = dynamic_cast<const ir::CallDirect&>(*body[index]);
    const auto& literal = std::get<ir::ResolvedCallLiteral>(
        call.arguments->value.values[0].value);
    if (!literal.value || literal.value->type != ir::ScalarType::F16 ||
        literal.value->bits != 0x3e00 || literal.value->integer_source_bits)
      return false;
    if (index == 5 &&
        (!literal.source_value ||
         !std::holds_alternative<
             ptx_frontend::declaration_semantics::FloatingConstantValue>(
             *literal.source_value)))
      return false;
  }
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  assigned.functions.back().body[1] = copied.functions.back().body[1]->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  auto& altered =
      dynamic_cast<ir::FmaMixedF32F16&>(*assigned.functions.back().body[1]);
  std::get<ir::ResolvedImmediate>(altered.src1.value).integer_source_bits = 0;
  return !ir::validateModule(assigned);
}

/** Exercise exact-form and module resolution from the installed component. */
int main() {
  if (!checkInstalledScalarHalfImmediates())
    return 29;
  if (!checkInstalledMbarrierTokenForms())
    return 1;
  if (!checkInstalledValueVectorSources())
    return 1;
  if (!checkInstalledHeaderArrayRank() || !checkInstalledParameterDomain())
    return 1;
  if (!checkInstalledHeaderCompatibility())
    return 1;
  if (!checkInstalledCombinedSourceStates())
    return 1;
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
  return 0;
}
