#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Report a failed installed operational-row contract. */
bool require(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "TCGEN MMA consumer: " << message << '\n';
  return condition;
}

/** Inspect only actual mask payloads while their current instruction owner is alive. */
struct InstalledMaskObserver final
    : ptx_frontend::resolved_ir::detail::IReferenceObserver {
  /** Borrowed only for the surrounding synchronous visit. */
  const ptx_frontend::resolved_ir::ResolvedRegisterVector& expected;
  /** Copied results; callback pointers and spans are never retained. */
  size_t calls = 0;
  bool valid = true;
  /** Bind an actual owned member before visiting. */
  explicit InstalledMaskObserver(
      const ptx_frontend::resolved_ir::ResolvedRegisterVector& value)
      : expected(value) {}
  /** Inspect every actual scalar lane and any named whole declaration. */
  void register_vector(
      const ptx_frontend::resolved_ir::ResolvedRegisterVector& value,
      std::span<const ptx_frontend::SourceRange> ranges,
      ptx_frontend::resolved_ir::checker::AddressSymbolResolutionPolicy)
      override {
    if (&value != &expected)
      return;
    ++calls;
    valid = valid && ranges.size() == value.elements.size();
    for (size_t lane = 0; lane < value.elements.size(); ++lane) {
      valid = valid && value.elements[lane].has_value();
      if (!value.elements[lane])
        continue;
      valid = valid && value.elements[lane]->symbol_id.has_value();
      if (value.source.whole_base)
        valid = valid &&
                value.elements[lane]->symbol_id ==
                    value.source.whole_base->value.register_ref.symbol_id &&
                value.elements[lane]->component &&
                value.elements[lane]->component->lane == lane;
    }
  }
};

/** Build normal public source without assuming known runtime descriptor words. */
std::string installedMaskSource(std::string_view carrier, unsigned group,
                                bool named, bool omit = false) {
  std::string source =
      ".version 9.3\n.target sm_100a\n.address_size 64\n.entry k(){"
      ".reg .b32 d,i; .reg .b64 ad,bd; .reg .pred p; .reg ." +
      std::string(carrier) + " m<8>; .reg .v4 ." + std::string(carrier) +
      " V; "
      "mov.b32 d,0;mov.b32 i,0;mov.b64 ad,0;mov.b64 bd,0;mov.pred p,1;";
  source += "tcgen05.mma.cta_group::" + std::to_string(group) +
            ".kind::f16 [d],ad,bd,i,";
  if (!omit) {
    if (named)
      source += "V";
    else {
      source += "{";
      for (unsigned lane = 0; lane < (group == 1 ? 4u : 8u); ++lane)
        source += (lane ? ",m" : "m") + std::to_string(lane);
      source += "}";
    }
    source += ",";
  }
  return source + "p;}";
}

/** Exercise the installed descriptor/resolver/owned named mask contract and preserved groups. */
bool checkInstalledNamedMasks() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(std::is_base_of_v<ir::Instruction, ir::Tcgen05MmaF16>);
  static_assert(std::is_default_constructible_v<ir::Tcgen05MmaF16>);
  static_assert(
      std::is_same_v<decltype(ir::Tcgen05MmaF16::disable_output_lane),
                     ptx_frontend::WithLocs<ir::ResolvedRegisterVector>>);
  for (const auto carrier : {"b32", "u32", "s32", "f32", "f16x2"}) {
    for (const auto& [group, named] :
         {std::pair{1u, false}, std::pair{1u, true}, std::pair{2u, false}}) {
      std::optional<ir::ResolvedModule> owned;
      {
        const auto source = installedMaskSource(carrier, group, named);
        ptx_frontend::PtxSyntaxParser parser(source);
        const auto ast = parser.parseModule();
        if (!ast || !ast.diagnostics.empty())
          return false;
        auto result = ir::resolveAndValidateModule(*ast);
        if (!result || !ir::validateModule(*ast, *result))
          return false;
        owned.emplace(std::move(*result));
      }
      auto copy = *owned;
      ir::ResolvedModule assigned;
      assigned = copy;
      for (auto& instruction : assigned.functions.front().body) {
        if (auto* form = dynamic_cast<ir::Tcgen05MmaF16*>(instruction.get())) {
          const auto view = ir::tcgen_mma_f16_view(*form);
          if (form->instruction_kind() != ir::Tcgen05MmaF16::kind ||
              form->opcode_kind() != ir::Tcgen05MmaF16::opcode ||
              !std::get_if<ir::ResolvedPredicate>(&form->enable_input_d.value))
            return false;
          if (!view ||
              view->disable_output_lane != &form->disable_output_lane.value ||
              form->disable_output_lane.value.elements.size() !=
                  (group == 1 ? 4u : 8u))
            return false;
          InstalledMaskObserver observer(form->disable_output_lane.value);
          form->visit_references(observer);
          if (!observer.valid || observer.calls != 1)
            return false;
          instruction = form->clone();
          break;
        }
      }
      if (!ir::validateModule(assigned))
        return false;
    }
    for (const bool omitted : {false, true}) {
      const auto source = installedMaskSource(carrier, 2, true, omitted);
      ptx_frontend::PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty() ||
          ir::resolveAndValidateModule(*ast))
        return false;
    }
  }
  return true;
}

}  // namespace

