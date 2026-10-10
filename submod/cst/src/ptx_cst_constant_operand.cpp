#include <ptx_frontend/cst/ptx_cst.hpp>
#include <type_traits>

namespace ptx_frontend::syntax_cst {
namespace {
/** Clone a parser-bounded syntax tree without sharing owned children. */
std::unique_ptr<CstConstantExpression> clone(
    const CstConstantExpression& value) {
  auto node = std::visit(
      [](const auto& item) -> CstConstantExpressionNode {
        using T = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::is_same_v<T, CstConstantLiteral> ||
                      std::is_same_v<T, CstConstantSymbol>)
          return item;
        else if constexpr (std::is_same_v<T, CstConstantParenthesized>)
          return T{item.left_paren, clone(*item.expression), item.right_paren};
        else if constexpr (std::is_same_v<T, CstConstantCall>)
          return T{clone(*item.callee), item.left_paren, clone(*item.argument),
                   item.right_paren};
        else if constexpr (std::is_same_v<T, CstConstantCast>)
          return T{item.left_paren, item.type, item.right_paren,
                   clone(*item.operand)};
        else if constexpr (std::is_same_v<T, CstConstantUnary>)
          return T{item.operator_token, clone(*item.operand)};
        else if constexpr (std::is_same_v<T, CstConstantBinary>)
          return T{clone(*item.left), item.operator_token, clone(*item.right)};
        else
          return T{clone(*item.condition), item.question,
                   clone(*item.true_expression), item.colon,
                   clone(*item.false_expression)};
      },
      value.node);
  return std::make_unique<CstConstantExpression>(
      CstConstantExpression{std::move(node), value.token_range});
}
}  // namespace
CstConstantOperand::CstConstantOperand(
    std::unique_ptr<CstConstantExpression> value, CstTokenRange range)
    : expression(std::move(value)), token_range(range) {}
CstConstantOperand::CstConstantOperand(const CstConstantOperand& other)
    : expression(other.expression ? clone(*other.expression) : nullptr),
      token_range(other.token_range) {}
CstConstantOperand& CstConstantOperand::operator=(
    const CstConstantOperand& other) {
  if (this != &other) {
    auto copy = other.expression ? clone(*other.expression) : nullptr;
    expression = std::move(copy);
    token_range = other.token_range;
  }
  return *this;
}
CstConstantOperand::CstConstantOperand(CstConstantOperand&&) noexcept = default;
CstConstantOperand& CstConstantOperand::operator=(
    CstConstantOperand&&) noexcept = default;
CstConstantOperand::~CstConstantOperand() = default;
}  // namespace ptx_frontend::syntax_cst
