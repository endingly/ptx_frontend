#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tensor_cache_controls.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ir = ptx_frontend::resolved_ir;

/** Return false with an actionable message for the installed smoke test. */
bool require(bool condition, const char* message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

/** Validate the installed selected-cache query after both source owners die. */
int main() {
  const std::string source = R"ptx(
.version 9.3
.target sm_110a
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 16 .b8 dst[1024];
.shared .align 8 .b64 mbar;
.entry kernel() {
  .reg .s32 %r0;
  .reg .b64 %policy;
  cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::bytes.L2::cache_hint
      [dst], [tensor_map, {%r0}], [mbar], %policy;
}
)ptx";

  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto parsed = parser.parseModule();
    if (!require(parsed.has_value(), "cache consumer parse failed"))
      return 1;
    auto resolved = ir::resolveModuleOnly(*parsed);
    if (!require(resolved.has_value(), "cache consumer resolve failed"))
      return 2;
    owned.emplace(std::move(*resolved));
  }

  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "cache consumer owned module validation failed"))
    return 3;
  const auto* copy = owned->functions.front().body.front().get();
  if (!require(copy != nullptr, "cache consumer did not select an instruction"))
    return 4;
  auto selected = ir::query_tensor_cache_controls(*copy);
  if (!require(selected.applicable && selected.hint.has_value() &&
                   selected.policy.has_value() && selected.diagnostics.empty(),
               "cache consumer selected state was incomplete"))
    return 5;
  if (!require(std::holds_alternative<ir::ResolvedRegisterRef>(
                   selected.policy->value) &&
                   std::get<ir::ResolvedRegisterRef>(selected.policy->value)
                           .spelling == "%policy",
               "cache consumer lost the authored policy source"))
    return 6;
  owned.reset();
  if (!require(selected.hint.has_value() &&
                   std::get<ir::ResolvedRegisterRef>(selected.policy->value)
                           .spelling == "%policy",
               "cache query borrowed parser or module storage"))
    return 7;
  std::cout << "owned tensor cache-control query passed\n";
}
