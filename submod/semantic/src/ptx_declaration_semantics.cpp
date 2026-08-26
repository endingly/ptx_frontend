#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>

#include <bit>
#include <charconv>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#include <fmt/format.h>

namespace ptx_frontend::declaration_semantics {
namespace {

using syntax_ast::AstConstantBinaryOperator;
using syntax_ast::AstConstantExpression;
using syntax_ast::AstConstantUnaryOperator;

enum class ExpressionCategory : uint8_t {
  Invalid,
  Integer,
  Floating,
  Address,
};

struct ExpressionInfo {
  ExpressionCategory category = ExpressionCategory::Invalid;
  enum class IntegerType : uint8_t {
    Signed,
    Unsigned,
  };

  struct IntegerValue {
    uint64_t bits{};
    IntegerType type = IntegerType::Signed;
    bool operator==(const IntegerValue&) const = default;
  };

  std::optional<IntegerValue> integer_value;
};

std::optional<ExpressionInfo::IntegerValue> parseIntegerLiteral(
    std::string_view spelling, int base) {
  const bool explicitly_unsigned =
      !spelling.empty() && (spelling.back() == 'u' || spelling.back() == 'U');
  if (explicitly_unsigned) {
    spelling.remove_suffix(1);
  }
  if (base == 16 && spelling.size() >= 2)
    spelling.remove_prefix(2);
  uint64_t value = 0;
  const auto [end, error] = std::from_chars(
      spelling.data(), spelling.data() + spelling.size(), value, base);
  if (error != std::errc{} || end != spelling.data() + spelling.size())
    return std::nullopt;
  return ExpressionInfo::IntegerValue{
      .bits = value,
      .type = explicitly_unsigned ||
                      value > static_cast<uint64_t>(
                                  std::numeric_limits<int64_t>::max())
                  ? ExpressionInfo::IntegerType::Unsigned
                  : ExpressionInfo::IntegerType::Signed,
  };
}

int64_t asSigned(ExpressionInfo::IntegerValue value) {
  return std::bit_cast<int64_t>(value.bits);
}

ExpressionInfo::IntegerValue signedBoolean(bool value) {
  return {
      .bits = static_cast<uint64_t>(value),
      .type = ExpressionInfo::IntegerType::Signed,
  };
}

ExpressionInfo::IntegerType usualIntegerType(
    ExpressionInfo::IntegerValue left, ExpressionInfo::IntegerValue right) {
  return left.type == ExpressionInfo::IntegerType::Unsigned ||
                 right.type == ExpressionInfo::IntegerType::Unsigned
             ? ExpressionInfo::IntegerType::Unsigned
             : ExpressionInfo::IntegerType::Signed;
}

std::optional<uint64_t> nonnegativeIntegerValue(const ExpressionInfo& info) {
  if (info.category != ExpressionCategory::Integer || !info.integer_value)
    return std::nullopt;
  if (info.integer_value->type == ExpressionInfo::IntegerType::Unsigned)
    return info.integer_value->bits;
  const int64_t value = asSigned(*info.integer_value);
  return value < 0 ? std::nullopt
                   : std::optional<uint64_t>{static_cast<uint64_t>(value)};
}

ExpressionInfo evaluateIntegerBinary(AstConstantBinaryOperator operation,
                                     ExpressionInfo::IntegerValue left,
                                     ExpressionInfo::IntegerValue right) {
  using IntegerType = ExpressionInfo::IntegerType;
  using IntegerValue = ExpressionInfo::IntegerValue;
  using Operator = AstConstantBinaryOperator;

  if (operation == Operator::ShiftLeft || operation == Operator::ShiftRight) {
    if (right.bits >= 64)
      return {};
    const uint32_t amount = static_cast<uint32_t>(right.bits);
    if (operation == Operator::ShiftLeft) {
      return {ExpressionCategory::Integer,
              IntegerValue{left.bits << amount, left.type}};
    }
    uint64_t result = left.bits >> amount;
    if (left.type == IntegerType::Signed && amount != 0 && asSigned(left) < 0) {
      result |= ~uint64_t{0} << (64 - amount);
    }
    return {ExpressionCategory::Integer, IntegerValue{result, left.type}};
  }

  const IntegerType converted_type = usualIntegerType(left, right);
  const auto converted = [converted_type](IntegerValue value) {
    value.type = converted_type;
    return value;
  };
  left = converted(left);
  right = converted(right);

  switch (operation) {
    case Operator::Less:
    case Operator::LessEqual:
    case Operator::Greater:
    case Operator::GreaterEqual:
    case Operator::Equal:
    case Operator::NotEqual: {
      bool result = false;
      if (operation == Operator::Equal || operation == Operator::NotEqual) {
        result = left.bits == right.bits;
        if (operation == Operator::NotEqual)
          result = !result;
      } else if (converted_type == IntegerType::Unsigned) {
        if (operation == Operator::Less)
          result = left.bits < right.bits;
        else if (operation == Operator::LessEqual)
          result = left.bits <= right.bits;
        else if (operation == Operator::Greater)
          result = left.bits > right.bits;
        else
          result = left.bits >= right.bits;
      } else {
        const int64_t lhs = asSigned(left);
        const int64_t rhs = asSigned(right);
        if (operation == Operator::Less)
          result = lhs < rhs;
        else if (operation == Operator::LessEqual)
          result = lhs <= rhs;
        else if (operation == Operator::Greater)
          result = lhs > rhs;
        else
          result = lhs >= rhs;
      }
      return {ExpressionCategory::Integer, signedBoolean(result)};
    }
    case Operator::LogicalAnd:
      return {ExpressionCategory::Integer,
              signedBoolean(left.bits != 0 && right.bits != 0)};
    case Operator::LogicalOr:
      return {ExpressionCategory::Integer,
              signedBoolean(left.bits != 0 || right.bits != 0)};
    case Operator::BitwiseAnd:
      return {ExpressionCategory::Integer,
              IntegerValue{left.bits & right.bits, IntegerType::Unsigned}};
    case Operator::BitwiseXor:
      return {ExpressionCategory::Integer,
              IntegerValue{left.bits ^ right.bits, IntegerType::Unsigned}};
    case Operator::BitwiseOr:
      return {ExpressionCategory::Integer,
              IntegerValue{left.bits | right.bits, IntegerType::Unsigned}};
    case Operator::Remainder:
      if (right.bits == 0)
        return {};
      return {ExpressionCategory::Integer,
              IntegerValue{left.bits % right.bits, IntegerType::Signed}};
    case Operator::ShiftLeft:
    case Operator::ShiftRight:
      return {};
    case Operator::Add:
      return {ExpressionCategory::Integer,
              IntegerValue{left.bits + right.bits, converted_type}};
    case Operator::Subtract:
      return {ExpressionCategory::Integer,
              IntegerValue{left.bits - right.bits, converted_type}};
    case Operator::Multiply:
      return {ExpressionCategory::Integer,
              IntegerValue{left.bits * right.bits, converted_type}};
    case Operator::Divide: {
      if (right.bits == 0)
        return {};
      if (converted_type == IntegerType::Unsigned) {
        return {ExpressionCategory::Integer,
                IntegerValue{left.bits / right.bits, converted_type}};
      }
      const bool negative = (asSigned(left) < 0) != (asSigned(right) < 0);
      const uint64_t left_magnitude =
          asSigned(left) < 0 ? uint64_t{0} - left.bits : left.bits;
      const uint64_t right_magnitude =
          asSigned(right) < 0 ? uint64_t{0} - right.bits : right.bits;
      uint64_t quotient = left_magnitude / right_magnitude;
      if (negative)
        quotient = uint64_t{0} - quotient;
      return {ExpressionCategory::Integer,
              IntegerValue{quotient, converted_type}};
    }
  }
  return {};
}

ExpressionInfo classifyExpression(const AstConstantExpression& expression);

ExpressionInfo classifyBinary(const syntax_ast::AstConstantBinary& binary) {
  const ExpressionInfo left = classifyExpression(*binary.left);
  const ExpressionInfo right = classifyExpression(*binary.right);
  using Operator = AstConstantBinaryOperator;

  const bool comparison = binary.operation == Operator::Less ||
                          binary.operation == Operator::LessEqual ||
                          binary.operation == Operator::Greater ||
                          binary.operation == Operator::GreaterEqual ||
                          binary.operation == Operator::Equal ||
                          binary.operation == Operator::NotEqual;

  if (binary.operation == Operator::Add ||
      binary.operation == Operator::Subtract) {
    if (left.category == ExpressionCategory::Address &&
        right.category == ExpressionCategory::Integer) {
      return {ExpressionCategory::Address, std::nullopt};
    }
    if (binary.operation == Operator::Add &&
        left.category == ExpressionCategory::Integer &&
        right.category == ExpressionCategory::Address) {
      return {ExpressionCategory::Address, std::nullopt};
    }
  }

  if (comparison && left.category == ExpressionCategory::Address &&
      right.category == ExpressionCategory::Address) {
    return {ExpressionCategory::Integer, std::nullopt};
  }

  if (left.category != right.category ||
      (left.category != ExpressionCategory::Integer &&
       left.category != ExpressionCategory::Floating)) {
    return {};
  }

  if (left.category == ExpressionCategory::Floating) {
    if (comparison)
      return {ExpressionCategory::Integer, std::nullopt};
    switch (binary.operation) {
      case Operator::Multiply:
      case Operator::Divide:
      case Operator::Add:
      case Operator::Subtract:
        return {ExpressionCategory::Floating, std::nullopt};
      default:
        return {};
    }
  }

  if (!left.integer_value || !right.integer_value)
    return {ExpressionCategory::Integer, std::nullopt};
  return evaluateIntegerBinary(binary.operation, *left.integer_value,
                               *right.integer_value);
}

ExpressionInfo classifyExpression(const AstConstantExpression& expression) {
  return std::visit(
      [](const auto& value) -> ExpressionInfo {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_ast::AstConstantLiteral>) {
          switch (value.value.kind) {
            case syntax_ast::AstImmediateKind::DecimalInteger:
              return {ExpressionCategory::Integer,
                      parseIntegerLiteral(value.value.syntax.text, 10)};
            case syntax_ast::AstImmediateKind::HexInteger:
              return {ExpressionCategory::Integer,
                      parseIntegerLiteral(value.value.syntax.text, 16)};
            case syntax_ast::AstImmediateKind::WarpSize:
              return {ExpressionCategory::Integer,
                      ExpressionInfo::IntegerValue{
                          .bits = 32,
                          .type = ExpressionInfo::IntegerType::Signed,
                      }};
            case syntax_ast::AstImmediateKind::F32Hex:
            case syntax_ast::AstImmediateKind::F64Hex:
            case syntax_ast::AstImmediateKind::DecimalFloat:
              return {ExpressionCategory::Floating, std::nullopt};
          }
        } else if constexpr (std::same_as<Value,
                                          syntax_ast::AstConstantSymbol>) {
          return {ExpressionCategory::Address, std::nullopt};
        } else if constexpr (std::same_as<
                                 Value, syntax_ast::AstConstantParenthesized>) {
          return classifyExpression(*value.expression);
        } else if constexpr (std::same_as<Value, syntax_ast::AstConstantCall>) {
          const auto* callee_symbol =
              std::get_if<syntax_ast::AstConstantSymbol>(&value.callee->node);
          if (callee_symbol != nullptr &&
              callee_symbol->name.syntax.text == "generic") {
            const ExpressionInfo argument = classifyExpression(*value.argument);
            return argument.category == ExpressionCategory::Address
                       ? ExpressionInfo{ExpressionCategory::Address,
                                        std::nullopt}
                       : ExpressionInfo{};
          }
          const auto* mask =
              std::get_if<syntax_ast::AstConstantLiteral>(&value.callee->node);
          if (mask != nullptr &&
              mask->value.kind == syntax_ast::AstImmediateKind::HexInteger) {
            const ExpressionInfo argument = classifyExpression(*value.argument);
            if (argument.category == ExpressionCategory::Integer ||
                argument.category == ExpressionCategory::Address) {
              return {ExpressionCategory::Integer, std::nullopt};
            }
          }
          return {};
        } else if constexpr (std::same_as<Value, syntax_ast::AstConstantCast>) {
          ExpressionInfo operand = classifyExpression(*value.operand);
          if (operand.category != ExpressionCategory::Integer)
            return {};
          if (operand.integer_value) {
            operand.integer_value->type =
                value.type.text == ".u64"
                    ? ExpressionInfo::IntegerType::Unsigned
                    : ExpressionInfo::IntegerType::Signed;
          }
          return operand;
        } else if constexpr (std::same_as<Value,
                                          syntax_ast::AstConstantUnary>) {
          ExpressionInfo operand = classifyExpression(*value.operand);
          if (value.operation == AstConstantUnaryOperator::Plus ||
              value.operation == AstConstantUnaryOperator::Minus) {
            if (operand.category != ExpressionCategory::Integer &&
                operand.category != ExpressionCategory::Floating) {
              return {};
            }
            if (value.operation == AstConstantUnaryOperator::Minus &&
                operand.integer_value) {
              operand.integer_value->bits =
                  uint64_t{0} - operand.integer_value->bits;
            }
            return operand;
          }
          if (operand.category != ExpressionCategory::Integer)
            return {};
          if (value.operation == AstConstantUnaryOperator::LogicalNot) {
            if (operand.integer_value) {
              operand.integer_value =
                  signedBoolean(operand.integer_value->bits == 0);
            }
          } else if (operand.integer_value) {
            operand.integer_value = ExpressionInfo::IntegerValue{
                .bits = ~operand.integer_value->bits,
                .type = ExpressionInfo::IntegerType::Unsigned,
            };
          }
          return operand;
        } else if constexpr (std::same_as<Value,
                                          syntax_ast::AstConstantBinary>) {
          return classifyBinary(value);
        } else {
          const ExpressionInfo condition = classifyExpression(*value.condition);
          ExpressionInfo true_value =
              classifyExpression(*value.true_expression);
          ExpressionInfo false_value =
              classifyExpression(*value.false_expression);
          if (condition.category != ExpressionCategory::Integer ||
              true_value.category != false_value.category) {
            return {};
          }
          if (true_value.category == ExpressionCategory::Integer) {
            if (!true_value.integer_value || !false_value.integer_value)
              return {ExpressionCategory::Integer, std::nullopt};
            const auto result_type = usualIntegerType(
                *true_value.integer_value, *false_value.integer_value);
            true_value.integer_value->type = result_type;
            false_value.integer_value->type = result_type;
          }
          if (condition.integer_value) {
            return condition.integer_value->bits != 0 ? true_value
                                                      : false_value;
          }
          return {true_value.category,
                  true_value.integer_value == false_value.integer_value
                      ? true_value.integer_value
                      : std::nullopt};
        }
        return {};
      },
      expression.node);
}

std::string expressionKey(const AstConstantExpression& expression) {
  return std::visit(
      [](const auto& value) -> std::string {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_ast::AstConstantLiteral>) {
          return fmt::format("l{}:{}", static_cast<int>(value.value.kind),
                             value.value.syntax.text);
        } else if constexpr (std::same_as<Value,
                                          syntax_ast::AstConstantSymbol>) {
          return "s:" + value.name.syntax.text;
        } else if constexpr (std::same_as<
                                 Value, syntax_ast::AstConstantParenthesized>) {
          return "(" + expressionKey(*value.expression) + ")";
        } else if constexpr (std::same_as<Value, syntax_ast::AstConstantCall>) {
          return "call(" + expressionKey(*value.callee) + "," +
                 expressionKey(*value.argument) + ")";
        } else if constexpr (std::same_as<Value, syntax_ast::AstConstantCast>) {
          return "cast(" + value.type.text + "," +
                 expressionKey(*value.operand) + ")";
        } else if constexpr (std::same_as<Value,
                                          syntax_ast::AstConstantUnary>) {
          return fmt::format("u{}({})", static_cast<int>(value.operation),
                             expressionKey(*value.operand));
        } else if constexpr (std::same_as<Value,
                                          syntax_ast::AstConstantBinary>) {
          return fmt::format("b{}({},{})", static_cast<int>(value.operation),
                             expressionKey(*value.left),
                             expressionKey(*value.right));
        } else {
          return "q(" + expressionKey(*value.condition) + "," +
                 expressionKey(*value.true_expression) + "," +
                 expressionKey(*value.false_expression) + ")";
        }
      },
      expression.node);
}

