#include <iostream>
#include <optional>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Report a failed installed-package contract even in optimized builds. */
bool require(bool result, std::string_view contract) {
  if (!result)
    std::cerr << "Tensor Memory copy/shift consumer: " << contract << '\n';
  return result;
}

}  // namespace

/** Exercise typed copy/shift and completion metadata after AST destruction. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  constexpr std::string_view source = R"ptx(
.version 9.0
.target sm_110a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %taddr;
  .reg .b64 %descriptor;
  .shared .align 8 .b64 barrier;
  tcgen05.cp.cta_group::1.64x128b.warpx2::02_13.b8x16.b6x16_p32 [%taddr], %descriptor;
  tcgen05.shift.down.cta_group::1 [%taddr];
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
  if (!require(owned->functions.size() == 1 &&
                   owned->functions.front().body.size() >= 3,
               "owned forms survive AST release"))
    return 1;
  const auto* copy_record =
      owned->functions.front().body[0].get_if<ir::Tcgen05>();
  const auto* shift_record =
      owned->functions.front().body[1].get_if<ir::Tcgen05>();
  const auto* commit_record =
      owned->functions.front().body[2].get_if<ir::Tcgen05>();
  if (!require(copy_record && shift_record && commit_record,
               "all actions share the typed TCGEN root"))
    return 1;
  const auto* copy = std::get_if<ir::Tcgen05::Cp>(&copy_record->variant);
  const auto* shift = std::get_if<ir::Tcgen05::Shift>(&shift_record->variant);
  if (!require(
          copy && shift && copy->descriptor_view() &&
              copy->descriptor_view()->source == &copy->s_desc.value &&
              copy->multicast_view() == ir::TcgenCopyMulticast::WarpX2_02_13 &&
              copy->format_view() == ir::TcgenCopyFormat::B6x16P32 &&
              !copy->dst_format.locs.empty() && !copy->src_b6.locs.empty() &&
              !shift->down.locs.empty() &&
              copy->completion_kind == ptx_frontend::base::AsyncCompletionKind::
                                           TcgenMbarrierArriveOne &&
              shift->completion_kind == copy->completion_kind,
          "typed roles and both written format locations remain stable"))
    return 1;
  if (!require(ir::Tcgen05::get_resolved_descriptor().variants.size() == 26,
               "parent forms plus two compact actions are installed"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent module validation succeeds")
             ? 0
             : 1;
}
