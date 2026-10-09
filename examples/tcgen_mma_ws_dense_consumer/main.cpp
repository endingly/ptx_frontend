#include <iostream>
#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Report one installed WS contract failure without decoding runtime words. */
bool require(bool valid, std::string_view message) {
  if (!valid)
    std::cerr << "TCGEN WS consumer: " << message << '\n';
  return valid;
}

}  // namespace

/** Exercise installed source, owned WS view and conditional B-history query. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i;
  .reg .b64 %bd, %z;
  .reg .pred %p;
  tcgen05.mma.ws.cta_group::1.kind::f16.collector::b1::use
      [%d], [%a], %bd, %i, %p, %z;
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    if (!require(ast.has_value() && ast.diagnostics.empty(), "source parses"))
      return 1;
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!require(resolved.has_value(), "source resolves"))
      return 1;
    owned.emplace(std::move(*resolved));
  }
  const auto view =
      ir::tcgen_mma_ws_view(*owned->functions.front().body.front());
  if (!require(
          view && view->zero_column && view->a_tmem &&
              view->collector->value.buffer == ir::TcgenCollectorBuffer::B1,
          "owned WS operands"))
    return 1;
  if (!require(ir::validateModule(*owned).has_value(), "AST-death validation"))
    return 1;
  ir::TcgenWsKnownFacts facts{
      .source_kind = ir::TcgenMmaKind::F16,
      .group = ir::TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction = ir::TcgenInstructionWord{(2U << 24) | (8U << 17),
                                              ir::TcgenMmaKind::F16},
      .a_lane_half = 0,
      .d_lane_half = 0,
      .zero_column_operand_present = true,
      .zero_column_word = ir::TcgenZeroColumnWord{0},
      .collector = {ir::TcgenCollectorBuffer::B1, ir::TcgenCollectorOp::Use},
  };
  const auto report = ir::check_tcgen_ws_known_operation(facts);
  return require(report.supplied_facts_ok() && !report.missing.empty(),
                 "conditional WS report")
             ? 0
             : 1;
}
