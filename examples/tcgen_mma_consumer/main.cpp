#include <algorithm>
#include <iostream>
#include <optional>
#include <string_view>
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

}  // namespace

/** Inspect caller-known rows without claiming opaque source-register values. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
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