std::string dimensionKey(const AstConstantExpression& expression) {
  const ExpressionInfo value = classifyExpression(expression);
  if (const auto integer = nonnegativeIntegerValue(value))
    return fmt::format("#{}", *integer);
  return expressionKey(expression);
}

binding::SymbolLinkage declarationLinkage(
    const std::vector<syntax_ast::AstSyntax>& qualifiers) {
  for (const auto& qualifier : qualifiers) {
    if (qualifier.text == ".extern")
      return binding::SymbolLinkage::External;
    if (qualifier.text == ".visible")
      return binding::SymbolLinkage::Visible;
    if (qualifier.text == ".weak")
      return binding::SymbolLinkage::Weak;
  }
  return binding::SymbolLinkage::None;
}

std::string optionalSyntaxKey(
    const std::optional<syntax_ast::AstSyntax>& syntax) {
  return syntax ? syntax->text : "-";
}

FunctionParameterContract parameterContract(
    const syntax_ast::AstFunctionParameter& parameter) {
  const auto syntax_text =
      [](const auto& syntax) -> std::optional<std::string> {
    return syntax ? std::optional<std::string>{syntax->text} : std::nullopt;
  };
  return {
      .state_space = parameter.state_space,
      .alignment = syntax_text(parameter.alignment),
      .type = parameter.type.text,
      .is_pointer = parameter.is_pointer,
      .pointer_space = syntax_text(parameter.pointer_space),
      .pointer_alignment = syntax_text(parameter.pointer_alignment),
      .is_array = parameter.is_array,
      .array_extent = parameter.array_size
                          ? std::optional{dimensionKey(*parameter.array_size)}
                          : std::nullopt,
  };
}

