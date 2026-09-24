#pragma once

#include <expected>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>
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

/** Owned diagnostic returned by standalone or module resolution. */
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
  /** Bound storage identities carrying a PTX `.unified` attribute. */
  std::span<const binding::SymbolId> unified_storage_symbols;
};

/** Preserve the written atomic address suffix after syntax selection. */
WithLocs<AtomicAddressQualifier> atomic_address_qualifier_from_ast(
    const syntax_ast::AstInstruction& ast);

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

}  // namespace ptx_frontend::resolved_ir
