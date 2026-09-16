#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise expanded bit-operation forms through installed public headers. */
int main() {
  using namespace ptx_frontend;
  using namespace resolved_ir;

  PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .u32 %r<8>;
  .reg .u64 %rd<6>;
  popc.b64 %r0, 1;
  bfind.shiftamt.s64 %r1, -1;
  bfe.s64 %rd2, -1, %r2, %r3;
  bfi.b64 %rd4, 1, 2, %r4, %r5;
  brev.b64 %rd5, 1;
}
)ptx");
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return 1;
  const auto module = resolveModule(*ast);
  if (!module || module->functions.size() != 1 ||
      module->functions.front().body.size() != 5) {
    return 2;
  }

  const auto& body = module->functions.front().body;
  if (!std::holds_alternative<Popc::B64>(std::get<Popc>(body[0]).variant) ||
      !std::holds_alternative<Bfind::ShiftamtS64>(
          std::get<Bfind>(body[1]).variant) ||
      !std::holds_alternative<Bfe::S64>(std::get<Bfe>(body[2]).variant) ||
      !std::holds_alternative<Bfi::B64>(std::get<Bfi>(body[3]).variant) ||
      !std::holds_alternative<Brev::B64>(std::get<Brev>(body[4]).variant)) {
    return 3;
  }

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
      .instruction_range = ast->range,
  };
  for (const auto& instruction : body) {
    const auto checked = std::visit(
        [&context](const auto& concrete) {
          return checker::check(concrete, context);
        },
        instruction);
    if (!checked)
      return 4;
  }
  return 0;
}
