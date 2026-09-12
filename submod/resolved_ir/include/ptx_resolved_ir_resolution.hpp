#pragma once

#include <expected>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::resolved_ir {

/** Origin of a diagnostic returned through the public resolution API. */
enum class ResolveDiagnosticStage : uint8_t {
  Resolution,
  Binding,
  DeclarationSemantics,
  Checking,
};

/**
 * Owned diagnostic returned by standalone or module resolution.
 *
 * Text and source ranges are values, so this record remains usable after the
 * parsed source, AST, and temporary resolution state have been destroyed.
 */
struct ResolveDiagnostic {
  SourceRange range;
  std::string message;
  std::optional<declaration_semantics::DeclarationDiagnosticKind>
      declaration_kind{};
  std::optional<SourceRange> previous_range{};
  std::optional<binding::BindDiagnosticKind> binding_kind{};
  std::optional<checker::CheckDiagnosticKind> checker_kind{};

  /** Derive the originating stage from the retained typed category. */
  [[nodiscard]] constexpr ResolveDiagnosticStage stage() const noexcept {
    if (binding_kind)
      return ResolveDiagnosticStage::Binding;
    if (declaration_kind)
      return ResolveDiagnosticStage::DeclarationSemantics;
    if (checker_kind)
      return ResolveDiagnosticStage::Checking;
    return ResolveDiagnosticStage::Resolution;
  }
};

/** Raised only when generated descriptors violate their internal contract. */
class ResolveException : public std::runtime_error {
 public:
  explicit ResolveException(
      std::string message,
      std::source_location where = std::source_location::current())
      : std::runtime_error(std::move(message)), where_(where) {}

  /** Return the frontend source location at which the contract failed. */
  [[nodiscard]] const std::source_location& where() const noexcept {
    return where_;
  }

 private:
  std::source_location where_;
};

/** Declaration context used while resolving an instruction inside a module. */
struct ResolveContext {
  const binding::SymbolTable& symbols;
  binding::ScopeId scope;
  std::optional<binding::ScopeId> function_scope;
  bool function_is_entry{};
};

/** Constrain generated instruction records to their public descriptor accessors. */
template <typename T>
concept PtxOperator = requires(T object) {
  typename T::VariantType;
  requires std::is_scoped_enum_v<typename T::VariantType>;
  {
    T::get_syntax_descriptor()
  } -> std::same_as<const check_end::SyntaxInstructionDescriptor&>;
  {
    T::get_resolved_descriptor()
  } -> std::same_as<const check_end::ResolvedInstructionDescriptor&>;
};

/** Resolve a generated instruction against an optional declaration context. */
template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast, const ResolveContext* context);

/** Resolve one standalone generated instruction without declaration binding. */
template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast) {
  return resolve<T>(ast, nullptr);
}

/** Resolve one generated instruction against an explicit declaration context. */
template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast, const ResolveContext& context) {
  return resolve<T>(ast, &context);
}

/** Policy for validating an owned model against source availability context. */
enum class ModuleValidationPolicy : uint8_t {
  /** Preserve fragment support; final instruction checks need version/target. */
  AvailableContext,
  /** Reject a checked source region lacking a recognized version/target. */
  RequireCompleteContext,
};

using ModuleResolveDiagnostics = std::vector<ResolveDiagnostic>;

/** Resolve one standalone instruction without declaration binding. */
std::expected<ResolvedInstruction, ResolveDiagnostic> resolveInstruction(
    const syntax_ast::AstInstruction& ast);

/** Resolve one instruction against an explicit declaration-binding context. */
std::expected<ResolvedInstruction, ResolveDiagnostic> resolveInstruction(
    const syntax_ast::AstInstruction& ast, const ResolveContext& context);

/**
 * Build the owned model and run declaration checks available from source.
 *
 * Binding, declaration semantics, operand resolution, and call ABI/staging run.
 * The final instruction checker (including its target-independent constraints)
 * and directive availability checks are deferred. Known source context still
 * constrains declaration availability. Success is not full instruction validity.
 */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveModuleOnly(
    const syntax_ast::AstModule& ast);

/**
 * Validate an already resolved module under the explicit source-context policy.
 *
 * Source structure must match; a replacement version/target is allowed and its
 * declaration rules are rechecked. Instruction diagnostics use owned IR ranges.
 * Guarantees cover only the modeled instruction/declaration subset.
 */
checker::CheckResult validateModule(
    const syntax_ast::AstModule& ast, const ResolvedModule& module,
    ModuleValidationPolicy policy =
        ModuleValidationPolicy::RequireCompleteContext);

/** Validate owned semantic invariants without reparsing or traversing an AST. */
checker::CheckResult validateModule(
    const ResolvedModule& module,
    ModuleValidationPolicy policy = ModuleValidationPolicy::RequireCompleteContext);

/** Resolve and check the modeled subset, rejecting missing validation context. */
std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveAndValidateModule(const syntax_ast::AstModule& ast);

/** Resolve and check available contexts; targetless success is not full validity. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveModule(
    const syntax_ast::AstModule& ast);

/** Compatibility wrapper for validateModule with AvailableContext policy. */
checker::CheckResult checkModuleAvailability(const syntax_ast::AstModule& ast,
                                             const ResolvedModule& module);

}  // namespace ptx_frontend::resolved_ir

#include "resolved_ir_resolution.gen.hpp"
