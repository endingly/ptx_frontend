#include <stdexcept>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Consume installed extended-precision CC.CF variants without source dispatch. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  // Resolution owns everything needed after parser and AST destruction.
  auto instruction = [] {
    ptx_frontend::PtxSyntaxParser parser("@!%p0 subc.cc.u64 %rd0, %rd1, 1;");
    const auto ast = parser.parseInstruction();
    if (!ast || !ast.diagnostics.empty())
      throw std::runtime_error("carry instruction failed to parse");
    return ir::resolve<ir::Subc>(*ast);
  }();
  if (!instruction || !instruction->execution_predicate ||
      !instruction->execution_predicate->value.negated)
    return 2;
  auto* variant = std::get_if<ir::Subc::Cc64>(&instruction->variant);
  if (!variant ||
      variant->condition_code_effect != ir::ConditionCodeEffect::BorrowInOut)
    return 3;
  if (ir::Subc::get_resolved_descriptor()
          .variants[instruction->variant.index()]
          .condition_code_effect != variant->condition_code_effect)
    return 4;
  auto mad = [] {
    ptx_frontend::PtxSyntaxParser parser(
        "@!%p0 mad.hi.cc.u64 %rd0, %rd1, 7, 9;");
    const auto ast = parser.parseInstruction();
    if (!ast || !ast.diagnostics.empty())
      throw std::runtime_error("mad carry instruction failed to parse");
    return ir::resolve<ir::Mad>(*ast);
  }();
  const auto* mad_variant =
      mad ? std::get_if<ir::Mad::HiCc64>(&mad->variant) : nullptr;
  if (!mad_variant ||
      mad_variant->condition_code_effect != ir::ConditionCodeEffect::CarryOut)
    return 5;
  auto madc = [] {
    ptx_frontend::PtxSyntaxParser parser(
        "@!%p0 madc.lo.cc.u64 %rd0, %rd1, 7, 9;");
    const auto ast = parser.parseInstruction();
    if (!ast || !ast.diagnostics.empty())
      throw std::runtime_error("madc carry instruction failed to parse");
    return ir::resolve<ir::Madc>(*ast);
  }();
  const auto* madc_variant =
      madc ? std::get_if<ir::Madc::LoCc64>(&madc->variant) : nullptr;
  if (!madc_variant || madc_variant->condition_code_effect !=
                           ir::ConditionCodeEffect::CarryInOut)
    return 6;
  const ir::checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  if (!ir::checker::check(*instruction, context))
    return 7;
  if (!ir::checker::check(*mad, context) || !ir::checker::check(*madc, context))
    return 8;
  variant->type.value = ptx_frontend::base::ScalarType::U32;
  return ir::checker::check(*instruction, context) ? 9 : 0;
}
