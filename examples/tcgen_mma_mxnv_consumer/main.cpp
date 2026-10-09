#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Report one installed-contract failure with a nonzero process result. */
bool require(bool value, std::string_view message) {
  if (!value)
    std::cerr << "TCGEN MX NV consumer: " << message << '\n';
  return value;
}

}  // namespace

/** Exercise source ownership and caller-known scale rows from an install. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  const auto rows = ir::tcgen_mx_scale_layout_rows();
  if (!require(
          ir::tcgen_mxnv_shape_rows().size() == 3 &&
              std::count_if(rows.begin(), rows.end(),
                            [](const auto& row) {
                              return !row.sparse &&
                                     row.kind == ir::TcgenMmaKind::MxF4NvF4;
                            }) == 12 &&
              std::count_if(rows.begin(), rows.end(),
                            [](const auto& row) {
                              return row.sparse &&
                                     row.kind == ir::TcgenMmaKind::MxF4NvF4;
                            }) == 8,
          "generated Table 42 and Tables 59/60 rows"))
    return 1;
  constexpr uint32_t word = (1U << 27) | (2U << 17) | (1U << 7) | (1U << 10);
  const ir::TcgenMxNvKnownFacts facts{
      .group = ir::TcgenCtaGroup::One,
      .a_in_tmem = false,
      .scale_selector = ir::TcgenScaleVectorSize::Block16,
      .instruction = {word, ir::TcgenMmaKind::MxF4NvF4},
      .scale_a_facts =
          ir::TcgenMxScaleRoleFacts{ir::TcgenMxScaleLayoutId::Mx4, 4},
      .scale_b_facts =
          ir::TcgenMxScaleRoleFacts{ir::TcgenMxScaleLayoutId::FourXN, 4},
  };
  const auto report = ir::check_tcgen_mxnv_known_operation(facts);
  if (!require(report.supplied_facts_ok() && report.scale_a_layout &&
                   report.scale_b_layout &&
                   report.scale_a_layout->selector ==
                       ir::TcgenScaleVectorSize::Block16 &&
                   report.scale_a_factor_count == 4,
               "known Table 47 and scale-row query"))
    return 1;
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100f
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %i, %sa, %sb;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
  tcgen05.mma.cta_group::1.kind::mxf4nvf4.block_scale.block16
      [%d], %ad, %bd, %i, [%sa], [%sb], !%p;
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
  const auto* form = dynamic_cast<const ir::Tcgen05MmaMxf4nvf4*>(
      owned->functions.front().body.front().get());
  if (!require(form != nullptr, "exact owned MX NV form"))
    return 1;
  const auto view = ir::tcgen_mma_mxnv_view(*form);
  if (!require(view && view->a_shared && view->scale_a && view->scale_b &&
                   view->scale_selector->value ==
                       ir::TcgenScaleVectorSize::Block16 &&
                   view->scale_selector->locs.size() == 1,
               "owned scale roles and written selector provenance"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent module check")
             ? 0
             : 1;
}
