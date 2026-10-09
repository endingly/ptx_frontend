#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Emit one failure from the installed frontend contract. */
bool require(bool value, std::string_view message) {
  if (!value)
    std::cerr << "TCGEN sparse MX consumer: " << message << '\n';
  return value;
}

}  // namespace

/** Exercise installed sparse MX source, owned view and generated row query. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  if (!require(ir::tcgen_sparse_mx_shape_rows().size() == 6 &&
                   ir::tcgen_sparse_mx_metadata_rules().size() == 3,
               "generated sparse MX rows"))
    return 1;
  constexpr uint32_t word =
      (2U << 27) | (4U << 17) | (1U << 23) | (1U << 7) | (1U << 10) | (1U << 2);
  const auto target = ptx_frontend::base::find_target_profile("sm_103a");
  if (!require(target.has_value(), "catalogued target"))
    return 1;
  const ir::TcgenSparseMxKnownFacts facts{
      .source_kind = ir::TcgenMmaKind::MxF4,
      .group = ir::TcgenCtaGroup::Two,
      .a_in_tmem = true,
      .scale_selector = ir::TcgenScaleVectorSize::Block32,
      .instruction = {word, ir::TcgenMmaKind::MxF4},
      .a_lane_half = 0,
      .d_lane_half = 0,
      .metadata_lane_half = 0,
      .metadata_nibbles = std::vector<uint8_t>{14},
      .target = target->identity,
      .ptx_version = ir::checker::PtxVersion{8, 8},
  };
  const auto report = ir::check_tcgen_sparse_mx_known_operation(facts);
  if (!require(report.supplied_facts_ok() && report.scale_a_factor_count == 2 &&
                   report.shape && report.shape->k == 128,
               "known sparse MX4 factors and K"))
    return 1;
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_103a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i, %sp, %sa, %sb;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
  tcgen05.mma.sp.cta_group::2.kind::mxf4.block_scale.block32
      [%d], [%a], %bd, [%sp], %i, [%sa], [%sb], %p;
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
  const auto* form = dynamic_cast<const ir::Tcgen05MmaSpMxf4*>(
      owned->functions.front().body.front().get());
  if (!require(form != nullptr, "exact sparse MX owned form"))
    return 1;
  const auto view = ir::tcgen_mma_sparse_mx_view(*form);
  if (!require(view && view->a_tmem && view->metadata == &form->sp_meta.value &&
                   view->scale_a == &form->scale_a.value,
               "borrowed metadata and scale roles"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent module check")
             ? 0
             : 1;
}
