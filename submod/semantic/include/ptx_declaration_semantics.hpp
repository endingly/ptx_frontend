#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/common/source_loc.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::declaration_semantics {

/** ABI-relevant, source-location-independent function parameter data. */
struct FunctionParameterContract {
  syntax_ast::AstStateSpace state_space{};
  /** Effective byte alignment as a decimal key, or retained invalid source text. */
  std::optional<std::string> alignment;
  std::string type;
  bool is_pointer{};
  std::optional<std::string> pointer_space;
  /** Effective pointee alignment (default four bytes); absent for non-pointers. */
  std::optional<std::string> pointer_alignment;
  bool is_array{};
  /** A normalized constant extent, or a structural key for an invalid one. */
  std::optional<std::string> array_extent;

  bool operator==(const FunctionParameterContract&) const = default;
};

/** Canonical ABI-relevant data shared by function declarations and bodies. */
struct FunctionSignature {
  bool is_entry{};
  bool is_noreturn{};
  std::vector<FunctionParameterContract> return_parameters;
  std::vector<FunctionParameterContract> parameters;

  bool operator==(const FunctionSignature&) const = default;
};

/** A fully evaluated integer constant with the signedness used by PTX rules. */
struct IntegerConstantValue {
  /** Two's-complement bits of the evaluated 64-bit integer expression. */
  uint64_t bits{};
  /** True when the expression's usual-arithmetic result is unsigned. */
  bool is_unsigned{};

  /** Compare the normalized integer bits and signedness. */
  bool operator==(const IntegerConstantValue&) const = default;
};

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
