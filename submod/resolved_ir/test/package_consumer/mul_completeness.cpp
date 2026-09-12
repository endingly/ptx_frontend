#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise complete MUL contracts through installed public frontend headers. */
int main() {
  using namespace ptx_frontend;
  using namespace resolved_ir;

  PtxSyntaxParser scalar_parser("mul.wide.s16 %r0, %h1, -7;");
  const auto scalar_ast = scalar_parser.parseInstruction();
  if (!scalar_ast || !scalar_ast.diagnostics.empty())
    return 1;
  const auto scalar = resolve<Mul>(*scalar_ast);
  if (!scalar)
    return 2;
  const auto* wide_s16 = std::get_if<Mul::WideS16>(&scalar->variant);
  if (wide_s16 == nullptr || !Mul::WideS16::wide ||
      Mul::WideS16::type != base::ScalarType::S16 ||
      !std::holds_alternative<ResolvedImmediate>(wide_s16->src2.value)) {
    return 3;
  }
  const auto scalar_checked = checker::check(
      *scalar,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                       .instruction_range = scalar_ast->range});
  if (!scalar_checked)
    return 4;

  PtxSyntaxParser packed_parser(R"ptx(
.version 8.6
.target sm_100
.entry kernel() {
  .reg .b64 %r<3>;
  mul.f32x2 %r0, %r1, %r2;
}
)ptx");
  const auto packed_ast = packed_parser.parseModule();
  if (!packed_ast || !packed_ast.diagnostics.empty())
    return 5;
  auto packed = resolveModule(*packed_ast);
  if (!packed || packed->functions.size() != 1 ||
      packed->functions.front().body.size() != 1) {
    return 6;
  }
  auto& packed_mul = std::get<Mul::F32x2>(
      std::get<Mul>(packed->functions.front().body.front()).variant);
  if (packed_mul.dst.value.declared_type != base::ScalarType::B64)
    return 7;
  packed_mul.dst.value.declared_type = base::ScalarType::B32;
  const auto packed_checked = checker::check(
      std::get<Mul>(packed->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {8, 6}, .sm_version = 100},
                       .instruction_range = packed_ast->range});
  if (packed_checked || packed_checked.error().empty() ||
      packed_checked.error().front().kind !=
          checker::CheckDiagnosticKind::OperandTypeMismatch) {
    return 8;
  }

  PtxSyntaxParser invalid_packed_parser(R"ptx(
.version 8.6
.target sm_100
.entry kernel() {
  .reg .b32 %r<3>;
  mul.f32x2 %r0, %r1, %r2;
}
)ptx");
  const auto invalid_packed_ast = invalid_packed_parser.parseModule();
  if (!invalid_packed_ast || !invalid_packed_ast.diagnostics.empty())
    return 9;
  if (resolveModule(*invalid_packed_ast))
    return 10;
  return 0;
}
