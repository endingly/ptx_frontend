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

/** Print the first failed installed tf32 contract without hiding the exit code. */
bool require(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "TCGEN tf32 MMA consumer: " << message << '\n';
  return condition;
}

}  // namespace

/** Exercise installed tf32 rows and borrowed owned roles after AST death. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  const auto shapes = ir::tcgen_tf32_shape_rows();
  const auto paths = ir::tcgen_tf32_path_rows();
  if (!require(shapes.size() == 2 && paths.size() == 4,
               "static tf32 Table 42/path rows"))
    return 1;
  if (!require(ir::tcgen_tf32_row_contains(shapes[0], 64, 8, 8) &&
                   !ir::tcgen_tf32_row_contains(shapes[0], 64, 9, 8) &&
                   paths[0].layout == 'F' && paths[0].half_path,
               "K8 and non-WS datapath facts"))
    return 1;
  const ir::TcgenTf32KnownFacts facts{
      .group = ir::TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction = ir::TcgenInstructionWord{0x910U | (4U << 24) | (1U << 17),
                                              ir::TcgenMmaKind::Tf32},
  };
  const auto report = ir::check_tcgen_tf32_known_operation(facts);
  if (!require(report.instruction_fields.defined_fields_ok() &&
                   std::find(report.missing.begin(), report.missing.end(),
                             ir::TcgenTf32Obligation::BSharedWord) !=
                       report.missing.end(),
               "known values do not claim opaque source descriptor bits"))
    return 1;
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %i;
  .reg .b64 %ad, %bd;
  .reg .f32 %mask<4>;
  .reg .pred %p;
  .shared .align 8 .b64 barrier;
  tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i,
      {%mask0, %mask1, %mask2, %mask3}, !%p, 15;
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
    auto result = ir::resolveAndValidateModule(*ast);
    if (!require(result.has_value(), "source resolves"))
      return 1;
    owned.emplace(std::move(*result));
  }
  const auto* instruction = owned->functions.front().body[0].get();
  if (!require(instruction != nullptr, "owned TCGEN instruction survives"))
    return 1;
  const auto* form = dynamic_cast<const ir::Tcgen05MmaTf32*>(instruction);
  const auto view = ir::tcgen_mma_tf32_view(*instruction);
  if (!require(
          form && view && view->group == ir::TcgenCtaGroup::One &&
              view->a_shared && !view->a_tmem &&
              view->a_shared->role == ir::MatrixFragmentRole::A &&
              view->b.role == ir::MatrixFragmentRole::B &&
              view->disable_output_lane &&
              view->disable_output_lane->elements.size() == 4 &&
              view->scale_d && view->scale_d->integer_source_bits == 15 &&
              std::holds_alternative<ir::ResolvedPredicate>(*view->enable_d) &&
              std::get<ir::ResolvedPredicate>(*view->enable_d).negated,
          "selected tf32 borrowed roles and source controls"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent module validation")
             ? 0
             : 1;
}
