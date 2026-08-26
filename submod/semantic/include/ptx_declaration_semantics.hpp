#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/common/source_loc.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::declaration_semantics {

/** ABI-relevant, source-location-independent function parameter data. */
struct FunctionParameterContract {
  syntax_ast::AstStateSpace state_space{};
  std::optional<std::string> alignment;
  std::string type;
  bool is_pointer{};
  std::optional<std::string> pointer_space;
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

/** Build the canonical signature used by declaration checking and call ABI work. */
[[nodiscard]] FunctionSignature functionSignature(
    const syntax_ast::AstFunction& function);

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
