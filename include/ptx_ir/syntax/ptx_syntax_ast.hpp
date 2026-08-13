#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "ptx_ir/source_loc.hpp"

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

/** Grammar shapes consumed by descriptor-driven operand resolution. */
using AstOperand =
    std::variant<AstIdentifierRef, AstPredicateOperand, AstImmediate,
                 AstAddress, AstVectorMember, AstVectorPack>;

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

struct AstArrayDimension {
  std::vector<AstSyntax> size_tokens;
  SourceRange range;
};

struct AstVariableDeclarator {
  AstIdentifierRef name;
  std::optional<AstSyntax> register_count;
  std::vector<AstArrayDimension> array_dimensions;
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

using AstFunctionBodyItem =
    std::variant<AstVariableDeclaration, AstLabel, AstInstruction>;

struct AstFunctionParameter {
  AstStateSpace state_space{};
  std::optional<AstSyntax> alignment;
  AstSyntax type;
  bool is_pointer{};
  std::optional<AstSyntax> pointer_space;
  std::optional<AstSyntax> pointer_alignment;
  AstIdentifierRef name;
  bool is_array{};
  std::optional<AstSyntax> array_size;
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
