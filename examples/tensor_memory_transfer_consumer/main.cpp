#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Build the 128-register source fragment without hiding its cardinality. */
std::string fragment128() {
  std::string result = "{";
  for (unsigned lane = 0; lane < 128; ++lane) {
    if (lane)
      result += ", ";
    result += "%r" + std::to_string(lane);
  }
  return result + "}";
}

/** Emit one complete target-qualified module for installed-package checking. */
std::string source() {
  return std::string(R"ptx(
.version 9.0
.target sm_110a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %r<128>;
  .reg .b32 %taddr;
  .reg .u32 %red;
  tcgen05.ld.sync.aligned.16x32bx2.x128.b32 )ptx") +
         fragment128() +
         R"ptx(, [%taddr], 0x100000010U;
  tcgen05.wait::ld.sync.aligned;
  tcgen05.ld.red.sync.aligned.32x32b.x2.min.u32 {%r0, %r1}, %red, [%taddr];
  tcgen05.wait::ld.sync.aligned;
  tcgen05.st.sync.aligned.32x32b.x2.b32 [%taddr], {%r0, %r1};
  tcgen05.wait::st.sync.aligned;
  ret;
}
)ptx";
}

/** Print an installed-package contract failure in every build configuration. */
bool require(bool condition, std::string_view contract) {
  if (!condition)
    std::cerr << "Tensor Memory transfer consumer: " << contract << '\n';
  return condition;
}

}  // namespace

/** Validate owned transfer values after the source AST has been destroyed. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  std::optional<ir::ResolvedModule> owned;
  {
    const std::string module_source = source();
    ptx_frontend::PtxSyntaxParser parser{module_source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value() && ast.diagnostics.empty(), "source parses"))
      return 1;
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!require(resolved.has_value(), "source resolves and validates"))
      return 1;
    owned.emplace(std::move(*resolved));
  }
  if (!require(owned->functions.size() == 1 &&
                   owned->functions.front().body.size() == 7,
               "six typed transfers survive AST release"))
    return 1;
  auto* transfer = owned->functions.front().body[0].get_if<ir::Tcgen05>();
  if (!require(transfer != nullptr, "load has a typed opcode"))
    return 1;
  auto* load = std::get_if<ir::Tcgen05::LdSplit>(&transfer->variant);
  if (!require(load != nullptr && load->r.value.elements.size() == 128 &&
                   load->completion_kind ==
                       ptx_frontend::base::AsyncCompletionKind::TcgenLoadWait &&
                   load->splitoff.value.source_bits == 0x100000010ULL &&
                   load->splitoff.value.source_kind ==
                       ir::TcgenIntegerSourceKind::Unsigned,
               "128-lane split load and source-only offset are owned"))
    return 1;
  auto* reduction = owned->functions.front().body[2].get_if<ir::Tcgen05>();
  if (!require(reduction != nullptr &&
                   std::holds_alternative<ir::Tcgen05::LdRedInteger>(
                       reduction->variant),
               "reduction remains a distinct typed form"))
    return 1;
  auto* store_wait = owned->functions.front().body[5].get_if<ir::Tcgen05>();
  if (!require(
          store_wait != nullptr &&
              std::holds_alternative<ir::Tcgen05::WaitSt>(store_wait->variant),
          "store wait retains its own completion class"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent module validation passes")
             ? 0
             : 1;
}