std::string variableSignature(
    const syntax_ast::AstVariableDeclaration& declaration,
    const syntax_ast::AstVariableDeclarator& declarator) {
  std::string signature = fmt::format(
      "variable:{}:{}:{}:{}:{}", static_cast<int>(declaration.state_space),
      optionalSyntaxKey(declaration.alignment),
      optionalSyntaxKey(declaration.vector_type), declaration.type.text,
      declarator.parameterized_count ? declarator.parameterized_count->text
                                     : "-");
  for (const auto& dimension : declarator.array_dimensions) {
    signature += "|d:";
    signature += dimension.size ? dimensionKey(*dimension.size) : "-";
  }
  return signature;
}

bool isUnsupportedInitializerType(std::string_view type) {
  return type == ".f16" || type == ".f16x2" || type == ".pred";
}

bool initializerTypeAccepts(std::string_view type,
                            ExpressionCategory category) {
  if (category == ExpressionCategory::Address)
    return type == ".u32" || type == ".u64";
  if (category == ExpressionCategory::Integer) {
    return type.starts_with(".u") || type.starts_with(".s") ||
           type.starts_with(".b") || type == ".pred";
  }
  if (category == ExpressionCategory::Floating) {
    return type.starts_with(".f") || type.starts_with(".bf") ||
           type.starts_with(".tf") || type.starts_with(".e");
  }
  return false;
}

