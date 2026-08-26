#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <ptx_frontend/common/source_loc.hpp>

namespace ptx_frontend::syntax_ast {

/** Source spelling retained by a semantic syntax leaf. */
struct AstSyntax {
  std::string text;
  SourceRange range;
};

struct AstOpcode {
  AstSyntax syntax;
};

struct AstModifier {
  AstSyntax syntax;
};

/** An unresolved register, variable, function, label, or symbol reference. */
struct AstIdentifierRef {
  AstSyntax syntax;
};

/** A predicate operand, optionally complemented with a leading ``!``. */
struct AstPredicateOperand {
  bool negated{};
  AstIdentifierRef name;
  SourceRange range;
};

/** Lexical category of the literal token underlying an immediate. */
enum class AstImmediateKind : uint8_t {
  DecimalInteger,
  HexInteger,
  F32Hex,
  F64Hex,
  DecimalFloat,
  WarpSize,
};

/** A lexical literal whose semantic value is decoded during resolution. */
struct AstImmediate {
  AstSyntax syntax;
  AstImmediateKind kind = AstImmediateKind::DecimalInteger;
};

struct AstAddressOffset {
  enum class Operator : uint8_t { Add, Subtract };

  Operator operation = Operator::Add;
  AstImmediate magnitude;
  SourceRange range;
};

/** An unresolved bracketed or unbracketed PTX address expression. */
struct AstAddress {
  std::variant<AstIdentifierRef, AstImmediate> base;
  std::optional<AstAddressOffset> offset;
  bool bracketed{};
  SourceRange range;
};

struct AstVectorMember {
  AstIdentifierRef base;
  AstSyntax selector;
  SourceRange range;
};

using AstVectorElement = std::variant<AstIdentifierRef, AstImmediate>;

struct AstVectorPack {
  std::vector<AstVectorElement> elements;
  SourceRange range;
};

enum class AstCallParameterListKind : uint8_t {
  Return,
  Input,
};

using AstCallParameter = std::variant<AstIdentifierRef, AstImmediate>;

struct AstCallParameterList {
  AstCallParameterListKind kind{};
  std::vector<AstCallParameter> parameters;
  SourceRange range;
};

struct AstCallTarget {
  AstIdentifierRef name;
  SourceRange range;
};

struct AstCallTargetSet {
  AstIdentifierRef name;
  SourceRange range;
};

struct AstBranchTarget {
  AstIdentifierRef name;
  SourceRange range;
};

struct AstBranchTargetSet {
  AstIdentifierRef name;
  SourceRange range;
};

/** Grammar shapes consumed by descriptor-driven operand resolution. */
using AstOperand =
    std::variant<AstIdentifierRef, AstPredicateOperand, AstImmediate,
                 AstAddress, AstVectorMember, AstVectorPack,
                 AstCallParameterList, AstCallTarget, AstCallTargetSet,
                 AstBranchTarget, AstBranchTargetSet>;

/** Return the source range shared by every operand alternative. */
inline SourceRange sourceRange(const AstOperand& operand) {
  return std::visit(
      [](const auto& value) {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, AstIdentifierRef> ||
                      std::same_as<Value, AstImmediate>)
          return value.syntax.range;
        else
          return value.range;
      },
      operand);
}

struct AstPredicate {
  bool negated{};
  AstIdentifierRef name;
  SourceRange range;
};

struct AstInstruction {
  AstOpcode opcode;
  std::vector<AstModifier> modifiers;
  std::vector<AstOperand> operands;
  std::optional<AstPredicate> predicate;
  SourceRange range;
};

struct AstVersionDirective {
  AstSyntax version;
  SourceRange range;
};

struct AstTargetDirective {
  std::vector<AstSyntax> targets;
  SourceRange range;
};

struct AstAddressSizeDirective {
  AstSyntax bit_width;
  SourceRange range;
};

struct AstConstantExpression;
using AstConstantExpressionPtr = std::unique_ptr<AstConstantExpression>;

enum class AstConstantUnaryOperator : uint8_t {
  Plus,
  Minus,
  LogicalNot,
  BitwiseNot,
};

enum class AstConstantBinaryOperator : uint8_t {
  Multiply,
  Divide,
  Remainder,
  Add,
  Subtract,
  ShiftLeft,
  ShiftRight,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Equal,
  NotEqual,
  BitwiseAnd,
  BitwiseXor,
  BitwiseOr,
  LogicalAnd,
  LogicalOr,
};

struct AstConstantLiteral {
  AstImmediate value;
};

struct AstConstantSymbol {
  AstIdentifierRef name;
};

struct AstConstantParenthesized {
  AstConstantExpressionPtr expression;
};

struct AstConstantCall {
  AstConstantExpressionPtr callee;
  AstConstantExpressionPtr argument;
};

struct AstConstantCast {
  AstSyntax type;
  AstConstantExpressionPtr operand;
};

struct AstConstantUnary {
  AstConstantUnaryOperator operation{};
  AstConstantExpressionPtr operand;
};

