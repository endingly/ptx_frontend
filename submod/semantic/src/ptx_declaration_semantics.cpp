#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>

#include <ptx_frontend/base/ptx_target.hpp>
#include <ptx_frontend/base/ptx_integer.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <compare>
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

using DiagnosticSink = std::vector<DeclarationDiagnostic>*;

std::optional<ExpressionInfo::IntegerValue> parseIntegerLiteral(
    std::string_view spelling) {
  const bool explicitly_unsigned =
      !spelling.empty() && (spelling.back() == 'u' || spelling.back() == 'U');
  const auto value = base::parseIntegerMagnitude(spelling);
  if (!value)
    return std::nullopt;
  return ExpressionInfo::IntegerValue{
      .bits = *value,
      .type = explicitly_unsigned ||
                      *value > static_cast<uint64_t>(
                                  std::numeric_limits<int64_t>::max())
                  ? ExpressionInfo::IntegerType::Unsigned
                  : ExpressionInfo::IntegerType::Signed,
  };
}

/** Report an integer spelling that cannot be decoded without truncation. */
void reportInvalidIntegerLiteral(
    DiagnosticSink diagnostics, const syntax_ast::AstImmediate& literal) {
  if (diagnostics == nullptr)
    return;
  diagnostics->push_back(DeclarationDiagnostic{
      .kind = DeclarationDiagnosticKind::InvalidIntegerLiteral,
      .range = literal.syntax.range,
      .message = fmt::format("Integer literal '{}' is not representable as "
                             "a uint64_t value.",
                             literal.syntax.text),
  });
}

std::optional<uint64_t> unsignedIntegerLiteral(std::string_view spelling) {
  return base::parseIntegerMagnitude(spelling);
}