bool isValidAlignment(std::string_view text) {
  if (!text.empty() && (text.back() == 'u' || text.back() == 'U'))
    text.remove_suffix(1);
  uint64_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return !text.empty() && error == std::errc{} &&
         end == text.data() + text.size() && value != 0 &&
         (value & (value - 1)) == 0;
}

class Checker {
 public:
  explicit Checker(const binding::SymbolTable& symbols) : symbols_(symbols) {}

  std::vector<DeclarationDiagnostic> run(const syntax_ast::AstModule& module) {
    checkRedeclarations(module);
    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        checkAlignment(declaration->alignment);
        checkVariableDeclaration(*declaration);
        if (declaration->state_space == syntax_ast::AstStateSpace::Parameter) {
          diagnose(DeclarationDiagnosticKind::ModuleScopeParameter,
                   declaration->range,
                   "A .param variable declaration must be local to a function.");
        }
      } else if (const auto* function =
                     std::get_if<syntax_ast::AstFunction>(&item)) {
        checkFunctionAlignments(*function);
        checkFunctionArrays(*function);
        for (const auto& body_item : function->body) {
          if (const auto* declaration =
                  std::get_if<syntax_ast::AstVariableDeclaration>(&body_item)) {
            checkAlignment(declaration->alignment);
            checkVariableDeclaration(*declaration);
          }
        }
      }
    }
    return std::move(diagnostics_);
  }

 private:
  struct SeenDeclaration {
    std::variant<std::string, FunctionSignature> signature;
    binding::SymbolLinkage linkage{};
    SourceRange range;
    std::optional<SourceRange> definition_range;
  };

  const binding::SymbolTable& symbols_;
  std::vector<DeclarationDiagnostic> diagnostics_;
  std::unordered_map<std::string, SeenDeclaration> declarations_;

  void diagnose(DeclarationDiagnosticKind kind, SourceRange range,
                std::string message,
                std::optional<SourceRange> previous = std::nullopt) {
    diagnostics_.push_back(DeclarationDiagnostic{
        .kind = kind,
        .range = range,
        .previous_range = previous,
        .message = std::move(message),
    });
  }

  void rememberDeclaration(
      std::string key, std::string_view name,
      std::variant<std::string, FunctionSignature> signature,
      binding::SymbolLinkage linkage, bool is_definition, SourceRange range) {
    auto iterator = declarations_.find(key);
    if (iterator == declarations_.end()) {
      declarations_.emplace(
          std::move(key),
          SeenDeclaration{.signature = std::move(signature),
                          .linkage = linkage,
                          .range = range,
                          .definition_range = is_definition
                                                  ? std::optional{range}
                                                  : std::nullopt});
      return;
    }

    SeenDeclaration& previous = iterator->second;
    if (previous.signature != signature || previous.linkage != linkage) {
      diagnose(
          DeclarationDiagnosticKind::IncompatibleRedeclaration, range,
          fmt::format("Redeclaration of '{}' is incompatible with its previous "
                      "declaration.",
                      name),
          previous.range);
      return;
    }
    if (is_definition && previous.definition_range) {
      diagnose(DeclarationDiagnosticKind::MultipleDefinitions, range,
               fmt::format("Symbol '{}' has multiple definitions.", name),
               previous.definition_range);
      return;
    }
    if (is_definition)
      previous.definition_range = range;
  }

  void checkRedeclarations(const syntax_ast::AstModule& module) {
    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        const auto linkage = declarationLinkage(declaration->qualifiers);
        for (const auto& declarator : declaration->declarators) {
          const std::string declaration_key =
              std::string{declarator.parameterized_count ? "p:" : "e:"} +
              declarator.name.syntax.text;
          rememberDeclaration(declaration_key, declarator.name.syntax.text,
                              variableSignature(*declaration, declarator),
                              linkage,
                              linkage != binding::SymbolLinkage::External,
                              declarator.name.syntax.range);
        }
        continue;
      }
      const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
      if (function == nullptr)
        continue;
      const auto linkage = declarationLinkage(function->qualifiers);
      if (linkage == binding::SymbolLinkage::External &&
          !function->is_prototype) {
        diagnose(DeclarationDiagnosticKind::InvalidLinkage,
                 function->name.syntax.range,
                 fmt::format("External function '{}' cannot have a body.",
                             function->name.syntax.text));
      }
      rememberDeclaration("e:" + function->name.syntax.text,
                          function->name.syntax.text,
                          functionSignature(*function), linkage,
                          !function->is_prototype, function->name.syntax.range);
    }
  }

  void checkDimension(const AstConstantExpression& expression) {
    const ExpressionInfo value = classifyExpression(expression);
    const auto integer = nonnegativeIntegerValue(value);
    if (!integer || *integer == 0) {
      diagnose(DeclarationDiagnosticKind::InvalidArrayDimension,
               expression.range,
               "Array dimension must evaluate to a positive integer constant.");
    }
  }

  void checkFunctionArrays(const syntax_ast::AstFunction& function) {
    const auto check_parameters = [this](const auto& parameters) {
      for (const auto& parameter : parameters) {
        if (parameter.array_size)
          checkDimension(*parameter.array_size);
      }
    };
    check_parameters(function.return_parameters);
    check_parameters(function.parameters);
  }

  void checkAlignment(const std::optional<syntax_ast::AstSyntax>& alignment) {
    if (alignment && !isValidAlignment(alignment->text)) {
      diagnose(DeclarationDiagnosticKind::InvalidAlignment, alignment->range,
               "Declaration alignment must be a positive power of two.");
    }
  }

  void checkFunctionAlignments(const syntax_ast::AstFunction& function) {
    const auto check_parameters = [this](const auto& parameters) {
      for (const auto& parameter : parameters) {
        checkAlignment(parameter.alignment);
        checkAlignment(parameter.pointer_alignment);
      }
    };
    check_parameters(function.return_parameters);
    check_parameters(function.parameters);
  }

  void checkVariableDeclaration(
      const syntax_ast::AstVariableDeclaration& declaration) {
    for (const auto& declarator : declaration.declarators) {
      std::vector<std::optional<uint64_t>> extents;
      extents.reserve(declarator.array_dimensions.size() +
                      (declaration.vector_type ? 1 : 0));
      for (size_t index = 0; index < declarator.array_dimensions.size();
           ++index) {
        const auto& dimension = declarator.array_dimensions[index];
        if (!dimension.size) {
          extents.push_back(std::nullopt);
          if (index != 0 || !declarator.initializer) {
            diagnose(DeclarationDiagnosticKind::UnsizedArrayDimension,
                     dimension.range,
                     index == 0
                         ? "An unsized first array dimension requires an "
                           "initializer."
                         : "Only the first array dimension may be unsized.");
          }
          continue;
        }
        const ExpressionInfo value = classifyExpression(*dimension.size);
        const auto integer = nonnegativeIntegerValue(value);
        if (!integer || *integer == 0) {
          diagnose(
              DeclarationDiagnosticKind::InvalidArrayDimension,
              dimension.size->range,
              "Array dimension must evaluate to a positive integer constant.");
          extents.push_back(std::nullopt);
        } else {
          extents.push_back(integer);
        }
      }
      if (declaration.vector_type) {
        extents.push_back(declaration.vector_type->text == ".v2" ? 2 : 4);
      }

      if (!declarator.initializer)
        continue;
      if (isUnsupportedInitializerType(declaration.type.text)) {
        diagnose(DeclarationDiagnosticKind::InitializerTypeMismatch,
                 declarator.initializer->range,
                 fmt::format("Type '{}' does not permit an initializer.",
                             declaration.type.text));
      }
      checkInitializer(*declarator.initializer, extents, 0,
                       declaration.type.text);
    }
  }

  void checkInitializer(const syntax_ast::AstInitializer& initializer,
                        const std::vector<std::optional<uint64_t>>& extents,
                        size_t depth, std::string_view element_type) {
    if (depth == extents.size()) {
      const auto* expression =
          std::get_if<AstConstantExpression>(&initializer.value);
      if (expression == nullptr) {
        diagnose(DeclarationDiagnosticKind::InitializerShapeMismatch,
                 initializer.range,
                 "Scalar initializer element cannot be a brace list.");
        return;
      }
      const ExpressionInfo info = classifyExpression(*expression);
      checkInitializerSymbols(*expression);
      if (info.category == ExpressionCategory::Invalid) {
        diagnose(DeclarationDiagnosticKind::InvalidInitializerExpression,
                 expression->range,
                 "Initializer contains an invalid constant expression.");
      } else if (!isUnsupportedInitializerType(element_type) &&
                 !initializerTypeAccepts(element_type, info.category)) {
        diagnose(DeclarationDiagnosticKind::InitializerTypeMismatch,
                 expression->range,
                 fmt::format(
                     "Initializer expression is incompatible with type '{}'.",
                     element_type));
      }
      return;
    }

    const auto* list =
        std::get_if<syntax_ast::AstInitializerList>(&initializer.value);
    if (list == nullptr) {
      diagnose(DeclarationDiagnosticKind::InitializerShapeMismatch,
               initializer.range,
               "Initializer brace nesting does not match the declared "
               "aggregate shape.");
      return;
    }
    if (extents[depth] && list->elements.size() > *extents[depth]) {
      diagnose(DeclarationDiagnosticKind::ExcessInitializerElements,
               list->range,
               fmt::format("Initializer dimension contains {} elements but its "
                           "declared extent is {}.",
                           list->elements.size(), *extents[depth]));
    }
    if (!extents[depth] && depth == 0 && list->elements.empty()) {
      diagnose(
          DeclarationDiagnosticKind::InvalidArrayDimension, list->range,
          "An unsized array initializer must contain at least one element.");
    }
    for (const auto& element : list->elements)
      checkInitializer(element, extents, depth + 1, element_type);
  }

  void checkInitializerSymbols(const AstConstantExpression& expression) {
    std::visit(
        [this](const auto& value) {
          using Value = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::same_as<Value, syntax_ast::AstConstantLiteral>) {
            return;
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantSymbol>) {
            const auto lookup =
                symbols_.lookup(symbols_.moduleScope(), value.name.syntax.text);
            if (!lookup)
              return;
            const binding::Symbol& symbol = symbols_.symbol(lookup->symbol);
            const bool allowed_variable =
                symbol.kind == binding::SymbolKind::Variable &&
                (symbol.state_space == syntax_ast::AstStateSpace::Global ||
                 symbol.state_space == syntax_ast::AstStateSpace::Constant);
            if (symbol.kind != binding::SymbolKind::Function &&
                !allowed_variable) {
              diagnose(DeclarationDiagnosticKind::InvalidInitializerExpression,
                       value.name.syntax.range,
                       fmt::format(
                           "Initializer symbol '{}' must name a function or a "
                           ".global/.const variable.",
                           value.name.syntax.text));
            }
          } else if constexpr (std::same_as<
                                   Value,
                                   syntax_ast::AstConstantParenthesized>) {
            checkInitializerSymbols(*value.expression);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantCall>) {
            const auto* callee_symbol =
                std::get_if<syntax_ast::AstConstantSymbol>(&value.callee->node);
            if (callee_symbol == nullptr ||
                callee_symbol->name.syntax.text != "generic") {
              checkInitializerSymbols(*value.callee);
            }
            checkInitializerSymbols(*value.argument);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantCast> ||
                               std::same_as<Value,
                                            syntax_ast::AstConstantUnary>) {
            checkInitializerSymbols(*value.operand);
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantBinary>) {
            checkInitializerSymbols(*value.left);
            checkInitializerSymbols(*value.right);
          } else {
            checkInitializerSymbols(*value.condition);
            checkInitializerSymbols(*value.true_expression);
            checkInitializerSymbols(*value.false_expression);
          }
        },
        expression.node);
  }
};

}  // namespace

FunctionSignature functionSignature(const syntax_ast::AstFunction& function) {
  FunctionSignature signature{
      .is_entry = function.is_entry,
      .is_noreturn = function.is_noreturn,
  };
  const auto append_contracts = [](const auto& parameters, auto& contracts) {
    contracts.reserve(parameters.size());
    for (const auto& parameter : parameters)
      contracts.push_back(parameterContract(parameter));
  };
  append_contracts(function.return_parameters, signature.return_parameters);
  append_contracts(function.parameters, signature.parameters);
  return signature;
}

std::vector<DeclarationDiagnostic> checkDeclarations(
    const syntax_ast::AstModule& module, const binding::SymbolTable& symbols) {
  return Checker{symbols}.run(module);
}

}  // namespace ptx_frontend::declaration_semantics
