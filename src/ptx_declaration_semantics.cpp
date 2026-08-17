#include "ptx_ir/semantic/ptx_declaration_semantics.hpp"

#include <charconv>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

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
  std::optional<uint64_t> nonnegative_value;
};

std::optional<uint64_t> parseIntegerLiteral(std::string_view spelling,
                                            int base) {
  if (!spelling.empty() && (spelling.back() == 'u' || spelling.back() == 'U')) {
    spelling.remove_suffix(1);
  }
  if (base == 16 && spelling.size() >= 2)
    spelling.remove_prefix(2);
  uint64_t value = 0;
  const auto [end, error] = std::from_chars(
      spelling.data(), spelling.data() + spelling.size(), value, base);
  if (error != std::errc{} || end != spelling.data() + spelling.size())
    return std::nullopt;
  return value;
}

std::optional<uint64_t> checkedAdd(uint64_t left, uint64_t right) {
  if (left > std::numeric_limits<uint64_t>::max() - right)
    return std::nullopt;
  return left + right;
}

std::optional<uint64_t> checkedMultiply(uint64_t left, uint64_t right) {
  if (right != 0 && left > std::numeric_limits<uint64_t>::max() / right)
    return std::nullopt;
  return left * right;
}

ExpressionInfo classifyExpression(const AstConstantExpression& expression);