std::optional<uint64_t> languageCode(std::string_view spelling) {
  if (spelling.size() >= 2 && spelling.front() == '"' &&
      spelling.back() == '"') {
    std::string name{spelling.substr(1, spelling.size() - 2)};
    std::ranges::transform(name, name.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    static constexpr std::array<std::pair<std::string_view, uint64_t>, 9>
        names = {{{"unknown", 0}, {"ptx", 3}, {"nvvm", 4},
                  {"cuda c++", 5}, {"cuda c++ tile", 6}, {"tile ir", 7},
                  {"python-cutile", 8}, {"fortran", 9}, {"optix", 10}}};
    const auto found = std::ranges::find_if(
        names, [&name](const auto& entry) { return entry.first == name; });
    return found == names.end() ? std::nullopt
                                : std::optional{found->second};
  }
  const auto code = unsignedIntegerLiteral(spelling);
  return code && *code <= 10 ? code : std::nullopt;
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

ExpressionInfo classifyExpression(const AstConstantExpression& expression,
                                  DiagnosticSink diagnostics);

ExpressionInfo classifyBinary(const syntax_ast::AstConstantBinary& binary,
                              DiagnosticSink diagnostics) {
  const ExpressionInfo left = classifyExpression(*binary.left, diagnostics);
  const ExpressionInfo right =
      classifyExpression(*binary.right, diagnostics);
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

ExpressionInfo classifyExpression(const AstConstantExpression& expression,
                                  DiagnosticSink diagnostics) {
  return std::visit(
      [diagnostics](const auto& value) -> ExpressionInfo {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_ast::AstConstantLiteral>) {
          switch (value.value.kind) {
            case syntax_ast::AstImmediateKind::DecimalInteger:
            case syntax_ast::AstImmediateKind::HexInteger: {
              const auto integer = parseIntegerLiteral(value.value.syntax.text);
              if (!integer) {
                reportInvalidIntegerLiteral(diagnostics, value.value);
                return {};
              }
              return {ExpressionCategory::Integer, integer};
            }
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
          return classifyExpression(*value.expression, diagnostics);
        } else if constexpr (std::same_as<Value, syntax_ast::AstConstantCall>) {
          const ExpressionInfo callee =
              classifyExpression(*value.callee, diagnostics);
          const ExpressionInfo argument =
              classifyExpression(*value.argument, diagnostics);
          const auto* callee_symbol =
              std::get_if<syntax_ast::AstConstantSymbol>(&value.callee->node);
          if (callee_symbol != nullptr &&
              callee_symbol->name.syntax.text == "generic") {
            return argument.category == ExpressionCategory::Address
                       ? ExpressionInfo{ExpressionCategory::Address,
                                        std::nullopt}
                       : ExpressionInfo{};
          }
          const auto* mask =
              std::get_if<syntax_ast::AstConstantLiteral>(&value.callee->node);
          if (mask != nullptr &&
              mask->value.kind == syntax_ast::AstImmediateKind::HexInteger) {
            if (callee.category == ExpressionCategory::Integer &&
                (argument.category == ExpressionCategory::Integer ||
                 argument.category == ExpressionCategory::Address)) {
              return {ExpressionCategory::Integer, std::nullopt};
            }
          }
          return {};
        } else if constexpr (std::same_as<Value, syntax_ast::AstConstantCast>) {
          ExpressionInfo operand =
              classifyExpression(*value.operand, diagnostics);
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
          ExpressionInfo operand =
              classifyExpression(*value.operand, diagnostics);
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
          return classifyBinary(value, diagnostics);
        } else {
          const ExpressionInfo condition =
              classifyExpression(*value.condition, diagnostics);
          ExpressionInfo true_value =
              classifyExpression(*value.true_expression, diagnostics);
          ExpressionInfo false_value =
              classifyExpression(*value.false_expression, diagnostics);
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
  const ExpressionInfo value = classifyExpression(expression, nullptr);
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

/** Compare numeric declaration fields by value while retaining invalid spelling. */
std::string integerSyntaxKey(const syntax_ast::AstSyntax& syntax) {
  const auto value = base::parseIntegerMagnitude(syntax.text);
  return value ? std::to_string(*value) : syntax.text;
}

std::optional<uint64_t> compactLabelIndex(std::string_view prefix,
                                          std::string_view label) {
  if (!label.starts_with(prefix) || label.size() == prefix.size())
    return std::nullopt;
  const std::string_view suffix = label.substr(prefix.size());
  if (suffix.size() > 1 && suffix.front() == '0')
    return std::nullopt;
  uint64_t index = 0;
  const auto [end, error] =
      std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
  if (error != std::errc{} || end != suffix.data() + suffix.size())
    return std::nullopt;
  return index;
}

std::optional<uint64_t> positiveCount(std::string_view text) {
  const auto count = base::parseIntegerMagnitude(text);
  if (!count || *count == 0) {
    return std::nullopt;
  }
  return count;
}

bool isValidAlignment(std::string_view text) {
  const auto value = positiveCount(text);
  return value && (*value & (*value - 1)) == 0;
}

/** Normalize known effective alignment while retaining invalid source for diagnostics. */
std::optional<std::string> parameterAlignmentContract(
    const std::optional<syntax_ast::AstSyntax>& explicit_alignment,
    std::optional<uint64_t> default_alignment) {
  if (explicit_alignment) {
    const auto value = positiveCount(explicit_alignment->text);
    return value ? std::to_string(*value) : explicit_alignment->text;
  }
  return default_alignment ? std::optional{std::to_string(*default_alignment)}
                           : std::nullopt;
}

FunctionParameterContract parameterContract(
    const syntax_ast::AstFunctionParameter& parameter) {
  const auto syntax_text =
      [](const auto& syntax) -> std::optional<std::string> {
    return syntax ? std::optional<std::string>{syntax->text} : std::nullopt;
  };
  const auto scalar = parameterScalarType(parameter.type.text);
  const std::optional<uint64_t> natural_alignment =
      scalar ? std::optional<uint64_t>{base::scalar_size_of(*scalar)}
      : parameter.type.text == ".pred" ? std::optional<uint64_t>{1}
      : parameter.type.text == ".f16x2" ? std::optional<uint64_t>{4}
                                        : std::nullopt;
  return {
      .state_space = parameter.state_space,
      .alignment = parameterAlignmentContract(parameter.alignment, natural_alignment),
      .type = parameter.type.text,
      .is_pointer = parameter.is_pointer,
      .pointer_space = syntax_text(parameter.pointer_space),
      .pointer_alignment = parameterAlignmentContract(
          parameter.pointer_alignment,
          parameter.is_pointer ? std::optional<uint64_t>{4} : std::nullopt),
      .is_array = parameter.is_array,
      .array_extent = parameter.array_size
                          ? std::optional{dimensionKey(*parameter.array_size)}
                          : std::nullopt,
  };
}

/** Return the natural alignment for a storage layout modeled by resolution. */
std::optional<uint64_t> naturalStorageAlignment(
    const syntax_ast::AstVariableDeclaration& declaration) {
  auto scalar = parameterScalarType(declaration.type.text);
  if (!scalar && declaration.type.text == ".f16x2")
    scalar = base::ScalarType::F16x2;
  if (!scalar)
    return std::nullopt;
  const uint64_t scalar_bytes = base::scalar_size_of(*scalar);
  if (scalar_bytes == 0)
    return std::nullopt;

  uint64_t lanes = 1;
  if (declaration.vector_type) {
    if (declaration.vector_type->text == ".v2")
      lanes = 2;
    else if (declaration.vector_type->text == ".v4")
      lanes = 4;
    else
      return std::nullopt;
  }
  if (scalar_bytes > 16 / lanes)
    return std::nullopt;
  return scalar_bytes * lanes;
}

/** Normalize storage alignment only when its omitted natural value is known. */
std::optional<std::string> variableAlignmentContract(
    const syntax_ast::AstVariableDeclaration& declaration) {
  if (declaration.alignment) {
    const auto value = positiveCount(declaration.alignment->text);
    return value && isValidAlignment(declaration.alignment->text)
               ? std::optional{std::to_string(*value)}
               : std::optional{declaration.alignment->text};
  }
  const auto natural_alignment = naturalStorageAlignment(declaration);
  return natural_alignment ? std::optional{std::to_string(*natural_alignment)}
                           : std::nullopt;
}

std::string variableSignature(
    const syntax_ast::AstVariableDeclaration& declaration,
    const syntax_ast::AstVariableDeclarator& declarator) {
  std::string signature = fmt::format(
      "variable:{}:{}:{}:{}:{}", static_cast<int>(declaration.state_space),
      variableAlignmentContract(declaration).value_or("-"),
      optionalSyntaxKey(declaration.vector_type), declaration.type.text,
      declarator.parameterized_count ? integerSyntaxKey(*declarator.parameterized_count)
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

/** Return whether a spelling denotes an opaque parameter identity. */
bool isOpaqueParameterType(std::string_view type) {
  return type == ".texref" || type == ".samplerref" || type == ".surfref";
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

class Checker {
 public:
  explicit Checker(const binding::SymbolTable& symbols) : symbols_(symbols) {}

  std::vector<DeclarationDiagnostic> run(const syntax_ast::AstModule& module) {
    const auto module_version = modulePtxVersion(module);
    const auto function_targets = functionTargetContexts(module);
    checkRedeclarations(module);
    checkControlFlowMetadata(module_version, function_targets);
    checkKernelResources(module);
    checkM11Directives(module);
    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        checkAlignment(declaration->alignment);
        checkVariableDeclaration(*declaration);
        if (declaration->state_space == syntax_ast::AstStateSpace::Parameter) {
          diagnose(
              DeclarationDiagnosticKind::ModuleScopeParameter,
              declaration->range,
              "A .param variable declaration must be local to a function.");
        }
      } else if (const auto* function =
                     std::get_if<syntax_ast::AstFunction>(&item)) {
        const auto context = std::ranges::find_if(
            function_targets, [function](const FunctionTargetContext& candidate) {
              return candidate.function == function;
            });
        checkFunctionParameters(*function, module_version, context->target_sm);
        checkFunctionBodyDeclarations(function->body, module_version,
                                      context->target_sm);
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

  struct SeenFunction {
    FunctionSignature signature;
  };

  struct FirstCallTarget {
    std::string name;
    FunctionSignature signature;
    SourceRange range;
  };

  struct PtxVersion {
    uint16_t major{};
    uint16_t minor{};
    constexpr auto operator<=>(const PtxVersion&) const = default;
  };

  /** Effective target information for one source function declaration. */
  struct FunctionTargetContext {
    /** Function whose complete header and body share this target context. */
    const syntax_ast::AstFunction* function{};
    /** Active supported target architecture, or no target validation context. */
    std::optional<uint32_t> target_sm;
  };

  /** Lexical role that determines the declaration rules for a parameter. */
  enum class ParameterContext : uint8_t {
    EntryInput,
    DeviceInput,
    DeviceReturn,
    CallPrototypeInput,
    CallPrototypeReturn,
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
    std::unordered_map<std::string, const syntax_ast::AstFunction*>
        first_functions;
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
      const auto [first, inserted] =
          first_functions.emplace(function->name.syntax.text, function);
      if (!inserted && !sameFunctionHeader(*first->second, *function)) {
        diagnose(DeclarationDiagnosticKind::IncompatibleRedeclaration,
                 function->range,
                 fmt::format("Function '{}' has incompatible header "
                             "directives.",
                             function->name.syntax.text),
                 first->second->range);
      }
    }
  }

  static bool sameFunctionHeader(const syntax_ast::AstFunction& left,
                                 const syntax_ast::AstFunction& right) {
    const auto same_attributes = [](const auto& lhs, const auto& rhs) {
      if (lhs.size() != rhs.size())
        return false;
      for (size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].kind != rhs[index].kind ||
            lhs[index].values.size() != rhs[index].values.size()) {
          return false;
        }
        for (size_t value = 0; value < lhs[index].values.size(); ++value) {
          if (unsignedIntegerLiteral(lhs[index].values[value].text) !=
              unsignedIntegerLiteral(rhs[index].values[value].text)) {
            return false;
          }
        }
      }
      return true;
    };
    const auto same_suffix = [](const auto& lhs, const auto& rhs) {
      return lhs.has_value() == rhs.has_value() &&
             (!lhs || integerSyntaxKey(lhs->count) == integerSyntaxKey(rhs->count));
    };
    const auto same_language = [](const auto& lhs, const auto& rhs) {
      if (lhs.has_value() != rhs.has_value())
        return false;
      if (!lhs)
        return true;
      if (lhs->values.size() != rhs->values.size())
        return false;
      for (size_t index = 0; index < lhs->values.size(); ++index) {
        if (languageCode(lhs->values[index].text) !=
            languageCode(rhs->values[index].text)) {
          return false;
        }
      }
      return true;
    };
    return same_attributes(left.attributes, right.attributes) &&
           same_suffix(left.abi_preserve, right.abi_preserve) &&
           same_suffix(left.abi_preserve_control, right.abi_preserve_control) &&
           same_language(left.language, right.language);
  }

  static std::optional<PtxVersion> parsePtxVersion(std::string_view text) {
    const auto dot = text.find('.');
    if (dot == std::string_view::npos)
      return std::nullopt;
    PtxVersion version;
    const auto parse = [](std::string_view value, uint16_t& output) {
      const auto [end, error] = std::from_chars(
          value.data(), value.data() + value.size(), output);
      return !value.empty() && error == std::errc{} &&
             end == value.data() + value.size();
    };
    if (!parse(text.substr(0, dot), version.major) ||
        !parse(text.substr(dot + 1), version.minor))
      return std::nullopt;
    return version;
  }

  static PtxVersion minimumPtxVersion(
      syntax_ast::AstKernelResourceKind kind) {
    switch (kind) {
      case syntax_ast::AstKernelResourceKind::MaxNreg:
      case syntax_ast::AstKernelResourceKind::MaxNtid:
        return {1, 3};
      case syntax_ast::AstKernelResourceKind::ReqNtid:
        return {2, 1};
      case syntax_ast::AstKernelResourceKind::MinNctaPerSm:
        return {2, 0};
      case syntax_ast::AstKernelResourceKind::ReqNctaPerCluster:
      case syntax_ast::AstKernelResourceKind::ExplicitCluster:
      case syntax_ast::AstKernelResourceKind::MaxClusterRank:
        return {7, 8};
    }
    return {};
  }

  static std::string_view kernelResourceName(
      syntax_ast::AstKernelResourceKind kind) {
    switch (kind) {
      case syntax_ast::AstKernelResourceKind::MaxNreg:
        return ".maxnreg";
      case syntax_ast::AstKernelResourceKind::MaxNtid:
        return ".maxntid";
      case syntax_ast::AstKernelResourceKind::ReqNtid:
        return ".reqntid";
      case syntax_ast::AstKernelResourceKind::MinNctaPerSm:
        return ".minnctapersm";
      case syntax_ast::AstKernelResourceKind::ReqNctaPerCluster:
        return ".reqnctapercluster";
      case syntax_ast::AstKernelResourceKind::ExplicitCluster:
        return ".explicitcluster";
      case syntax_ast::AstKernelResourceKind::MaxClusterRank:
        return ".maxclusterrank";
    }
    return "kernel resource directive";
  }

  void checkKernelResources(const syntax_ast::AstModule& module) {
    std::optional<PtxVersion> module_version;
    for (const auto& item : module.items) {
      const auto* version = std::get_if<syntax_ast::AstVersionDirective>(&item);
      if (version != nullptr) {
        module_version = parsePtxVersion(version->version.text);
        break;
      }
    }

    for (const auto& item : module.items) {
      const auto* function = std::get_if<syntax_ast::AstFunction>(&item);
      if (function == nullptr || !function->is_entry)
        continue;
      const syntax_ast::AstKernelResourceDirective* first_thread_count =
          nullptr;
      const syntax_ast::AstKernelResourceDirective* req_cluster = nullptr;
      const syntax_ast::AstKernelResourceDirective* max_cluster = nullptr;
      for (const auto& resource : function->resources) {
        if (module_version &&
            *module_version < minimumPtxVersion(resource.kind)) {
          const PtxVersion required = minimumPtxVersion(resource.kind);
          diagnose(DeclarationDiagnosticKind::UnsupportedKernelResourcePtxVersion,
                   resource.range,
                   fmt::format("{} requires PTX ISA >= {}.{}, but module PTX "
                               "ISA is {}.{}.",
                               kernelResourceName(resource.kind), required.major,
                               required.minor, module_version->major,
                               module_version->minor));
        }
        if (resource.kind == syntax_ast::AstKernelResourceKind::MaxNtid ||
            resource.kind == syntax_ast::AstKernelResourceKind::ReqNtid) {
          if (first_thread_count == nullptr) {
            first_thread_count = &resource;
          } else if (first_thread_count->kind != resource.kind) {
            diagnose(
                DeclarationDiagnosticKind::IncompatibleKernelResourceDirective,
                resource.range,
                ".reqntid cannot be used together with .maxntid.",
                first_thread_count->range);
          }
        }

        if (resource.kind ==
            syntax_ast::AstKernelResourceKind::ReqNctaPerCluster) {
          if (max_cluster != nullptr) {
            diagnose(
                DeclarationDiagnosticKind::IncompatibleKernelResourceDirective,
                resource.range,
                ".reqnctapercluster cannot be used together with "
                ".maxclusterrank.",
                max_cluster->range);
          } else {
            req_cluster = &resource;
          }
        } else if (resource.kind ==
                   syntax_ast::AstKernelResourceKind::MaxClusterRank) {
          if (req_cluster != nullptr) {
            diagnose(
                DeclarationDiagnosticKind::IncompatibleKernelResourceDirective,
                resource.range,
                ".maxclusterrank cannot be used together with "
                ".reqnctapercluster.",
                req_cluster->range);
          } else {
            max_cluster = &resource;
          }
        }
      }
    }
  }

  std::optional<PtxVersion> modulePtxVersion(
      const syntax_ast::AstModule& module) const {
    for (const auto& item : module.items) {
      if (const auto* version =
              std::get_if<syntax_ast::AstVersionDirective>(&item))
        return parsePtxVersion(version->version.text);
    }
    return std::nullopt;
  }

  /** Return the supported architecture selected by one target directive. */
  static std::optional<uint32_t> targetSmVersion(
      const syntax_ast::AstTargetDirective& target) {
    if (target.targets.empty())
      return std::nullopt;
    const auto profile = base::find_target_profile(target.targets.front().text);
    return profile ? std::optional{profile->identity.architecture.number}
                   : std::nullopt;
  }

  /** Associate each function with the target directive active at its source range. */
  static std::vector<FunctionTargetContext> functionTargetContexts(
      const syntax_ast::AstModule& module) {
    std::optional<uint32_t> active_target;
    std::vector<FunctionTargetContext> contexts;
    for (const auto& item : module.items) {
      if (const auto* target =
              std::get_if<syntax_ast::AstTargetDirective>(&item)) {
        // An empty or unsupported target intentionally clears prior context.
        active_target = targetSmVersion(*target);
      } else if (const auto* function =
                     std::get_if<syntax_ast::AstFunction>(&item)) {
        contexts.push_back(FunctionTargetContext{
            .function = function,
            .target_sm = active_target,
        });
      }
    }
    return contexts;
  }

  void requirePtx(std::optional<PtxVersion> module_version, PtxVersion required,
                  SourceRange range, std::string_view spelling) {
    if (!module_version || *module_version >= required)
      return;
    diagnose(DeclarationDiagnosticKind::UnsupportedDirectivePtxVersion, range,
             fmt::format("{} requires PTX ISA >= {}.{}, but module PTX ISA is "
                         "{}.{}.",
                         spelling, required.major, required.minor,
                         module_version->major, module_version->minor));
  }

  static bool hasResource(const syntax_ast::AstFunction& function,
                          syntax_ast::AstKernelResourceKind kind) {
    return std::ranges::any_of(function.resources,
                               [kind](const auto& resource) {
                                 return resource.kind == kind;
                               });
  }

  void checkM11Directives(const syntax_ast::AstModule& module) {
    const auto module_version = modulePtxVersion(module);
    std::unordered_map<std::string, SourceRange> seen_aliases;
    const auto functions_named = [&module](std::string_view name) {
      std::vector<const syntax_ast::AstFunction*> found;
      for (const auto& candidate : module.items) {
        const auto* function = std::get_if<syntax_ast::AstFunction>(&candidate);
        if (function != nullptr && function->name.syntax.text == name)
          found.push_back(function);
      }
      return found;
    };
    const auto check_attributes = [this, module_version](
                                      const auto& attributes,
                                      syntax_ast::AstStateSpace state_space,
                                      bool function) {
      bool managed = false;
      bool unified = false;
      for (const auto& attribute : attributes) {
        const bool is_managed =
            attribute.kind == syntax_ast::AstAttributeKind::Managed;
        bool& repeated = is_managed ? managed : unified;
        if (repeated) {
          diagnose(DeclarationDiagnosticKind::InvalidDeclarationDirective,
                   attribute.range, "Duplicate .attribute member.");
        }
        repeated = true;
        requirePtx(module_version, is_managed ? PtxVersion{4, 0}
                                               : PtxVersion{8, 0},
                   attribute.range, is_managed ? ".managed" : ".unified");
        if (function ? !is_managed
                     : state_space == syntax_ast::AstStateSpace::Global) {
          continue;
        }
        diagnose(DeclarationDiagnosticKind::InvalidDeclarationDirective,
                 attribute.range,
                 is_managed ? ".managed is only valid on a .global variable."
                            : ".unified is only valid on a .global variable "
                              "or device function.");
      }
    };

    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        check_attributes(declaration->attributes, declaration->state_space,
                         false);
      } else if (const auto* alias =
                     std::get_if<syntax_ast::AstAliasDirective>(&item)) {
        requirePtx(module_version, {6, 3}, alias->range, ".alias");
        const auto [previous, inserted] =
            seen_aliases.emplace(alias->alias.syntax.text, alias->range);
        if (!inserted) {
          diagnose(DeclarationDiagnosticKind::InvalidFunctionAlias,
                   alias->alias.syntax.range,
                   "A function may have only one .alias directive.",
                   previous->second);
        }
        const auto targets = functions_named(alias->aliasee.syntax.text);
        const auto target = std::ranges::find_if(
            targets, [](const auto* function) { return !function->is_prototype; });
        if (target == targets.end() || (*target)->is_entry ||
            declarationLinkage((*target)->qualifiers) ==
                binding::SymbolLinkage::Weak) {
          diagnose(DeclarationDiagnosticKind::InvalidFunctionAlias,
                   alias->aliasee.syntax.range,
                   ".alias requires a defined, non-weak device function in "
                   "the same module.");
          continue;
        }
        const auto declarations = functions_named(alias->alias.syntax.text);
        const auto declared = std::ranges::find_if(
            declarations,
            [](const auto* function) { return function->is_prototype; });
        if (declared == declarations.end()) {
          diagnose(DeclarationDiagnosticKind::InvalidFunctionAlias,
                   alias->alias.syntax.range,
                   ".alias requires a matching device-function prototype.");
          continue;
        }
        for (const auto* candidate : declarations) {
          if (candidate->is_entry || !candidate->is_prototype ||
              functionSignature(*candidate) != functionSignature(**target)) {
            diagnose(DeclarationDiagnosticKind::InvalidFunctionAlias,
                     alias->alias.syntax.range,
                     ".alias requires a matching device-function declaration "
                     "without a body.");
            break;
          }
        }
      } else if (const auto* function =
                     std::get_if<syntax_ast::AstFunction>(&item)) {
        check_attributes(function->attributes, syntax_ast::AstStateSpace::Global,
                         true);
        if (function->noreturn_directive) {
          requirePtx(module_version, {6, 4},
                     function->noreturn_directive->range, ".noreturn");
          if (!function->return_parameters.empty()) {
            diagnose(DeclarationDiagnosticKind::InvalidDeclarationDirective,
                     function->noreturn_directive->range,
                     "A .noreturn function cannot have return parameters.");
          }
        }
        if (function->abi_preserve)
          requirePtx(module_version, {9, 0}, function->abi_preserve->range,
                     ".abi_preserve");
        if (function->abi_preserve_control)
          requirePtx(module_version, {9, 0},
                     function->abi_preserve_control->range,
                     ".abi_preserve_control");
        if (function->blocks_are_clusters) {
          requirePtx(module_version, {9, 0},
                     function->blocks_are_clusters->range,
                     ".blocksareclusters");
          if (!hasResource(*function,
                           syntax_ast::AstKernelResourceKind::ReqNtid) ||
              !hasResource(*function, syntax_ast::AstKernelResourceKind::
                                         ReqNctaPerCluster)) {
            diagnose(DeclarationDiagnosticKind::InvalidDeclarationDirective,
                     function->blocks_are_clusters->range,
                     ".blocksareclusters requires .reqntid and "
                     ".reqnctapercluster.");
          }
        }
        if (function->language) {
          requirePtx(module_version, {9, 3}, function->language->range,
                     ".language");
          for (const auto& value : function->language->values) {
            if (!languageCode(value.text)) {
              diagnose(DeclarationDiagnosticKind::InvalidDeclarationDirective,
                       value.range, "Invalid .language value.");
            }
          }
        }
        const auto check_body = [&](const auto& self, const auto& body) -> void {
          for (const auto& body_item : body) {
            if (const auto* declaration =
                    std::get_if<syntax_ast::AstVariableDeclaration>(&body_item)) {
              check_attributes(declaration->attributes, declaration->state_space,
                               false);
            } else if (const auto* prototype =
                    std::get_if<syntax_ast::AstCallPrototype>(&body_item)) {
              if (prototype->noreturn_directive)
                requirePtx(module_version, {6, 4},
                           prototype->noreturn_directive->range, ".noreturn");
              if (prototype->abi_preserve)
                requirePtx(module_version, {9, 0}, prototype->abi_preserve->range,
                           ".abi_preserve");
              if (prototype->abi_preserve_control)
                requirePtx(module_version, {9, 0},
                           prototype->abi_preserve_control->range,
                           ".abi_preserve_control");
            } else if (const auto* block =
                           std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(
                               &body_item);
                       block != nullptr && *block) {
              self(self, (*block)->body);
            }
          }
        };
        check_body(check_body, function->body);
      }
    }
  }

  void checkCallTargets(
      const syntax_ast::AstCallTargets& targets,
      const std::unordered_map<std::string, SeenFunction>& seen_functions) {
    std::unordered_map<std::string, SourceRange> seen_targets;
    std::optional<FirstCallTarget> first_target;
    for (const auto& target : targets.targets) {
      const auto [duplicate, inserted] = seen_targets.emplace(
          target.syntax.text, target.syntax.range);
      if (!inserted) {
        diagnose(DeclarationDiagnosticKind::DuplicateMetadataTarget,
                 target.syntax.range,
                 fmt::format("Duplicate .calltargets member '{}'.",
                             target.syntax.text),
                 duplicate->second);
      }

      const auto symbol =
          symbols_.lookup(symbols_.moduleScope(), target.syntax.text);
      if (!symbol) {
        diagnose(DeclarationDiagnosticKind::UnresolvedMetadataTarget,
                 target.syntax.range,
                 fmt::format("Call target '{}' must be declared before its "
                             ".calltargets directive.",
                             target.syntax.text));
        continue;
      }
      const binding::Symbol& bound = symbols_.symbol(symbol->symbol);
      if (bound.kind != binding::SymbolKind::Function ||
          bound.function_is_entry) {
        diagnose(DeclarationDiagnosticKind::InvalidMetadataTarget,
                 target.syntax.range,
                 fmt::format("Call target '{}' must name a device .func "
                             "declaration.",
                             target.syntax.text),
                 bound.declaration_range);
        continue;
      }
      const auto seen = seen_functions.find(target.syntax.text);
      if (seen == seen_functions.end()) {
        diagnose(DeclarationDiagnosticKind::UnresolvedMetadataTarget,
                 target.syntax.range,
                 fmt::format("Call target '{}' must be declared before its "
                             ".calltargets directive.",
                             target.syntax.text));
        continue;
      }

      if (!first_target) {
        first_target.emplace(target.syntax.text, seen->second.signature,
                             target.syntax.range);
      } else if (seen->second.signature != first_target->signature) {
        diagnose(DeclarationDiagnosticKind::IncompatibleCallTargetSignature,
                 target.syntax.range,
                 fmt::format("Call target '{}' has a signature incompatible "
                             "with '{}'.",
                             target.syntax.text, first_target->name),
                 first_target->range);
      }
    }
  }

  /**
   * Validate every .branchtargets slot against labels in its enclosing function.
   *
   * Repeated explicit and compact destinations are legal and retain their AST
   * ordering; this check only validates each destination independently.
   */
  void checkBranchTargets(
      binding::ScopeId function_scope,
      const syntax_ast::AstBranchTargets& targets) {
    std::unordered_map<std::string_view, SourceRange> labels;
    for (const binding::Symbol& symbol : symbols_.symbols()) {
      if (symbol.scope == function_scope &&
          symbol.kind == binding::SymbolKind::Label) {
        labels.emplace(symbol.name, symbol.declaration_range);
      }
    }

    const auto check_label = [this, function_scope, &labels](
                                 std::string_view name, SourceRange range) {
      const auto label = labels.find(name);
      if (label == labels.end()) {
        const auto bound = symbols_.lookup(function_scope, name);
        if (bound) {
          diagnose(DeclarationDiagnosticKind::InvalidMetadataTarget, range,
                   fmt::format("Branch target '{}' must name a label in the "
                               "current function.",
                               name),
                   symbols_.symbol(bound->symbol).declaration_range);
        } else {
          diagnose(DeclarationDiagnosticKind::UnresolvedMetadataTarget, range,
                   fmt::format("Branch target '{}' is not declared in the "
                               "current function.",
                               name));
        }
        return;
      }
    };

    for (const auto& target : targets.targets) {
      if (!target.count) {
        check_label(target.name.syntax.text, target.range);
        continue;
      }
      const auto count = positiveCount(target.count->text);
      if (!count) {
        diagnose(DeclarationDiagnosticKind::InvalidMetadataTarget,
                 target.count->range,
                 "Compact branch target count must be a positive unsigned "
                 "integer.");
        continue;
      }

      uint64_t matched = 0;
      for (const auto& entry : labels) {
        const auto index =
            compactLabelIndex(target.name.syntax.text, entry.first);
        if (!index || *index >= *count)
          continue;
        ++matched;
      }
      if (matched != *count) {
        diagnose(DeclarationDiagnosticKind::UnresolvedMetadataTarget,
                 target.range,
                 fmt::format("Compact branch target '{}<{}>' includes labels "
                             "not declared in the current function.",
                             target.name.syntax.text, target.count->text));
      }
    }
  }

  /** Report a parameter construct whose PTX or SM availability is too old. */
  void requireParameterAvailability(std::optional<PtxVersion> module_version,
                                    std::optional<uint32_t> module_sm,
                                    PtxVersion required_version,
                                    uint32_t required_sm, SourceRange range,
                                    std::string_view spelling) {
    if (module_version && *module_version < required_version) {
      diagnose(
          DeclarationDiagnosticKind::UnsupportedParameterDeclaration, range,
          fmt::format("{} requires PTX ISA >= {}.{}, but module PTX ISA "
                      "is {}.{}.",
                      spelling, required_version.major, required_version.minor,
                      module_version->major, module_version->minor));
    }
    if (module_sm && *module_sm < required_sm) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               range,
               fmt::format("{} requires sm_{} or later, but module "
                           "target is sm_{}.",
                           spelling, required_sm, *module_sm));
    }
  }

  /** Return whether a header role permits a register formal parameter. */
  static bool permitsRegisterParameter(ParameterContext context) {
    return context == ParameterContext::DeviceInput ||
           context == ParameterContext::DeviceReturn ||
           context == ParameterContext::CallPrototypeInput ||
           context == ParameterContext::CallPrototypeReturn;
  }

  /** Return whether an input role can carry the documented unsized byte array. */
  static bool permitsUnsizedInput(ParameterContext context) {
    return context == ParameterContext::DeviceInput ||
           context == ParameterContext::CallPrototypeInput;
  }

  /** Return a static scalar or one-dimensional array byte count when known. */
  static std::optional<uint64_t> parameterByteExtent(
      base::ScalarType type, bool is_array,
      const std::optional<AstConstantExpression>& array_size) {
    const uint64_t scalar_bytes = base::scalar_size_of(type);
    if (scalar_bytes == 0 || (is_array && !array_size))
      return std::nullopt;
    if (!is_array)
      return scalar_bytes;
    const auto extent = constantArrayExtent(*array_size);
    if (!extent || *extent == 0 ||
        scalar_bytes > std::numeric_limits<uint64_t>::max() / *extent) {
      return std::nullopt;
    }
    return scalar_bytes * *extent;
  }

  /** Validate type, state space, shape, availability, and pointer attributes. */
  void checkHeaderParameter(const syntax_ast::AstFunctionParameter& parameter,
                            ParameterContext context, bool is_final,
                            std::optional<PtxVersion> module_version,
                            std::optional<uint32_t> module_sm) {
    checkAlignment(parameter.alignment);
    checkAlignment(parameter.pointer_alignment);
    if (parameter.array_size)
      checkDimension(*parameter.array_size);

    const bool is_parameter =
        parameter.state_space == syntax_ast::AstStateSpace::Parameter;
    const bool is_register =
        parameter.state_space == syntax_ast::AstStateSpace::Register;
    if ((!is_parameter &&
         !(is_register && permitsRegisterParameter(context))) ||
        (context == ParameterContext::EntryInput && !is_parameter)) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               parameter.range,
               "This parameter role does not permit the declared state space.");
      return;
    }

    if (isOpaqueParameterType(parameter.type.text)) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               parameter.type.range,
               "Opaque .texref/.samplerref/.surfref parameters are not "
               "modeled by this frontend.");
      return;
    }
    const bool predicate = parameter.type.text == ".pred";
    const bool packed_half_register =
        parameter.type.text == ".f16x2" && is_register;
    const auto scalar = parameterScalarType(parameter.type.text);
    if (!scalar && !(predicate && is_register) && !packed_half_register) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               parameter.type.range,
               predicate ? ".pred parameters must use .reg state space."
               : parameter.type.text == ".f16x2"
                   ? "Fundamental .f16x2 parameter storage is not "
                     "modeled by this frontend."
                   : fmt::format("Parameter type '{}' is not a supported "
                                 "fundamental scalar type.",
                                 parameter.type.text));
      return;
    }

    if (parameter.is_array && !is_parameter) {
      const bool is_call_prototype =
          context == ParameterContext::CallPrototypeInput ||
          context == ParameterContext::CallPrototypeReturn;
      diagnose(is_call_prototype ? DeclarationDiagnosticKind::InvalidCallPrototype
                                 : DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               parameter.range,
               is_call_prototype
                   ? "A .callprototype array parameter must use .param state space."
                   : "Array parameters must use .param state space.");
      return;
    }
    if (parameter.is_array && !parameter.array_size) {
      if (!permitsUnsizedInput(context) || !is_final || !scalar ||
          *scalar != base::ScalarType::B8) {
        diagnose(DeclarationDiagnosticKind::UnsizedArrayDimension,
                 parameter.range,
                 "Only a final device or call-prototype .param .b8 input may "
                 "be unsized.");
        return;
      }
      requireParameterAvailability(module_version, module_sm, {6, 0}, 30,
                                   parameter.range, "Unsized .param input");
    }
    if (is_parameter && (context == ParameterContext::DeviceInput ||
                         context == ParameterContext::DeviceReturn)) {
      requireParameterAvailability(module_version, module_sm, {2, 0}, 20,
                                   parameter.range, "Device .param formal");
    }
    if (parameter.is_pointer) {
      if (context != ParameterContext::EntryInput || !is_parameter || !scalar ||
          parameter.is_array ||
          (*scalar != base::ScalarType::U32 &&
           *scalar != base::ScalarType::U64)) {
        diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
                 parameter.range,
                 ".ptr is supported only on scalar .entry .param .u32/.u64 "
                 "inputs.");
      } else {
        requireParameterAvailability(module_version, module_sm, {2, 2}, 0,
                                     parameter.range, ".ptr entry parameter");
      }
    }
    if (scalar && parameter.array_size) {
      const auto extent = constantArrayExtent(*parameter.array_size);
      if (extent && *extent != 0 &&
          !parameterByteExtent(*scalar, true, parameter.array_size)) {
        diagnose(DeclarationDiagnosticKind::StorageExtentOverflow,
                 parameter.array_size->range,
                 "Parameter byte extent overflows uint64_t.");
      }
    }
  }

  /** Validate the role-specific formal parameters of one function header. */
  void checkFunctionParameters(const syntax_ast::AstFunction& function,
                               std::optional<PtxVersion> module_version,
                               std::optional<uint32_t> module_sm) {
    if (function.is_entry) {
      if (!function.return_parameters.empty()) {
        diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
                 function.return_parameters.front().range,
                 ".entry declarations cannot have return parameters.");
      }
      for (size_t index = 0; index < function.parameters.size(); ++index) {
        checkHeaderParameter(
            function.parameters[index], ParameterContext::EntryInput,
            index + 1 == function.parameters.size(), module_version, module_sm);
      }
      if (!function.parameters.empty()) {
        requireParameterAvailability(module_version, module_sm, {1, 4}, 0,
                                     function.parameters.front().range,
                                     ".entry parameter list");
        checkEntryParameterSize(function.parameters, module_version);
      }
      return;
    }
    if (function.return_parameters.size() > 1 && module_version && module_sm &&
        *module_version >= PtxVersion{2, 0} && *module_sm >= 20) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               function.return_parameters[1].range,
               "Modern device-function declarations support at most one return "
               "parameter.",
               function.return_parameters.front().range);
    }
    for (size_t index = 0; index < function.return_parameters.size(); ++index) {
      checkHeaderParameter(function.return_parameters[index],
                           ParameterContext::DeviceReturn,
                           index + 1 == function.return_parameters.size(),
                           module_version, module_sm);
    }
    for (size_t index = 0; index < function.parameters.size(); ++index) {
      checkHeaderParameter(
          function.parameters[index], ParameterContext::DeviceInput,
          index + 1 == function.parameters.size(), module_version, module_sm);
    }
  }

  /** Check the version-dependent static byte limit for one entry header. */
  void checkEntryParameterSize(
      const std::vector<syntax_ast::AstFunctionParameter>& parameters,
      std::optional<PtxVersion> module_version) {
    if (!module_version)
      return;
    const uint64_t limit = *module_version < PtxVersion{1, 5}   ? 256
                           : *module_version < PtxVersion{8, 1} ? 4352
                                                                : 32764;
    uint64_t total = 0;
    for (const auto& parameter : parameters) {
      if (parameter.state_space != syntax_ast::AstStateSpace::Parameter)
        return;
      const auto scalar = parameterScalarType(parameter.type.text);
      const auto bytes = scalar
                             ? parameterByteExtent(*scalar, parameter.is_array,
                                                   parameter.array_size)
                             : std::nullopt;
      if (!bytes)
        return;
      if (parameter.alignment && !isValidAlignment(parameter.alignment->text))
        return;
      const uint64_t alignment =
          parameter.alignment
              ? positiveCount(parameter.alignment->text).value_or(0)
              : base::scalar_size_of(*scalar);
      if (alignment == 0)
        return;
      if (total > std::numeric_limits<uint64_t>::max() - (alignment - 1)) {
        diagnose(DeclarationDiagnosticKind::StorageExtentOverflow,
                 parameter.range,
                 "Aligned entry parameter byte size overflows uint64_t.");
        return;
      }
      total = ((total + alignment - 1) / alignment) * alignment;
      if (total > std::numeric_limits<uint64_t>::max() - *bytes) {
        diagnose(DeclarationDiagnosticKind::StorageExtentOverflow,
                 parameter.range,
                 "Aligned entry parameter byte size overflows uint64_t.");
        return;
      }
      total += *bytes;
    }
    if (total > limit) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               parameters.front().range,
               fmt::format("Static entry parameter bytes ({}) exceed the PTX "
                           "ISA {}.{} limit of {} bytes.",
                           total, module_version->major, module_version->minor,
                           limit));
    }
  }

  /** Validate PTX-versioned local call-prototype parameter declarations. */
  void checkCallPrototype(const syntax_ast::AstCallPrototype& prototype,
                          std::optional<PtxVersion> module_version,
                          std::optional<uint32_t> module_sm) {
    requireParameterAvailability(module_version, module_sm, {2, 1}, 20,
                                 prototype.range, ".callprototype");
    if (prototype.noreturn_directive && !prototype.return_parameters.empty()) {
      diagnose(DeclarationDiagnosticKind::InvalidCallPrototype,
               prototype.noreturn_directive->range,
               "A .callprototype with return parameters cannot specify "
               ".noreturn.",
               prototype.return_parameters.front().range);
    }
    if (prototype.return_parameters.size() > 1 && module_version && module_sm &&
        *module_version >= PtxVersion{2, 0} && *module_sm >= 20) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               prototype.return_parameters[1].range,
               "Modern call-prototype declarations support at most one return "
               "parameter.",
               prototype.return_parameters.front().range);
    }
    for (size_t index = 0; index < prototype.return_parameters.size();
         ++index) {
      checkHeaderParameter(prototype.return_parameters[index],
                           ParameterContext::CallPrototypeReturn,
                           index + 1 == prototype.return_parameters.size(),
                           module_version, module_sm);
    }
    for (size_t index = 0; index < prototype.parameters.size(); ++index) {
      checkHeaderParameter(
          prototype.parameters[index], ParameterContext::CallPrototypeInput,
          index + 1 == prototype.parameters.size(), module_version, module_sm);
    }
  }

  void checkControlFlowMetadata(std::optional<PtxVersion> module_version,
                                const std::vector<FunctionTargetContext>&
                                    function_targets) {
    std::unordered_map<std::string, SeenFunction> seen_functions;
    for (const FunctionTargetContext& context : function_targets) {
      const auto* function = context.function;
      seen_functions.try_emplace(function->name.syntax.text,
                                 SeenFunction{functionSignature(*function)});
      const auto scope = symbols_.functionScope(function->range);
      if (scope)
        checkControlFlowMetadataBody(function->body, *scope, seen_functions,
                                     module_version, context.target_sm);
    }
  }

  void checkControlFlowMetadataBody(
      const std::vector<syntax_ast::AstFunctionBodyItem>& body,
      binding::ScopeId function_scope,
      const std::unordered_map<std::string, SeenFunction>& seen_functions,
      std::optional<PtxVersion> module_version,
      std::optional<uint32_t> module_sm) {
    for (const auto& body_item : body) {
      if (const auto* prototype =
              std::get_if<syntax_ast::AstCallPrototype>(&body_item)) {
        checkCallPrototype(*prototype, module_version, module_sm);
      } else if (const auto* targets =
                     std::get_if<syntax_ast::AstCallTargets>(&body_item)) {
        checkCallTargets(*targets, seen_functions);
      } else if (const auto* targets =
                     std::get_if<syntax_ast::AstBranchTargets>(&body_item)) {
        checkBranchTargets(function_scope, *targets);
      } else if (const auto* block =
                     std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(
                         &body_item);
                 block != nullptr && *block) {
        checkControlFlowMetadataBody((*block)->body, function_scope,
                                     seen_functions, module_version, module_sm);
      }
    }
  }

  void checkFunctionBodyDeclarations(
      const std::vector<syntax_ast::AstFunctionBodyItem>& body,
      std::optional<PtxVersion> module_version,
      std::optional<uint32_t> module_sm) {
    for (const auto& body_item : body) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&body_item)) {
        checkAlignment(declaration->alignment);
        checkVariableDeclaration(*declaration);
        if (declaration->state_space == syntax_ast::AstStateSpace::Parameter) {
          checkBodyParameterDeclaration(*declaration, module_version,
                                        module_sm);
        }
      } else if (const auto* block =
                     std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(
                         &body_item);
                 block != nullptr && *block) {
        checkFunctionBodyDeclarations((*block)->body, module_version,
                                      module_sm);
      }
    }
  }

  /** Validate body-local .param objects without assigning call ABI allocation. */
  void checkBodyParameterDeclaration(
      const syntax_ast::AstVariableDeclaration& declaration,
      std::optional<PtxVersion> module_version,
      std::optional<uint32_t> module_sm) {
    requireParameterAvailability(module_version, module_sm, {2, 0}, 20,
                                 declaration.range, "Body-local call .param");
    if (declaration.vector_type) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               declaration.vector_type->range,
               "Vector body-local .param declarations are not modeled by this "
               "frontend.");
      return;
    }
    if (isOpaqueParameterType(declaration.type.text)) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               declaration.type.range,
               "Opaque .texref/.samplerref/.surfref parameters are not "
               "modeled by this frontend.");
      return;
    }
    const auto scalar = parameterScalarType(declaration.type.text);
    if (!scalar) {
      diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
               declaration.type.range,
               declaration.type.text == ".pred"
                   ? ".pred parameters must use .reg state space."
               : declaration.type.text == ".f16x2"
                   ? "Fundamental .f16x2 parameter storage is not "
                     "modeled by this frontend."
                   : fmt::format("Parameter type '{}' is not a supported "
                                 "fundamental scalar type.",
                                 declaration.type.text));
      return;
    }
    for (const auto& declarator : declaration.declarators) {
      if (declarator.initializer) {
        diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
                 declarator.initializer->range,
                 "Body-local .param declarations cannot have initializers.");
      }
      if (declarator.parameterized_count) {
        diagnose(DeclarationDiagnosticKind::UnsupportedParameterDeclaration,
                 declarator.parameterized_count->range,
                 "Parameterized body-local .param declaration groups are not "
                 "modeled by this frontend.");
        continue;
      }
      uint64_t bytes = base::scalar_size_of(*scalar);
      for (const auto& dimension : declarator.array_dimensions) {
        if (!dimension.size) {
          if (declarator.initializer) {
            diagnose(DeclarationDiagnosticKind::UnsizedArrayDimension,
                     dimension.range,
                     "Body-local .param array dimensions must be sized.");
          }
          break;
        }
        const auto extent = constantArrayExtent(*dimension.size);
        if (!extent || *extent == 0) {
          break;
        }
        if (bytes > std::numeric_limits<uint64_t>::max() / *extent) {
          diagnose(DeclarationDiagnosticKind::StorageExtentOverflow,
                   dimension.range,
                   "Body-local parameter byte extent overflows uint64_t.");
          break;
        }
        bytes *= *extent;
      }
    }
  }

  void checkDimension(const AstConstantExpression& expression) {
    const ExpressionInfo value = classifyExpression(expression, &diagnostics_);
    const auto integer = nonnegativeIntegerValue(value);
    if (!integer || *integer == 0) {
      diagnose(DeclarationDiagnosticKind::InvalidArrayDimension,
               expression.range,
               "Array dimension must evaluate to a positive integer constant.");
    }
  }

  void checkAlignment(const std::optional<syntax_ast::AstSyntax>& alignment) {
    if (alignment && !isValidAlignment(alignment->text)) {
      diagnose(DeclarationDiagnosticKind::InvalidAlignment, alignment->range,
               "Declaration alignment must be a positive power of two.");
    }
  }

  void checkVariableDeclaration(
      const syntax_ast::AstVariableDeclaration& declaration) {
    for (const auto& declarator : declaration.declarators) {
      std::vector<std::optional<uint64_t>> extents;
      extents.reserve(declarator.array_dimensions.size() +
                      (declaration.vector_type ? 1 : 0));
      const bool is_external = declarationLinkage(declaration.qualifiers) ==
                               binding::SymbolLinkage::External;
      const bool is_external_storage =
          is_external &&
          (declaration.state_space == syntax_ast::AstStateSpace::Global ||
           declaration.state_space == syntax_ast::AstStateSpace::Constant ||
           declaration.state_space == syntax_ast::AstStateSpace::Shared ||
           declaration.state_space == syntax_ast::AstStateSpace::Local);
      for (size_t index = 0; index < declarator.array_dimensions.size();
           ++index) {
        const auto& dimension = declarator.array_dimensions[index];
        if (!dimension.size) {
          extents.push_back(std::nullopt);
          if (index != 0 || (!declarator.initializer && !is_external_storage)) {
            diagnose(DeclarationDiagnosticKind::UnsizedArrayDimension,
                     dimension.range,
                     index == 0
                         ? "An unsized first array dimension requires an "
                           "initializer unless it is an external declaration."
                         : "Only the first array dimension may be unsized.");
          }
          continue;
        }
        const ExpressionInfo value =
            classifyExpression(*dimension.size, &diagnostics_);
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
      const ExpressionInfo info =
          classifyExpression(*expression, &diagnostics_);
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

  /** Return the binding target recorded for this exact initializer token. */
  std::optional<binding::SymbolLookup> initializerTarget(
      SourceRange range) const {
    const auto reference = std::ranges::find_if(
        symbols_.references(),
        [range](const binding::SymbolReference& candidate) {
          return candidate.kind == binding::ReferenceKind::Initializer &&
                 candidate.range == range;
        });
    if (reference == symbols_.references().end())
      return std::nullopt;
    return reference->target;
  }

  void checkInitializerSymbols(const AstConstantExpression& expression) {
    std::visit(
        [this](const auto& value) {
          using Value = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::same_as<Value, syntax_ast::AstConstantLiteral>) {
            return;
          } else if constexpr (std::same_as<Value,
                                            syntax_ast::AstConstantSymbol>) {
            const auto lookup = initializerTarget(value.name.syntax.range);
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

FunctionSignature functionSignature(
    const syntax_ast::AstCallPrototype& prototype) {
  FunctionSignature signature{
      .is_noreturn = prototype.noreturn_directive.has_value(),
  };
  const auto append_contracts = [](const auto& parameters, auto& contracts) {
    contracts.reserve(parameters.size());
    for (const auto& parameter : parameters)
      contracts.push_back(parameterContract(parameter));
  };
  append_contracts(prototype.return_parameters, signature.return_parameters);
  append_contracts(prototype.parameters, signature.parameters);
  return signature;
}

std::optional<uint64_t> constantArrayExtent(
    const syntax_ast::AstConstantExpression& expression) {
  return nonnegativeIntegerValue(classifyExpression(expression, nullptr));
}

std::optional<IntegerConstantValue> constantIntegerValue(
    const syntax_ast::AstConstantExpression& expression) {
  const ExpressionInfo info = classifyExpression(expression, nullptr);
  if (info.category != ExpressionCategory::Integer || !info.integer_value)
    return std::nullopt;
  return IntegerConstantValue{
      .bits = info.integer_value->bits,
      .is_unsigned =
          info.integer_value->type == ExpressionInfo::IntegerType::Unsigned,
  };
}

std::optional<base::ScalarType> parameterScalarType(
    std::string_view spelling) noexcept {
  using base::ScalarType;
  if (spelling == ".u8")
    return ScalarType::U8;
  if (spelling == ".u16")
    return ScalarType::U16;
  if (spelling == ".u32")
    return ScalarType::U32;
  if (spelling == ".u64")
    return ScalarType::U64;
  if (spelling == ".s8")
    return ScalarType::S8;
  if (spelling == ".s16")
    return ScalarType::S16;
  if (spelling == ".s32")
    return ScalarType::S32;
  if (spelling == ".s64")
    return ScalarType::S64;
  if (spelling == ".b8")
    return ScalarType::B8;
  if (spelling == ".b16")
    return ScalarType::B16;
  if (spelling == ".b32")
    return ScalarType::B32;
  if (spelling == ".b64")
    return ScalarType::B64;
  if (spelling == ".b128")
    return ScalarType::B128;
  if (spelling == ".f16")
    return ScalarType::F16;
  if (spelling == ".f32")
    return ScalarType::F32;
  if (spelling == ".f64")
    return ScalarType::F64;
  return std::nullopt;
}

std::vector<DeclarationDiagnostic> checkDeclarations(
    const syntax_ast::AstModule& module, const binding::SymbolTable& symbols) {
  return Checker{symbols}.run(module);
}

}  // namespace ptx_frontend::declaration_semantics
