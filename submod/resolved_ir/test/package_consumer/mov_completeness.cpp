#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise the installed MOV predicate and pack/unpack contracts. */
int main() {
  using namespace ptx_frontend;
  using namespace resolved_ir;

  PtxSyntaxParser predicate_parser("mov.pred %p1, !%is_explicit_cluster;");
  const auto predicate_ast = predicate_parser.parseInstruction();
  if (!predicate_ast || !predicate_ast.diagnostics.empty())
    return 1;
  const auto predicate = resolve<Mov>(*predicate_ast);
  if (!predicate)
    return 2;
  const auto* pred = std::get_if<Mov::Pred>(&predicate->variant);
  if (pred == nullptr)
    return 3;
  const auto* predicate_source =
      std::get_if<ResolvedPredicateSpecialRegister>(&pred->src.value);
  if (predicate_source == nullptr || !predicate_source->negated ||
      pred->dst.value.negated) {
    return 4;
  }

  constexpr std::pair<std::string_view, bool> constants[] = {
      {"mov.pred %p1, 0;", false},  {"mov.pred %p1, 2;", true},
      {"mov.pred %p1, -1;", true},  {"mov.pred %p1, !0;", true},
      {"mov.pred %p1, !1;", false}, {"mov.pred %p1, !-1;", false},
  };
  for (const auto& [source, expected] : constants) {
    PtxSyntaxParser constant_parser(source);
    const auto constant_ast = constant_parser.parseInstruction();
    if (!constant_ast || !constant_ast.diagnostics.empty())
      return 5;
    const auto constant_mov = resolve<Mov>(*constant_ast);
    if (!constant_mov)
      return 6;
    const auto* constant_variant =
        std::get_if<Mov::Pred>(&constant_mov->variant);
    if (constant_variant == nullptr)
      return 7;
    const auto* constant =
        std::get_if<ResolvedPredicateConstant>(&constant_variant->src.value);
    if (constant == nullptr || constant->value != expected)
      return 8;
  }

  PtxSyntaxParser pack_parser("mov.b128 %b0, {%rd0, %rd1};");
  const auto pack_ast = pack_parser.parseInstruction();
  if (!pack_ast || !pack_ast.diagnostics.empty())
    return 9;
  const auto pack = resolve<Mov>(*pack_ast);
  if (!pack)
    return 10;
  const auto* pack_unpack = std::get_if<Mov::B128PackUnpack>(&pack->variant);
  if (pack_unpack == nullptr || pack_unpack->type != base::ScalarType::B128 ||
      !std::holds_alternative<Mov::B128PackUnpack::PackOperands>(
          pack_unpack->operands)) {
    return 11;
  }
  const auto checked = checker::check(
      *pack,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 90},
                       .instruction_range = pack_ast->range});
  return checked ? 0 : 12;
}