/** Inspect caller-known rows without claiming opaque source-register values. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  if (!require(checkInstalledNamedMasks(),
               "named-mask carrier and group contract"))
    return 1;
  const auto shapes = ir::tcgen_f16_shape_rows();
  const auto paths = ir::tcgen_f16_path_rows();
  if (!require(shapes.size() == 4 && paths.size() == 4,
               "static f16 shape/path rows"))
    return 1;
  if (!require(shapes.data() == ir::tcgen_f16_shape_rows().data(),
               "borrowed row storage has stable lifetime"))
    return 1;
  if (!require(ir::tcgen_f16_row_contains(shapes[0], 64, 8, 16) &&
                   !ir::tcgen_f16_row_contains(shapes[0], 64, 9, 16) &&
                   paths[0].layout == 'F' && paths[0].half_path,
               "Table 42 and dense datapath facts"))
    return 1;
  const ir::TcgenF16KnownFacts facts{
      .group = ir::TcgenCtaGroup::One,
      .a_in_tmem = true,
      .scaled_d = false,
      .instruction = ir::TcgenInstructionWord{(4U << 24) | (1U << 17),
                                              ir::TcgenMmaKind::F16},
  };
  const auto report = ir::check_tcgen_f16_known_operation(facts);
  if (!require(report.supplied_facts_ok() && report.path_layout == 'F' &&
                   std::find(report.missing.begin(), report.missing.end(),
                             ir::TcgenF16Obligation::BSharedWord) !=
                       report.missing.end(),
               "known facts are distinct from missing live descriptor bits"))
    return 1;
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %i, %m<4>;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
  .shared .align 8 .b64 barrier;
  tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i,
      {%m0, %m1, %m2, %m3}, !%p, 15;
  tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value() && ast.diagnostics.empty(), "source parses"))
      return 1;
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!require(resolved.has_value(), "source resolves"))
      return 1;
    owned.emplace(std::move(*resolved));
  }
  const auto* record = owned->functions.front().body[0].get();
  if (!require(record != nullptr, "owned MMA survives AST release"))
    return 1;
  const auto* form = dynamic_cast<const ir::Tcgen05MmaF16*>(record);
  const auto view = ir::tcgen_mma_f16_view(*record);
  if (!require(
          form && view && view->group == ir::TcgenCtaGroup::One &&
              view->a_shared && !view->a_tmem &&
              view->a_shared->role == ir::MatrixFragmentRole::A &&
              view->b.role == ir::MatrixFragmentRole::B &&
              view->disable_output_lane &&
              view->disable_output_lane->elements.size() == 4 &&
              view->scale_d && view->scale_d->bits == 15 &&
              std::holds_alternative<ir::ResolvedPredicate>(*view->enable_d) &&
              std::get<ir::ResolvedPredicate>(*view->enable_d).negated,
          "borrowed selected roles and source controls"))
    return 1;
  const auto variants = ir::tcgen05_resolved_descriptor().variants;
  const bool f16_present =
      std::find_if(variants.begin(), variants.end(), [](const auto& variant) {
        return variant.variant_name == "MmaF16";
      }) != variants.end();
  return require(f16_present && ir::validateModule(*owned).has_value(),
                 "f16 identity and AST-independent validation")
             ? 0
             : 1;
}
