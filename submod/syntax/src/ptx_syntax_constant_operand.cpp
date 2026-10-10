#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::syntax_ast {
namespace {
/** Clone a bounded source constant tree without sharing mutable subexpressions. */
std::unique_ptr<AstConstantExpression> clone(
    const AstConstantExpression& value) {
  auto node = std::visit(
      [](const auto& item) -> AstConstantExpressionNode {
        using T = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::same_as<T, AstConstantLiteral> ||
                      std::same_as<T, AstConstantSymbol>)
          return item;
        else if constexpr (std::same_as<T, AstConstantParenthesized>)
          return T{clone(*item.expression)};
        else if constexpr (std::same_as<T, AstConstantCall>)
          return T{clone(*item.callee), clone(*item.argument)};
        else if constexpr (std::same_as<T, AstConstantCast>)
          return T{item.type, clone(*item.operand)};
        else if constexpr (std::same_as<T, AstConstantUnary>)
          return T{item.operation, clone(*item.operand)};
        else if constexpr (std::same_as<T, AstConstantBinary>)
          return T{clone(*item.left), item.operation, clone(*item.right)};
        else
          return T{clone(*item.condition), clone(*item.true_expression),
                   clone(*item.false_expression)};
      },
      value.node);
  return std::make_unique<AstConstantExpression>(
      AstConstantExpression{std::move(node), value.range});
}
}  // namespace

AstConstantOperand::AstConstantOperand(
    std::unique_ptr<AstConstantExpression> value, SourceRange loc)
    : expression(std::move(value)), range(loc) {}
AstConstantOperand::AstConstantOperand(const AstConstantOperand& other)
    : expression(other.expression ? clone(*other.expression) : nullptr),
      range(other.range) {}
AstConstantOperand& AstConstantOperand::operator=(
    const AstConstantOperand& other) {
  if (this != &other) {
    auto copy = other.expression ? clone(*other.expression) : nullptr;
    expression = std::move(copy);
    range = other.range;
  }
  return *this;
}
AstConstantOperand::AstConstantOperand(AstConstantOperand&&) noexcept = default;
AstConstantOperand& AstConstantOperand::operator=(
    AstConstantOperand&&) noexcept = default;
AstConstantOperand::~AstConstantOperand() = default;
}  // namespace ptx_frontend::syntax_ast
