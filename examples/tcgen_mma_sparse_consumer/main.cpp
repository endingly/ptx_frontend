#include <algorithm>
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

/** Report an installed API or source contract failure. */
bool require(bool value, std::string_view message) {
  if (!value)
    std::cerr << "TCGEN sparse consumer: " << message << '\n';
  return value;
}

}  // namespace

/** Exercise typed sparse rows, owned metadata and conditional query facts. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  if (!require(ir::tcgen_sparse_metadata_rules().size() == 4 &&
                   ir::tcgen_sparse_path_rows().size() == 4,
               "generated sparse metadata and path rows"))
    return 1;
  constexpr uint32_t word = (4U << 24) | (1U << 17) | (1U << 2);
  const ir::TcgenSparseKnownFacts facts{
      .source_kind = ir::TcgenMmaKind::F16,
      .group = ir::TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction = {word, ir::TcgenMmaKind::F16},
      .a_lane_half = 0,
      .d_lane_half = 0,
      .metadata_lane_half = 0,
      .metadata_nibbles = std::vector<uint8_t>{14},
  };
  const auto report = ir::check_tcgen_sparse_known_operation(facts);
  if (!require(report.supplied_facts_ok() && report.compressed_a_k == 16 &&
                   report.logical_b_k == 32 && report.path_layout == 'F',
               "known sparse shape and metadata"))
    return 1;
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i, %sp;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
  tcgen05.mma.sp.cta_group::1.kind::f16 [%d], [%a], %bd, [%sp], %i, %p;
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
  const auto* form = dynamic_cast<const ir::Tcgen05MmaSpF16*>(
      owned->functions.front().body.front().get());
  if (!require(form != nullptr, "exact owned sparse form"))
    return 1;
  const auto view = ir::tcgen_mma_sparse_view(*form);
  if (!require(view && view->a_tmem && view->metadata == &form->sp_meta.value,
               "borrowed metadata and placement"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent module check")
             ? 0
             : 1;
}
