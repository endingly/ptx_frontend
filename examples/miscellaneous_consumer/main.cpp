#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

namespace ir = ptx_frontend::resolved_ir;

/** Complete source whose instruction records must outlive parser and AST. */
constexpr std::string_view kModule = R"ptx(
.version 9.3
.target sm_90a
.entry kernel() {
  .reg .u32 %r;
  brkpt;
  nanosleep.u32 %r;
  nanosleep.u32 42;
  nanosleep.u32 -1;
  nanosleep.u32 4294967296;
  pmevent 15;
  pmevent.mask 0;
  trap;
  setmaxnreg.inc.sync.aligned.u32 192;
  setmaxnreg.dec.sync.aligned.u32 64;
}
)ptx";

/** Report one public-contract check in both Debug and Release builds. */
bool require(bool condition, std::string_view description) {
  if (!condition)
    std::cerr << "miscellaneous consumer failed: " << description << '\n';
  return condition;
}

}  // namespace

/** Exercise the installed public resolved IR and owned validation contract. */
int main() {
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{kModule};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "module parses"))
      return 1;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "module resolves"))
      return 1;
    owned.emplace(std::move(*resolved));
  }

  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned module validates after AST destruction"))
    return 1;
  auto& body = owned->functions.front().body;
  if (!require(body.size() == 10, "all family forms are retained"))
    return 1;

  const auto* narrowed = body[4].get_if<ir::Nanosleep>();
  const auto* duration =
      narrowed ? std::get_if<ir::Nanosleep::U32>(&narrowed->variant) : nullptr;
  const auto* integer =
      duration ? std::get_if<ir::ResolvedImmediate>(&duration->t.value)
               : nullptr;
  if (!require(integer && integer->bits == 0 &&
                   integer->integer_source_bits == 0x100000000ULL,
               "integer duration narrows at its u32 use"))
    return 1;

  const auto* event = body[6].get_if<ir::Pmevent>();
  const auto* mask =
      event ? std::get_if<ir::Pmevent::Mask>(&event->variant) : nullptr;
  if (!require(mask && mask->a.value.bits == 0,
               "zero event mask keeps its own variant"))
    return 1;
  const auto* count = body[9].get_if<ir::Setmaxnreg>();
  const auto* decrease =
      count ? std::get_if<ir::Setmaxnreg::DecSyncAlignedU32>(&count->variant)
            : nullptr;
  if (!require(decrease && decrease->count.value.bits == 64,
               "decreasing register count is typed"))
    return 1;

  auto* mutable_event = body[5].get_if<ir::Pmevent>();
  auto* index = mutable_event
                    ? std::get_if<ir::Pmevent::Index>(&mutable_event->variant)
                    : nullptr;
  if (!require(index != nullptr, "event index variant exists"))
    return 1;
  index->a.value.bits = 16;
  if (!require(!ir::validateModule(
                    *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                    .has_value(),
               "rechecking owned metadata rejects mismatched event bits"))
    return 1;
  index->a.value.integer_source_bits = 16;
  if (!require(!ir::validateModule(
                    *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                    .has_value(),
               "rechecking owned metadata rejects invalid event index"))
    return 1;
  return 0;
}
