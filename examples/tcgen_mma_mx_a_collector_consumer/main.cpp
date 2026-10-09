#include <iostream>
#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Report one installed MX collector-contract failure. */
bool require(bool value, std::string_view message) {
  if (!value)
    std::cerr << "TCGEN MX A collector consumer: " << message << '\n';
  return value;
}

}  // namespace

/** Exercise sparse MX source, owned controls and conditional history. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  const ir::TcgenMxACollectorKnownFacts known{
      .collector = {ir::TcgenCollectorBuffer::A,
                    ir::TcgenCollectorOp::LastUse}};
  const auto report = ir::check_tcgen_mx_a_collector_known_facts(known);
  if (!require(report.supplied_facts_ok() && !report.missing.empty(),
               "conditional known facts"))
    return 1;
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %meta, %i, %sa, %sb;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
  tcgen05.mma.sp.cta_group::1.kind::mxf4.block_scale.scale_vec::2X.collector::a::lastuse
      [%d], [%a], %bd, [%meta], %i, [%sa], [%sb], %p;
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
  const auto view =
      ir::tcgen_mma_mx_a_collector_view(*owned->functions.front().body.front());
  if (!require(
          view && view->sparse && view->a_in_tmem &&
              view->kind == ir::TcgenMmaKind::MxF4 &&
              view->collector->value.operation == ir::TcgenCollectorOp::LastUse,
          "owned source provenance"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent module check")
             ? 0
             : 1;
}
