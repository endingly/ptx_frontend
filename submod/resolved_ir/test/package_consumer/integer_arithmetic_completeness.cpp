#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise newly exported integer-arithmetic variants through installed headers. */
int main() {
  using namespace ptx_frontend;
  using namespace resolved_ir;

  PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .u16 %h<3>;
  .reg .u32 %u<4>;
  .reg .s32 %s<4>;
  .reg .u64 %rd<4>;
  .reg .b32 %b<2>;
  mad.wide.s16 %s0, %h1, %h2, %s1;
  clmad.lo.u64 %rd0, %rd1, %rd2, %rd3;
  fns.b32 %b0, %b1, %u0, %s0;
  dp4a.u32.s32 %s0, %u1, %s1, %s2;
  dp2a.hi.s32.u32 %s0, %s1, %u2, %s2;
}
)ptx");
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return 1;
  const auto module = resolveModule(*ast);
  if (!module || module->functions.size() != 1 ||
      module->functions.front().body.size() != 5)
    return 2;
  const auto& body = module->functions.front().body;
  if (!std::holds_alternative<Mad::WideS16>(std::get<Mad>(body[0]).variant) ||
      !std::holds_alternative<Clmad::LoU64>(std::get<Clmad>(body[1]).variant) ||
      !std::holds_alternative<Fns::B32>(std::get<Fns>(body[2]).variant) ||
      !std::holds_alternative<Dp4a::U32S32>(std::get<Dp4a>(body[3]).variant) ||
      !std::holds_alternative<Dp2a::HiS32U32>(std::get<Dp2a>(body[4]).variant))
    return 3;
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
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