struct AstConstantBinary {
  AstConstantExpressionPtr left;
  AstConstantBinaryOperator operation{};
  AstConstantExpressionPtr right;
};

struct AstConstantConditional {
  AstConstantExpressionPtr condition;
  AstConstantExpressionPtr true_expression;
  AstConstantExpressionPtr false_expression;
};

using AstConstantExpressionNode =
    std::variant<AstConstantLiteral, AstConstantSymbol,
                 AstConstantParenthesized, AstConstantCall, AstConstantCast,
                 AstConstantUnary, AstConstantBinary, AstConstantConditional>;

struct AstConstantExpression {
  AstConstantExpressionNode node;
  SourceRange range;
};

struct AstInitializer;

struct AstInitializerList {
  std::vector<AstInitializer> elements;
  SourceRange range;
};

struct AstInitializer {
  std::variant<AstConstantExpression, AstInitializerList> value;
  SourceRange range;
};

struct AstArrayDimension {
  std::optional<AstConstantExpression> size;
  SourceRange range;
};

struct AstVariableDeclarator {
  AstIdentifierRef name;
  std::optional<AstSyntax> parameterized_count;
  std::vector<AstArrayDimension> array_dimensions;
  std::optional<AstInitializer> initializer;
  SourceRange range;
};

enum class AstStateSpace : uint8_t {
  Register,
  Parameter,
  Local,
  Shared,
  Global,
  Constant,
};

struct AstVariableDeclaration {
  std::vector<AstSyntax> qualifiers;
  AstStateSpace state_space{};
  std::optional<AstSyntax> alignment;
  std::optional<AstSyntax> vector_type;
  AstSyntax type;
  std::vector<AstVariableDeclarator> declarators;
  SourceRange range;
};

struct AstLabel {
  AstIdentifierRef name;
  SourceRange range;
};

struct AstFunctionParameter {
  AstStateSpace state_space{};
  std::optional<AstSyntax> alignment;
  AstSyntax type;
  bool is_pointer{};
  std::optional<AstSyntax> pointer_space;
  std::optional<AstSyntax> pointer_alignment;
  AstIdentifierRef name;
  bool is_array{};
  std::optional<AstConstantExpression> array_size;
  SourceRange range;
};

struct AstCallPrototypeAbiSuffix {
  AstSyntax directive;
  AstSyntax count;
  SourceRange range;
};

/** A function-local label-associated indirect-call prototype. */
struct AstCallPrototype {
  AstIdentifierRef label;
  std::vector<AstFunctionParameter> return_parameters;
  AstIdentifierRef sink;
  std::vector<AstFunctionParameter> parameters;
  std::optional<AstSyntax> noreturn_directive;
  std::optional<AstCallPrototypeAbiSuffix> abi_preserve;
  std::optional<AstCallPrototypeAbiSuffix> abi_preserve_control;
  SourceRange range;
};

/** A function-local label-associated indirect-call target list. */
struct AstCallTargets {
  AstIdentifierRef label;
  std::vector<AstIdentifierRef> targets;
  SourceRange range;
};

struct AstBranchTargetEntry {
  AstIdentifierRef name;
  std::optional<AstSyntax> count;
  SourceRange range;
};

/** A function-local label-associated indexed branch target list. */
struct AstBranchTargets {
  AstIdentifierRef label;
  std::vector<AstBranchTargetEntry> targets;
  SourceRange range;
};

struct AstBlock;

using AstFunctionBodyItem =
    std::variant<AstVariableDeclaration, AstLabel, AstCallPrototype,
                 AstCallTargets, AstBranchTargets, std::unique_ptr<AstBlock>,
                 AstInstruction>;

/** A lexically nested function-body block. */
struct AstBlock {
  std::vector<AstFunctionBodyItem> body;
  SourceRange range;
};

/** Initial function container; declarations and parameters refine this later. */
struct AstFunction {
  bool is_entry{};
  bool is_prototype{};
  bool is_noreturn{};
  std::vector<AstSyntax> qualifiers;
  AstIdentifierRef name;
  std::vector<AstFunctionParameter> return_parameters;
  std::vector<AstFunctionParameter> parameters;
  std::vector<AstFunctionBodyItem> body;
  SourceRange range;
};

using AstModuleItem =
    std::variant<AstVersionDirective, AstTargetDirective,
                 AstAddressSizeDirective, AstVariableDeclaration, AstFunction>;

struct AstModule {
  std::vector<AstModuleItem> items;
  SourceRange range;
};

using AstRoot = std::variant<AstInstruction, AstModule>;

struct AstFile {
  AstRoot root;

  [[nodiscard]] const AstInstruction* instruction() const noexcept {
    return std::get_if<AstInstruction>(&root);
  }

  [[nodiscard]] const AstModule* module() const noexcept {
    return std::get_if<AstModule>(&root);
  }
};

}  // namespace ptx_frontend::syntax_ast
