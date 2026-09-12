#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise complete SETP contracts through installed public frontend headers. */
int main() {
  using namespace ptx_frontend;
  using namespace resolved_ir;

  PtxSyntaxParser float_parser("setp.nan.ftz.f32 %p0, %f0, %f1;");
  const auto float_ast = float_parser.parseInstruction();
  if (!float_ast || !float_ast.diagnostics.empty())
    return 1;
  const auto float_setp = resolve<Setp>(*float_ast);
  if (!float_setp)
    return 2;
  const auto* float_variant = std::get_if<Setp::Float>(&float_setp->variant);
  if (float_variant == nullptr ||
      float_variant->comparison.value != base::ComparisonOperator::Nan ||
      !float_variant->ftz.value || Setp::Float::type != base::ScalarType::F32) {
    return 3;
  }

  PtxSyntaxParser sink_parser("setp.eq.xor.u32 _|%p1, %r0, %r1, !%p2;");
  const auto sink_ast = sink_parser.parseInstruction();
  if (!sink_ast || !sink_ast.diagnostics.empty())
    return 4;
  auto sink_setp = resolve<Setp>(*sink_ast);
  if (!sink_setp)
    return 5;
  auto* sink_variant = std::get_if<Setp::UnsignedBoolean>(&sink_setp->variant);
  if (sink_variant == nullptr ||
      !std::holds_alternative<Setp::UnsignedBoolean::PairOperands>(
          sink_variant->operands)) {
    return 6;
  }
  const auto& sink_operands =
      std::get<Setp::UnsignedBoolean::PairOperands>(sink_variant->operands);
  if (sink_operands.dst.value.first.has_value() ||
      !sink_operands.dst.value.second.has_value()) {
    return 7;
  }

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
      .instruction_range = sink_ast->range,
  };
  if (!checker::check(*sink_setp, context))
    return 8;

  PtxSyntaxParser constant_parser("setp.eq.and.u32 %p0, %r0, %r1, !0;");
  const auto constant_ast = constant_parser.parseInstruction();
  if (!constant_ast || !constant_ast.diagnostics.empty())
    return 9;
  const auto constant_setp = resolve<Setp>(*constant_ast);
  if (!constant_setp)
    return 10;
  const auto* constant_variant =
      std::get_if<Setp::UnsignedBoolean>(&constant_setp->variant);
  if (constant_variant == nullptr ||
      !std::holds_alternative<Setp::UnsignedBoolean::SingleOperands>(
          constant_variant->operands))
    return 11;
  const auto& constant_operands =
      std::get<Setp::UnsignedBoolean::SingleOperands>(
          constant_variant->operands);
  const auto* constant =
      std::get_if<ResolvedPredicateConstant>(&constant_operands.combine.value);
  if (constant == nullptr || !constant->value)
    return 12;

  sink_variant->comparison.value = base::ComparisonOperator::Nan;
  const auto invalid_domain = checker::check(*sink_setp, context);
  if (invalid_domain || invalid_domain.error().empty() ||
      invalid_domain.error().front().kind !=
          checker::CheckDiagnosticKind::ModifierValueDomainMismatch) {
    return 13;
  }
  return 0;
}