ExpressionInfo classifyBinary(const syntax_ast::AstConstantBinary& binary) {
  const ExpressionInfo left = classifyExpression(*binary.left);
  const ExpressionInfo right = classifyExpression(*binary.right);
  using Operator = AstConstantBinaryOperator;

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

  const bool comparison = binary.operation == Operator::Less ||
                          binary.operation == Operator::LessEqual ||
                          binary.operation == Operator::Greater ||
                          binary.operation == Operator::GreaterEqual ||
                          binary.operation == Operator::Equal ||
                          binary.operation == Operator::NotEqual;
  if (comparison && left.category == right.category &&
      left.category != ExpressionCategory::Invalid) {
    std::optional<uint64_t> value;
    if (left.nonnegative_value && right.nonnegative_value) {
      const uint64_t lhs = *left.nonnegative_value;
      const uint64_t rhs = *right.nonnegative_value;
      switch (binary.operation) {
        case Operator::Less:
          value = lhs < rhs;
          break;
        case Operator::LessEqual:
          value = lhs <= rhs;
          break;
        case Operator::Greater:
          value = lhs > rhs;
          break;
        case Operator::GreaterEqual:
          value = lhs >= rhs;
          break;
        case Operator::Equal:
          value = lhs == rhs;
          break;
        case Operator::NotEqual:
          value = lhs != rhs;
          break;
        default:
          break;
      }
    }
    return {ExpressionCategory::Integer, value};
  }

  if (left.category != right.category ||
      (left.category != ExpressionCategory::Integer &&
       left.category != ExpressionCategory::Floating)) {
    return {};
  }

  if (left.category == ExpressionCategory::Floating) {
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

  std::optional<uint64_t> value;
  if (left.nonnegative_value && right.nonnegative_value) {
    const uint64_t lhs = *left.nonnegative_value;
    const uint64_t rhs = *right.nonnegative_value;
    switch (binary.operation) {
      case Operator::Multiply:
        value = checkedMultiply(lhs, rhs);
        break;
      case Operator::Divide:
        if (rhs != 0)
          value = lhs / rhs;
        break;
      case Operator::Remainder:
        if (rhs != 0)
          value = lhs % rhs;
        break;
      case Operator::Add:
        value = checkedAdd(lhs, rhs);
        break;
      case Operator::Subtract:
        if (lhs >= rhs)
          value = lhs - rhs;
        break;
      case Operator::ShiftLeft:
        if (rhs < 64 && lhs <= (std::numeric_limits<uint64_t>::max() >> rhs))
          value = lhs << rhs;
        break;
      case Operator::ShiftRight:
        if (rhs < 64)
          value = lhs >> rhs;
        break;
      case Operator::BitwiseAnd:
        value = lhs & rhs;
        break;
      case Operator::BitwiseXor:
        value = lhs ^ rhs;
        break;
      case Operator::BitwiseOr:
        value = lhs | rhs;
        break;
      case Operator::LogicalAnd:
        value = lhs != 0 && rhs != 0;
        break;
      case Operator::LogicalOr:
        value = lhs != 0 || rhs != 0;
        break;
      default:
        break;
    }
  }
  return {ExpressionCategory::Integer, value};
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
              return {ExpressionCategory::Integer, 32};
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
          const ExpressionInfo operand = classifyExpression(*value.operand);
          return operand.category == ExpressionCategory::Integer
                     ? operand
                     : ExpressionInfo{};
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
                operand.nonnegative_value && *operand.nonnegative_value != 0) {
              operand.nonnegative_value.reset();
            }
            return operand;
          }
          if (operand.category != ExpressionCategory::Integer)
            return {};
          if (value.operation == AstConstantUnaryOperator::LogicalNot) {
            if (operand.nonnegative_value)
              operand.nonnegative_value = *operand.nonnegative_value == 0;
          } else {
            operand.nonnegative_value.reset();
          }
          return operand;
        } else if constexpr (std::same_as<Value,
                                          syntax_ast::AstConstantBinary>) {
          return classifyBinary(value);
        } else {
          const ExpressionInfo condition = classifyExpression(*value.condition);
          const ExpressionInfo true_value =
              classifyExpression(*value.true_expression);
          const ExpressionInfo false_value =
              classifyExpression(*value.false_expression);
          if (condition.category != ExpressionCategory::Integer ||
              true_value.category != false_value.category) {
            return {};
          }
          if (condition.nonnegative_value) {
            return *condition.nonnegative_value != 0 ? true_value : false_value;
          }
          return {true_value.category,
                  true_value.nonnegative_value == false_value.nonnegative_value
                      ? true_value.nonnegative_value
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
  if (value.category == ExpressionCategory::Integer &&
      value.nonnegative_value) {
    return fmt::format("#{}", *value.nonnegative_value);
  }
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

std::string parameterKey(const syntax_ast::AstFunctionParameter& parameter) {
  return fmt::format(
      "{}:{}:{}:{}:{}:{}:{}:{}", static_cast<int>(parameter.state_space),
      optionalSyntaxKey(parameter.alignment), parameter.type.text,
      parameter.is_pointer, optionalSyntaxKey(parameter.pointer_space),
      optionalSyntaxKey(parameter.pointer_alignment), parameter.is_array,
      parameter.array_size ? dimensionKey(*parameter.array_size) : "-");
}

std::string functionSignature(const syntax_ast::AstFunction& function) {
  std::string signature =
      fmt::format("function:{}:{}", function.is_entry, function.is_noreturn);
  for (const auto& parameter : function.return_parameters)
    signature += "|r:" + parameterKey(parameter);
  signature += "|inputs";
  for (const auto& parameter : function.parameters)
    signature += "|p:" + parameterKey(parameter);
  return signature;
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

class Checker {
 public:
  explicit Checker(const binding::SymbolTable& symbols) : symbols_(symbols) {}

  std::vector<DeclarationDiagnostic> run(const syntax_ast::AstModule& module) {
    checkRedeclarations(module);
    for (const auto& item : module.items) {
      if (const auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
        checkVariableDeclaration(*declaration);
      } else if (const auto* function =
                     std::get_if<syntax_ast::AstFunction>(&item)) {
        checkFunctionArrays(*function);
        for (const auto& body_item : function->body) {
          if (const auto* declaration =
                  std::get_if<syntax_ast::AstVariableDeclaration>(&body_item)) {
            checkVariableDeclaration(*declaration);
          }
        }
      }
    }
    return std::move(diagnostics_);
  }

 private:
  struct SeenDeclaration {
    std::string signature;
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

  void rememberDeclaration(std::string_view name, std::string signature,
                           binding::SymbolLinkage linkage, bool is_definition,
                           SourceRange range) {
    auto iterator = declarations_.find(std::string{name});
    if (iterator == declarations_.end()) {
      declarations_.emplace(
          std::string{name},
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
          rememberDeclaration(declarator.name.syntax.text,
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
      rememberDeclaration(function->name.syntax.text,
                          functionSignature(*function), linkage,
                          !function->is_prototype, function->name.syntax.range);
    }
  }

  void checkDimension(const AstConstantExpression& expression) {
    const ExpressionInfo value = classifyExpression(expression);
    if (value.category != ExpressionCategory::Integer ||
        !value.nonnegative_value || *value.nonnegative_value == 0) {
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
        if (value.category != ExpressionCategory::Integer ||
            !value.nonnegative_value || *value.nonnegative_value == 0) {
          diagnose(
              DeclarationDiagnosticKind::InvalidArrayDimension,
              dimension.size->range,
              "Array dimension must evaluate to a positive integer constant.");
          extents.push_back(std::nullopt);
        } else {
          extents.push_back(value.nonnegative_value);
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

std::vector<DeclarationDiagnostic> checkDeclarations(
    const syntax_ast::AstModule& module, const binding::SymbolTable& symbols) {
  return Checker{symbols}.run(module);
}

}  // namespace ptx_frontend::declaration_semantics
