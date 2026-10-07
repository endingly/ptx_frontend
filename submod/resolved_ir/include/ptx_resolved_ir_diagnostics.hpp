#pragma once

#include <cstdint>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>

namespace ptx_frontend::declaration_semantics {
/** Stable diagnostic category identity without importing the full Syntax AST. */
enum class DeclarationDiagnosticKind : uint8_t;
}  // namespace ptx_frontend::declaration_semantics

namespace ptx_frontend::resolved_ir {

/** Origin of a diagnostic returned through the public resolution API. */
enum class ResolveDiagnosticStage : uint8_t {
  Resolution,
  Binding,
  DeclarationSemantics,
  Checking,
};

/** Owned diagnostic retained independently of source text and Syntax AST. */
struct ResolveDiagnostic {
  /** Primary source range of the failure. */
  SourceRange range;
  /** Human-readable explanation owned by this value. */
  std::string message;
  /** Declaration category, present only for imported declaration errors. */
  std::optional<declaration_semantics::DeclarationDiagnosticKind>
      declaration_kind{};
  /** Related source range, when the origin supplies one. */
  std::optional<SourceRange> previous_range{};
  /** Binding category, present only for imported binding errors. */
  std::optional<binding::BindDiagnosticKind> binding_kind{};
  /** Checker category, present only for imported checker errors. */
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
  /** Retain the generator bug and its frontend call site. */
  explicit ResolveException(
      std::string message,
      std::source_location where = std::source_location::current())
      : std::runtime_error(std::move(message)), where_(where) {}

  /** Return the frontend source location at which the contract failed. */
  [[nodiscard]] const std::source_location& where() const noexcept {
    return where_;
  }

 private:
  /** Call site of the violated descriptor contract. */
  std::source_location where_;
};

}  // namespace ptx_frontend::resolved_ir
