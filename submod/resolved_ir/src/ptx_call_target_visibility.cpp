#include "ptx_call_target_visibility.hpp"

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>

#include <tuple>

namespace ptx_frontend::resolved_ir::detail {
namespace {
/** Compare source positions without deriving declaration order from coordinates. */
bool before_or_equal(SourcePos left, SourcePos right) {
  return std::tie(left.line, left.column) <= std::tie(right.line, right.column);
}
/** Require a positive, nonempty target range inside its owned instruction range. */
bool valid_use_range(SourceRange use, SourceRange owner) {
  return use.start.line > 0 && use.start.column > 0 && use.end.line > 0 &&
         use.end.column > 0 && owner.start.line > 0 && owner.start.column > 0 &&
         owner.end.line > 0 && owner.end.column > 0 && use.start != use.end &&
         before_or_equal(use.start, use.end) &&
         before_or_equal(owner.start, use.start) &&
         before_or_equal(use.end, owner.end);
}
/** Capture direct target identity and locations without retaining callback borrows. */
struct CallTargetUse final : IReferenceObserver {
  /** Copied semantic identity; spelling is diagnostic provenance only. */
  std::optional<ResolvedFunctionRef> target;
  /** Owned exact use locations, independent of parser/AST lifetime. */
  std::vector<SourceRange> locations;
  /** Selected layout must expose exactly one direct or one indirect target. */
  size_t direct_count{};
  bool indirect{};
  /** Copy the actual bound device function and its use ranges. */
  void function_ref(const ResolvedFunctionRef& value,
                    std::span<const SourceRange> ranges,
                    checker::AddressSymbolResolutionPolicy) override {
    ++direct_count;
    target = value;
    locations.assign(ranges.begin(), ranges.end());
  }
  /** Preserve indirect calls' separate metadata/register/table contract. */
  void indirect_callee(const ResolvedIndirectCallee&,
                       std::span<const SourceRange>,
                       checker::AddressSymbolResolutionPolicy) override {
    indirect = true;
  }
};
}  // namespace

ModuleResolveDiagnostics check_direct_call_visibility(
    const ResolvedModule& module) {
  ModuleResolveDiagnostics diagnostics;
  for (const auto& function : module.functions) {
    for (size_t index = 0; index < function.body.size(); ++index) {
      const auto* call =
          dynamic_cast<const CallDirect*>(function.body[index].get());
      if (!call)
        continue;
      const auto owner = index < function.instruction_ranges.size()
                             ? function.instruction_ranges[index]
                             : SourceRange{};
      CallTargetUse use;
      call->visit_references(use);
      if (use.direct_count == 0 && use.indirect)
        continue;
      if (use.direct_count != 1 || !use.target || use.indirect) {
        diagnostics.push_back(
            {.range = owner,
             .message = "Call target has no valid selected layout."});
        continue;
      }
      if (use.locations.size() != 1 ||
          !valid_use_range(use.locations.front(), owner)) {
        diagnostics.push_back(
            {.range = owner,
             .message = "A direct call target requires one nonempty location "
                        "inside its instruction."});
        continue;
      }
      const auto range = use.locations.front();
      if (!use.target->symbol_id ||
          use.target->symbol_id->value >= module.symbols.symbols().size()) {
        diagnostics.push_back(
            {.range = range,
             .message =
                 "A direct call target must name a bound device function."});
        continue;
      }
      const auto& symbol = module.symbols.symbol(*use.target->symbol_id);
      if (symbol.kind != binding::SymbolKind::Function ||
          symbol.function_is_entry) {
        diagnostics.push_back(
            {.range = range,
             .message =
                 "A direct call target must name a bound device function."});
        continue;
      }
      const auto prior = module.symbols.hasPriorDeclaration(symbol.id, range);
      if (!prior || !*prior)
        diagnostics.push_back(
            {.range = range,
             .message = prior ? "A device function must be declared or defined "
                                "before its direct call."
                              : "Direct call target declaration order has no "
                                "unambiguous lexical context."});
    }
  }
  return diagnostics;
}
}  // namespace ptx_frontend::resolved_ir::detail
