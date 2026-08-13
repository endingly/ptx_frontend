#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ptx_ir/lex/ptx_token.hpp"

namespace ptx_frontend::syntax_ast {

/**
 * Source-derived spelling of one syntax node or token sequence.
 *
 * `text`, `range`, and leading trivia support diagnostics and resolution. This
 * is not a lossless source representation: trivia between combined tokens and
 * punctuation trivia are not retained. Formatting and source-preserving
 * rewriting require the future CST layer.
 */
struct AstSyntax {
  std::string text;
  SourceRange range;
  std::vector<Trivia> leading_trivia;
};

struct AstOpcode {
  AstSyntax syntax;
};

struct AstModifier {
  AstSyntax syntax;
};

/**
 * An unresolved identifier reference.
 *
 * This deliberately represents both `%r1` and `foo`. Whether a spelling names
 * a register, variable, function, or label is a resolution concern.
 */
struct AstIdentifierRef {
  AstSyntax syntax;
};

/** A predicate operand, optionally complemented with a leading ``!``. */
struct AstPredicateOperand {
  AstSyntax syntax;
  bool negated{};
  AstIdentifierRef name;
};

/**
 * A lexical immediate literal, including its parsed spelling.
 *
 * The AST does not decode it to an integer or floating-point value. That may
 * depend on the selected instruction form and is performed during resolution.
 */
struct AstImmediate {
  AstSyntax syntax;
};

struct AstAddressOffset {
  AstSyntax operator_token;  // "+" or "-"
  AstImmediate magnitude;
  SourceRange range;
};

/**
 * A bracketed or unbracketed PTX address expression.
 *
 * The base is still syntactic: it has not been resolved to a register or a
 * symbol, and the AST does not assign an address space.
 */
struct AstAddress {
  AstSyntax syntax;
  std::variant<AstIdentifierRef, AstImmediate> base;
  std::optional<AstAddressOffset> offset;
  bool bracketed{};
};

struct AstVectorMember {
  AstSyntax syntax;
  AstIdentifierRef base;
  AstSyntax selector;  // for example, ".x"
};

using AstVectorElement = std::variant<AstIdentifierRef, AstImmediate>;

struct AstVectorPack {
  AstSyntax syntax;
  std::vector<AstVectorElement> elements;
};

/**
 * One PTX instruction operand, represented by its grammar shape.
 *
 * These alternatives are syntax facts, not semantic operand categories. In
 * particular, `AstIdentifierRef` is intentionally not split into register,
 * variable, label, or function references until the resolver runs.
 */
using AstOperand = std::variant<AstIdentifierRef, AstPredicateOperand,
                                AstImmediate, AstAddress, AstVectorMember,
                                AstVectorPack>;

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

}  // namespace ptx_frontend::syntax_ast
