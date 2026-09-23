#pragma once

// Compatibility aggregate for consumers resolving the complete model.
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>

namespace ptx_frontend::resolved_ir {

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
    ModuleValidationPolicy policy =
        ModuleValidationPolicy::RequireCompleteContext);

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

#include <ptx_frontend/resolved_ir/resolved_ir_resolution.gen.hpp>
