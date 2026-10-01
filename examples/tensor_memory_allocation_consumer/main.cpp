#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Complete allocation module with two independently selected CTA groups. */
constexpr std::string_view kSource = R"ptx(
.version 9.0
.target sm_110a
.address_size 64
.entry first() {
  .shared .align 4 .b32 slot;
  .reg .b32 %taddr;
  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 [slot], 32;
  ld.shared.b32 %taddr, [slot];
  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %taddr, 32;
  tcgen05.relinquish_alloc_permit.cta_group::1.sync.aligned;
  ret;
}
.entry second() {
  .shared .align 4 .b32 slot;
  tcgen05.alloc.cta_group::2.sync.aligned.b32 [slot], 64;
  ret;
}
)ptx";

/** Print a failed installed-package contract in any build configuration. */
bool require(bool condition, std::string_view contract) {
  if (!condition)
    std::cerr << "Tensor Memory consumer: " << contract << '\n';
  return condition;
}

}  // namespace

/** Validate owned allocation forms after releasing their source syntax tree. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{kSource};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "module parses"))
      return 1;
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!require(resolved.has_value(), "module resolves and validates"))
      return 1;
    owned.emplace(std::move(*resolved));
  }
  if (!require(owned->functions.size() == 2 &&
                   owned->functions.front().body.size() >= 3,
               "both function bodies survive AST release"))
    return 1;
  auto* allocation =
      owned->functions.front().body.front().get_if<ir::Tcgen05>();
  if (!require(allocation != nullptr, "allocation has a typed opcode"))
    return 1;
  auto* selected =
      std::get_if<ir::Tcgen05::AllocSharedCta>(&allocation->variant);
  if (!require(
          selected != nullptr &&
              selected->allocation_action == ir::TcgenAllocationAction::Alloc &&
              selected->cta_group.value == ir::TcgenCtaGroup::One &&
              std::holds_alternative<ir::ResolvedImmediate>(
                  selected->ncols.value),
          "allocation retains action, group, and column source"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent validation retains per-body groups")
             ? 0
             : 1;
}
