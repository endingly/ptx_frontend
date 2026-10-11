#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/common/source_loc.hpp>
#include <ptx_frontend/semantic/ptx_constant_value.hpp>
#include <ptx_frontend/semantic/ptx_function_contract.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::declaration_semantics {

/** Why a source constant cannot supply a concrete numeric operand value. */
struct ConstantEvaluationDiagnostic {
  /** Failing subexpression rather than an unrelated instruction range. */
  SourceRange range;
  /** Explanation of the invalid or deferred numeric use. */
  std::string message;
  /** True for a valid symbolic/deferred expression without a numeric value. */
  bool deferred{};
};

/** Evaluate a bounded source tree, distinguishing invalid from deferred values. */
[[nodiscard]] std::expected<NumericConstantValue, ConstantEvaluationDiagnostic>
numericConstantValue(const syntax_ast::AstConstantExpression& expression);

/** Build the canonical signature used by declaration checking and call ABI work. */
[[nodiscard]] FunctionSignature functionSignature(
    const syntax_ast::AstFunction& function);

/** Build the same canonical ABI signature for a local call prototype. */
[[nodiscard]] FunctionSignature functionSignature(
    const syntax_ast::AstCallPrototype& prototype);

/**
 * Return a nonnegative constant array extent when the expression has one.
 *
 * Invalid literals and valid deferred expressions both return ``nullopt``;
 * ``checkDeclarations`` emits the source-located invalid-literal diagnostic
 * for declaration expressions.
 */
[[nodiscard]] std::optional<uint64_t> constantArrayExtent(
    const syntax_ast::AstConstantExpression& expression);

/**
 * Return an evaluated integer expression without imposing an array-extent
 * sign rule.
 *
 * Invalid literals and valid deferred expressions both return ``nullopt``;
 * ``checkDeclarations`` emits the source-located invalid-literal diagnostic
 * for declaration expressions.
 */
[[nodiscard]] std::optional<IntegerConstantValue> constantIntegerValue(
    const syntax_ast::AstConstantExpression& expression);

/**
 * Classify a supported non-predicate fundamental scalar parameter spelling.
 *
 * Opaque parameter identities and unsupported or instruction-only spellings
 * deliberately have no scalar classification.
 */
[[nodiscard]] std::optional<base::ScalarType> parameterScalarType(
    std::string_view spelling) noexcept;

enum class DeclarationDiagnosticKind : uint8_t {
  InvalidArrayDimension,
  UnsizedArrayDimension,
  InitializerShapeMismatch,
  ExcessInitializerElements,
  InitializerTypeMismatch,
  InvalidInitializerExpression,
  IncompatibleRedeclaration,
  MultipleDefinitions,
  InvalidLinkage,
  InvalidAlignment,
  ModuleScopeParameter,
  DuplicateMetadataTarget,
  UnresolvedMetadataTarget,
  InvalidMetadataTarget,
  IncompatibleCallTargetSignature,
  InvalidCallPrototype,
  UnsupportedKernelResourcePtxVersion,
  IncompatibleKernelResourceDirective,
  UnsupportedDirectivePtxVersion,
  InvalidDeclarationDirective,
  InvalidFunctionAlias,
  StorageExtentOverflow,
  UnsupportedStorageDeclaration,
  UnsupportedStorageInitializer,
  UnsupportedParameterDeclaration,
  /** An integer literal cannot be decoded as an exact uint64_t magnitude. */
  InvalidIntegerLiteral,
  /** A function address was used before its declaration occurrence. */
  FunctionAddressBeforeDeclaration,
  /** A `.reg` declaration type token is not a recognized PTX scalar spelling. */
  UnknownRegisterDeclarationType,
  /** A recognized instruction-only scalar spelling was used in `.reg`. */
  UnsupportedRegisterDeclarationType,
  /** A `.reg` declaration uses a predicate or vector shape PTX does not allow. */
  InvalidRegisterDeclarationShape,
};

struct DeclarationDiagnostic {
  DeclarationDiagnosticKind kind{};
  SourceRange range;
  std::optional<SourceRange> previous_range;
  std::string message;
};

/**
 * Check declaration rules that require structured AST or cross-declaration
 * information. Lexical name lookup remains the responsibility of binding.
 */
[[nodiscard]] std::vector<DeclarationDiagnostic> checkDeclarations(
    const syntax_ast::AstModule& module, const binding::SymbolTable& symbols);

}  // namespace ptx_frontend::declaration_semantics
